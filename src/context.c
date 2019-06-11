#include<types.h>
#include<lib.h>
#include<context.h>
#include<memory.h>
#include<init.h>
#include<apic.h>
#include<idt.h>

static struct exec_context *current; 
static struct exec_context *ctx_list;

struct exec_context *get_new_ctx()
{
  int ctr;
  struct exec_context *ctx = ctx_list;
  for(ctr=0; ctr < MAX_PROCESSES; ++ctr){
      if(ctx->state == UNUSED){ 
          ctx->pid = ctr;
          ctx->state = NEW;
          return ctx; 
      }
     ctx++;
  }
  printf("%s: System limit on processes reached\n", __func__);
  return NULL;
}

struct exec_context *create_context(char *name, u8 type)
{
  
  struct mm_segment *seg;
  struct exec_context *ctx = get_new_ctx();
  ctx->state = NEW;
  ctx->type = type;
  ctx->used_mem = 0;
  ctx->pgd = 0;
  ctx->os_stack_pfn = os_pfn_alloc(OS_PT_REG);
  ctx->os_rsp = (u64)osmap(ctx->os_stack_pfn + 1);

  memcpy(ctx->name, name, strlen(name));
  seg = &ctx->mms[MM_SEG_CODE];
  
  seg->start = CODE_START;
  seg->end   = RODATA_START - 1;
  seg->access_flags = MM_RD | MM_EX;
  seg++; 
  
  seg->start = RODATA_START;
  seg->next_free = seg->start;
  seg->end   = DATA_START - 1;
  seg->access_flags = MM_RD;
  seg++; 
  
  seg->start = DATA_START;
  seg->end   = STACK_START - MAX_STACK_SIZE - 1;
  seg->access_flags = MM_RD | MM_WR;
  seg++; 
  
  seg->start = STACK_START - MAX_STACK_SIZE;
  seg->end   = STACK_START;
  seg->access_flags = MM_RD | MM_WR;

  return ctx; 
}

static u64 os_pmd_pfn;

static int install_os_pts(u32 pl4)
{
    u64 pfn;
    u64 pt_base = (u64)pl4 << PAGE_SHIFT;
    unsigned long *entry = (unsigned long *)(pt_base);
    if(!*entry){
          /*Create PUD entry in PGD*/
          pfn = os_pfn_alloc(OS_PT_REG);
          *entry = (pfn << PAGE_SHIFT) | 0x3;
          pt_base = pfn << PAGE_SHIFT; 
    }else{
          pt_base = *entry & FLAG_MASK;  
    }
   
    entry = (unsigned long *)(pt_base);
    if(*entry){
         pt_base = *entry & FLAG_MASK;  
    }else{
        /*Create PMD entry in PUD*/
        pfn = os_pfn_alloc(OS_PT_REG);
        os_pmd_pfn = pfn;
        *entry = (pfn << PAGE_SHIFT) | 0x3;
        pt_base = pfn << PAGE_SHIFT; 
       
    }
    entry = (unsigned long *)(pt_base);
    for(pfn=0; pfn < OS_PT_MAPS; ++pfn){
           if(config->global_mapping)
                *entry = (pfn << 21) | 0x183;  //Global
           else
                 *entry = (pfn << 21) | 0x83;  
          entry++;
    }
/*APIC mapping needed*/
    install_apic_mapping((u64) pl4); 
    return 0;
}


