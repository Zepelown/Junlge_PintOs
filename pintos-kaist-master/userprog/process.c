#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include <bits/sigcontext.h>

#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "threads/pte.h"
#include "intrinsic.h"

#include "threads/synch.h" // struct semaphore 사용을 위해

#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);

void argument_stack(char **parse, int count, void **esp);

static char *allocate_file_name_copy(const char *file_name);

struct fork_boomo_data // process_fork(부모 컨텍스트)에서 __do_fork(자식 컨텍스트)로 정보를 전달하고 알리기 위한 구조체.
{
	struct thread *parent_thread; // fork를 호출한 부모 스레드의 포인터
	struct intr_frame *parent_if; // fork 호출 시점의 부모 사용자 인터럽트 프레임
	struct semaphore child_sema; //자식의 자원 복제 완료/실패를 부모에게 알릴 세마포어
	bool fork_success; //자식의 자원 복제 성공 여부(자식이 설정)
	//tid_t child_tid; 선택사항 자식이 자신의 tid를 알아야 할 경우
};

/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */

//새로운 커널 스레드를 만드는 것
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid;

	fn_copy = allocate_file_name_copy(file_name);
	if (!fn_copy)
		return TID_ERROR;

	tid = thread_create (file_name, PRI_DEFAULT, initd, fn_copy);
	if (tid == TID_ERROR)
		palloc_free_page (fn_copy); // 할당 해제
	return tid;
}

/* A thread function that launches first user process. 새로운 커널 스레드의 시작점 */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();

	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

static struct thread *get_child_process(int tid)
{
	struct thread *cur = thread_current ();
	struct list_elem *e;

	for (e = list_begin(&cur->child_list); e != list_end(&cur->child_list); e = list_next(e)) {
		struct thread *t = list_entry(e, struct thread, child_elem);
		if (t->tid == tid) {
			return t;
		}
	}
	return NULL;
}


