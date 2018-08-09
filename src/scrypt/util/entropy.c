#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

#include "scrypt/util/entropy.h"

/**
 * XXX Portability
 * XXX We obtain random bytes from the operating system by opening
 * XXX /dev/urandom and reading them from that device; this works on
 * XXX modern UNIX-like operating systems but not on systems like
 * XXX win32 where there is no concept of /dev/urandom.
 */

/**
 * entropy_read(buf, buflen):
 * Fill the given buffer with random bytes provided by the operating system.
 */
int
entropy_read(uint8_t * buf, size_t buflen)
{
	int fd;
	ssize_t lenread;

	/* Sanity-check the buffer size. */
	if (buflen > SSIZE_MAX) {
		fprintf(stderr,"Programmer error: "
		    "Trying to read insane amount of random data: %zu\n",
		    buflen);
		goto err0;
	}

	/* Open /dev/urandom. */
	if ((fd = open("/dev/urandom", O_RDONLY)) == -1) {
		fprintf(stderr,"open(/dev/urandom)\n");
		goto err0;
	}

	/* Read bytes until we have filled the buffer. */
	while (buflen > 0) {
		if ((lenread = read(fd, buf, buflen)) == -1) {
			fprintf(stderr,"read(/dev/urandom)\n");
			goto err1;
		}

		/* The random device should never EOF. */
		if (lenread == 0) {
			fprintf(stderr,"EOF on /dev/urandom?\n");
			goto err1;
		}

		/* We've filled a portion of the buffer. */
		buf += (size_t)lenread;
		buflen -= (size_t)lenread;
	}

	/* Close the device. */
	while (close(fd) == -1) {
		if (errno != EINTR) {
			fprintf(stderr,"close(/dev/urandom)\n");
			goto err0;
		}
	}

	/* Success! */
	return (0);

err1:
	close(fd);
err0:
	/* Failure! */
	return (-1);
}