static u32 pt_walk(struct exec_context *ctx, u32 segment, u32 stack)
{ 
   unsigned long pt_base = (u64) ctx->pgd << PAGE_SHIFT;
   unsigned long entry;
   struct mm_segment *mm = &ctx->mms[segment];
   unsigned long start = mm->start;
   if(stack)
       start = mm->end - PAGE_SIZE;

   
   entry = *((unsigned long *)(pt_base + ((start & PGD_MASK) >> PGD_SHIFT)));
   if((entry & 0x1) == 0)
        return -1;
 
   pt_base = entry & FLAG_MASK;
   
   entry = *((unsigned long *)pt_base + ((start & PUD_MASK) >> PUD_SHIFT));
   if((entry & 0x1) == 0)
        return -1;
   
   pt_base = entry & FLAG_MASK;
   
   entry = *((unsigned long *)pt_base + ((start & PMD_MASK) >> PMD_SHIFT));
   if((entry & 0x1) == 0)
        return -1;
   
   pt_base = entry & FLAG_MASK;
   
   entry = *((unsigned long *)pt_base + ((start & PTE_MASK) >> PTE_SHIFT));
   if((entry & 0x1) == 0)
        return -1;
    
   return (entry >> PAGE_SHIFT);
        
}


static u32 install_ptable(unsigned long base, struct  mm_segment *mm, u64 address, u32 upfn)
{
    void *os_addr;
    u64 pfn;
    
    unsigned long *ptep;
    if(!address)
        address = mm->start;

    ptep  = (unsigned long *)base + ((address & PGD_MASK) >> PGD_SHIFT);    
    if(!*ptep){
                pfn = os_pfn_alloc(OS_PT_REG);
                *ptep = (pfn << PAGE_SHIFT) | 0x7; 
                os_addr = osmap(pfn);
                bzero((char *)os_addr, PAGE_SIZE);
    }else{
                os_addr = (void *) ((*ptep) & FLAG_MASK);
    }
   ptep = (unsigned long *)os_addr + ((address & PUD_MASK) >> PUD_SHIFT); 
   if(!*ptep){
                pfn = os_pfn_alloc(OS_PT_REG);
                *ptep = (pfn << PAGE_SHIFT) | 0x7; 
                os_addr = osmap(pfn);
                bzero((char *)os_addr, PAGE_SIZE);
   }else{
                os_addr = (void *) ((*ptep) & FLAG_MASK);
   }
   ptep = (unsigned long *)os_addr + ((address & PMD_MASK) >> PMD_SHIFT); 
   if(!*ptep){
                pfn = os_pfn_alloc(OS_PT_REG);
                *ptep = (pfn << PAGE_SHIFT) | 0x7; 
                os_addr = osmap(pfn);
                bzero((char *)os_addr, PAGE_SIZE);
   }else{
                os_addr = (void *) ((*ptep) & FLAG_MASK);
   }
   ptep = (unsigned long *)os_addr + ((address & PTE_MASK) >> PTE_SHIFT); 
   if(!upfn)
         upfn = os_pfn_alloc(USER_REG);
   *ptep = ((u64)upfn << PAGE_SHIFT) | 0x5;
   if(mm->access_flags & MM_WR)
       *ptep |= 0x2;
   return upfn;    
}

/*
  Works only when count fits in last level page table 
  TODO  hope every user page is contineous 
*/
static u32 install_ptable_multi(unsigned long pgd, unsigned long start, int count, int write)
{
    void *os_addr;
    u64 pfn, start_pfn, last_pfn;
    int ctr;
    unsigned long *ptep = (unsigned long *)pgd + ((start & PGD_MASK) >> PGD_SHIFT);    
    if(!*ptep){
                pfn = os_pfn_alloc(OS_PT_REG);
                *ptep = (pfn << PAGE_SHIFT) | 0x7; 
                os_addr = osmap(pfn);
                bzero((char *)os_addr, PAGE_SIZE);
    }else{
                os_addr = (void *) ((*ptep) & FLAG_MASK);
    }
   ptep = (unsigned long *)os_addr + ((start & PUD_MASK) >> PUD_SHIFT); 
   if(!*ptep){
                pfn = os_pfn_alloc(OS_PT_REG);
                *ptep = (pfn << PAGE_SHIFT) | 0x7; 
                os_addr = osmap(pfn);
                bzero((char *)os_addr, PAGE_SIZE);
   }else{
                os_addr = (void *) ((*ptep) & FLAG_MASK);
   }
   ptep = (unsigned long *)os_addr + ((start & PMD_MASK) >> PMD_SHIFT); 
   if(!*ptep){
                pfn = os_pfn_alloc(OS_PT_REG);
                *ptep = (pfn << PAGE_SHIFT) | 0x7; 
                os_addr = osmap(pfn);
                bzero((char *)os_addr, PAGE_SIZE);
   }else{
                os_addr = (void *) ((*ptep) & FLAG_MASK);
   }
   ptep = (unsigned long *)os_addr + ((start & PTE_MASK) >> PTE_SHIFT); 
   
   start_pfn = os_pfn_alloc(USER_REG);
   for(ctr=0; ctr < count; ++ctr){
         if(!ctr){
             pfn = start_pfn;
         }
         else{
               pfn = os_pfn_alloc(USER_REG);
               if(last_pfn != pfn - 1)
                      printf("BUG! PFN not in sequence\n");
         }
         *ptep = (pfn << PAGE_SHIFT) | 0x5;
         if(write)
              *ptep |= 0x2;
         ptep++;
         last_pfn = pfn;
   }
   return start_pfn;
}

