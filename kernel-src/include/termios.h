#ifndef _TERMIOS_H
#define _TERMIOS_H

typedef unsigned char cc_t;
typedef unsigned int speed_t;
typedef unsigned int tcflag_t;

#define VINTR 0
#define ISIG 0000001
#define NCCS 32
#define ECHO 0000010
#define ICANON 0000002
#define VTIME 5
#define VMIN 6
#define INLCR 0000100
#define IGNCR 0000200
#define ICRNL 0000400
#define OCRNL 0000010

#define TCGETS 0x5401
#define TCSETS 0x5402
#define TIOCGWINSZ 0x5413

typedef struct {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
} winsize_t;

typedef struct {
        tcflag_t c_iflag;
        tcflag_t c_oflag;
        tcflag_t c_cflag;
        tcflag_t c_lflag;
        cc_t c_line;
        cc_t c_cc[NCCS];
        speed_t ibaud;
        speed_t obaud;
} termios_t;

#endif
