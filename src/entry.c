#include<entry.h>
#include<lib.h>
#include<context.h>
#include<memory.h>
#include<schedule.h>



/* Returns the pte coresponding to a user address. 
Return NULL if mapping is not present or mapped
in ring-0 */


u64* get_user_pte(struct exec_context *ctx, u64 addr) 
{
    u64 *vaddr_base = (u64 *)osmap(ctx->pgd);
    u64 *entry;
    u32 phy_addr;
    
    entry = vaddr_base + ((addr & PGD_MASK) >> PGD_SHIFT);
    phy_addr = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
    vaddr_base = (u64 *)osmap(phy_addr);
  
    /* Address should be mapped as un-priviledged in PGD*/
    if( (*entry & 0x1) == 0 || (*entry & 0x4) == 0)
        goto out;

     entry = vaddr_base + ((addr & PUD_MASK) >> PUD_SHIFT);
     phy_addr = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
     vaddr_base = (u64 *)osmap(phy_addr);
    
     /* Address should be mapped as un-priviledged in PUD*/
      if( (*entry & 0x1) == 0 || (*entry & 0x4) == 0)
          goto out;


      entry = vaddr_base + ((addr & PMD_MASK) >> PMD_SHIFT);
      phy_addr = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
      vaddr_base = (u64 *)osmap(phy_addr);
      
      /* 
        Address should be mapped as un-priviledged in PMD 
         Huge page mapping not allowed
      */
      if( (*entry & 0x1) == 0 || (*entry & 0x4) == 0 || (*entry & 0x80) == 1)
          goto out;
     
      entry = vaddr_base + ((addr & PTE_MASK) >> PTE_SHIFT);
      
      /* Address should be mapped as un-priviledged in PTE*/
      if( (*entry & 0x1) == 0 || (*entry & 0x4) == 0)
          goto out;
      return entry;

out:
      return NULL;
}

/* Returns 0 if successfully mmaped else return -1 (if not found)*/
int do_unmap_user(struct exec_context *ctx, u64 addr) 
{
    u64 *pte_entry = get_user_pte(ctx, addr);
    if(!pte_entry)
             return -1;
  
    os_pfn_free(USER_REG, (*pte_entry >> PTE_SHIFT) & 0xFFFFFFFF);
    *pte_entry = 0;  // Clear the PTE
  
    asm volatile ("invlpg (%0);" 
                    :: "r"(addr) 
                    : "memory");   // Flush TLB
      return 0;
}

void install_page_table(struct exec_context *ctx, u64 addr, u64 error_code) {
    u64 *vaddr_base = (u64 *)osmap(ctx->pgd);
    u64 *entry;
    u64 pfn;
    u64 ac_flags = 0x5 | (error_code & 0x2);
  
    entry = vaddr_base + ((addr & PGD_MASK) >> PGD_SHIFT);
    if(*entry & 0x1) {
      // PGD->PUD Present, access it
       pfn = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
       vaddr_base = (u64 *)osmap(pfn);
    }else{
      // allocate PUD
      pfn = os_pfn_alloc(OS_PT_REG);
      *entry = (pfn << PTE_SHIFT) | ac_flags;
      vaddr_base = osmap(pfn);
    }
  
    entry = vaddr_base + ((addr & PUD_MASK) >> PUD_SHIFT);
    if(*entry & 0x1) {
       // PUD->PMD Present, access it
       pfn = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
       vaddr_base = (u64 *)osmap(pfn);
    }else{
       // allocate PMD
       pfn = os_pfn_alloc(OS_PT_REG);
       *entry = (pfn << PTE_SHIFT) | ac_flags;
       vaddr_base = osmap(pfn);
    }
  
   entry = vaddr_base + ((addr & PMD_MASK) >> PMD_SHIFT);
    if(*entry & 0x1) {
       // PMD->PTE Present, access it
       pfn = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
       vaddr_base = (u64 *)osmap(pfn);
    }else{
       // allocate PMD
       pfn = os_pfn_alloc(OS_PT_REG);
       *entry = (pfn << PTE_SHIFT) | ac_flags;
       vaddr_base = osmap(pfn);
    }
   
   entry = vaddr_base + ((addr & PTE_MASK) >> PTE_SHIFT);
   // since this fault occured as frame was not present, we don't need present check here
   pfn = os_pfn_alloc(USER_REG);
   *entry = (pfn << PTE_SHIFT) | ac_flags;
}


