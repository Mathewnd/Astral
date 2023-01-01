#include <arch/e9.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

static char* syscalls[] = {
	"syscall_libc_log",
	"syscall_mmap",
	"syscall_arch_ctl",
	"syscall_gettid",
	"syscall_open",
	"syscall_read",
	"syscall_lseek",
	"syscall_close",
	"syscall_isatty",
	"syscall_write",
	"syscall_stat",
	"syscall_fstat",
	"syscall_fork",
	"syscall_execve",
	"syscall_waitpid",
	"syscall_exit",
	"syscall_dup",
	"syscall_dup2",
	"syscall_fcntl",
	"syscall_getpid",
	"syscall_getdirent",
	"syscall_ioctl",
	"syscall_chdir",
	"syscall_fstatat",
	"syscall_pipe2",
	"syscall_mkdir",
	"syscall_munmap",
	"syscall_umask",
	"syscall_poll",
	"syscall_fchmodat",
	"syscall_openat",
	"syscall_chroot",
	"syscall_mkdirat",
	"syscall_clock_gettime",
	"syscall_socket",
	"syscall_bind",
	"syscall_listen",
	"syscall_connect",
	"syscall_accept",
	"syscall_nanosleep",
	"syscall_fchmod",
	"syscall_linkat",
	"syscall_recvmsg",
	"syscall_futex",
	"syscall_newthread",
	"syscall_threadexit"
	
};

void syscalllogger(uint64_t num, uint64_t error){
	
	e9_puts("syscall: ");
	e9_puts(syscalls[num]);
	e9_putc(':');
	char err[] = {'0', '0', '0', NULL};
	err[2] += error % 10;
	error /= 10;
	err[1] += error % 10;
	error /= 10;
	err[0] += error % 10;
	e9_puts(err);
	e9_putc('\n');

}
