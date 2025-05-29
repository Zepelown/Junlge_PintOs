#ifndef THREADS_INTERRUPT_H
#define THREADS_INTERRUPT_H

#include <stdbool.h>
#include <stdint.h>
//enum은 enumeration(열거형)을 의미하는 C언어의 키워드
/* Interrupts on or off? */
enum intr_level {
	INTR_OFF,             /* Interrupts disabled. */
	INTR_ON               /* Interrupts enabled. */
};

enum intr_level intr_get_level (void); //현재 인터럽트 상태를 반환
enum intr_level intr_set_level (enum intr_level); //레벨에 따라 인터럽트를 켜거나 끔. 이전 인터럽트 상태를 반환함
enum intr_level intr_enable (void);//인터럽트를 켭니다. 이전 인터럽트 상태를 반환함.
enum intr_level intr_disable (void);//인터럽를 끕니다. 이전 인터럽트 상태를 반환함.

/* Interrupt stack frame. */
struct gp_registers {
	uint64_t r15;
	uint64_t r14;
	uint64_t r13;
	uint64_t r12;
	uint64_t r11;
	uint64_t r10;
	uint64_t r9;
	uint64_t r8;
	uint64_t rsi;
	uint64_t rdi;
	uint64_t rbp;
	uint64_t rdx;
	uint64_t rcx;
	uint64_t rbx;
	uint64_t rax;
} __attribute__((packed));

struct intr_frame {
	/* Pushed by intr_entry in intr-stubs.S.
	   These are the interrupted task's saved registers.
	   intr-stubs.S의 intr_entry에 의해 푸시됨. 이것들은 인터럽트된 작업의
	   저장된 레지스터들임.*/
	struct gp_registers R;//범용 레지스터들을 담는 구조체(예: rax, rbx, rdi, rsi 등)
	uint16_t es; //es세그먼트 레지스터
	uint16_t __pad1; //패딩
	uint32_t __pad2; //패딩
	uint16_t ds; //DS 세그먼트 레지스터
	uint16_t __pad3; //패딩
	uint32_t __pad4; //패딩
	/* Pushed by intrNN_stub in intr-stubs.S. intr-stubs.S의 intrNN_stub에 의해 푸시됩니다.*/
	uint64_t vec_no; /* Interrupt vector number. 인터럽트 벡터 번호.*/
/* Sometimes pushed by the CPU,
   otherwise for consistency pushed as 0 by intrNN_stub.
   The CPU puts it just under `eip', but we move it here.
	때로는 CPU에 의해 푸시되기도 하고,
	그렇지 않으면 일관성을 위해 intrNN_stub에 의해 0으로 푸시됩니다.
	CPU는 이것을 `rip` 바로 아래에 두지만, 우리는 여기로 옮겼습니다. */
	uint64_t error_code; //오류 코드(특정 예외 발생 시 CPU가 푸시)
/* Pushed by the CPU.
   These are the interrupted task's saved registers.
   CPU에 의해 푸시됨.
   이것들은 인터럽트된 작업의 저장된 레지스터들임.*/
	uintptr_t rip; //명령어 포인터 (다음에 실행할 명령어 주소)
	uint16_t cs; //CS 세그먼트 레지스터 (코드 세그먼트)
	uint16_t __pad5; //패딩
	uint32_t __pad6; //패딩
	uint64_t eflags; //EFLAGS 레지스터 (CPU 상태 플래그)
	uintptr_t rsp; //스택 포인터
	uint16_t ss; //SS 세그먼트 레지스터 (스택 세그먼트)
	uint16_t __pad7; //패딩
	uint32_t __pad8; //패딩
} __attribute__((packed)); //패딩 바이트를 최소화하기 위한 컴파일러 지시자

typedef void intr_handler_func (struct intr_frame *);

void intr_init (void);
void intr_register_ext (uint8_t vec, intr_handler_func *, const char *name);
void intr_register_int (uint8_t vec, int dpl, enum intr_level,
                        intr_handler_func *, const char *name);
bool intr_context (void);
void intr_yield_on_return (void);

void intr_dump_frame (const struct intr_frame *);
const char *intr_name (uint8_t vec);

#endif /* threads/interrupt.h */