static long do_write(struct exec_context *ctx, u64 address, u64 length)
{
    if(length > MAX_WRITE_LEN)
       goto bad_write;
    if(!get_user_pte(ctx, address) || 
       ((address >> PAGE_SHIFT) != ((address + length) >> PAGE_SHIFT) && !get_user_pte(ctx, address + length)))
       goto bad_write;
     print_user((char *)address, length);
     return length;

bad_write:
      printf("Bad write address = %x length=%d\n", address, length);
      return -1;
}

static long do_expand(struct exec_context *ctx, u64 size, int segment_t)
{
    u64 old_next_free;
    struct mm_segment *segment;

    // Sanity checks
    if(size > MAX_EXPAND_PAGES)
              goto bad_segment;

    if(segment_t == MAP_RD)
         segment = &ctx->mms[MM_SEG_RODATA];
    else if(segment_t == MAP_WR)
         segment = &ctx->mms[MM_SEG_DATA];
    else
          goto bad_segment;

    if(segment->next_free + (size << PAGE_SHIFT) > segment->end)
          goto bad_segment;
     
     // Good expand call, do it!
     old_next_free = segment->next_free;
     segment->next_free += (size << PAGE_SHIFT);
     return old_next_free;
   
bad_segment:
      return 0;
}

static long do_shrink(struct exec_context *ctx, u64 size, int segment_t)
{
    struct mm_segment *segment;
    u64 address;
    // Sanity checks
    if(size > MAX_EXPAND_PAGES)
              goto bad_segment;

    if(segment_t == MAP_RD)
         segment = &ctx->mms[MM_SEG_RODATA];
    else if(segment_t == MAP_WR)
         segment = &ctx->mms[MM_SEG_DATA];
    else
          goto bad_segment;
    if(segment->next_free - (size << PAGE_SHIFT) < segment->start)
          goto bad_segment;
    
    // Good shrink call, do it!
    segment->next_free -= (size << PAGE_SHIFT);
    for(address = segment->next_free; address < segment->next_free + (size << PAGE_SHIFT); address += PAGE_SIZE) 
          do_unmap_user(ctx, address);
    return segment->next_free;

bad_segment:
      return 0;
   
}

void do_exit() 
{
  /*You may need to invoke the scheduler from here if there are
    other processes except swapper in the system. Make sure you make 
    the status of the current process to UNUSED before scheduling 
    the next process. If the only process alive in system is swapper, 
    invoke do_cleanup() to shutdown gem5 (by crashing it, huh!)
    */
  // Scheduling new process (swapper if no other available)
  int ctr;
  struct exec_context *ctx = get_current_ctx();
  struct exec_context *new_ctx;
  
  // cleanup of this process
  os_pfn_free(OS_PT_REG, ctx->os_stack_pfn);
  ctx->state = UNUSED;
  // check if we need to do cleanup
  int proc_exist = -1;

  for(ctr = 1; ctr < MAX_PROCESSES; ctr++) {
    struct exec_context *new_ctx = get_ctx_by_pid(ctr);
    if(new_ctx->state != UNUSED) {
          proc_exist = 1;
          break;
    }
  }
  
  stats->num_processes--; 
  if(proc_exist == -1) 
        do_cleanup();  /*Call this conditionally, see comments above*/
   
  new_ctx = pick_next_context(ctx);
  schedule(new_ctx);  //Calling from exit

}

/*system call handler for sleep*/
static long do_sleep(u32 ticks) 
{
  struct exec_context *new_ctx;
  struct exec_context *ctx = get_current_ctx();
  ctx->ticks_to_sleep = ticks;
  ctx->state = WAITING;
  new_ctx = pick_next_context(ctx);
  schedule(new_ctx);  //Calling from sleep
  return 0;
}