int process_add_file(struct file *f)
{
	struct thread *current = thread_current ();
	struct file **fd_table = current->fd_table;

	//limit을 넘지 않는 범위 안에서 빈 자리 탐색
	while (current->fd_idx < FDT_COUNT_LIMIT && fd_table[current->fd_idx])
		current->fd_table++;
	if (current->fd_idx >= FDT_COUNT_LIMIT)
		return -1;
	fd_table[current->fd_idx] = f;

	return current->fd_idx;
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t
process_fork (const char *name, struct intr_frame *if_) {
	struct thread *cur = thread_current ();


	memcpy (&cur->parent_if, if_, sizeof (struct intr_frame));

	tid_t tid = thread_create (name, PRI_DEFAULT, __do_fork, cur);

	if (tid == TID_ERROR) {
		return TID_ERROR;
	}

	struct thread *child = get_child_process(tid);
	if (child == NULL) {
		// 여기서 자식 스레드가 생성은 되었으나 리스트에 추가되지 않았거나 하는 문제일 수 있음
		// 실제로는 여기서 TID_ERROR를 반환하기 전에 생성된 스레드에 대한 정리 작업이 필요할 수 있음
		return TID_ERROR;
	}
	sema_down(&child->fork_sema); // 자식 스레드의 __do_fork 완료 대기

	if (child->exit_status == TID_ERROR) { // __do_fork에서 오류 발생
		// 자식 스레드 정리 로직 필요 (예: 리스트에서 제거, 자원 해제 등)
		// list_remove(&child->child_elem);
		// palloc_free_page(child); // 주의: 이 시점에 자식 스레드는 이미 종료되어 자원이 해제되었을 수 있음
		return TID_ERROR;
	}
	return tid;
}
#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *child = (struct thread *) aux;

	// 커널 주소 공간은 복사 대상이 아님
	if (is_kernel_vaddr(va)) {
		return true;
	}

	// 매핑되지 않은 페이지는 건너뜀
	if (!(*pte & PTE_P)) {
		return true;
	}

	// PTE로부터 커널 가상 주소와 쓰기 가능 여부를 얻음 (표준 방식)
	void *parent_page = ptov(PTE_ADDR(*pte));
	bool writable = (*pte & PTE_W) != 0;

	// 1. 자식 프로세스를 위한 새 물리 페이지(프레임) 할당
	void *child_page = palloc_get_page(PAL_USER | PAL_ZERO);
	if (child_page == NULL) {
		return false; // 메모리 할당 실패
	}

	// 2. 부모 페이지의 내용을 자식 페이지로 복사
	memcpy(child_page, parent_page, PGSIZE);

	// 3. 자식의 페이지 테이블에 새 페이지를 매핑
	if (!pml4_set_page(child->pml4, va, child_page, writable)) {
		palloc_free_page(child_page);
		return false; // 매핑 실패
	}

	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork (void *aux) {
	struct thread *cur = thread_current ();
	struct thread *parent = (struct thread *) aux;

	/* 1. 부모의 인터럽트 프레임을 자식에게 복사하고, 반환 값을 0으로 설정 */
	memcpy (&cur->tf, &parent->parent_if, sizeof (struct intr_frame));
	cur->tf.R.rax = 0;

	/* 2. 페이지 테이블 생성 및 복제 */
	cur->pml4 = pml4_create();
	if (cur->pml4 == NULL)
		goto error;

	if (!pml4_for_each (parent->pml4, duplicate_pte, cur)){
		// pml4_for_each 실패 시 pml4 해제 필요
		pml4_destroy(cur->pml4);
		cur->pml4 = NULL; // 안전하게 NULL로 설정
		goto error;
	}

	/* 3. 파일 디스크립터 복제 */
	cur->fd_table = palloc_get_multiple(PAL_ZERO, FDT_PAGES);
	if (cur->fd_table == NULL) {
		pml4_destroy(cur->pml4); // 이전 단계에서 할당한 pml4 해제
		cur->pml4 = NULL;
		goto error;
	}

	cur->fd_idx = parent->fd_idx;
	for (int i = 0; i < parent->fd_idx; i++) { // 부모가 사용하는 fd 개수만큼만 순회
		struct file *f = parent->fd_table[i];
		if (f == NULL) {
			cur->fd_table[i] = NULL; // 명시적으로 NULL 설정 (PAL_ZERO로 이미 0이겠지만)
			continue;
		}
		// Pintos에서는 보통 STDIN_FILENO (0)과 STDOUT_FILENO (1)에
		// 실제 file 객체 대신 특별한 값(예: 1, 2)을 사용하기도 합니다.
		// 이 경우 해당 값만 복사합니다.
		// 실제 파일 객체라면 file_duplicate를 사용합니다.
		// 프로젝트의 표준 입출력 처리 방식에 따라 이 조건은 달라질 수 있습니다.
		if (f == (struct file *)STDIN_FILENO || f == (struct file *)STDOUT_FILENO) { // 예시: STDIN_FILENO 등이 0, 1로 define 되어 있다고 가정
			cur->fd_table[i] = f;
		} else if (i < 2) { // 또는 단순히 인덱스 0, 1을 특별 취급
             cur->fd_table[i] = f;
        }
		else {
			struct file *df = file_duplicate(f);
			if (df == NULL) { // file_duplicate 실패
				// 이전에 복제 성공한 파일들(인덱스 2부터 i-1까지)을 닫아야 함
				for (int j = 2; j < i; j++) {
					if (cur->fd_table[j]) file_close(cur->fd_table[j]);
				}
				palloc_free_multiple(cur->fd_table, FDT_PAGES); // fd_table 해제
				cur->fd_table = NULL;
				pml4_destroy(cur->pml4); // pml4 해제
				cur->pml4 = NULL;
				goto error;
			}
			cur->fd_table[i] = df;
		}
	}
    // 나머지 fd_table 슬롯은 PAL_ZERO에 의해 이미 0으로 채워져 있어야 함

    // running_file 복제 (필요하다면)
    if (parent->running_file) {
        cur->running_file = file_duplicate(parent->running_file);
        if (cur->running_file == NULL) {
            // 위와 유사하게 자원 정리 후 goto error
            // (fd_table 및 복제된 fd들, pml4 정리)
            goto error; // 단순화된 에러 처리, 실제로는 더 많은 정리 필요
        }
    }


	/* 부모의 자식 리스트에 자신을 추가 */
	list_push_back(&parent->child_list, &cur->child_elem);

	/* fork 완료를 부모에게 알림 */
	sema_up(&cur->fork_sema);

	do_iret (&cur->tf);

	error:
	// 자식 스레드(cur)에 할당된 자원들을 해제합니다.
		if (cur->pml4) {
			pml4_destroy(cur->pml4); // 페이지 테이블 제거
			cur->pml4 = NULL;
		}
	if (cur->fd_table) {
		// 이미 복제된 파일 디스크립터들(표준 입출력 제외) 닫기
		// cur->fd_idx가 parent->fd_idx로 설정된 상태이므로,
		// 실제 cur->fd_table에 할당된 것들만 닫도록 주의
		for (int i = 2; i < cur->fd_idx; i++) { // 0, 1은 표준 입출력이므로 닫지 않음
			if (cur->fd_table[i] && parent->fd_table[i] != cur->fd_table[i]) { // 복제된 파일인 경우
				file_close(cur->fd_table[i]);
			}
		}
		palloc_free_multiple(cur->fd_table, FDT_PAGES); // fd_table 자체 해제
		cur->fd_table = NULL;
	}
	if (cur->running_file && parent->running_file != cur->running_file) { // 복제된 실행 파일인 경우
		file_close(cur->running_file);
		cur->running_file = NULL;
	}
	// 필요하다면 다른 자원들도 해제

	cur->exit_status = TID_ERROR; // 또는 -1
	sema_up(&cur->fork_sema); // 부모에게 실패 알림
	thread_exit ();           // 스레드 종료
}


/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
int
process_exec (void *f_name) {
	char *file_name = f_name;
	bool success;

	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context  현재(사용자) 컨텍스트를 정리함*/
	process_cleanup ();

	/* And then load the binary 그리고나서 바이너리를 로드함*/
	success = load (file_name, &_if);

	/* If load failed, quit. */
		palloc_free_page (file_name);
	if (!success)
		return -1;

	//make check(test)할 때 기대하는 출력문과 달라지게 되어 fail 원인이 됨
	// hex_dump((uintptr_t)_if.rsp, (void *)_if.rsp, USER_STACK - (uintptr_t)_if.rsp, true);
	/* Start switched process. */
	do_iret (&_if);
	NOT_REACHED ();
}


/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int
process_wait (tid_t child_tid UNUSED) {
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */
	for (int i = 0; i < 2000000000; i++)
	{

	}
	return -1;
}

/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
	struct thread *curr = thread_current ();
	/* TODO: Your code goes here.
	 * TODO: Implement process teurmination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */

	process_cleanup ();
}

