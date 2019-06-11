#include <kbd.h>
void kbd_reboot(void)
{
    u8 temp;
    asm volatile ("cli"); /* disable all interrupts */
 
    /* Clear all keyboard buffers (output and command buffers) */
    do
    {
        temp = inb(KBD_CTRL_PORT); /* empty user data */
        if(temp & (1 << KBD_CDATA_BIT))
            inb(KBD_DATA_PORT); /* empty keyboard data */
    } while (temp & ( 1 << KBD_DDATA_BIT));
 
    outb(KBD_CTRL_PORT, KBRD_RESET); /* pulse CPU reset line */

    loop:
    asm volatile ("hlt"); /* if that didn't work, halt the CPU */
    goto loop; /* But if an interrupt is received, halt again */ 
}
void kbd_read(char *s){
   u8 to_read = 1;
   u32 ctr = 0; 
    while(to_read){
      if(inb(KBD_CTRL_PORT) & 0x1){
            u8 scancode = inb(KBD_DATA_PORT);
            if(scancode & 0x80){ /*Handle SHIFT etc. for key release codes*/
                switch(scancode){
                      case SC_LSFT_REL:
                                      kbd_status ^= (1 << LSHIFT);
                                      break; 
                      case SC_RSFT_REL:
                                      kbd_status ^= (1 << RSHIFT);
                                      break; 
                      default:
                                   break;
                }
            }else{
                switch(scancode){
                     case SC_ERROR:
                                      break;
                     case SC_CAPS:
                                      kbd_status ^= (1 << CAPS_ON);
                                      break;
                     case SC_LSFT:    
                                      kbd_status ^= (1 << LSHIFT);
                                      break;
                     case SC_RSFT:    
                                      kbd_status ^= (1 << RSHIFT);
                                      break;
                     default:
                                      if(kbd_status == 0x1 || kbd_status == 0x2 || 
                                         kbd_status == 0x4){
                                          printf("%c",kbmap_upper[scancode]);
                                          s[ctr++] = kbmap_upper[scancode];
                                      }else{
                                          printf("%c",kbmap_base[scancode]);
                                          s[ctr++] = kbmap_base[scancode];
                                      }    
                                      if(kbmap_base[scancode] == '\n'){
                                           s[--ctr] = 0;    
                                           to_read = 0;
                                      }
                                   
                }
       //         printf("scancode = %d ascii = %d char = %c \n", scancode, kbmap_base[scancode], kbmap_base[scancode]);
            }
      /*      if(kbmap_base[scancode] == '\n')
                 to_read = 0;*/
      } 
   }
  return;
}
 
