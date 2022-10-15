#ifndef _TERMIOS_H_INCLUDE
#define _TERMIOS_H_INCLUDE

typedef unsigned char cc_t;
typedef unsigned int speed_t;
typedef unsigned int tcflag_t;

#define VINTR    0
#define ISIG 0000001
#define NCCS     32
#define ECHO 0000010
#define ICANON 0000002
#define VMIN     6
#define INLCR 0000100
#define IGNCR 0000200
#define ICRNL 0000400
#define OCRNL 0000010


typedef struct{
        tcflag_t c_iflag;
        tcflag_t c_oflag;
        tcflag_t c_cflag;
        tcflag_t c_lflag;
        cc_t c_line;
        cc_t c_cc[NCCS];
        speed_t ibaud;
        speed_t obaud;
} termios;

#endif
