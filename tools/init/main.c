#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pwd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>

#include "common.h"

static void sendacpicommand(char c) {
	int acpifd = open("/dev/acpi", O_WRONLY);
	if (acpifd == -1) {
		perror("init: open /dev/acpi");
		goto oops;
	}

	write(acpifd, &c, 1);
	perror("init: write /dev/acpi");

	oops:
	printf("init: failed to send acpi command. please manually do what is needed.\n");
	for (;;)
		sleep(1);
}

static void preparetodie(void) {
	printf("init: bringing system down\n");
	//printf("init: sending SIGTERM to all processes\n");
	// TODO kill(-1, SIGTERM);
	// TODO wait 5 seconds or until all children have died
	//printf("init: sending SIGKILL to all processes\n");
	// TODO kill(-1, SIGKILL);
	// TODO wait until all children have died

	printf("init: syncing disks\n");
	sync();
}

static void doreboot(void) {
	printf("init: rebooting\n");
	sendacpicommand('r');
}

static void dopoweroff(void) {
	printf("init: powering off\n");
	sendacpicommand('p');
}

static void handlecommands(void) {
	for (;;) {
		int fd = open(COMMAND_FIFO, O_RDONLY);
		if (fd == -1) {
			perror("init: open command fifo");
			return;
		}

		char c;
		int num;
		for (;;) {
			num = read(fd, &c, 1);
			if (num == 0)
				break;

			if (num == -1) {
				if (errno == EINTR)
					continue;

				perror("init: read");
				close(fd);
				continue;
			}

			switch (c) {
				case COMMAND_POWEROFF:
					preparetodie();
					dopoweroff();
					break;
				case COMMAND_REBOOT:
					preparetodie();
					doreboot();
				default:
					printf("init: unknown command %c\n", c);
			}
		}


		close(fd);
	}
}

static void reap(int signum) {
	pid_t pid;

	for (;;) {
		pid = waitpid(-1, NULL, WNOHANG);
		if (pid == 0)
			break;

		if (pid == -1) {
			if (errno == ECHILD)
				break;

			perror("init: waitpid");
		}
	}
}

static void snatchconsole(void) {
	int rfd = open("/dev/console", O_RDONLY);
	int wfd = open("/dev/console", O_WRONLY);
	close(0);
	close(1);
	close(2);

	dup2(rfd, 0);
	dup2(wfd, 1);
	dup2(wfd, 2);

	close(rfd);
	close(wfd);
}

static void dowaitpid(pid_t pid) {
	int status;
	pid_t retpid;
	do {
		retpid = waitpid(-1, &status, 0);
		if (retpid == -1) {
			perror("init: waitpid failed");
			exit(EXIT_FAILURE);
		}
	} while (retpid != pid);

	if (WEXITSTATUS(status) == EXIT_FAILURE)
		exit(EXIT_FAILURE);
}

static void dorootshell(void) {
	printf("init: starting root shell\n");

	pid_t pid = fork();
	if (pid == -1) {
		perror("init: fork failed");
		exit(EXIT_FAILURE);
	}

	if (pid == 0) {
		// create its own process group and set is as the foreground group
		if (setpgid(0, 0) == -1) {
			perror("init: setpgrp failed");
			exit(EXIT_FAILURE);
		}

		if (tcsetpgrp(0, getpid()) == -1) {
			perror("init: tcsetpgrp failed");
			exit(EXIT_FAILURE);
		}

		execl("/usr/bin/bash", "/usr/bin/bash", "-l", NULL);
		perror("init: execl /usr/bin/bash failed");
		return exit(EXIT_FAILURE);
	}

	handlecommands();
}

static void dologinprompt(void) {
	pid_t pid = fork();
	if (pid == -1) {
		perror("init: fork failed");
		exit(EXIT_FAILURE);
	}

	if (pid == 0) {
		setsid();
		snatchconsole();
		execl("/bin/login", NULL);
		perror("init: exec /bin/login failed");
		exit(EXIT_FAILURE);
	}

	handlecommands();
}

static void dostartwm(void) {
	pid_t pid = fork();
	if (pid == -1) {
		perror("init: fork failed");
		exit(EXIT_FAILURE);
	}

	if (pid == 0) {

		execl("/usr/bin/startwm", NULL);
		perror("init: exec /usr/bin/startwm failed");
		exit(EXIT_FAILURE);
	}

	handlecommands();
}

int main(int argc, char *argv[]) {
	printf("init: Welcome to Astral!\n");

	// set up signal mask
	sigset_t signals;
	sigfillset(&signals);
	sigprocmask(SIG_SETMASK, &signals, NULL);

	// set up signal actions
	struct sigaction sa;
	memset(&sa, 0, sizeof(sigaction));
	sa.sa_handler = reap;
	sigaction(SIGCHLD, &sa, NULL);

	// set up initial environmental variables
	struct passwd* pw = getpwuid(getuid());
	if (pw == NULL) {
		perror("init: failed to open /etc/passwd");
		return EXIT_FAILURE;
	}

	if (chdir(pw->pw_dir) == -1) {
		perror("init: chdir to user home directory failed");
		return EXIT_FAILURE;
	}

	setenv("PWD", pw->pw_dir, 1);
	setenv("HOME", pw->pw_dir, 1);
	setenv("PATH", "/bin:/usr/bin:/usr/local/bin:/sbin:/usr/sbin:/usr/local/sbin", 1);
	setenv("TERM", "linux", 1);
	//setenv("DISPLAY", ":0", 1);

	// set hostname
	char hostname[HOST_NAME_MAX + 1];
	int hostnamefd = open("/etc/hostname", O_RDONLY);
	if (hostnamefd == -1) {
		// non fatal error
		perror("init: failed to open /etc/hostname");
	} else {
		int readcount = read(hostnamefd, hostname, HOST_NAME_MAX);

		if (readcount == -1) {
			perror("init: failed to read /etc/hostname");
		} else {
			hostname[readcount] = '\0';
			for (int i = 0; i < readcount; ++i) {
				if (hostname[i] == '\n')
					hostname[i] = '\0';
			}

			sethostname(hostname, strlen(hostname));
		}


		close(hostnamefd);
	}

	// run rc to initialize environment (mount filesystems, etc)
	printf("init: running /etc/rc\n");
	pid_t pid = fork();
	if (pid == -1) {
		perror("init: fork failed");
		return EXIT_FAILURE;
	}

	if (pid == 0) {
		execl("/usr/bin/bash", "/usr/bin/bash", "/etc/rc", NULL);
		perror("init: exec /usr/bin/bash failed");
		return EXIT_FAILURE;
	}

	int status = 0;
	if (waitpid(pid, &status, 0) == -1) {
		perror("init: waitpid failed");
		return EXIT_FAILURE;
	}

	if (WEXITSTATUS(status)) {
		printf("init: /etc/rc returned failure status %d\n", WEXITSTATUS(status));
		return EXIT_FAILURE;
	}

	// unmask sigchild to reap children now
	sigdelset(&signals, SIGCHLD);

	printf("init: creating %s\n", COMMAND_FIFO);
	if (mkfifo(COMMAND_FIFO, 0600)) {
		perror("init: mkfifo failed");
		return EXIT_FAILURE;
	}

	if (argc >1 && strcmp(argv[1], "withlogin") == 0)
		dologinprompt();

	// open /dev/console as the controlling terminal if we're not doing login
	printf("init: reopening /dev/console as the controlling terminal\n");
	snatchconsole();

	if (argc > 1 && strcmp(argv[1], "withx") == 0)
		dostartwm();

	dorootshell();
	__builtin_unreachable();
}
