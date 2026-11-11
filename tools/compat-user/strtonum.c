/* PFLINUX: BSD strtonum(3) for glibc — follows OpenBSD
 * lib/libc/stdlib/strtonum.c semantics (public domain).
 * Declared in tools/include/pflinux_compat.h. */

#include <sys/types.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>

#define INVALID		1
#define TOO_SMALL	2
#define TOO_LARGE	3

long long
strtonum(const char *numstr, long long minval, long long maxval,
    const char **errstrp)
{
	long long ll = 0;
	int error = 0;
	struct errval {
		const char *errstr;
		int err;
	} ev[4] = {
		{ NULL,		0 },
		{ "invalid",	EINVAL },
		{ "too small",	ERANGE },
		{ "too large",	ERANGE },
	};

	ev[0].err = errno;
	errno = 0;
	if (minval > maxval) {
		error = INVALID;
	} else {
		char *str;
		ll = strtoll(numstr, &str, 10);
		if (numstr == str || str == NULL || *str != '\0')
			error = INVALID;
		else if ((errno == ERANGE && ll < minval) || ll < minval)
			error = TOO_SMALL;
		else if ((errno == ERANGE && ll > maxval) || ll > maxval)
			error = TOO_LARGE;
	}

	if (errstrp != NULL)
		*errstrp = ev[error].errstr;
	errno = ev[error].err;
	if (error)
		ll = 0;

	return (ll);
}
