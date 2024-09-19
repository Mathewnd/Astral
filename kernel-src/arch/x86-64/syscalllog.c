#include <logging.h>
#include <arch/e9.h>
#include <arch/cpu.h>
#include <kernel/vmmcache.h>

#ifdef SYSCALL_LOGGING

#define SYSCALL_COUNT 92
#define LOGSTR(x) arch_e9_puts(x)

static char *name[] = {
	"print",
	"mmap",
	"openat",
	"read",
	"seek",
	"close",
	"archctl",
	"write",
	"getpid",
	"fstat",
	"fstatat",
	"fork",
	"execve",
	"exit",
	"waitpid",
	"munmap",
	"getdents",
	"dup",
	"dup2",
	"dup3",
	"fcntl",
	"chdir",
	"pipe2",
	"isatty",
	"faccessat",
	"unlinkat",
	"ioctl",
	"mkdirat",
	"clockget",
	"linkat",
	"readlinkat",
	"fchmod",
	"fchmodat",
	"umask",
	"poll",
	"nanosleep",
	"ftruncate",
	"mount",
	"fchownat",
	"utimensat",
	"renameat",
	"socket",
	"bind",
	"sendmsg",
	"setsockopt",
	"recvmsg",
	"listen",
	"connect",
	"accept",
	"newthread",
	"threadexit",
	"futex",
	"gettid",
	"getppid",
	"getpgid",
	"getsid",
	"setsid",
	"setpgid",
	"sigaction",
	"sigaltstack",
	"sigprocmask",
	"kill",
	"sigreturn",
	"uname",
	"hostname",
	"sync",
	"fsync",
	"fchdir",
	"setitimer",
	"getitimer",
	"socketpair",
	"getsockname",
	"getpeername",
	"chroot",
	"pause",
	"ppoll",
	"pread",
	"pwrite",
	"mknodat",
	"getresuid",
	"getresgid",
	"setresuid",
	"setresgid",
	"mprotect",
	"setuid",
	"seteuid",
	"setgid",
	"setegid",
	"sigsuspend",
	"sigtimedwait",
	"sigpending",
	"killthread"
};

