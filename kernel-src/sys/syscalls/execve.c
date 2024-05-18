#include <kernel/syscalls.h>
#include <kernel/alloc.h>
#include <string.h>
#include <errno.h>
#include <kernel/vmm.h>
#include <arch/cpu.h>
#include <kernel/elf.h>
#include <kernel/scheduler.h>
#include <logging.h>

static void freevec(char **v) {
	char **it = v;
	while (*it)
		free(*it++);

	free(v);
}

#define MAX_DEPTH 16
#define SHEBANG_SIZE 128

static syscallret_t execve(context_t *context, char *upath, char *uargv[], char *uenvp[], char *sbarg, char *sbold, int depth) {
	syscallret_t ret = {
		.ret = -1
	};

	if (depth >= MAX_DEPTH) {
		ret.errno = ELOOP;
		return ret;
	}

	// TODO user str ops
	char *path = alloc(strlen(upath) + 1);
	if (path == NULL) {
		ret.errno = ENOMEM;
		return ret;
	}
	strcpy(path, upath);

	size_t argsize = 0;
	size_t envsize = 0;
	char **argv = NULL;
	char **envp = NULL;
	vmmcontext_t *vmmctx = NULL;
	vmmcontext_t *oldctx = _cpu()->vmmctx;
	vnode_t *node = NULL;
	vnode_t *refnode = NULL;
	char *interp = NULL;
	void *stack = NULL;
	char shebang[SHEBANG_SIZE];

	while (uargv[argsize++]);
	while (uenvp[envsize++]);

	size_t argoffset = (sbold ? 1 : 0) + (sbarg ? 1 : 0);
	argsize += argoffset;

	argv = alloc((argsize + 1) * sizeof(char *));
	envp = alloc((envsize + 1) * sizeof(char *));

	if (argv == NULL || envp == NULL) {
		ret.errno = ENOMEM;
		goto error;
	}

	if (sbarg) {
		char *tmp = alloc(strlen(sbarg) + 1);
		if (tmp == NULL) {
			ret.errno = ENOMEM;
			goto error;
		}
		strcpy(tmp, sbarg);
		argv[0] = tmp;
	}

	if (sbold) {
		char *tmp = alloc(strlen(sbold) + 1);
		if (tmp == NULL) {
			ret.errno = ENOMEM;
			goto error;
		}

		strcpy(tmp, sbold);
		argv[argoffset - 1] = tmp;
	}

	for (int i = argoffset; i < argsize - 1; ++i) {
		argv[i] = alloc(strlen(uargv[i - argoffset]) + 1);
		if (argv[i] == NULL) {
			ret.errno = ENOMEM;
			goto error;
		}
		strcpy(argv[i], uargv[i - argoffset]);
	}

	for (int i = 0; i < envsize - 1; ++i) {
		envp[i] = alloc(strlen(uenvp[i]) + 1);
		if (envp[i] == NULL) {
			ret.errno = ENOMEM;
			goto error;
		}
		strcpy(envp[i], uenvp[i]);
	}

	refnode = *path == '/' ? sched_getroot() : sched_getcwd();

	ret.errno = vfs_lookup(&node, refnode, path, NULL, 0);
	if (ret.errno)
		goto error;

	size_t readcount;
	ret.errno = vfs_read(node, shebang, SHEBANG_SIZE - 1, 0, &readcount, 0);
	if (ret.errno)
		goto error;

	if (readcount >= 2 && shebang[0] == '#' && shebang[1] == '!') {
		shebang[readcount] = '\0';
		// newline -> null
		for (int i = 2; i < readcount; ++i)
			if (shebang[i] == '\n')
				shebang[i] = '\0';

		// get start of command
		char *command = &shebang[2];
		while (*command == ' ')
			++command;

		// get end of command
		char *argument = command;
		while (*argument != ' ' && *argument != '\0')
			++argument;

		if (*argument == ' ')
			*argument++ = '\0';

		// get start of argument (passed as one big argument to the program)
		while (*argument == ' ')
			++argument;

		size_t commandlen = strlen(command);
		size_t argumentlen = strlen(argument);

		if (commandlen) {
			// valid, run with interpreter
			char *argvold = argv[0];
			argv[0] = path;
			ret = execve(context, command, argv, envp, argumentlen ? argument : NULL, path, depth + 1);
			argv[0] = argvold; // to clean up later
			goto error; // jump to cleanup
		}
	}

	vmmctx = vmm_newcontext();
	if (vmmctx == NULL) {
		ret.errno = ENOMEM;
		goto error;
	}

	// TODO permission and type checks and all

	vmm_switchcontext(vmmctx);

	auxv64list_t auxv64;
	void *entry;
	ret.errno = elf_load(node, NULL, &entry, &interp, &auxv64);
	if (ret.errno)
		goto error;

	if (interp) {
		vnode_t *interpnode = NULL;
		vnode_t *interprefnode = *interp == '/' ? sched_getroot() : sched_getcwd();
		ret.errno = vfs_lookup(&interpnode, interprefnode, interp, NULL, 0);
		if (ret.errno)
			goto error;

		auxv64list_t interpauxv;
		char *interpinterp = NULL;
		ret.errno = elf_load(interpnode, INTERP_BASE, &entry, &interpinterp, &interpauxv);
		if (ret.errno)
			goto error;

		__assert(interpinterp == NULL);
		VOP_RELEASE(interpnode);
	}

	stack = elf_preparestack(STACK_TOP, &auxv64, argv, envp);
	if (stack == NULL) {
		ret.errno = ENOMEM;
		goto error;
	}

	ret.ret = 0;

	sched_stopotherthreads();

	// close O_CLOEXEC fds

	proc_t *proc = _cpu()->thread->proc;
	for (int fd = 0; fd < proc->fdcount; ++fd) {
		if (proc->fd[fd].flags & O_CLOEXEC)
                	fd_close(fd);
	}

	memset(&_cpu()->thread->signals.stack, 0, sizeof(stack_t));
	memset(&proc->signals.actions[0], 0, sizeof(sigaction_t) * NSIG);

	vmm_destroycontext(oldctx);
	CTX_SP(context) = (uint64_t)stack;
	CTX_IP(context) = (uint64_t)entry;

	error:
	free(path);
	if (argv)
		freevec(argv);

	if (envp)
		freevec(envp);

	if (vmmctx && ret.errno) {
		vmm_switchcontext(oldctx);
		vmm_destroycontext(vmmctx);
	}

	if (refnode)
		VOP_RELEASE(refnode);

	if (node)
		VOP_RELEASE(node);

	if (interp)
		free(interp);

	return ret;
}

syscallret_t syscall_execve(context_t *context, char *upath, char *uargv[], char *uenvp[]) {
	return execve(context, upath, uargv, uenvp, NULL, NULL, 0);
}
