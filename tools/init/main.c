#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pwd.h>

int main() {
	printf("init: Welcome to Astral!\n");

	struct passwd* pw = getpwuid(getuid());
	if (chdir(pw->pw_dir) == -1) {
		perror("init: chdir to user home directory failed");
		return EXIT_FAILURE;
	}

	setenv("PWD", pw->pw_dir, 1);
	setenv("HOME", pw->pw_dir, 1);
	setenv("PATH", "/bin:/usr/bin:/usr/local/bin:/sbin:/usr/sbin:/usr/local/sbin", 1);
	setenv("TERM", "linux", 1);

	printf("init: running /usr/bin/bash\n");

	pid_t pid = fork();
	if (pid == -1) {
		perror("init: fork failed:");
		return EXIT_FAILURE;
	}

	if (pid == 0) {
		execl("/usr/bin/bash", "/usr/bin/bash", "-l", NULL);
		perror("init: execl /usr/bin/bash failed:");
		return EXIT_FAILURE;
	}

	for (;;) {
		int status;
		if (waitpid(-1, &status, 0) == -1) {
			perror("init: waitpid failed:");
			return EXIT_FAILURE;
		}
	}
}