// process_create_initd, load 함수 내 strtok_r 사용 시 문자열 복사본 생성
static char *allocate_file_name_copy(const char *file_name) {
	char *fn_copy = palloc_get_page(PAL_ZERO);
	if (fn_copy == NULL) {
		return NULL;
	}
	strlcpy(fn_copy, file_name, PGSIZE);
	return fn_copy;
}


/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_, char *command_line);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	char *fn_copy;        // file_name의 수정 가능한 복사본
    char *exec_file_name; // 실제 실행할 파일 이름 (첫 번째 토큰)
    char *save_ptr_load;  // load 함수 내 strtok_r 용 save_ptr

	/* 1. file_name의 수정 가능한 복사본 생성 */
	fn_copy = allocate_file_name_copy(file_name);
	if (!fn_copy) {
		return false; // 메모리 할당 실패
	}

    /* 1-1. 실행 파일 이름 분리 (원본 fn_copy는 setup_stack을 위해 보존) */
    // strtok_r은 원본을 변경하므로, 실행 파일 이름만 얻기 위해 임시 복사본을 사용하거나,
    // fn_copy에서 첫 토큰만 얻고 setup_stack에서 다시 fn_copy를 파싱하도록 합니다.
    // 여기서는 간단히 첫 토큰만 얻는 방식을 사용합니다.
    // (주의: 이 방식은 fn_copy를 변경하므로, setup_stack에 fn_copy를 그대로 넘기려면
    //  setup_stack에서 strtok_r(fn_copy, ...)를 다시 호출해야 합니다.
    //  또는 fn_copy의 복사본을 만들어 strtok_r에 사용합니다.)
    char *temp_fn_for_first_token = allocate_file_name_copy(fn_copy);
    if (!temp_fn_for_first_token) {
        palloc_free_page(fn_copy);
        return false;
    }
    exec_file_name = strtok_r(temp_fn_for_first_token, " ", &save_ptr_load);
    if (exec_file_name == NULL) { // 실행할 파일 이름이 없는 경우
        palloc_free_page(temp_fn_for_first_token);
        palloc_free_page(fn_copy);
        return false;
    }


	/* 2. 페이지 디렉토리 할당 및 활성화 */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL) {
        palloc_free_page(temp_fn_for_first_token);
		palloc_free_page(fn_copy);
		goto done;
	}
	process_activate (thread_current ());

	/* 3. 실행 파일 열기 (파싱된 exec_file_name 사용) */
	file = filesys_open (exec_file_name);
	if (file == NULL) {
		printf ("load: %s: open failed\n", exec_file_name);
		goto done;
	}

	t->running_file = file;
	file_deny_write(file);

	/* 4. ELF 헤더 읽기 및 검증 */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", exec_file_name);
		goto done;
	}

	/* 5. 프로그램 헤더 읽기 및 세그먼트 로드 */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			// (기존 PT_LOAD 처리 로직과 동일)
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* 6. 스택 설정 (수정된 setup_stack 호출, fn_copy 전달) */
	if (!setup_stack (if_, fn_copy)) // fn_copy를 command_line으로 전달
		goto done;

	/* 7. 프로그램 시작 주소 설정 */
	if_->rip = ehdr.e_entry;

	success = true;

