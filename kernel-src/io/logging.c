#include <mutex.h>

mutex_t printf_mutex;

static void nohook(char c) {

}

static void (*hook)(char) = nohook;

// hook used by mpaland printf
void _putchar(char c) {
	hook(c);
}

void logging_sethook(void (*fun)(char)) {
	hook = fun;
}

void logging_init() {
	MUTEX_INIT(&printf_mutex);
}
