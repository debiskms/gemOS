#include<init.h>
static void exit(int);
static int main(u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5);


void init_start(u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5)
{
  int retval = main(arg1, arg2, arg3, arg4, arg5);
  exit(0);
}

/*Invoke system call with no additional arguments*/
static long _syscall0(int syscall_num)
{
  asm volatile ( "int $0x80;"
                 "leaveq;"
                 "retq;"
                :::"memory");
  return 0;   /*gcc shutup!*/
}

/*Invoke system call with one argument*/

static long _syscall1(int syscall_num, long exit_code)
{
  asm volatile ( "int $0x80;"
                 "leaveq;"
                 "retq;"
                :::"memory");
  return 0;   /*gcc shutup!*/
}
/*Invoke system call with two arguments*/

static long _syscall2(int syscall_num, u64 arg1, u64 arg2)
{
  asm volatile ( "int $0x80;"
                 "leaveq;"
                 "retq;"
                :::"memory");
  return 0;   /*gcc shutup!*/
}


static void exit(int code)
{
  _syscall1(SYSCALL_EXIT, code); 
}

static long getpid()
{
  return(_syscall0(SYSCALL_GETPID));
}

static long fork()
{
  return(_syscall0(SYSCALL_FORK));
}

static long get_stats()
{
  return(_syscall0(SYSCALL_STATS));
}

static long configure(struct os_configs *new_config)
{
  return(_syscall1(SYSCALL_CONFIGURE, (u64)new_config));
}

static long write(char *ptr, int size)
{
   return(_syscall2(SYSCALL_WRITE, (u64)ptr, size));
}

u64 signal(int num, void *handler)
{
    return _syscall2(SYSCALL_SIGNAL, num, (u64)handler);
}

u64 sleep(int ticks)
{
    return _syscall1(SYSCALL_SLEEP, ticks);
}

static long expand(unsigned size, int flags) {
	return (long)_syscall2(SYSCALL_EXPAND, size, flags);
}

int clone(void (func)(void), long stack_addr)
{
    return _syscall2(SYSCALL_CLONE, (u64)func, stack_addr);
}


#define MSG "pid = &\n"
#define DECLARE_MSG(pid) \
    char msg[sizeof(MSG)] = MSG;    \
    msg[sizeof(MSG)-3] = '0' + (char)pid;


#define MAP_WR  0x1
static void pageset(u64 *p, u64 val)
{
   int ctr;
   for(ctr=0; ctr < 4096; ctr += 1024, p += 128)
       *p = val;
}


u32 rand_rdtsc()
{
        unsigned hi, lo;
        __asm__ __volatile__
            ("rdtsc;" : "=a" (lo), "=d" (hi));

        return (hi + lo);

}

static int main(u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5)
{
  int ctr, ctr1, ctr2;
  char message[20];
  char number[10];
  message[0] = 'A';
  message[1] = 'R';
  message[2] = 'G';
  message[3] = '1';
  message[4] = '=';
  ctr = 0;
  while(arg1){
       number[ctr] = arg1 % 10 + '0';
       arg1 /= 10;
       ++ctr;
  }
  ctr1 = 5;
  --ctr;
  for(ctr2=0; ctr >= 0; ++ctr2, --ctr)
     message[ctr1 + ctr2] = number[ctr];
  ctr1 += ctr2;
  message[ctr1] = '\n';
  ctr1++;
  message[ctr1] = 0;
  ++ctr1;
  write(message, ctr1);
  
  /*struct os_configs new_config;
  unsigned total_count = 64;
  long pid = getpid();
  DECLARE_MSG(pid)
  write(msg, sizeof(msg));
 
  new_config.global_mapping = 1;
  new_config.apic_tick_interval = 8;
  new_config.debug = 0;
  new_config.aperm = 0;

  configure(&new_config);

  pid = fork();
  if(pid == 0){
         char *ptr;
         long mpid, ctr;
         fork();
         mpid  = getpid();
         DECLARE_MSG(mpid);
         ptr = (char *)expand(512, MAP_WR);
         
         while(total_count--){
              for(ctr=0; ctr<512; ++ctr){
                   pageset((u64 *)(ptr + (ctr << 12)), 0x1000);
                   if(ctr % 512 == 0 && mpid % 2 == 0 && rand_rdtsc() % 4 == 0)
                        getpid();
              } 
         }
  }else{ 
       sleep(1000);
  }
  get_stats();*/
  return 0;
}
