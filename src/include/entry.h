#ifndef __ENTRY_S_
#define __ENTRY_S_
#include <types.h>
#include <context.h>

#define SYSCALL_EXIT      1
#define SYSCALL_GETPID    2
#define SYSCALL_WRITE     3
#define SYSCALL_EXPAND    4
#define SYSCALL_SHRINK    5
#define SYSCALL_ALARM     6
#define SYSCALL_SLEEP     7
#define SYSCALL_SIGNAL    8
#define SYSCALL_CLONE     9
#define SYSCALL_FORK      10
#define SYSCALL_STATS     11
#define SYSCALL_CONFIGURE 12


#define MAX_WRITE_LEN 1024
#define MAX_EXPAND_PAGES 1024

struct os_stats{
                u64 swapper_invocations;
                u64 context_switches;
                u64 lw_context_switches;
                u64 ticks;
                u64 page_faults;
                u64 syscalls;
                u64 used_memory;
                u64 num_processes;
};
extern struct os_stats *stats;
struct os_configs{
                u64 global_mapping;
                u64 apic_tick_interval;
                u64 debug;
                u64 adv_global; 
};

extern struct os_configs *config;

extern long do_syscall(int syscall, u64 param1, u64 param2, u64 param3, u64 param4);
extern int handle_div_by_zero(struct user_regs *regs);
extern int handle_page_fault(struct user_regs *regs);
extern u64* get_user_pte(struct exec_context *ctx, u64 addr);
extern int do_unmap_user(struct exec_context *ctx, u64 addr);
extern void do_exit(void);

#endif //__ENTRY_S
