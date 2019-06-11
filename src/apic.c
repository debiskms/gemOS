#include<types.h>
#include<apic.h>
#include<lib.h>
#include<idt.h>
#include<memory.h>
#include<context.h>
#include<entry.h>

#define PGD_MASK 0xff8000000000UL 
#define PUD_MASK 0x7fc0000000UL
#define PMD_MASK 0x3fe00000UL
#define PTE_MASK 0x1ff000UL

#define PGD_SHIFT 39
#define PUD_SHIFT 30
#define PMD_SHIFT 21
#define PTE_SHIFT 12

#define APIC_INTERVAL 4

#define FLAG_MASK 0x3ffffffff000UL 
static unsigned long apic_base;

static void clflush(void *addr)
{
    asm volatile(
                   "clflush %0;"
                    :
                    :"m" (addr)
                    :"memory"
    );
   
}
void ack_irq()
{
    u32 *vector;
#ifndef PERIODIC_TIMER
    //Initial count
     vector = (u32 *)(apic_base + APIC_TIMER_INIT_COUNT_OFFSET); 
     *vector = APIC_INTERVAL; // initial timer value
     clflush(vector);
    //Current count
    vector = (u32 *)(apic_base + APIC_TIMER_CURRENT_COUNT_OFFSET); 
    *vector = APIC_INTERVAL; // current count
    clflush(vector);
#endif
    vector = (u32 *)(apic_base + APIC_EOI_OFFSET);  
    *vector = 0;
    return;
}
int do_irq(struct user_regs *regs)
{
    return(handle_timer_tick(regs)); 
    //regs == stack pointer after pushing the registers  
}

int is_apic_base (u64 pfn)
{
   if(pfn == (apic_base >> PAGE_SHIFT))
       return 1;

   return 0;
}
void install_apic_mapping(u64 pl4)
{
   u64 address = (apic_base >> 21)<<21, pfn;
   u64 *pgd, *pmd, *pud;
   
   pl4 = pl4 << PAGE_SHIFT; 

   pgd = (u64 *)pl4 + ((address & PGD_MASK) >> PGD_SHIFT);
   if(((*pgd) & 0x1) == 0){
           pfn = os_pfn_alloc(OS_PT_REG);
           pfn = pfn << PAGE_SHIFT;
           *pgd = pfn  | 0x3;
   }else{
          pfn = ((*pgd) >> PAGE_SHIFT) << PAGE_SHIFT;  
   }
   pud = (u64 *)pfn + ((address & PUD_MASK) >> PUD_SHIFT);
   if(((*pud) & 0x1) == 0){
           pfn = os_pfn_alloc(OS_PT_REG);
           pfn = pfn << PAGE_SHIFT;
           *pud = pfn | 0x3;
   }else{
          pfn = ((*pud) >> PAGE_SHIFT) << PAGE_SHIFT;  
   }
   pmd = (u64 *)pfn + ((address & PMD_MASK) >> PMD_SHIFT);
   if((*pmd) & 0x1)
        printf("BUG!\n");
   if(config->global_mapping && config->adv_global)
       *pmd = address | 0x183UL | (1UL << 52);
   else if(config->global_mapping)
       *pmd = address | 0x183;
   else
       *pmd = address | 0x83;
   //printf("%s pmd=%x *pmd=%x\n", __func__, pmd, *pmd);
   return;
}

void remove_apic_mapping(u64 pl4)
{
   u64 address = (apic_base >> 21)<<21, pfn;
   u64 *pgd, *pmd, *pud;
   
   pl4 = pl4 << PAGE_SHIFT; 

   pgd = (u64 *)pl4 + ((address & PGD_MASK) >> PGD_SHIFT);
   if(((*pgd) & 0x1) == 0){
           printf("APIC free BUG! pgd\n");
   }else{
          pfn = ((*pgd) >> PAGE_SHIFT) << PAGE_SHIFT;  
   }
   pud = (u64 *)pfn + ((address & PUD_MASK) >> PUD_SHIFT);
   if(((*pud) & 0x1) == 0){
           printf("APIC free BUG! pud\n");
   }else{
          pfn = ((*pud) >> PAGE_SHIFT) << PAGE_SHIFT;  
   }
   pmd = (u64 *)pfn + ((address & PMD_MASK) >> PMD_SHIFT);
   //printf("%s pmd=%x *pmd=%x\n", __func__, pmd, *pmd);
   if(((*pmd) & 0x1) != 0x1)
        printf("APIC free BUG!\n");
   *pmd = 0;
   return;
  
}
void init_apic()
{
    u32 msr = IA32_APIC_BASE_MSR;
    u64 base_low, base_high;
    u32 *vector;
    printf("Initializing APIC\n");
    asm volatile(
                    "movl %2, %%ecx;"
                    "rdmsr;"
                    :"=a" (base_low), "=d" (base_high)
                    :"r" (msr)
                    : "memory"
    );
    base_high = (base_high << 32) | base_low;
    dprintf("APIC MSR = %x\n", base_high);
    dprintf("APIC initial state = %d\n", (base_high & (0x1 << 11)) >> 11);
    base_high = (base_high >> 12) << 12;
    dprintf("APIC Base address = %x\n", base_high);
    dprintf("APIC ID = %u\n", *((int *)(base_high + APIC_ID_OFFSET)));
    dprintf("APIC VERSION = %x\n", *((int *)(base_high + APIC_VERSION_OFFSET)));
    apic_base = base_high;
    /*Setup LDR and DFR*/
    vector = (u32 *)(base_high + APIC_DFR_OFFSET);  
    dprintf("DFR before = %x\n", *vector);
    *vector = 0xffffffff;
    clflush(vector);
    vector = (u32 *)(base_high + APIC_LDR_OFFSET);  
    dprintf("LDR before = %x\n", *vector);
    *vector = (*vector) & 0xffffff;
    *vector |= 1;
    
    /*Initialize the spurious interrupt LVT*/
    vector = (u32 *)(base_high + APIC_SPURIOUS_VECTOR_OFFSET);  
    *vector = 1 << 8 | IRQ_SPURIOUS;   
    clflush(vector);
    dprintf("SPR = %x\n", *((int *)(base_high + APIC_SPURIOUS_VECTOR_OFFSET)));
    
    /*APIC timer*/
    // Divide configuration register
    vector = (u32 *)(base_high + APIC_TIMER_DIVIDE_CONFIG_OFFSET); 
    *vector = 10; // 1010 --> Divide by 128
    clflush(vector);
    //Initial count
    vector = (u32 *)(base_high + APIC_TIMER_INIT_COUNT_OFFSET); 
    *vector = APIC_INTERVAL; // initial timer value
    clflush(vector);
    //Current count
    vector = (u32 *)(base_high + APIC_TIMER_CURRENT_COUNT_OFFSET); 
    *vector = APIC_INTERVAL; // current count
    clflush(vector);
    //The timer LVT in periodic mode
    vector = (u32 *)(base_high + APIC_LVT_TIMER_OFFSET); 
    #ifdef  PERIODIC_TIMER
    *vector = APIC_TIMER | (1 << 17);
    #else
    *vector = APIC_TIMER;
     
    #endif
}
