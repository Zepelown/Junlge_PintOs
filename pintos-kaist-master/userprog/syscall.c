// #include "syscall.h"
// #include "lib/user/syscall.h"
#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "userprog/process.h"

void syscall_entry(void);

void syscall_handler(struct intr_frame *);

static int64_t get_user (const uint8_t *uaddr);
static bool put_user (uint8_t *udst, uint8_t byte);
int sys_write (int fd, const void *buffer, unsigned length);
void sys_exit(int status);
int exec(const char *cmd_line);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init(void) {
	write_msr(MSR_STAR, ((uint64_t) SEL_UCSEG - 0x10) << 48 |
	                    ((uint64_t) SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
	          FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

#define MAX_ARGS 6
/* The main system call interface */
void
syscall_handler(struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	// printf("system call!\n");
	// printf("system call Num : %llu\n", f->R.rax);
	int system_call_num = f->R.rax;
	// /*
	// SYS_HALT,                   /* Halt the operating system. */
	// SYS_EXIT,                   /* Terminate this process. */
	// SYS_FORK,                   /* Clone current process. */
	// SYS_EXEC,                   /* Switch current process. */
	// SYS_WAIT,                   /* Wait for a child process to die. */
	// SYS_CREATE,                 /* Create a file. */
	// SYS_REMOVE,                 /* Delete a file. */
	// SYS_OPEN,                   /* Open a file. */
	// SYS_FILESIZE,               /* Obtain a file's size. */
	// SYS_READ,                   /* Read from a file. */
	// SYS_WRITE,                  /* Write to a file. */
	// SYS_SEEK,                   /* Change position in a file. */
	// SYS_TELL,                   /* Report current position in a file. */
	// SYS_CLOSE,                  /* Close a file. */
	//
	switch (system_call_num) {
		case SYS_HALT:
			printf("HALT\n");
			break;
		case SYS_EXIT:
			// printf("EXIT\n");
			sys_exit(f->R.rdi);
			break;
		case SYS_FORK:
			printf("FORK\n");
			break;
		case SYS_EXEC:
			printf("EXEC\n");
			break;
		case SYS_WAIT:
			printf("WAIT\n");
			break;
		case SYS_CREATE:
			printf("CREATE\n");
			break;
		case SYS_REMOVE:
			printf("REMOVE\n");
			break;
		case SYS_OPEN:
			printf("OPEN\n");
			break;
		case SYS_FILESIZE:
			printf("FILESIZE\n");
			break;
		case SYS_READ:
			printf("READ\n");
			break;
		case SYS_WRITE: {
			// printf("WRITE\n");
			f->R.rax = sys_write(f->R.rdi, (char *) f->R.rsi, f->R.rdx);
			break;
		}
		case SYS_SEEK:
			printf("SEEK\n");
			break;
		case SYS_TELL:
			printf("TELL\n");
			break;
		case SYS_CLOSE:
			printf("CLOSE\n");
			break;
	}

}

int sys_write(int fd, const void *buffer, unsigned size){
	//printf("write\n");
	// if(fd == 0) exit(-1);
	// if(fd < 0) exit(-1);
	// if(fd >= FD_MAX) exit(-1);

	// 표준 출력
	// check_valid_buffer(buffer, size);
	if(fd == 1 || fd == 2){
		putbuf(buffer, (size_t)size);
		return (int)size;
	}
	else{
		// struct thread* curr = thread_current();
		// struct file* f = curr->fd_table->fd_entries[fd];
		// if(f == NULL) exit(-1);
		// return file_write(f, buffer, size);
	}
	return 0;
}


void sys_exit(int status) {
	printf("%s: exit(%d)\n", thread_name(), status);
	thread_current()->exit_status = status;
	thread_exit();
}


/* Reads a byte at user virtual address UADDR.
 * UADDR must be below KERN_BASE.
 * Returns the byte value if successful, -1 if a segfault
 * occurred. */
static int64_t
get_user (const uint8_t *uaddr) {
	int64_t result;
	__asm __volatile (
	"movabsq $done_get, %0\n"
	"movzbq %1, %0\n"
	"done_get:\n"
	: "=&a" (result) : "m" (*uaddr));
	return result;
}

/* Writes BYTE to user address UDST.
 * UDST must be below KERN_BASE.
 * Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte) {
	int64_t error_code;
	__asm __volatile (
	"movabsq $done_put, %0\n"
	"movb %b2, %1\n"
	"done_put:\n"
	: "=&a" (error_code), "=m" (*udst) : "q" (byte));
	return error_code != -1;
}

