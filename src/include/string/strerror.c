#include <string.h>

static char* strs[] = {
	"No error",
	"Not a directory",
	"No such device",
	"Out of memory",
	"Invalid",
	"Function unimplemented",
	"File already exists",
	"No such file or directory"
};

char* strerror(int errnum){
	return strs[errnum];
}
