/*
 * termios.h --
 *
 *	Declarations of structures and flags for controlling the `termio'
 *      general terminal interface.
 *
 * Copyright 1989 Regents of the University of California
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 *
 * $Header: /sprite/src/lib/include/sys/RCS/termios.h,v 1.2 91/03/29 18:06:51 shirriff Exp $
 */

#ifndef _TERMIOS_
#define _TERMIOS_

/*
 * input flag bits
 */
#define	IGNBRK	0x00000001
#define	BRKINT	0x00000002
#define	IGNPAR	0x00000004
#define	PARMRK	0x00000008
#define	INPCK	0x00000010
#define	ISTRIP	0x00000020
#define	INLCR	0x00000040
#define	IGNCR	0x00000080
#define	ICRNL	0x00000100
#define	IUCLC	0x00000200
#define	IXON	0x00000400
#define	IXANY	0x00000800
#define	IXOFF	0x00001000
#define	IMAXBEL	0x00002000

/*
 * output flag bits
 */
#define	OPOST	0x00000001
#define	OLCUC	0x00000002
#define	ONLCR	0x00000004
#define	OCRNL	0x00000008
#define	ONOCR	0x00000010
#define	ONLRET	0x00000020
#define	OFILL	0x00000040
#define	OFDEL	0x00000080
#define	NLDLY	0x00000100
#define	CRDLY	0x00000600
#define	TABDLY	0x00001800
#define	TAB3	XTABS
#define	BSDLY	0x00002000
#define	VTDLY	0x00004000
#define	VT0	0
#define	VT1	0x00004000
#define	FFDLY	0x00008000
#define	PAGEOUT	0x00010000
#define	WRAP	0x00020000

/*
 * control flag bits
 */
#define	CBAUD	0x0000000f
#define	CSIZE	0x00000030
#define	CS5	0
#define	CS6	0x00000010
#define	CS7	0x00000020
#define	CS8	0x00000030
#define	CSTOPB	0x00000040
#define	CREAD	0x00000080
#define	PARENB	0x00000100
#define	PARODD	0x00000200
#define	HUPCL	0x00000400
#define	CLOCAL	0x00000800
#define	LOBLK	0x00001000
#define	CIBAUD	0x000f0000
#define	CRTSCTS	0x80000000

#define	IBSHIFT	16

/*
 * line discipline flag bits
 */
#define	ISIG	0x00000001
#define	ICANON	0x00000002
#define	XCASE	0x00000004
#define	ECHOE	0x00000010
#define	ECHOK	0x00000020
#define	ECHONL	0x00000040
#define	ECHOCTL	0x00000200
#define	ECHOPRT	0x00000400
#define	ECHOKE	0x00000800
#define	DEFECHO	0x00001000
#define IEXTEN	0x00008000


/*
 * control characters
 */
#define	VINTR		0
#define	VQUIT		1
#define	VERASE		2
#define	VKILL		3
#define	VEOF		4
#define	VMIN		VEOF
#define	VEOL		5
#define	VTIME		VEOL
#define	VEOL2		6
#define	VSWTCH		7
#define	VSTART		8
#define	VSTOP		9
#define	VSUSP		10
#define	VDSUSP		11
#define	VREPRINT	12
#define	VDISCARD	13
#define	VWERASE		14
#define	VLNEXT		15
#define	VSTATUS		16

/*
 * codes 1-5 are used for obsolete calls
 */
#define	TCXONC		_IO('T', 6)
#define	TCFLSH		_IO('T', 7)
#define	TCGETS		_IOR('T', 8, struct termios)
#define	TCSETS		_IOW('T', 9, struct termios)
#define	TCSETSW		_IOW('T', 10, struct termios)
#define	TCSETSF		_IOW('T', 11, struct termios)
#define	TCSNDBRK	_IO('T', 12)
#define	TCDRAIN		_IO('T', 13)

#define	NCCS	17

/*
 * Ioctl control packet
 */
struct termios {
	unsigned long	c_iflag;	/* input */
	unsigned long	c_oflag;	/* output */
	unsigned long	c_cflag;	/* control */
	unsigned long	c_lflag;	/* line discipline */
	char		c_line;		/* line discipline number */
	unsigned char	c_cc[NCCS];	/* control characters */
};

#endif /* _TERMIOS */

