#include <lib.h>
#include <kbd.h>
#include <memory.h>
#include <context.h>
#include <init.h>

void dsh_help(void)
{
   printf("This is a minimal shell.\n");
   printf("---------\n");
   printf("commands:\n"); 
   printf("---------\n");
   printf("help: prints this help\n");
   printf("clear: clears the screen\n");
   printf("config: set and get the current configuration. To set use #config set var=value\n");
   printf("stats: displays the OS statistics\n");
   printf("init: launch the init process. usage: init [arg1] [arg2] ... [arg5]\n");
   printf("exit|shutdown: shutdown the OS\n");
   
}
void parse_init(char *cmd, struct init_args *args)
{
    u64 value = 0;
    int ctr, num = 0;
    for(ctr=5; cmd[ctr]; ++ctr){
         if(cmd[ctr] == 32 || cmd[ctr] == '\t'){
              *((u64*) args + num) = value;
              ++num;
              value=0;
              while(cmd[ctr] == 32 || cmd[ctr] == '\t')
                    ++ctr;
         }
         
         value *= 10;
         value += cmd[ctr] - '0'; 
    }   
   *((u64*) args + num) = value;
   return;
     
}

void invoke_dsh(void)
{
    struct acpi_info *acpi;
    char *command;
    struct init_args args = {0, 0 , 0 , 0 , 0};
    command = os_alloc(1024);
    

    while(1){ 
               printf("GemOS# ");
               serial_read(command);
               if(command[0] == 0)
                    continue;
               else if(!strcmp(command,"help"))
                   dsh_help();
               else if(!strcmp(command, "clear")){
                   char abc[10];
                   abc[0] = 27;
                   abc[1] = '[';
                   abc[2] = '2';
                   abc[3] = 'J';
                   abc[4] = 27;
                   abc[5] = '[';
                   abc[6] = 'H';
                   abc[7] = 0;
                   serial_write(abc);
               }
               else if(!strcmp(command, "exit") || !strcmp(command, "shutdown"))
                   kbd_reboot();
#if 0
               else if(!strcmp(command, "acpi")){
                   acpi = os_alloc(sizeof(struct acpi_info));
                   acpi->SCI_EN = 0;
                 if(acpi->SCI_EN)
                      printf("ACPI already initialized\n");
                 else{      
                       if(find_and_enable_acpi(acpi))
                            printf("ACPI not found in BIOS mmap\n");
                       printf("ACPI initialized\n");
                  }
                  os_free(acpi, sizeof(struct acpi_info));
               }else if(!strcmp(command, "halt")){
                        return;
               }else if(!strcmp(command, "shutdown")){
                       acpi = os_alloc(sizeof(struct acpi_info));
                       acpi->SCI_EN = 0;
                       if(!acpi->SCI_EN && find_and_enable_acpi(acpi))
                            printf("ACPI not found in BIOS mmap\n");
                      else
                            handle_acpi_action(acpi, POWEROFF);   
                      os_free(acpi, sizeof(struct acpi_info));
                             
               }
#endif    
            else if(!memcmp(command, "init", 4)){
                    struct exec_context *ctx;
                    //printf("Mem pt=%d user=%d\n",  get_free_pages_region(OS_PT_REG), get_free_pages_region(USER_REG));
                    ctx = create_context("init", EXEC_CTX_USER);
                    parse_init(command, &args);
                    exec_init(ctx, &args);
                    //printf("Mem pt=%d user=%d\n",  get_free_pages_region(OS_PT_REG), get_free_pages_region(USER_REG));
                    /*Should not return here if all goes well*/
                    
             }else if(!strcmp(command, "stats")){
                 
                 printf("ticks = %d swapper_invocations = %d context_switches = %d lw_context_switches = %d\n", 
                         stats->ticks, stats->swapper_invocations, stats->context_switches, stats->lw_context_switches);
                 printf("syscalls = %d page_faults = %d used_memory = %d num_processes = %d\n",
                          stats->syscalls, stats->page_faults, stats->used_memory, stats->num_processes);
             }else
                     printf("Invalid command\n");
    }
}
