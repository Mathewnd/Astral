#ifndef _TERMIOS_H_INCLUDE
#define _TERMIOS_H_INCLUDE

typedef unsigned char cc_t;
typedef unsigned int speed_t;
typedef unsigned int tcflag_t;
#define NCCS     32
#define ECHO 0000010
#define ICANON 0000002
#define VMIN     6

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
