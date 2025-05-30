#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
void syscall_init (void);
int sys_write(int fd, const void *buffer, unsigned length);

int sys_exec(const char *cmd_line);

void sys_exit(int status);

int exec(const char *cmd_line);

int sys_read(int fd, void *buffer, unsigned length);

bool sys_create(const char *file, unsigned initial_size);

void check_address(void *addr);

int sys_open (const char *file);

int sys_filesize (int fd);
#endif /* userprog/syscall.h */
