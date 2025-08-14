#include <kernel/init.h>
#include <arch/cpu.h>
#include <logging.h>

void init_run_routine(init_routine_t *routine) {
	if (routine->flags & INIT_ROUTINE_FLAGS_ALREADY_DONE)
		return;
	
	for (int i = 0; i < routine->dependency_count; ++i)
		init_run_routine(*routine->dependencies[i]);

	if ((routine->flags & INIT_ROUTINE_FLAGS_QUIET) == 0)
		printf("kernel_init: running routine %s\n", routine->name);

	if ((routine->flags & INIT_ROUTINE_FLAGS_PHONY) == 0)
		routine->fn();

	routine->flags |= INIT_ROUTINE_FLAGS_ALREADY_DONE;
}

extern init_routine_t *init_routines;
extern init_routine_t *init_routines_end;

void init_run_all_routines(void) {
	for (init_routine_t **routine_ptr = &init_routines; routine_ptr < &init_routines_end; ++routine_ptr)
		init_run_routine(*routine_ptr);
}
