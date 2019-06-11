#include<serial.h>
static u16 base = 0x3f8;  /* ttyS0 */;

void serial_init()
{
   u32 div = 115200 / BAUD;
   u8 c;

   outb(base + LCR, 0x3);        /* 8n1 */
   outb(base + IER, 0);  /* no interrupt */
   outb(base + FCR, 0);  /* no fifo */
   outb(base + MCR, 0x3);        /* DTR + RTS */

   c = inb(base + LCR);
   outb(base + LCR, c | DLAB);
   outb(base + DLL, div & 0xff);
   outb(base + DLH, (div >> 8) & 0xff);
   outb(base + LCR, c & ~DLAB);
}

static void serial_putc(u8 ch)
{
        while ((inb(base + LSR) & XMTRDY) == 0);
        outb(base + TXR, ch);
}

void serial_write(char *buf)
{
    u8 ch;
    while (ch = *buf++){
        serial_putc(ch);
        if(ch == '\n')
             serial_putc('\r');
    }
}

static u8 serial_getc()
{
        while ((inb(base + LSR) & RCVRDY) == 0);
        return inb(base + RXR);
}


void serial_read(char *buf)
{
   u32 ctr = 0;
   u8 ch;
   while((ch=serial_getc()) != '\r')
        buf[ctr++] = ch;
   buf[ctr] = 0;

   while((ch=serial_getc()) != '\n'); 

   return;
}
