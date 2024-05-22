#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pwd.h>
#include <fcntl.h>
#include <string.h>

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
	setenv("DISPLAY", ":0", 1);

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
		printf("/etc/rc returned failure status %d\n", WEXITSTATUS(status));
		return EXIT_FAILURE;
	}

	// open /dev/console but as the controlling terminal
	printf("init: reopening /dev/console as the controlling terminal\n");
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

	// run the startwm script
	// if that exits, we will fall back to the console shell
	if (argc > 1 && strcmp(argv[1], "withx") == 0)
		system("/usr/bin/startwm");

	// start user shell
	printf("init: running /usr/bin/bash\n");
	pid = fork();
	if (pid == -1) {
		perror("init: fork failed");
		return EXIT_FAILURE;
	}

	if (pid == 0) {
		// create its own process group and set is as the foreground group
		if (setpgid(0, 0) == -1) {
			perror("init: setpgrp failed");
			return EXIT_FAILURE;
		}

		if (tcsetpgrp(0, getpid()) == -1) {
			perror("init: tcsetpgrp failed");
			return EXIT_FAILURE;
		}

		execl("/usr/bin/bash", "/usr/bin/bash", "-l", NULL);
		perror("init: execl /usr/bin/bash failed");
		return EXIT_FAILURE;
	}

	for (;;) {
		int status;
		if (waitpid(-1, &status, 0) == -1) {
			perror("init: waitpid failed");
			return EXIT_FAILURE;
		}
	}
}