int exec_init(struct exec_context *ctx, struct init_args *args)
{
   unsigned long pl4;
   extern unsigned long saved_ebp;  /*Useful when init exits*/
   u64 stack_start, sptr, textpfn, fmem;
   void *os_addr;

   printf("Setting up init process ...\n");
   ctx->pgd = os_pfn_alloc(OS_PT_REG);
   os_addr = osmap(ctx->pgd);
   bzero((char *)os_addr, PAGE_SIZE);
  
   textpfn = install_ptable_multi((u64) os_addr, ctx->mms[MM_SEG_CODE].start, CODE_PAGES, 0);   
   ctx->mms[MM_SEG_CODE].next_free = ctx->mms[MM_SEG_CODE].start + (CODE_PAGES << PAGE_SHIFT);

   install_ptable((u64) os_addr, &ctx->mms[MM_SEG_DATA], 0, 0);   
   ctx->mms[MM_SEG_DATA].next_free = ctx->mms[MM_SEG_DATA].start + PAGE_SIZE;

   stack_start = ctx->mms[MM_SEG_STACK].start;
   ctx->mms[MM_SEG_STACK].start = ctx->mms[MM_SEG_STACK].end - PAGE_SIZE;
   install_ptable((u64) os_addr, &ctx->mms[MM_SEG_STACK], 0, 0);   
   ctx->mms[MM_SEG_STACK].start = stack_start;
   ctx->mms[MM_SEG_STACK].next_free = ctx->mms[MM_SEG_STACK].end - PAGE_SIZE;
   
   pl4 = ctx->pgd; 
   if(install_os_pts(pl4))
        return -1;
   pl4 = pl4 << PAGE_SHIFT;
   set_tss_stack_ptr(ctx); 
   stats->num_processes++;   
   sptr = STACK_START;
   fmem = textpfn << PAGE_SHIFT;
   memcpy((char *)fmem, (char *)(0x200000), CODE_PAGES << PAGE_SHIFT);   /*This is where INIT is in the os binary*/ 
   fmem = CODE_START;
   current = ctx;
   current->state = RUNNING;
   printf("Page table setup done, launching init ...\n");

   asm volatile("mov %%rbp, %0;"
           : "=r" (saved_ebp)
           : 
           :
          ); 

   asm volatile( 
            "cli;"
            "mov %1, %%cr3;"
            "pushq $0x2b;"
            "pushq %0;"
            "pushfq;"
            "popq %%rax;"
            "or $0x200, %%rax;"
            "pushq %%rax;" 
            "pushq $0x23;"
            "pushq %2;"
            "mov $0x2b, %%ax;"
            "mov %%ax, %%ds;"
            "mov %%ax, %%es;"
            "mov %%ax, %%gs;"
            "mov %%ax, %%fs;"
           : 
           : "r" (sptr), "r" (pl4), "r" (fmem) 
           : "memory"
          );
 asm volatile( "mov %0, %%rdi;"
            "mov %1, %%rsi;"
            "mov %2, %%rcx;"
            "mov %3, %%rdx;"
            "mov %4, %%r8;"
            "iretq"
            :
            : "m" (args->rdi), "m" (args->rsi), "m" (args->rcx), "m" (args->rdx), "m" (args->r8)
            : "rsi", "rdi", "rcx", "rdx", "r8", "memory"
           );
   
   return 0; 
}
struct exec_context* get_current_ctx(void)
{
   return current;
}
void set_current_ctx(struct exec_context *ctx)
{
   current = ctx;
}