static char *args[] = {
	"N/A", // print will not have its argument printed
	"hint %p length %lu prot %d flags %d fd %d offset %ld", // mmap
	"dirfd %d path %s flags %d mode %o", // openat
	"fd %d buffer %p size %lu", // read
	"fd %d offset %ld whence %d", // seek
	"fd %d", // close
	"func %d arg %p", // archctl
	"fd %d buffer %p size %lu", // write
	"N/A", // getpid
	"fd %d ustat %p", // fstat
	"dirfd %d path %s ustat %p flags %d", // fstatat
	"N/A", // fork
	"path %s uargv %p uenvp %p", // execve
	"status %d", // exit
	"pid %d statusp %p options %d", // waitpid
	"addr %p length %lu", // munmap
	"dirfd %d buffer %p readmax %lu", // getdents
	"oldfd %d", // dup
	"oldfd %d newfd %d", // dup2
	"oldfd %d newfd %d flags %d", //dup3
	"fd %d cmd %d arg %lu", // fcntl
	"path %s", // chdir
	"flags %d", // pipe2
	"fd %d", // isatty
	"dirfd %d pathname %s mode %d flags %d", // faccessat
	"dirfd %d pathname %s flags %d", // unlinkat
	"fd %d request %lu argument %p", // ioctl
	"dirfd %d path %s mode %o", // mkdirat
	"clockid %d timespec %p", // clockget
	"olddirfd %d oldpath %s newdirfd %d oldpath %s flags %d type %d", // linkat
	"dirfd %d path %s buffer %p buffer length %lu", // readlinkat
	"fd %d mode %o", // fchmod
	"dirfd %d path %s mode %o flags %d", // fchmodat
	"mode %o", // umask
	"fds %p nfds %d timeout %d", // poll
	"time %p remaining %p", // nanosleep
	"fd %d size %lu", // ftruncate
	"backing %p mountpoint %s fs %s flags %d data %p", // mount
	"fd %d path %s owner %d group %d flags %x", // fchownat
	"dirfd %d path %p times %p flags %x", // utimensat
	"olddirfd %d oldpath %s newdirfd %d newpath %s flags %d", // renameat
	"domain %d type %d protocol %d\n", // socket
	"fd %d addr %p len %d\n", // bind
	"fd %d msghdr %p flags %d", // sendmsg
	"fd %d level %d optname %d optval %p optlen %lu", // setsockopt
	"fd %d msghdr %p flags %d", // recvmsg
	"fd %d backlog %d", // listen
	"fd %d addr %p addrlen %lu", // connect
	"fd %d addr %p addrlen %p flags %x", // accept
	"entry %p stack %p", // newthread
	"N/A", // threadexit
	"futex %p op %d value %d timespec %p", // futex
	"N/A", // gettid
	"N/A", // getppid
	"pid %d", // getpgid
	"pid %d", // getsid
	"N/A", // setsid
	"pid %d pgid %d", // setpgid
	"signal %d new %p old %p", // sigaction
	"new %p old %p", // sigaltstack
	"how %d set %p oldset %p", // sigprocmask
	"pid %d signal %d", // kill
	"N/A", // sigreturn
	"utsname %p", // uname
	"new %p newsize %lu old %p oldsize %lu", // hostname
	"N/A", // sync
	"fd %d", // fsync
	"fd %d",  // fchdir
	"which %d new %p old %p\n", // setitimer
	"which %d value %p\n", // getitimer
	"domain %d type %d protocol %d", // socketpair
	"fd %d addr %p addrlen %p", // getsockname
	"fd %d addr %p addrlen %p",  // getpeername
	"path %s", // chroot
	"N/A", // pause
	"fds %p nfds %d timeout %p sigmask %p", // ppoll
	"fd %d buffer %p count %lu offset %lu\n", // pread
	"fd %d buffer %p count %lu offset %lu\n", // pwrite
	"dirfd %d path %s mode %o dev %x\n", // mknodat
	"uidp %p euidp %p suidp %p", // getresuid
	"gidp %p egidp %p sgidp %p", // getresgid
	"uid %d euid %d suid %d", // setresuid
	"gid %d egid %d sgid %d", // setresgid
	"addr %p len %lu prot %d", // mprotect
	"uid %d", // setuid
	"euid %d", // seteuid
	"gid %d", // setgid
	"egid %d", // setegid
	"sigset %p", // sigsuspend
	"sigset %p info %p, timespec %p", // sigtimedwait
	"sigset %p\n", // sigpending
	"pid %d tid %d signal %d" // killthread
};

#endif

#ifdef SYSCALL_LOGGING
static spinlock_t lock;
#endif
extern size_t freepagecount;
__attribute__((no_caller_saved_registers)) void arch_syscall_log(int syscall, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
#ifdef SYSCALL_LOGGING
	char argbuff[768];
	char printbuff[1024];

	thread_t *thread = current_thread();
	proc_t *proc = thread->proc;
	snprintf(argbuff, 768, syscall < SYSCALL_COUNT ? args[syscall] : "N/A", a1, a2, a3, a4, a5, a6);
	snprintf(printbuff, 1024, "\e[92msyscall: pid %d tid %d: %s: %s (%lu cached pages, %lu free pages)\n\e[0m", proc->pid, thread->tid, syscall < SYSCALL_COUNT ? name[syscall] : "invalid syscall", argbuff, vmmcache_cachedpages, freepagecount);

	arch_interrupt_disable();
	spinlock_acquire(&lock);
	LOGSTR(printbuff);
	spinlock_release(&lock);
	arch_interrupt_enable();
#endif
}

__attribute__((no_caller_saved_registers)) void arch_syscall_log_return(uint64_t ret, uint64_t errno) {
#ifdef SYSCALL_LOGGING
	char printbuff[1024];

	thread_t *thread = current_thread();
	proc_t *proc = thread->proc;

	snprintf(printbuff, 1024, "\e[94msyscall return: pid %d tid %d: %lu %s (%lu cached pages, %lu free pages)\n\e[0m", proc->pid, thread->tid, ret, strerror(errno), vmmcache_cachedpages, freepagecount);
	arch_interrupt_disable();
	spinlock_acquire(&lock);
	LOGSTR(printbuff);
	spinlock_release(&lock);
	arch_interrupt_enable();
#endif
}
