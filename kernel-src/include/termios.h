#ifndef _TERMIOS_H
#define _TERMIOS_H

typedef unsigned char cc_t;
typedef unsigned int speed_t;
typedef unsigned int tcflag_t;

#define ISIG 0000001
#define ECHO 0000010
#define ICANON 0000002
#define INLCR 0000100
#define IGNCR 0000200
#define ICRNL 0000400
#define OCRNL 0000010
#define ECHOCTL 0001000
#define ONLCR 0000004
#define CSTOPB 0000100
#define CSIZE 0000060
#define PARENB 0000400
#define PARODD 0001000
#define CBAUD   0010017

#define B0       0
#define B50      1
#define B75      2
#define B110     3
#define B134     4
#define B150     5
#define B200     6
#define B300     7
#define B600     8
#define B1200    9
#define B1800    10
#define B2400    11
#define B4800    12
#define B9600    13
#define B19200   14
#define B38400   15
#define B57600   0010001
#define B115200  0010002
#define B230400  0010003
#define B460800  0010004
#define B500000  0010005
#define B576000  0010006
#define B921600  0010007
#define B1000000 0010010
#define B1152000 0010011
#define B1500000 0010012
#define B2000000 0010013
#define B2500000 0010014
#define B3000000 0010015
#define B3500000 0010016
#define B4000000 0010017

#define CS5 0000000
#define CS6 0000020
#define CS7 0000040
#define CS8 0000060

#define TCGETS 0x5401
#define TCSETS 0x5402
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414

#define NCCS     32
#define VINTR    0
#define VQUIT    1
#define VERASE   2
#define VKILL    3
#define VEOF     4
#define VTIME    5
#define VMIN     6
#define VSWTC    7
#define VSTART   8
#define VSTOP    9
#define VSUSP    10
#define VEOL     11
#define VREPRINT 12
#define VDISCARD 13
#define VWERASE  14
#define VLNEXT   15
#define VEOL2    16

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

#define BAUD_DO(x) \
	case B##x: \
		return x; \
		break;

static inline int termios_baud_to_number(termios_t *termios) {
	switch (termios->c_cflag & CBAUD) {
		BAUD_DO(0)
		BAUD_DO(50)
		BAUD_DO(75)
		BAUD_DO(110)
		BAUD_DO(134)
		BAUD_DO(150)
		BAUD_DO(200)
		BAUD_DO(300)
		BAUD_DO(600)
		BAUD_DO(1200)
		BAUD_DO(1800)
		BAUD_DO(2400)
		BAUD_DO(4800)
		BAUD_DO(9600)
		BAUD_DO(19200)
		BAUD_DO(38400)
		BAUD_DO(57600)
		BAUD_DO(115200)
		BAUD_DO(230400)
		BAUD_DO(460800)
		BAUD_DO(500000)
		BAUD_DO(576000)
		BAUD_DO(921600)
		BAUD_DO(1000000)
		BAUD_DO(1152000)
		BAUD_DO(1500000)
		BAUD_DO(2000000)
		BAUD_DO(2500000)
		BAUD_DO(3000000)
		BAUD_DO(3500000)
		BAUD_DO(4000000)
	}

	return -1;
}

#undef BAUD_DO

#endif
