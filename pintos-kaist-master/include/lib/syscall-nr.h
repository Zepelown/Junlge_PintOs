#ifndef __LIB_SYSCALL_NR_H
#define __LIB_SYSCALL_NR_H

/* System call numbers. 시스템 콜 번호
 * 각 시스템 콜을 고유하게 식별하기 위해 부여된 상수 값
 * enum(열거자: enumerators) 또는 심볼릭 상수(symbolic constants)
 * 특별히 값을 정하지 않으면 컴파일러에 의해 자동으로 0부터 시작하는 정수값이 할당됨(enum)의 특성
 */
enum {
	/* Projects 2 and later. */
	SYS_HALT,                   /* 0Halt the operating system. 운영 체제를 중단시킴.*/
	SYS_EXIT,                   /* 1Terminate this process. 현재 프로세스를 종료함*/
	SYS_FORK,                   /* 2Clone current process. 현재 프로세스를 복제함*/
	SYS_EXEC,                   /* 3Switch current process. 현재 프로세스를 다른 실행 파일로 교체함.*/
	SYS_WAIT,                   /* 4Wait for a child process to die. 자식 프로세스가 종료되기를 기다림.*/
	SYS_CREATE,                 /* 5Create a file. 파일을 생성함.*/
	SYS_REMOVE,                 /* 6Delete a file. 파일을 삭제함.*/
	SYS_OPEN,                   /* 7Open a file. 파일을 염.*/
	SYS_FILESIZE,               /* 8Obtain a file's size. 파일의 크기를 얻음.*/
	SYS_READ,                   /* 9Read from a file. 파일에서 읽음.*/
	SYS_WRITE,                  /* 10Write to a file. 파일에 씀.*/
	SYS_SEEK,                   /* 11Change position in a file. 파일 내의 위치를 변경*/
	SYS_TELL,                   /* 12Report current position in a file. 파일 내의 현재 위치를 보고함.*/
	SYS_CLOSE,                  /* 13Close a file. 파일을 닫음.*/

	/* Project 3 and optionally project 4. */
	SYS_MMAP,                   /* 14Map a file into memory. */
	SYS_MUNMAP,                 /* 15Remove a memory mapping. */

	/* Project 4 only. */
	SYS_CHDIR,                  /* 16Change the current directory. */
	SYS_MKDIR,                  /* 17Create a directory. */
	SYS_READDIR,                /* 18Reads a directory entry. */
	SYS_ISDIR,                  /* 19Tests if a fd represents a directory. */
	SYS_INUMBER,                /* 20Returns the inode number for a fd. */
	SYS_SYMLINK,                /* 21Returns the inode number for a fd. */

	/* Extra for Project 2 프로젝트2 추가 요구사항*/
	SYS_DUP2,                   /* 22Duplicate the file descriptor 파일 디스크립터 복제*/

	SYS_MOUNT, //23
	SYS_UMOUNT,//24
};

#endif /* lib/syscall-nr.h */
