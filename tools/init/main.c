#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pwd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/param.h>

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
	// loop starting a root shell every time it exits
	for (;;) {
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

		dowaitpid(pid);
	}
}

static void dologinprompt(void) {
	for (;;) {
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

		dowaitpid(pid);
	}
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

	dowaitpid(pid);

	// fall back to a root shell if this ever returns (it shouldn't)
	dorootshell();
}

int main(int argc, char *argv[]) {
	printf("init: Welcome to Astral!\n");

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
