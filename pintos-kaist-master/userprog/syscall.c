// #include "syscall.h"
#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>

// #include "directory.h"
#include "userprog/syscall.h"

#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/palloc.h"
#include "string.h"
#include "userprog/process.h"
#include "threads/synch.h"

#include "threads/vaddr.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

static bool addr_gumsa(const void *uaddr);
static bool string_gumsa(const char *ustr);
static bool buffer_gumsa(const void *ubuffer, unsigned size, bool write);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual.
 * 시스템 콜.
 * 이전에는 시스템 콜 서비스가 인터럽트 핸들러에 의해 처리되었습니다
   (예: 리눅스의 int 0x80). 그러나 x86-64에서는 제조사가 시스템 콜을
   요청하기 위한 효율적인 경로인 syscall 명령어를 제공합니다.
   syscall 명령어는 모델 특정 레지스터(Model Specific Register, MSR)에서
   값을 읽어와 작동합니다. 자세한 내용은 매뉴얼을 참조하십시오. */

#define MSR_STAR 0xc0000081         /* Segment selector msr 세그먼트 셀렉터 MSR*/
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target 롱 모드(64비트) SYSCALL 목적지 주소*/
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags EFLAGS 레지스터를 위한 마스크*/

//검사 선언
static bool addr_gumsa(const void *uaddr);
static bool string_gumsa(const char *ustr);
static bool buffer_gumsa(const void *ubuffer, unsigned size, bool write);

//전역 락 선언
static struct lock filesys_lock;

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32); //SEL_UCSEG는 유저 코드 세그먼트 셀렉터, SEL_KCSEG는 커널 코드 세그먼트 셀렉터
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL.
	 * 인터럽트 서비스 루틴(여기서는 syscall_entry를 의미)은 syscall_entry가
       사용자 영역 스택을 커널 모드 스택으로 전환하기 전까지는 어떠한
	   인터럽트도 처리해서는 안 됩니다. 따라서 우리는 FLAG_IF (원문 주석의 FLAG_FL은 오타일 가능성이 높으며,
	   인터럽트 활성화 플래그인 FLAG_IF를 의미하는 것으로 해석)를 마스크했습니다(비활성화했습니다).*/
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);


	lock_init(&filesys_lock);
}

//유효성 검사
//1. 단일 사용자 주소 유효성 검사
static bool addr_gumsa(const void *uaddr)
{
	if (uaddr == NULL || !is_user_vaddr(uaddr)) //사용자 주소가 NULL이 아니며 커널 미만인지 확인(사용자 영역)
	{
		return false;
	}

	if (pml4_get_page(thread_current()->pml4, uaddr) == NULL)//현재 프로세스의 페이지 테이블에 해당 주소가 매핑됬는지 확인
	{
		return false; // 페이지가 매핑되어 있지 않음
	}
	return true;
}

//2. 사용자 문자열 유효성 검사
static bool string_gumsa(const char *ustr)
{
	if (!addr_gumsa(ustr))//시작 주소부터 유효한지
	{
		return false;
	}

	const char *ptr = ustr;
	while (true)
	{
		if (addr_gumsa(ptr))
		{
			return false;
		}

		if (*ptr == '\0')
		{
			break;
		}
		ptr++;
	}
	return true;
}

//3. 버퍼 검사
static bool buffer_gumsa(const void *ubuffer, unsigned size, bool write)
{
	if (size == 0)
	{
		return true; // 크기가 0인 버퍼는 일단 유효
	}

	const char *ptr = (const char *)ubuffer;

	for (unsigned i = 0; i < size; i++)
	{
		if (!addr_gumsa(ptr + i))
		{
			return false;
		}
	}
	return true;
}


//  SYS_HALT,                   /* Halt the operating system. 운영 체제를 중단시킴.*/
// 	SYS_EXIT,                   /* Terminate this process. 현재 프로세스를 종료함*/
// 	SYS_FORK,                   /* Clone current process. 현재 프로세스를 복제함*/
// 	SYS_EXEC,                   /* Switch current process. 현재 프로세스를 다른 실행 파일로 교체함.*/
// 	SYS_WAIT,                   /* Wait for a child process to die. 자식 프로세스가 종료되기를 기다림.*/
// 	SYS_CREATE,                 /* Create a file. 파일을 생성함.*/
// 	SYS_REMOVE,                 /* Delete a file. 파일을 삭제함.*/
// 	SYS_OPEN,                   /* Open a file. 파일을 염.*/
// 	SYS_FILESIZE,               /* Obtain a file's size. 파일의 크기를 얻음.*/
// 	SYS_READ,                   /* Read from a file. 파일에서 읽음.*/
// 	SYS_WRITE,                  /* Write to a file. 파일에 씀.*/
// 	SYS_SEEK,                   /* Change position in a file. 파일 내의 위치를 변경*/
// 	SYS_TELL,                   /* Report current position in a file. 파일 내의 현재 위치를 보고함.*/
// 	SYS_CLOSE,					/* Close a file. 파일을 닫음.*/

