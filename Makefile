KERNEL_SOURCE_FILE = kern/intr_entry.S lib/kern/print.S kern/interrupt.c kern/init.c dev/timer.c kern/main.c kern/debug.c lib/string.c lib/kern/bitmap.c kern/memory.c thread/thread.c thread/switch.S lib/kern/list.c thread/sync.c dev/console.c dev/keyboard.c dev/ioqueue.c userproc/tss.c userproc/process.c userproc/syscall_init.c lib/user/syscall.c lib/stdio.c lib/kern/stdio_kern.c dev/ide.c fs/fs.c fs/dir.c fs/file.c fs/inode.c userproc/fork.c lib/user/assert.c shell/shell.c shell/buildin_cmd.c userproc/exec.c userproc/wait_exit.c shell/pipe.c
KERNEL_OBJECT_FILE = kern/main.o kern/intr_entry.o kern/interrupt.o kern/init.o lib/print.o dev/timer.o kern/debug.o lib/string.o lib/bitmap.o kern/memory.o thread/thread.o thread/switch.o lib/list.o thread/sync.o dev/console.o dev/keyboard.o dev/ioqueue.o userproc/tss.o userproc/process.o userproc/syscall_init.o lib/syscall.o lib/stdio.o lib/stdio_kern.o dev/ide.o fs/fs.o fs/dir.o fs/file.o fs/inode.o userproc/fork.o lib/assert.o shell/shell.o shell/buildin_cmd.o userproc/exec.o userproc/wait_exit.o shell/pipe.o

boot.bin: boot/boot.S
	make -C boot boot.bin 
	cp boot/boot.bin ./

loader.bin: boot/loader.S
	make -C boot loader.bin
	cp boot/loader.bin ./

kernel.bin: $(KERNEL_SOURCE_FILE)
	make -C lib all 
	make -C dev all
	make -C kern all
	make -C thread all
	make -C userproc all
	make -C fs all
	make -C shell all
	ld $(KERNEL_OBJECT_FILE) -o ./system -T ./kernel.lds
	objcopy -I elf32-i386 -S -R .got.plt -R .comment -R .eh_frame -R .note.gnu.property ./system $@
	rm -rf ./system

symble_kernel.bin: $(KERNEL_SOURCE_FILE)
	make -C lib all 
	make -C dev all
	make -C kern all
	make -C thread all
	make -C userproc all
	make -C fs all
	make -C shell all
	ld $(KERNEL_OBJECT_FILE) -o ./system -T ./kernel.lds
	objcopy -I elf32-i386 -R .got.plt -R .comment -R .eh_frame -R .note.gnu.property ./system $@
	rm -rf ./system

all: boot.bin loader.bin kernel.bin

build:
	make all
	bximage -mode=create -hd=60M -q x86_system.img
	dd if=boot.bin of=x86_system.img bs=512 count=1 conv=notrunc
	dd if=loader.bin of=x86_system.img bs=512 count=5 seek=1 conv=notrunc
	dd if=kernel.bin of=x86_system.img bs=512 count=200 seek=9 conv=notrunc

clean:
	make -C boot clean
	make -C kern clean
	make -C lib clean
	make -C dev clean
	make -C thread clean
	make -C userproc clean
	make -C fs clean
	make -C shell clean
	rm -rf *.bin
	rm -rf *.o
	rm -rf *.img
