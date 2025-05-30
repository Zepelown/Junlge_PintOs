// #include "syscall.h"
// #include "lib/user/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>

#include "filesys/file.h"
#include "filesys/filesys.h"
#include "devices/input.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "userprog/process.h"
#include "user/syscall.h"
#include "threads/palloc.h"
#include "lib/string.h"

void syscall_entry(void);

void syscall_handler(struct intr_frame *);

static int64_t get_user(const uint8_t *uaddr);

static bool put_user(uint8_t *udst, uint8_t byte);

int sys_write(int fd, const void *buffer, unsigned length);

int sys_exec(const char *cmd_line);

void sys_exit(int status);

int exec(const char *cmd_line);

int sys_read(int fd, void *buffer, unsigned length);

bool sys_create(const char *file, unsigned initial_size);

void check_address(void *addr);

int sys_open (const char *file);

int sys_filesize (int fd);
pid_t sys_fork(const char *thread_name, struct intr_frame *f);
int sys_wait (pid_t pid);

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
	switch (system_call_num) {
		case SYS_HALT:
			break;
		case SYS_EXIT:
			sys_exit(f->R.rdi);
			break;
		case SYS_FORK:
			// printf("FORK START\n");
			f->R.rax = sys_fork((char *)f->R.rdi, f);
			break;
		case SYS_EXEC:
			// printf("EXIT START\n");
			f->R.rax = sys_exec((char *) f->R.rdi);
			break;
		case SYS_WAIT:
			// printf("WAIT START\n");
			f->R.rax = sys_wait(f->R.rdi);
			break;
		case SYS_CREATE:
			// exist 제외 통과
			f->R.rax = sys_create((char *) f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE:
			break;
		case SYS_OPEN:
			// 모두 통과
			// printf("OPEN START\n");
			f->R.rax = sys_open((char *) f->R.rdi);
			break;
		case SYS_FILESIZE:
			f->R.rax = sys_filesize(f->R.rdi);
			break;
		case SYS_READ:
			// 모두 통과
			// printf("READ START\n");
			f->R.rax = sys_read(f->R.rdi, (void *) f->R.rsi, f->R.rdx);
			break;
		case SYS_WRITE:{
			// 모두 통과
			// printf("WRITE START\n");
			f->R.rax = sys_write(f->R.rdi, (char *) f->R.rsi, f->R.rdx);
			break;
		}
		case SYS_SEEK:
			break;
		case SYS_TELL:
			break;
		case SYS_CLOSE:
			// 모두 통과
			break;
	}
}

int sys_exec(const char *cmd_line) {
	// printf("cmd line : %s\n", cmd_line);
	// thread_exit();
	check_address(cmd_line);
	char *cmd_line_copy;
	cmd_line_copy = palloc_get_page(0);
	if (cmd_line_copy == NULL)
		sys_exit(-1);
	strlcpy(cmd_line_copy, cmd_line, PGSIZE);
	if (process_exec(cmd_line_copy) == -1)
		sys_exit(-1); // 실패 시 status -1로 종료한다.
}

int sys_read(int fd, void *buffer, unsigned length) {
	if (fd < 0 || fd == 1 || fd >= MAX_FDT_SIZE) {
		return -1;
	}
	// printf("read\n");
	if (length > 0) {
		check_address(buffer);
		check_address(buffer+length-1);

	} else {
		return 0;
	}

	if (fd == 0) {
		// 리드 테스트는 중에, 일단 그 전에 다른 테스트에서 터져서 진행 불가
		// input_getc();
		char *char_buffer = (char *)buffer;
		unsigned int i;
		for (i = 0; i < length; i++) {
			char key = input_getc(); // 한 문자 읽기
			// EOF (End-Of-File) 처리나 특정 종료 문자 처리 (예: '\0', '\n')
			if (key == '\0' || key == '\n') { // 예: 개행이나 NULL 만나면 중단
				char_buffer[i] = key; // 해당 문자까지 저장
				i++; // 실제 읽은 문자 수에 포함
				break;
			}
			char_buffer[i] = key;
		}
		return i; // 실제로 읽은 바이트 수 반환

	} else {
		// printf("fd > 0\n");
		// printf("length: %u\n", length);
		struct file * cur = thread_current()->fd_table[fd];
		if (cur == NULL) {
			return -1;
		}
		return file_read(cur, buffer, length);
	}
}

bool sys_create(const char *file, unsigned initial_size) {
	check_address((void *) file);
	check_address((void *)file+initial_size - 1);
	return filesys_create(file, initial_size);
}

// 이미 열려 있거나 존재하는 파일에 내용을 씀
int sys_write(int fd, const void *buffer, unsigned size) {
	// printf("write \n");
	if(fd == 0 || fd < 0 || fd >= MAX_FDT_SIZE) return -1;
	// printf("write normal\n");
	if (size > 0) {
		check_address((void *) buffer);
		check_address((void *) buffer + size - 1);
	} else {
		return 0;
	}
	if (fd == 1) {
		putbuf(buffer, size);
		return (int) size;
	}
	struct thread *curr = thread_current();
	struct file *f = curr->fd_table[fd];
	if (f == NULL) {
		return -1;
	}
	int bytes_written = file_write(f, buffer, size);
	// printf("bytes_written = %d\n", bytes_written);
	return bytes_written;
}
int sys_open (const char *file) {
	check_address((void *)file);
	struct file * cur = filesys_open(file);
	if (cur == NULL) {
		return -1;
	}
	thread_current()->next_fd++;
	thread_current()->fd_table[thread_current()->next_fd] = cur;
	return thread_current()->next_fd;
}

void sys_exit(int status) {
	printf("%s: exit(%d)\n", thread_name(), status);
	thread_current()->exit_status = status;
	thread_exit();
}

int sys_filesize (int fd) {
	struct file * cur = thread_current()->fd_table[fd];
	if (cur == NULL) {
		return -1;
	}
	return file_length(cur);
}

pid_t sys_fork(const char *thread_name, struct intr_frame *f) {
	// file_duplicate()
	// printf("FORK START\n");
	tid_t thread_id = process_fork(thread_name, f);

	if (TID_ERROR == thread_id) {
		return TID_ERROR;
	}
	return thread_id;
}

int sys_wait (pid_t pid) {
	// printf("WAIT START\n");
	return process_wait(pid);
}

int sys_close() {

}


/* Reads a byte at user virtual address UADDR.
 * UADDR must be below KERN_BASE.
 * Returns the byte value if successful, -1 if a segfault
 * occurred. */
static int64_t
get_user(const uint8_t *uaddr) {
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
put_user(uint8_t *udst, uint8_t byte) {
	int64_t error_code;
	__asm __volatile (
		"movabsq $done_put, %0\n"
		"movb %b2, %1\n"
		"done_put:\n"
		: "=&a" (error_code), "=m" (*udst) : "q" (byte));
	return error_code != -1;
}

void check_address(void *addr) {
	struct thread *t = thread_current();
	// Check if address is a user virtual address, not NULL, and mapped.
	if (!is_user_vaddr(addr) || addr == NULL || pml4_get_page(t->pml4, addr) == NULL) {
		sys_exit(-1); // Terminate the process
	}
}


