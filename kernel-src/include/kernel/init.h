#ifndef _INIT_H
#define _INIT_H

#include <stddef.h>

#define INIT_ROUTINE_FLAGS_NONE 0
#define INIT_ROUTINE_FLAGS_ALREADY_DONE 1
#define INIT_ROUTINE_FLAGS_QUIET 2
#define INIT_ROUTINE_FLAGS_PHONY 4

typedef struct init_routine_t {
	const char *name;
	int flags;
	void (*fn)(void);
	struct init_routine_t ***dependencies;
	size_t dependency_count;
} init_routine_t;

// tasty tasty macro soup!
// this will make it cleaner overall and keep the ugly here
// if any routine has more than 10 deps add more here
#define INIT_MACRO_ARG_COUNT_(n0, n1, n2, n3, n4, n5, n6, n7, n8, n9, n10, n, ...) n
#define INIT_MACRO_ARG_COUNT(...) INIT_MACRO_ARG_COUNT_(0, ## __VA_ARGS__, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define INIT_CONCAT_(a, b) a##b
#define INIT_CONCAT(a, b) INIT_CONCAT_(a, b)


#define INIT_GET_DEP_NAME(x) init_routine_ ## x

#define INIT_DEFINE_EXTERN(x) extern init_routine_t * INIT_GET_DEP_NAME(x)
#define INIT_DEFINE_EXTERN_0(...)
#define INIT_DEFINE_EXTERN_1(x) INIT_DEFINE_EXTERN(x)
#define INIT_DEFINE_EXTERN_2(x, ...)  INIT_DEFINE_EXTERN(x); INIT_DEFINE_EXTERN_1(__VA_ARGS__)
#define INIT_DEFINE_EXTERN_3(x, ...)  INIT_DEFINE_EXTERN(x); INIT_DEFINE_EXTERN_2(__VA_ARGS__)
#define INIT_DEFINE_EXTERN_4(x, ...)  INIT_DEFINE_EXTERN(x); INIT_DEFINE_EXTERN_3(__VA_ARGS__)
#define INIT_DEFINE_EXTERN_5(x, ...)  INIT_DEFINE_EXTERN(x); INIT_DEFINE_EXTERN_4(__VA_ARGS__)
#define INIT_DEFINE_EXTERN_6(x, ...)  INIT_DEFINE_EXTERN(x); INIT_DEFINE_EXTERN_5(__VA_ARGS__)
#define INIT_DEFINE_EXTERN_7(x, ...)  INIT_DEFINE_EXTERN(x); INIT_DEFINE_EXTERN_6(__VA_ARGS__)
#define INIT_DEFINE_EXTERN_8(x, ...)  INIT_DEFINE_EXTERN(x); INIT_DEFINE_EXTERN_7(__VA_ARGS__)
#define INIT_DEFINE_EXTERN_9(x, ...)  INIT_DEFINE_EXTERN(x); INIT_DEFINE_EXTERN_8(__VA_ARGS__)
#define INIT_DEFINE_EXTERN_10(x, ...) INIT_DEFINE_EXTERN(x); INIT_DEFINE_EXTERN_9(__VA_ARGS__)

#define INIT_GET_DEP_0(...)
#define INIT_GET_DEP_1(x) &INIT_GET_DEP_NAME(x)
#define INIT_GET_DEP_2(x, ...)  &INIT_GET_DEP_NAME(x), INIT_GET_DEP_1(__VA_ARGS__)
#define INIT_GET_DEP_3(x, ...)  &INIT_GET_DEP_NAME(x), INIT_GET_DEP_2(__VA_ARGS__)
#define INIT_GET_DEP_4(x, ...)  &INIT_GET_DEP_NAME(x), INIT_GET_DEP_3(__VA_ARGS__)
#define INIT_GET_DEP_5(x, ...)  &INIT_GET_DEP_NAME(x), INIT_GET_DEP_4(__VA_ARGS__)
#define INIT_GET_DEP_6(x, ...)  &INIT_GET_DEP_NAME(x), INIT_GET_DEP_5(__VA_ARGS__)
#define INIT_GET_DEP_7(x, ...)  &INIT_GET_DEP_NAME(x), INIT_GET_DEP_6(__VA_ARGS__)
#define INIT_GET_DEP_8(x, ...)  &INIT_GET_DEP_NAME(x), INIT_GET_DEP_7(__VA_ARGS__)
#define INIT_GET_DEP_9(x, ...)  &INIT_GET_DEP_NAME(x), INIT_GET_DEP_8(__VA_ARGS__)
#define INIT_GET_DEP_10(x, ...) &INIT_GET_DEP_NAME(x), INIT_GET_DEP_9(__VA_ARGS__)

#define INIT_GET_ROUTINE(name) init_routine_##name

// ... is deps
#define INIT_ROUTINE_DEFINE(name_, flags_, fn_, ...) \
	/* first define all dependencies as extern. if none, then it will not define any
	 * but since it will not be used, then there is no harm done. */ \
	INIT_CONCAT(INIT_DEFINE_EXTERN_, INIT_MACRO_ARG_COUNT(__VA_ARGS__))(__VA_ARGS__); \
	/* then, create the array of the dependencies for the use in the main struct.
	 * if none, then it will just be empty */ \
	static init_routine_t **init_routine_deps_##name_ [] = { \
		INIT_CONCAT(INIT_GET_DEP_, INIT_MACRO_ARG_COUNT(__VA_ARGS__))(__VA_ARGS__) \
	}; \
	/* finally, define the init routine struct and keep a pointer to it in the special section */ \
	static init_routine_t struct_init_routine_##name_ = { \
		.name = #name_, \
		.flags = flags_, \
		.fn = fn_, \
		.dependencies = init_routine_deps_##name_, \
		.dependency_count = INIT_MACRO_ARG_COUNT(__VA_ARGS__) \
	}; \
	__attribute__((section(".init_routines"), used)) init_routine_t *init_routine_##name_ = &struct_init_routine_##name_;

void init_run_routine(init_routine_t *routine);
void init_run_all_routines(void);

#endif
