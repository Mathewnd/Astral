#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>

void *memcpy(void *d, void *s, size_t c) {
#ifdef __x86_64__
	asm volatile ("rep movsb" : : "S"(s), "D"(d), "c"(c) : "memory");
#else
	uint8_t *sc = (uint8_t *)s;
	uint8_t *dc = (uint8_t *)d;

	while (c--)
	*dc++ = *sc++;
#endif
	return d;
}

void *memset(void *ptr, unsigned long value, size_t num) {
#ifdef __x86_64__
	asm volatile ("rep stosb" : : "D"(ptr), "a"(value), "c"(num) : "memory");
#else
	char *p = (char *)ptr;
	while (num--)
	*p++ = value;
#endif
	return ptr;
}

char *strcat(char *dest, const char *src) {
	dest += strlen(dest);

	strcpy(dest, src);

	return dest;
	
}
int strcmp(const char *a, const char *b) {
	while (*a && (*a == *b)) {
		++a;
		++b;
	}

	return *(const unsigned char *)a - *(const unsigned char *)b;
}

char *strcpy(char *p, const char *s) {
	const char *temp1 = s;
	char *temp2 = p;
	while (*temp1 != '\0') {
		*temp2 = *temp1;
		temp1++;
		temp2++;
	}
	*temp2 = '\0';
	return p;
}

size_t strlen(const char *a) {
	size_t s = 0;

	while (a[s] != '\0')
		++s;

	return s;
}

int strncmp(const char *a, const char *b, size_t c) {
	int diff = 0;

	while (c-- && diff == 0)
	diff += *a++ - *b++;

	return diff;
}

int memcmp(const void *_a, const void *_b, size_t s) {
	const uint8_t *a = _a;
	const uint8_t *b = _b;
	int diff = 0;

	while (s-- && diff == 0)
		diff += *a++ - *b++;

	return diff;
}

#define DO_NAME(e) \
	case e: \
		return #e;

char *strerror(int errno) {
	switch (errno) {
		case 0:
			return "OK";
		DO_NAME(EPERM)
		DO_NAME(ENOENT)
		DO_NAME(ESRCH)
		DO_NAME(EINTR)
		DO_NAME(EIO)
		DO_NAME(ENXIO)
		DO_NAME(E2BIG)
		DO_NAME(ENOEXEC)
		DO_NAME(EBADF)
		DO_NAME(ECHILD)
		DO_NAME(EAGAIN)
		DO_NAME(ENOMEM)
		DO_NAME(EACCES)
		DO_NAME(EFAULT)
		DO_NAME(ENOTBLK)
		DO_NAME(EBUSY)
		DO_NAME(EEXIST)
		DO_NAME(EXDEV)
		DO_NAME(ENODEV)
		DO_NAME(ENOTDIR)
		DO_NAME(EISDIR)
		DO_NAME(EINVAL)
		DO_NAME(ENFILE)
		DO_NAME(EMFILE)
		DO_NAME(ENOTTY)
		DO_NAME(ETXTBSY)
		DO_NAME(EFBIG)
		DO_NAME(ENOSPC)
		DO_NAME(ESPIPE)
		DO_NAME(EROFS)
		DO_NAME(EMLINK)
		DO_NAME(EPIPE)
		DO_NAME(EDOM)
		DO_NAME(ERANGE)
		DO_NAME(EDEADLK)
		DO_NAME(ENAMETOOLONG)
		DO_NAME(ENOLCK)
		DO_NAME(ENOSYS)
		DO_NAME(ENOTEMPTY)
		DO_NAME(ELOOP)
		DO_NAME(ENOMSG)
		DO_NAME(EIDRM)
		DO_NAME(ECHRNG)
		DO_NAME(EL2NSYNC)
		DO_NAME(EL3HLT)
		DO_NAME(EL3RST)
		DO_NAME(ELNRNG)
		DO_NAME(EUNATCH)
		DO_NAME(ENOCSI)
		DO_NAME(EL2HLT)
		DO_NAME(EBADE)
		DO_NAME(EBADR)
		DO_NAME(EXFULL)
		DO_NAME(ENOANO)
		DO_NAME(EBADRQC)
		DO_NAME(EBADSLT)
		DO_NAME(EBFONT)
		DO_NAME(ENOSTR)
		DO_NAME(ENODATA)
		DO_NAME(ETIME)
		DO_NAME(ENOSR)
		DO_NAME(ENONET)
		DO_NAME(ENOPKG)
		DO_NAME(EREMOTE)
		DO_NAME(ENOLINK)
		DO_NAME(EADV)
		DO_NAME(ESRMNT)
		DO_NAME(ECOMM)
		DO_NAME(EPROTO)
		DO_NAME(EMULTIHOP)
		DO_NAME(EDOTDOT)
		DO_NAME(EBADMSG)
		DO_NAME(EOVERFLOW)
		DO_NAME(ENOTUNIQ)
		DO_NAME(EBADFD)
		DO_NAME(EREMCHG)
		DO_NAME(ELIBACC)
		DO_NAME(ELIBBAD)
		DO_NAME(ELIBSCN)
		DO_NAME(ELIBMAX)
		DO_NAME(ELIBEXEC)
		DO_NAME(EILSEQ)
		DO_NAME(ERESTART)
		DO_NAME(ESTRPIPE)
		DO_NAME(EUSERS)
		DO_NAME(ENOTSOCK)
		DO_NAME(EDESTADDRREQ)
		DO_NAME(EMSGSIZE)
		DO_NAME(EPROTOTYPE)
		DO_NAME(ENOPROTOOPT)
		DO_NAME(EPROTONOSUPPORT)
		DO_NAME(ESOCKTNOSUPPORT)
		DO_NAME(EOPNOTSUPP)
		DO_NAME(EPFNOSUPPORT)
		DO_NAME(EAFNOSUPPORT)
		DO_NAME(EADDRINUSE)
		DO_NAME(EADDRNOTAVAIL)
		DO_NAME(ENETDOWN)
		DO_NAME(ENETUNREACH)
		DO_NAME(ENETRESET)
		DO_NAME(ECONNABORTED)
		DO_NAME(ECONNRESET)
		DO_NAME(ENOBUFS)
		DO_NAME(EISCONN)
		DO_NAME(ENOTCONN)
		DO_NAME(ESHUTDOWN)
		DO_NAME(ETOOMANYREFS)
		DO_NAME(ETIMEDOUT)
		DO_NAME(ECONNREFUSED)
		DO_NAME(EHOSTDOWN)
		DO_NAME(EHOSTUNREACH)
		DO_NAME(EALREADY)
		DO_NAME(EINPROGRESS)
		DO_NAME(ESTALE)
		DO_NAME(EUCLEAN)
		DO_NAME(ENOTNAM)
		DO_NAME(ENAVAIL)
		DO_NAME(EISNAM)
		DO_NAME(EREMOTEIO)
		DO_NAME(EDQUOT)
		DO_NAME(ENOMEDIUM)
		DO_NAME(EMEDIUMTYPE)
		DO_NAME(ECANCELED)
		DO_NAME(ENOKEY)
		DO_NAME(EKEYEXPIRED)
		DO_NAME(EKEYREVOKED)
		DO_NAME(EKEYREJECTED)
		DO_NAME(EOWNERDEAD)
		DO_NAME(ENOTRECOVERABLE)
		DO_NAME(ERFKILL)
		DO_NAME(EHWPOISON)
		default:
			return "?";
	}
}