struct exec_context *get_ctx_list()
{
   return ctx_list; 
}

static void pt_cleanup(u64 pfn,int level)
{
     unsigned long* v_add=(unsigned long*) osmap(pfn); 
     unsigned long* entry_addr;
     unsigned long entry_val;
     unsigned int i;
     if(level<1 && !is_apic_base(pfn)){
            os_pfn_free(USER_REG, pfn);  
            return;
      }
        
     if(level == 3)
             i = 1;
     else
             i = 0;     
     for(;i < 512; i++)
	{
		
			entry_addr=v_add+i;
			entry_val=*(entry_addr);
			if((entry_val & 0x1) == 0)
                              continue;
			else{
				pt_cleanup(entry_val>>12, level-1);
				*(entry_addr) = 0x0;
			}	
		
		
	}
        if(!is_apic_base(pfn))
	      os_pfn_free(OS_PT_REG, pfn);

}


void do_cleanup()
{
   /*TODO split the exit into two parts. load OS CR3 and saved ebp before cleaning up 
     this guy, There is a chicken-egg here.*/
   struct exec_context *ctx = current; 
   printf("Cleaned up %s process\n", current->name);
   os_pfn_free(OS_PT_REG, ctx->os_stack_pfn);
   //os_free((void *)ctx, sizeof(struct exec_context));
   printf("GemOS shell again!\n", current->name);
   asm volatile("cli;"
                 :::"memory"); 
   remove_apic_mapping((u64) ctx->pgd); 
   pt_cleanup(ctx->pgd, 4);
   os_pfn_free(OS_PT_REG, os_pmd_pfn);
   current->state = UNUSED;
   current = NULL;
   ret_from_user();
}

static void swapper_task()
{
   stats->swapper_invocations++;
   while(1){
             asm volatile("sti;"
                          "hlt;"
                           :::"memory"
                         );
   }
}


struct exec_context *get_ctx_by_pid(u32 pid)
{
   if(pid >= MAX_PROCESSES){
        printf("%s: invalid pid %d\n", __func__, pid);
        return NULL;
   }
  return (ctx_list + pid);
}

int set_process_state(struct exec_context *ctx, u32 state)
{
   if(state >= MAX_STATE){
        printf("%s: invalid state %d\n", __func__, state);
        return -1;
   }
   ctx->state = state;
   return 0;
}
#if 0
void load_swapper(struct user_regs *regs)
{
   extern void *return_from_os;
   unsigned long retptr = (unsigned long)(&return_from_os);
   struct exec_context *swapper = ctx_list;
   u64 cr3 = swapper->pgd;
   current = swapper;
   current->state = RUNNING;
   set_tss_stack_ptr(swapper);
   memcpy((char *)regs, (char *)(&swapper->regs), sizeof(struct user_regs));
   ack_irq();
   asm volatile("mov %0, %%cr3;"
                "mov %1, %%rsp;"
                "callq *%2;"
                :
                :"r" (cr3), "r" (regs), "r"  (retptr)
                :"memory");
   
}
#endif
void init_swapper()
{
   u64 ss, cs, rflags; 
   struct exec_context *swapper;
   u64 ctx_l = os_pfn_alloc(OS_PT_REG);
   ctx_list = (struct exec_context *) (ctx_l << PAGE_SHIFT);
   bzero((char*)ctx_list, PAGE_SIZE);
   swapper = ctx_list;
   swapper->state = READY;
   swapper->pgd = 0x70;
   memcpy(swapper->name, "swapper", 8);
   swapper->os_stack_pfn = os_pfn_alloc(OS_PT_REG);
   swapper->os_rsp = (((u64) swapper->os_stack_pfn) << PAGE_SHIFT) + PAGE_SIZE;
   bzero((char *)(&swapper->regs), sizeof(struct user_regs));
   swapper->regs.entry_rip = (unsigned long)(&swapper_task);
   swapper->regs.entry_rsp = swapper->os_rsp;
   asm volatile( "mov %%ss, %0;"
                 "mov %%cs, %1;"
                 "pushf;"
                 "pop %%rax;"
                 "mov %%rax, %2;"
                 : "=r" (ss), "=r" (cs), "=r" (rflags)
                 :
                 : "rax", "memory"
   );
   swapper->regs.entry_ss = ss; 
   swapper->regs.entry_rflags = rflags;
   swapper->regs.entry_cs = cs;
   return;
}

