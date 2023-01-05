#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pwd.h>

FILE* openordie(const char* name, const char* mode){
	
	FILE* file = fopen(name, mode);

	if(!file){
		printf("init: Failed to open /etc/initlist: %s\n", strerror(errno));
		exit(1);
	}

	return file;
}

void oom(){
	printf("init: %s", strerror(ENOMEM));
	exit(1);
}

void printwelcome(FILE* welcome){
	char buff[4096];

	while(1){
		
		size_t readc = fread(buff, 1, 4095, welcome);

		if(readc == 0)
			break;
		
		buff[readc] = '\0';

		printf("%s", buff);

	}

	printf("\n");
	
}

int main(int argc, char* argv[]){

	FILE* shell = openordie("/etc/init/shell", "r");
	FILE* shellenv = openordie("/etc/init/shellenv", "r");
	FILE* welcome = openordie("/etc/init/welcome", "r");

	printwelcome(welcome);
	
	fclose(welcome);
	
	char readbuff[4096];	
	size_t readc = fread(readbuff, 4095, 1, shell);

	if(ferror(shell)){
		printf("init: Failed to read from /etc/init/shell: %s\n", strerror(errno));
		exit(1);
	}
	
	void* shellbuff = strtok(readbuff, "\n");

	char* name = strtok(shellbuff, " ");
	
	// test if shell exists
	
	struct stat st;

	if(stat(name, &st) == -1){
		printf("init: stat failed on %s: %s\n", name, strerror(errno));
		exit(1);
	}
	
	char* token = name;
	size_t tokenc = 0;

	char** args = malloc(1); // initialize this


	while(token){
		args = realloc(args, ++tokenc*sizeof(char*));
		if(!args)
			oom();

		args[tokenc-1] = malloc(strlen(token)+1);

		if(!args[tokenc-1])
			oom();

		strcpy(args[tokenc-1], token);

		token = strtok(NULL, " ");
		
	}
		
	args = realloc(args, (tokenc+1)*sizeof(char*)); // for null terminator
	if(!args)
		oom();
	
	args[tokenc] = NULL;
	
	char* envbuff;
	size_t envsize;

	if(getline(&envbuff, envsize, shellenv) == -1){
		if(ferror(shellenv)){
			printf("init: getline failed on shellenv: %s\n", strerror(errno));
			exit(1);
		}
		else envbuff = NULL;
	}


	while(envbuff){
		
		if(putenv(envbuff)) oom();

		if(getline(&envbuff, envsize, shellenv) == -1 && ferror(shellenv)){
			printf("init: getline failed on shellenv: %s\n", strerror(errno));
			exit(1);
		}
	}
	
	// set some more specific env stuff
	
	struct passwd* pw = getpwuid(getuid());
	
	setenv("PWD", pw->pw_dir, 1);
	setenv("HOME", pw->pw_dir, 1);
	chdir(pw->pw_dir);
	setenv("PATH", "/bin:/usr/bin:/usr/local/bin:/sbin:/usr/sbin:/usr/local/sbin", 1);
	setenv("DISPLAY", ":0", 1);
	setenv("TERM", "linux", 1);

	printf("init: running /etc/rc\n");

	// execute init script
	
	pid_t scriptpid = fork();
	
	if(scriptpid == -1){
		perror("init: fork failed");
		exit(1);
	}	

	if(scriptpid == 0){
		char* args[] = {name, "/etc/rc", 0};
		execv(name, args);
		perror("init: execv failed");
		exit(1);
	}
	
	int status = 0;

	waitpid(scriptpid, &status, 0);

	if(status){
		printf("init: running the /etc/rc script failed: status %lu\n", status);
		exit(1);
	}

	// execute shell
	
	pid_t shellpid = fork();

	if(shellpid == -1){
		perror("init: fork failed");
		exit(1);
	}
	
	if(shellpid == 0){
		execv(name, args);
		perror("init: execv failed");
		exit(1);
	}

	for(;;) waitpid(-1, &status, 0);

}
