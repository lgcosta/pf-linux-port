/* PFLINUX: small bag of BSD-isms glibc lacks, referenced by the copied
 * 8.0 pfctl sources (see tools/UPSTREAM.md). */

#include <sys/types.h>
#include <stdio.h>

/* pf_print_state.c: TCP state names, normally provided by OpenBSD
 * <netinet/tcp_fsm.h> under TCPSTATES (array text copied verbatim from
 * _vendor/openbsd/obbsd/sys/netinet/tcp_fsm.h). */
const char *const tcpstates[] = {
	"CLOSED",	"LISTEN",	"SYN_SENT",	"SYN_RCVD",
	"ESTABLISHED",	"CLOSE_WAIT",	"FIN_WAIT_1",	"CLOSING",
	"LAST_ACK",	"FIN_WAIT_2",	"TIME_WAIT",
};

/*
 * pledge(2)/unveil(2): BSD-only process sandboxes.  OpenBSD 8.0 pfctl
 * sources do not call them, but the no-op stubs keep future upstream
 * syncs building without changes here.
 */
int
pledge(const char *promises, const char *paths[])
{
	(void)promises;
	(void)paths;
	return (0);
}

int
unveil(const char *path, const char *permissions)
{
	(void)path;
	(void)permissions;
	return (0);
}

/*
 * fgetln(3): BSD stdio line reader (returns the line *including* the
 * trailing newline, sets *len, no NUL terminator).  Used by
 * pfctl_osfp.c to read fingerprint files.  Backed by glibc getline(3);
 * the buffer is valid until the next call on the same stream, matching
 * the single-file usage in pfctl.
 */
char *
fgetln(FILE *stream, size_t *len)
{
	static char	*buf;
	static size_t	 bufsz;
	ssize_t		 n;

	n = getline(&buf, &bufsz, stream);
	if (n == -1)
		return (NULL);
	/* OpenBSD fgetln(): *len INCLUDES the trailing newline (when
	 * present) and the buffer is NUL-terminated as an extension;
	 * getline() already gives exactly that. */
	*len = n;
	return (buf);
}

/*
 * uid_from_user(3)/gid_from_group(3): OpenBSD <pwd.h>/<grp.h> helpers
 * used by parse.y for the `user`/`group` filter options.  OpenBSD
 * returns 0 on success; on failure returns -1 and sets errno (ENOENT
 * when the name is unknown).
 */
#include <pwd.h>
#include <grp.h>
#include <errno.h>

int
uid_from_user(const char *name, uid_t *uidp)
{
	struct passwd	*pw;

	errno = 0;
	if ((pw = getpwnam(name)) == NULL) {
		if (errno == 0)
			errno = ENOENT;
		return (-1);
	}
	if (uidp != NULL)
		*uidp = pw->pw_uid;
	return (0);
}

int
gid_from_group(const char *name, gid_t *gidp)
{
	struct group	*gr;

	errno = 0;
	if ((gr = getgrnam(name)) == NULL) {
		if (errno == 0)
			errno = ENOENT;
		return (-1);
	}
	if (gidp != NULL)
		*gidp = gr->gr_gid;
	return (0);
}

/*
 * freezero(3): OpenBSD secure realloc-free.  Used by pf_ruleset.c's
 * userland rs_free().  (explicit_bzero is in glibc <string.h>.)
 */
#include <string.h>

void
freezero(void *ptr, size_t siz)
{

	if (ptr == NULL)
		return;
	explicit_bzero(ptr, siz);
	free(ptr);
}
