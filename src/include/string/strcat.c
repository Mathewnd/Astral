#include <string.h>


char *strcat(char *dest, const char * src){
	
	dest += strlen(dest);

	strcpy(dest, src);

	return dest;
	
}
