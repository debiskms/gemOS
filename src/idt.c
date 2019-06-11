#include <idt.h>
#include <memory.h>
#include <lib.h>
#include <init.h>
#include <entry.h>

static unsigned long tss_page;
static unsigned long irq_stack;
static struct IDTR idt;
static unsigned long idt_table_base;

static void install_idt()
{
   __asm__("lidt %0;" 
             :
             : "m" (idt)
             : "memory"
          );
}     

static struct IDTR * read_idt()
{
   __asm__("sidt %0;" 
             : "=m" (idt)
             :
             : "memory"
          );
    return &idt;

}

static struct IDTR * read_gdt()
{
   void *gdp = &idt.limit; 
   __asm__("sgdt %0;" 
             : "=m" (gdp)
             :
             : "memory"
          );
    return &idt;

}

static void  install_gdt(struct IDTR *gdt)
{   
   __asm__("lgdt %0;" 
             :
             : "m" (*gdt)
             : "memory"
          );
}

void setup_gdt_tss(struct IDTR * gdt)
{
     struct tss *tss;
     struct gdt_entry *gdt_e;
     int total_gdts;
     gdt_e =(struct gdt_entry *) ((unsigned long)gdt->base);
     total_gdts = (gdt->limit + 1) / (sizeof(struct gdt_entry));
     while(total_gdts){
          printf("base = %x ",gdt_e->base_low | ((gdt_e->base_mid) << 16) | ((gdt_e->base_high) << 24));
          printf("limit low = %x high=%x ac_byte = %x flags=%x\n", gdt_e->limit_low, gdt_e->limit_high, gdt_e->ac_byte, gdt_e->flags);
          gdt_e->limit_low = 0xffff;
          gdt_e++;
          --total_gdts;
     }
    
     printf("Initializing user segments\n");
/*Add user code segment*/      
     gdt_e->base_low = 0; 
     gdt_e->base_mid = 0;
     gdt_e->base_high = 0;
     gdt_e->limit_low = 0xffff;
     gdt_e->limit_high = 0xf;
     gdt_e->ac_byte = 0xfa;
     gdt_e->flags = 0xa;
     gdt->limit += 8;
     gdt_e++;
/*Add user data segment*/      
     gdt_e->base_low = 0; 
     gdt_e->base_mid = 0;
     gdt_e->base_high = 0;
     gdt_e->limit_low = 0xffff;
     gdt_e->limit_high = 0xf;
     gdt_e->ac_byte = 0xf2;
     gdt_e->flags = 0xa;
     gdt->limit += 8;
     gdt_e++;
/*TSS*/
     tss_page = os_pfn_alloc(OS_PT_REG);  
     tss_page = tss_page << PAGE_SHIFT;
     bzero((char*)tss_page, PAGE_SIZE);
     gdt_e->limit_low = 0x1;
     gdt_e->base_low = tss_page & 0xffff;
     gdt_e->base_mid = (tss_page  >> 16) & 0xff;
     gdt_e->ac_byte = 0xe9;
     gdt_e->limit_high = 0;
     gdt_e->flags = 0x8;
     gdt_e->base_high = (tss_page >> 24) & 0xff;
     gdt_e++;
     *((u32 *)gdt_e) = tss_page >> 32;
     *((u32 *)gdt_e + 1) = 0x0;
     gdt_e++;
     gdt->limit += 16;
     install_gdt(gdt); 
     asm volatile(
                    "mov $0x33, %%rax;"
                    "ltr %%ax;"
                    :::"memory"
     );  
     tss = (struct tss *)tss_page;
     irq_stack = os_pfn_alloc(OS_PT_REG);  
     irq_stack++;
     irq_stack = irq_stack << PAGE_SHIFT;
     tss->ist1_low = irq_stack & 0xffffffff; 
     tss->ist1_high = irq_stack >> 32;
     return;
}

void set_tss_stack_ptr(struct exec_context *ctx)
{
  unsigned long sp  = ctx->os_stack_pfn + 1;    /*Stack grows from higher to lower address*/
  struct tss *t = (struct tss *) tss_page;
  sp = sp << PAGE_SHIFT; 
  t->rsp0_low = sp & 0xffffffff; 
  t->rsp0_high = sp >> 32;
}


int do_gp (u64 error, u64 rip)
{
    u64 cr2;
    asm volatile("mov %%cr2, %0;"
                  :"=r" (cr2)
                  :: "memory");
                   
    printf("General protection fault @ RIP = %x error=%x UserVA=%x\n", rip, error, cr2);
    do_exit();
    return 0;   /*GCC shut up!*/
}

static void create_idt(struct idt_entry *e, u8 type, unsigned long callback, int ist)
{
    e->unused = 0;
    e->ist = ist;
    e->offset_low = callback & 0xffff;
    e->offset_mid = (callback >> 16) & 0xffff;
    e->offset_high = callback >> 32;
    e->flags_type = type;
    e->segment = 0x8;
    return;
}

static inline void create_null_idt(struct idt_entry *e)
{
   e->flags_type = 0;   
}

void setup_idt()
{
   int i;
   struct idt_entry *idt_e;
   extern void *handle_syscall;
   extern void *handle_gp;
   #if 1
   extern void *handle_irq;
   #endif
   idt_table_base = os_pfn_alloc(OS_PT_REG);  
   idt_table_base = idt_table_base << PAGE_SHIFT;
   idt_e = (struct idt_entry *) idt_table_base;
   for(i=0; i<NUM_IDT_ENTRY; ++i){
        switch(i){
            case FAULT_DIVZERO:
                            create_idt(idt_e, IDT_DESC_TYPE_INT_SW, (unsigned long)(&handle_div_by_zero), 0);
                            break;
            case FAULT_PF:
                           create_idt(idt_e, IDT_DESC_TYPE_INT_SW, (unsigned long)(&handle_page_fault), 0);
                            break;
                             
            case FAULT_GP:
                             create_idt(idt_e, IDT_DESC_TYPE_INT_SW, (unsigned long)(&handle_gp), 0);
                             break;
                               
            case SYSCALL_IDT:
                             create_idt(idt_e, IDT_DESC_TYPE_INT_SW, (unsigned long)(&handle_syscall), 0);
                             break;
            case IRQ_SPURIOUS:
            case APIC_TIMER:
                             #if 1
                             create_idt(idt_e, IDT_DESC_TYPE_INT_HW, (unsigned long)(&handle_irq), 1);
                             #else
                             create_idt(idt_e, IDT_DESC_TYPE_INT_HW, (unsigned long)(&handle_timer_tick), 1);
                             #endif
                             break;
                             
            default:
                             create_null_idt(idt_e);
        }
       idt_e++;
   }  
   idt.limit = (1 << 12) - 1;
   idt.base = idt_table_base;  
   install_idt();
   return;
}
