#include <lib.h>
#include <serial.h>
#include <memory.h>
/* String Library routines */

int strlen(char *ptr){
      int i = 0;
      while(ptr[i] && i >= 0)
              ++i;
      return i;
}
void bzero(char *ptr,int length){
    int i;
    for(i=0;i<length;++i) ptr[i] = 0;
}
char *strcat(char *dst,char *app){
    int len = strlen(dst),i=0;
    while(app[i]){
        dst[i+len] = app[i];
        ++i;
    }
    dst[i+len] = 0;
   return dst;
}
int strcmp(char *s, char *d){
   int ctr = 0;
   while(s[ctr] != 0 && d[ctr] != 0 && s[ctr] == d[ctr])
         ++ctr;
   if(s[ctr] == 0 && d[ctr] == 0)
        return 0;
   return -1;
}
int memcmp(char *s, char *d, u32 len)
{
   int ctr = 0;
   for(ctr = 0; ctr<len && s[ctr] == d[ctr]; ++ctr);
   return ((ctr == len) ? 0 : (s[ctr] - d[ctr]));
}

int memcpy(char *d, char *s, u32 size)
{
   int ctr=0;
   for(ctr=0; ctr<size; ++ctr){
     *d = *s;
     d++;
     s++;
   }
  return 0;
}
/* print routines */

void console_init(){
    serial_init();
}

static int vprintf(char *buf,char *format,va_list args){
 int count = 0,ch,out=0;
 while((ch=format[count++])){
    if(ch != '%')
           buf[out++] = ch;
    else{
           ch=format[count++];
           switch(ch){
               case 'c':
                          buf[out++] = va_arg(args,int);
                          break;
               case 'd':
               case 'u':
                         {
                            int num = va_arg(args,int);
                            int tmp = num,count=0;
                            s8 chars[20];
                            if(!num){
                                  buf[out++] = '0';
                                  break;
                            }
                            if(tmp < 0){
                                  buf[out++] = '-';
                                  tmp=-tmp;
                                  num=-num;
                             }
                             while(tmp){
                                  chars[count++] = '0' + tmp % 10;
                                  tmp /= 10;
                              }
                              while(count--)
                                    buf[out++] = chars[count];
                              break;         
                         }
                       
               case 'x':
                          {
                            long num = va_arg(args, long);
                            long tmp = num, count=0;
                            s8 chars[20];
                            if(tmp < 0){
                                  buf[out++] = '-';
                                  tmp=-tmp;
                                  num=-num;
                             }
                            buf[out++] = '0';
                            buf[out++] = 'x';
                            if(!num){
                                buf[out++] = '0';
                                break;
                            }
                             while(tmp){
                                  if(tmp % 16 > 9)
                                     chars[count++] = 'A' + (tmp % 16 - 10);
                                  else
                                     chars[count++] = '0' + tmp % 16;
                                  tmp /= 16;
                              }
                              while(count--)
                                    buf[out++] = chars[count];
                              break;

                          }      
               case 's':
                          {
                            char *str=va_arg(args,char*);
                            if(str){
                                s8 p;
                                int i=0;
                                while((p=str[i++]))
                                  buf[out++] = p;
                             }
                            break;
                          }
               default:
                        buf[out++] = ch;
                        break;
           }
    }
 }   
 return out;
}

/*XXX TODO convert static*/
static char buff[4096];

int printf(char *format,...)
{
    int retval;
    va_list args;
    bzero(buff,4096);
    va_start(args,format);
    retval=vprintf(buff,format,args);
    va_end(args);
    serial_write(buff);
    return retval;
}

int sprintf(char *out, char *format, ...)
{
    int retval;
    va_list args;

    va_start(args, format);
    retval = vprintf(out, format, args);
    va_end(args);
    out[retval] = 0;

    return retval;
}

void print_user(char *user, int length)
{
    memcpy(buff, user, length);
    buff[length] = 0;
    serial_write(buff);
    return;
}
int enqueue_tail(struct list *l, u64 item)
{
   struct node *n;
   n = node_alloc();
   n->next = NULL;
   n->value = item;

   if(is_empty(l)){
       if(!l->head){
          l->head = node_alloc(); 
          l->tail = node_alloc(); 
       }
       l->head->next = n;
       l->tail->prev = n; 
       n->next = l->tail;
       goto inc_ret;
   }

   (l->tail->prev)->next = n;
   l->tail->prev = n;

   inc_ret:
          l->size++;
   return l->size;
} 

struct node * dequeue_front(struct list *l)
{
   struct node *n;
   if(is_empty(l)){
         n = NULL;
         goto out;
   }

  n = l->head->next;
  l->head->next = n->next; 
  l->size--;
  out:
  return n;       
}
