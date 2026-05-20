/*
 * POSIX.1-2024 compliant header (non-XSI).
 */

#ifndef _SYS_TERMIOS_H
#define _SYS_TERMIOS_H

#include <sys/types.h>

typedef unsigned int cc_t;
typedef unsigned int speed_t;
typedef unsigned int tcflag_t;

#define VEOF	0
#define VEOL	1
#define VERASE	2
#define VINTR	3
#define VKILL	4
#define VMIN	5
#define VQUIT	6
#define VSTART	7
#define VSTOP	8
#define VSUSP	9
#define VTIME	10
#define NCCS	11

struct termios {
    tcflag_t  c_iflag;    // Input modes
    tcflag_t  c_oflag;    // Output modes
    tcflag_t  c_cflag;    // Control modes
    tcflag_t  c_lflag;    // Local modes
    cc_t      c_cc[NCCS]; // Control characters
};

/* constants used for c_iflag */
#define BRKINT	0x0001 // Signal interrupt on break
#define ICRNL	0x0002 // Map CR to NL on input
#define IGNBRK	0x0004 // Ignore break condition
#define IGNCR	0x0008 // Ignore CR
#define IGNPAR	0x0010 // Ignore characters with parity errors
#define INLCR	0x0020 // Map NL to CR on input
#define INPCK	0x0040 // Enable input parity check
#define ISTRIP	0x0080 // Strip character
#define IXANY	0x0100 // Enable any character to restart output
#define IXOFF	0x0200 // Enable start/stop input control
#define IXON	0x0400 // Enable start/stop output control
#define PARMRK	0x0800 // Mark parity errors

/* constants used for c_oflag */
#define OPOS	0x0001

/* POSIX required baud rates */
#define B0			0U
#define B50			50U
#define B75			75U
#define B110		110U
#define B134		134U
#define B150		150U
#define B200		200U
#define B300		300U
#define B600		600U
#define B1200		1200U
#define B1800		1800U
#define B2400		2400U
#define B4800		4800U
#define B9600		9600U
#define	B19200		19200U
#define	B38400		38400U

/* constants used for c_cflag */
#define CSIZE	0x00F
#define   CS5	0x001
#define   CS6	0x002
#define   CS7	0x004
#define   CS8	0x008
#define CSTOPB	0x010
#define CREAD	0x020
#define PARENB	0x040
#define PARODD	0x080
#define HUPCL	0x100
#define CLOCAL	0x200

/* constants used for c_lflag */
#define ISIG	0x00001  // Enable signals
#define ICANON	0x00002  // Canonical input (erase and kill processing)
#define ECHO	0x00004  // Enable echo
#define ECHOE	0x00008  // Echo erase character as error-correcting backspace
#define ECHOK	0x00010  // Echo KILL
#define ECHONL	0x00020  // Echo NL
#define NOFLSH	0x00040  // Disable flush after interrupt or quit
#define TOSTOP	0x00080  // Send SIGTTOU for background output
#define IEXTEN	0x00100  // Enable implementation-defined input processing

struct winsize {
	unsigned short ws_row; // Rows, in characters
	unsigned short ws_col; // Columns, in characters
};

/* constants used for tcsetattr() */
#define TCSANOW		0
#define TCSADRAIN	1
#define TCSAFLUSH	2

/* constants used for tcflush() */
#define	TCIFLUSH	0
#define	TCOFLUSH	1
#define	TCIOFLUSH	2

/* constants used for tcflow() */
#define	TCOOFF		0
#define	TCOON		1
#define	TCIOFF		2
#define	TCION		3

speed_t cfgetispeed(const struct termios *);
speed_t cfgetospeed(const struct termios *);
int     cfsetispeed(struct termios *, speed_t);
int     cfsetospeed(struct termios *, speed_t);
int     tcdrain(int);
int     tcflow(int, int);
int     tcflush(int, int);
int     tcgetattr(int, struct termios *);
pid_t   tcgetsid(int);
int     tcgetwinsize(int, struct winsize *);
int     tcsendbreak(int, int);
int     tcsetattr(int, int, const struct termios *);
int     tcsetwinsize(int, const struct winsize *);

#endif /* _SYS_TERMIOS_H */
