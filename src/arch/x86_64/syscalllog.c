#include <arch/e9.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <arch/cls.h>
#include <kernel/sched.h>

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

void syscalllogger(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6, uint64_t num){

	thread_t* thread = arch_getcls()->thread;
	proc_t* proc = thread->proc;

	char tmp[100];

	sprintf(tmp, "PID %d TID %d: %s (%d): %p %p %p %p %p %p\n", proc->pid, thread->tid, syscalls[num], num, arg1, arg2, arg3, arg4, arg5, arg6);
	
	e9_puts(tmp);

}

void syscalllogger_return(uint64_t value, uint64_t errno){
	char tmp[100];

	thread_t* thread = arch_getcls()->thread;
	proc_t* proc = thread->proc;
	
	sprintf(tmp, "PID %d TID %d: returned %p %d\n", proc->pid, thread->tid, value, errno);

	e9_puts(tmp);
}
