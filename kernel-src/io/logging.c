#include <mutex.h>

MUTEX_DEFINE(printf_mutex);

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
