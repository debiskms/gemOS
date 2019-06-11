all: gemOS.kernel
SRCS = main.c lib.c idt.c kbd.c shell.c serial.c memory.c context.c entry.c apic.c schedule.c
OBJS = boot.o main.o lib.o idt.o kbd.o shell.o serial.o memory.o context.o entry.o apic.o schedule.o
CFLAGS  = -nostdlib -nostdinc -fno-builtin -fno-stack-protector -fpic -m64 -I./include -I../include 
LDFLAGS = -nostdlib -nodefaultlibs  -q -melf_x86_64 -Tlink64.ld
ASFLAGS = --64  
AS = as 


%o: %c
	gcc -c $(CFLAGS) $< -o $@ 

boot.o : boot_64.S
	$(AS) $(ASFLAGS) $^ -o $@

user/init.o:user/init.c
	gcc -c $(CFLAGS) user/init.c -o user/init.o
gemOS.kernel: $(OBJS) user/init.o
	ld $(LDFLAGS) -o $@ $(OBJS) user/init.o

.Phony: clean
clean:
	rm -f *.o; rm -f gemOS.kernel; rm -f user/*.o;