static long do_fork()
{
  
  struct exec_context *new_ctx = get_new_ctx();
  struct exec_context *ctx = get_current_ctx();
  u32 pid = new_ctx->pid;
  
  *new_ctx = *ctx;  //Copy the process
  new_ctx->pid = pid;
  new_ctx->ppid = ctx->pid; 
  
  copy_mm(new_ctx, ctx);
  setup_child_context(new_ctx);

  return pid;
}
/*
  system call handler for clone, create thread like 
  execution contexts
*/
static long do_clone(void *th_func, void *user_stack) 
{
  int ctr;
  struct exec_context *new_ctx = get_new_ctx();
  struct exec_context *ctx = get_current_ctx();
  new_ctx->type = ctx->type;
  new_ctx->state = READY;
  new_ctx->used_mem = ctx->used_mem;
  new_ctx->pgd = ctx->pgd;

  // allocate page for stack
  new_ctx->os_stack_pfn = os_pfn_alloc(OS_PT_REG);
  new_ctx->os_rsp = (u64)osmap(new_ctx->os_stack_pfn + 1);
  for(ctr = 0; ctr < MAX_MM_SEGS; ++ctr)
    new_ctx->mms[ctr] = ctx->mms[ctr];

  new_ctx->regs = ctx -> regs;  // XXX New context inherits the register state

  // Context executes the function on return
  new_ctx->regs.entry_rip = (u64) th_func;
  new_ctx->regs.entry_rsp = (u64) user_stack;
  new_ctx->regs.rbp = new_ctx->regs.entry_rsp;

  new_ctx->pending_signal_bitmap = ctx->pending_signal_bitmap;

  new_ctx->ticks_to_alarm = ctx->ticks_to_alarm;
  new_ctx->ticks_to_sleep = ctx->ticks_to_sleep;
  new_ctx->alarm_config_time = ctx->alarm_config_time;


  for(ctr = 0; ctr < MAX_SIGNALS; ctr++)
      new_ctx->sighandlers[ctr] = ctx -> sighandlers[ctr];

  //Update the name of the process
  for(ctr = 0; ctr < CNAME_MAX && ctx->name[ctr]; ++ctr) 
      new_ctx->name[ctr] = ctx->name[ctr];
  sprintf(new_ctx->name+ctr, "%d", ctx->pid + 1); 

  return 0;
}

long invoke_sync_signal(int signo, u64 *ustackp, u64 *urip) 
{
  /*
     If signal handler is registered, manipulate user stack and RIP to execute signal handler
     ustackp and urip are pointers to user RSP and user RIP in the exception/interrupt stack
     Default behavior is exit() if sighandler is not registered for SIGFPE or SIGSEGV.
     Ignored for SIGALRM
  */

  struct exec_context *ctx = get_current_ctx();
  dprintf("Called signal with ustackp=%x urip=%x\n", *ustackp, *urip);
  
  if(ctx->sighandlers[signo] == NULL && signo != SIGALRM) {
      do_exit();
  }else if(ctx->sighandlers[signo]){  
      /*
        Add a frame to user stack
        XXX Better to check the sanctity before manipulating user stack
      */
      u64 rsp = (u64)*ustackp;
      *((u64 *)(rsp - 8)) = *urip;
      *ustackp = (u64)(rsp - 8);
      *urip = (u64)(ctx->sighandlers[signo]);
  }
  return 0;

}

/*system call handler for signal, to register a handler*/
static long do_signal(int signo, unsigned long handler) 
{
  struct exec_context *ctx = get_current_ctx();
  if(signo < MAX_SIGNALS && signo > -1) {
         ctx -> sighandlers[signo] = (void *)handler; // save in context
         return 0;
  }
 
  return -1;
}