/*
   Copies the mm structures along with the page table entries
   Note: CODE and RODATA are shared now!
*/
void copy_mm(struct exec_context *child, struct exec_context *parent)
{
   void *os_addr;
   u64 vaddr; 
   struct mm_segment *seg;

   child->pgd = os_pfn_alloc(OS_PT_REG);

   os_addr = osmap(child->pgd);
   bzero((char *)os_addr, PAGE_SIZE);
   
   //CODE segment
   seg = &parent->mms[MM_SEG_CODE];
   for(vaddr = seg->start; vaddr < seg->next_free; vaddr += PAGE_SIZE){
      u64 *parent_pte =  get_user_pte(parent, vaddr);
      if(parent_pte)
           install_ptable((u64) os_addr, seg, vaddr, (*parent_pte & FLAG_MASK) >> PAGE_SHIFT);   
   } 
   //RODATA segment
   
   seg = &parent->mms[MM_SEG_RODATA];
   for(vaddr = seg->start; vaddr < seg->next_free; vaddr += PAGE_SIZE){
      u64 *parent_pte =  get_user_pte(parent, vaddr);
      if(parent_pte)
           install_ptable((u64)os_addr, seg, vaddr, (*parent_pte & FLAG_MASK) >> PAGE_SHIFT);   
   } 
   
   //DATA segment
  seg = &parent->mms[MM_SEG_DATA];
  for(vaddr = seg->start; vaddr < seg->next_free; vaddr += PAGE_SIZE){
      u64 *parent_pte =  get_user_pte(parent, vaddr);
      
      if(parent_pte){
           u64 pfn = install_ptable((u64)os_addr, seg, vaddr, 0);  //Returns the blank page  
           pfn = (u64)osmap(pfn);
           memcpy((char *)pfn, (char *)(*parent_pte & FLAG_MASK), PAGE_SIZE); 
      }
  } 
  
  //STACK segment
  seg = &parent->mms[MM_SEG_STACK];
  for(vaddr = seg->end - PAGE_SIZE; vaddr >= seg->next_free; vaddr -= PAGE_SIZE){
      u64 *parent_pte =  get_user_pte(parent, vaddr);
      
     if(parent_pte){
           u64 pfn = install_ptable((u64)os_addr, seg, vaddr, 0);  //Returns the blank page  
           pfn = (u64)osmap(pfn);
           memcpy((char *)pfn, (char *)(*parent_pte & FLAG_MASK), PAGE_SIZE); 
      }
  } 
  
  install_os_pts(child->pgd); 
  return; 
}

void setup_child_context(struct exec_context *child)
{
   //Allocate pgd and OS stack
   child->os_stack_pfn = os_pfn_alloc(OS_PT_REG);
   child->os_rsp = (((u64) child->os_stack_pfn) << PAGE_SHIFT) + PAGE_SIZE;
   child->state = READY;  // Will be eligible in next tick
   stats->num_processes++;
}
