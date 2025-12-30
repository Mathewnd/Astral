#include <astral/archctl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdint.h>

static int arch_ctl_systrace(uint64_t mode) {
	return arch_ctl(ARCH_CTL_SET_SYSTRACE, (void *)mode);
}

int main(int argc, char *argv[]) {
	// make sure syscall tracing is supported
	if (arch_ctl_systrace(ARCH_CTL_SYSTRACE_OFF) != 0) {
		if (errno == ENOTSUP) {
			fprintf(stderr, "%s: compile the kernel with syscall tracing enabled\n", argv[0]);
		} else {
			perror("arch_ctl failed");
		}
		return 1;
	}

	uint64_t mode = ARCH_CTL_SYSTRACE_SELF;

	// check for -ff flag
	if (argc > 1 && strcmp(argv[1], "-ff") == 0) {
		mode = ARCH_CTL_SYSTRACE_ALL;
		argv++;
		argc--;
	}

	if (argc < 2) {
		fprintf(stderr, "usage: %s [-ff] <command> [args...]\n", argv[0]);
		fprintf(stderr, "example: %s ls -l /\n", argv[0]);
		return 1;
	}

	int pid = fork();
	if (pid < 0) {
		perror("fork failed");
		return 1;
	}

	if (pid == 0) {
		// enable syscall tracing for the child process
		if (arch_ctl_systrace(mode) != 0) {
			perror("failed to enable syscall tracing");
			return 1;
		}
		fprintf(stderr, "%s: tracee is pid %d\n", argv[0], getpid());
		if (execvp(argv[1], &argv[1]) != 0) {
			arch_ctl_systrace(ARCH_CTL_SYSTRACE_OFF);
			perror("execvp failed");
			return 1;
		}
		// not reached
	} else {
		int status;
		waitpid(pid, &status, 0);
		return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
	}
}