/*system call handler for alarm*/
static long do_alarm(u32 ticks) 
{
  struct exec_context *ctx = get_current_ctx();
  if(ticks > 0) {
    ctx -> alarm_config_time = ticks;
    ctx -> ticks_to_alarm = ticks;
    return 0;
  }
  return -1;
}
/*System Call handler*/
long  do_syscall(int syscall, u64 param1, u64 param2, u64 param3, u64 param4)
{
    struct exec_context *current = get_current_ctx();
    unsigned long saved_sp;
    
    asm volatile(
                   "mov %%rbp, %0;"
                    : "=r" (saved_sp) 
                    :
                    : "memory"
                );  

    saved_sp += 0x10;    //rbp points to entry stack and the call-ret address is pushed onto the stack
    memcpy((char *)(&current->regs), (char *)saved_sp, sizeof(struct user_regs));  //user register state saved onto the regs 
    stats->syscalls++;
    dprintf("[GemOS] System call invoked. syscall no = %d\n", syscall);
    switch(syscall)
    {
          case SYSCALL_EXIT:
                              dprintf("[GemOS] exit code = %d\n", (int) param1);
                              do_exit();
                              break;
          case SYSCALL_GETPID:
                              dprintf("[GemOS] getpid called for process %s, with pid = %d\n", current->name, current->pid);
                              return current->pid;      
          case SYSCALL_WRITE:
                             return do_write(current, param1, param2);
          case SYSCALL_EXPAND:
                             return do_expand(current, param1, param2);
          case SYSCALL_SHRINK:
                             return do_shrink(current, param1, param2);
          case SYSCALL_ALARM:
                              return do_alarm(param1);
          case SYSCALL_SLEEP:
                              return do_sleep(param1);
          case SYSCALL_SIGNAL: 
                              return do_signal(param1, param2);
          case SYSCALL_CLONE:
                              return do_clone((void *)param1, (void *)param2);
          case SYSCALL_FORK:
                              return do_fork();
          case SYSCALL_STATS:
                             printf("ticks = %d swapper_invocations = %d context_switches = %d lw_context_switches = %d\n", 
                             stats->ticks, stats->swapper_invocations, stats->context_switches, stats->lw_context_switches);
                             printf("syscalls = %d page_faults = %d used_memory = %d num_processes = %d\n",
                             stats->syscalls, stats->page_faults, stats->used_memory, stats->num_processes);
                             break;
          case SYSCALL_CONFIGURE:
                             memcpy((char *)config, (char *)param1, sizeof(struct os_configs));      
                             break;
          default:
                              return -1;
                                
    }
    return 0;   /*GCC shut up!*/
}

extern int do_div_by_zero(struct user_regs *regs) {
    u64 rip = regs->entry_rip;
    printf("Div-by-zero @ [%x]\n", rip);
    invoke_sync_signal(SIGFPE, &regs->entry_rsp, &regs->entry_rip);
    return 0;
}

extern int do_page_fault(struct user_regs *regs, u64 error_code)
{
     u64 rip, cr2;
     struct exec_context *current = get_current_ctx();
     rip = regs->entry_rip;
     stats->page_faults++;
     /*Get the Faulting VA from cr2 register*/
     asm volatile ("mov %%cr2, %0;"
                  :"=r"(cr2)
                  ::"memory");

     /*Check error code. We only handle user pages that are not present*/
     if((error_code & 0x1))
              goto sig_exit;

     if((cr2 >= (current -> mms)[MM_SEG_DATA].start) && (cr2 <= (current -> mms)[MM_SEG_DATA].end)){
       //Data segment
            if(cr2 < (current -> mms)[MM_SEG_DATA].next_free){
            // allocate
                   install_page_table(current, cr2, error_code);
                   goto done;
            }else
                   goto sig_exit;

      }

    if((cr2 >= (current -> mms)[MM_SEG_RODATA].start) && (cr2 <= (current -> mms)[MM_SEG_RODATA].end)) {
    // ROData Segment
        if(cr2 < (current -> mms)[MM_SEG_RODATA].next_free && ((error_code & 0x2) == 0)){
                 //Bit pos 1 in error code = Read access
               install_page_table(current, cr2, error_code);
               goto done;
        }else
            goto sig_exit;
    }

    if ((cr2 >= (current -> mms)[MM_SEG_STACK].start) && (cr2 <= (current -> mms)[MM_SEG_STACK].end)) {
      // Stack Segment
           if(cr2 < (current -> mms)[MM_SEG_STACK].next_free){
                    install_page_table(current, cr2, error_code);
                    goto done;
            }else
                    goto sig_exit;
    }

/*This must be at the beginning*/
sig_exit:
  printf("PF Error @ [RIP: %x] [accessed VA: %x] [error code: %x]\n", rip, cr2, error_code);
  invoke_sync_signal(SIGSEGV, &regs->entry_rsp, &regs->entry_rip);
  return 0;

done:
  return 0;
}