done:
	if (file != NULL) {
		file_close (file);
    }
    // 임시 복사본 해제
    if (temp_fn_for_first_token != NULL) { // temp_fn_for_first_token이 할당된 경우에만 해제
        palloc_free_page(temp_fn_for_first_token);
    }
    // 원본 복사본 해제
	if (fn_copy != NULL) {
	    palloc_free_page (fn_copy);
    }
	return success;
}


/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_, char *command_line) {
	char *token, *save_ptr;
	int argc = 0;
	char *argv_values[128]; // 파싱된 토큰(문자열 자체)을 저장

	// 1. command_line (fn_copy)을 파싱하여 argv_values 배열에 토큰 저장
	for (token = strtok_r(command_line, " ", &save_ptr); token != NULL; token = strtok_r(NULL, " ", &save_ptr)) {
		if (argc < 128) {
			argv_values[argc++] = token;
		} else {
			// 너무 많은 인자 처리
			return false;
		}
	}

	// 사용자 스택에 인자 저장 (역순으로)
	char *user_argv_addrs[128]; // 스택에 저장된 각 인자 문자열의 주소를 임시 저장

	// 2. 실제 문자열들을 사용자 스택에 복사 (역순으로)
	for (int i = argc - 1; i >= 0; i--) {
		int len = strlen(argv_values[i]) + 1; // NULL 문자 포함 길이
		if_->rsp -= len;
		memcpy((void *)if_->rsp, argv_values[i], len);
		user_argv_addrs[i] = (char *)if_->rsp; // 스택에 복사된 문자열의 주소 저장
	}

	// 3. 워드 정렬 (8바이트 경계로 맞춤)
	while (if_->rsp % 8 != 0) {
		if_->rsp--;
		*(uint8_t *)if_->rsp = 0; // 패딩 바이트는 0으로 채움
	}

	// 4. argv 문자열 주소 배열의 끝을 표시하는 NULL 포인터 푸시
	if_->rsp -= sizeof(char *);
	*(char **)(if_->rsp) = NULL;

	// 5. 스택에 저장된 각 인자 문자열의 주소들(포인터)을 푸시 (역순으로)
	for (int i = argc - 1; i >= 0; i--) {
		if_->rsp -= sizeof(char *);
		*(char **)(if_->rsp) = user_argv_addrs[i];
	}

	// 6. 레지스터 설정: %rdi = argc, %rsi = argv (스택 상의 주소)
	if_->R.rdi = argc;
	if_->R.rsi = if_->rsp; // 현재 스택 포인터가 argv 배열의 시작 주소가 됨

	// 7. 가짜 반환 주소 (0) 푸시
	if_->rsp -= sizeof(void *);
	*(void **)(if_->rsp) = NULL;

	return true;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		void *aux = NULL;
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, aux))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
// static bool
// setup_stack (struct intr_frame *if_, char *command_line) {
// 	bool success = false;
// 	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);
//
// 	/* TODO: Map the stack on stack_bottom and claim the page immediately.
// 	 * TODO: If success, set the rsp accordingly.
// 	 * TODO: You should mark the page is stack. */
// 	/* TODO: Your code goes here */
//
// 	return success;
// }
#endif /* VM */
