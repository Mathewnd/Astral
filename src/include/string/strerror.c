#include <string.h>

static char* strs[] = {
	"Unknown error",
	"Not a directory",
	"No such device",
	"Out of memory",
	"Invalid",
	"Function unimplemented",
	"File already exists",
	"No such file or directory"
};

char* strerror(int errnum){
	return strs[0]; // FIXME 0 for now because ABI change
}