//프로세스 관련 함수
static void halt()
{
	power_off();
}


static void exit(int status)
{
	struct thread* cur_thread = thread_current();

	cur_thread->exit_status = status; //스레드에 exit상태 저장

	printf("%s: exit(%d)\n", cur_thread->name, status);

	thread_exit();
}

static int exec(const char *cmd_line)
{
	if (!addr_gumsa(cmd_line))
	{
		return -1;
	}

	char *cmd_line_copy;
	cmd_line_copy = palloc_get_page(0);
	if (cmd_line_copy == NULL)
		exit(-1);
	strlcpy(cmd_line_copy, cmd_line, PGSIZE);

	if (process_exec(cmd_line_copy) == -1)
		exit(-1);


}

static int fork(const char *thread_name, struct intr_frame *f)
{
	return process_fork(thread_name, f);
}

//파일 시스템 관련 시스템 콜 함수
static bool create (const char *file_name, unsigned initial_size)
{
	bool success = false;// 일단 실패로 초기화

	if (!addr_gumsa(file_name))//주소의 유효성 검사
	{
		return false;
	}

	lock_acquire(&filesys_lock);//전역 락 획득, 파일 시스템 함수를 호출하기 전후에는 반드시 전역 파일 시스템 락을 사용하여 코드 블록 보호

	success = filesys_create (file_name, initial_size);//함수 호출

	lock_release(&filesys_lock);//전역 락 해제

	return success;
}

static bool remove (const char *file_name)
{
	bool success = false;// 일단 실패로 초기화

	if (!addr_gumsa(file_name))//주소의 유효성 검사
	{
		return false;
	}

	lock_acquire(&filesys_lock);//전역 락 획득, 파일 시스템 함수를 호출하기 전후에는 반드시 전역 파일 시스템 락을 사용하여 코드 블록 보호

	success = filesys_remove (&file_name);//함수 호출

	lock_release(&filesys_lock);//전역 락 해제

	return success;

}

static int open (const char *file_name)
{
	if (!addr_gumsa(file_name))
	{
		return -1;
	}

	struct file *file = filesys_open(file_name);

	if (file == NULL)
	{
		return -1;
	}

	int fd = process_add_file(file);
	if (fd == -1)
		file_close(file);
	return fd;
}



// static int write(int fd, const void *buffer, unsigned length)
// {
// 	if (fd == 1)
// 	{
// 		putbuf()
// 	}
// 	else(fd < 2)
// 	{
//
// 	}
//
// }

//rax 시스템 콜 번호
//rdi 1번째 인자
//rsi 2번째 인자
//rdx 3번째 인자
//r10 4번째 인자
//r8 5번째 인자
//r9 6번째 인자

//fd 0: 입력 stdin
//fd 1: 출력 stdout
//fd 2: 오류 stderr

/* The main system call interface */
void
syscall_handler (struct intr_frame *f)
{
	printf("PINTOS_DBG: syscall_handler entered. Syscall num from RAX: %lld\n", (long long)f->R.rax);
	// TODO: Your implementation goes here.

	int syscall_no = f->R.rax;

	switch (syscall_no)
	{
	//프로세스 시스템 콜
	case SYS_HALT:
		{
			halt();
			break;
		}

	case SYS_EXIT:
		{
			int exit_status = (int)f->R.rdi;
			exit(exit_status);
			break;
		}

	case SYS_EXEC:
		{
			f->R.rax = exec ((const char *)f->R.rdi);
			break;
		}

	case SYS_FORK:
		{
			f->R.rax = fork(f->R.rdi, f);
			break;
		}

	case SYS_WAIT:
		{

			break;
		}

	//파일 시스템 콜
	case SYS_WRITE:
		{
			putbuf((const char *)f->R.rsi,f->R.rdx);
			break;
		}

	case SYS_CREATE:
		{
			const char *file_name = (const char *)f->R.rdi;
			unsigned initial_size = (unsigned)f-> R.rsi;

			bool result = create(file_name, initial_size);//sys_create함수 호출

			f->R.rax = result;//결과를 rax레지스터에 반환
			break;
		}

	case SYS_REMOVE:
		{
			const char *file_name = (const char *)f->R.rdi;

			bool result = remove(file_name);//sys_create함수 호출

			f->R.rax = result;//결과를 rax레지스터에 반환

			break;
		}

	case SYS_OPEN:
		{
			f->R.rax = open ((const char *)f->R.rdi);
			break;
		}

		printf ("system call!\n");
	}
}
