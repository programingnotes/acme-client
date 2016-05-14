/*	$Id$ */
/*
 * Copyright (c) 2016 Kristaps Dzonsons <kristaps@bsd.lv>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHORS DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <sys/stat.h>

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __APPLE__
# include <sandbox.h>
#endif

#include "extern.h"

int
chngproc(int netsock, const char *root)
{
	int		  rc;
	long		  op;
	char		 *tok, *thumb;
	char		**fs;
	size_t		  i, fsz;
	void		 *pp;
	FILE		 *f;
	extern enum comp  proccomp;

	proccomp = COMP_CHALLENGE;
	rc = 0;
	thumb = tok = NULL;
	f = NULL;
	fs = NULL;
	fsz = 0;

	/*
	 * File-system and sandbox jailing.
	 */

#ifdef __APPLE__
	if (-1 == sandbox_init(kSBXProfileNoNetwork, 
 	    SANDBOX_NAMED, NULL)) {
		dowarn("sandbox_init");
		goto out;
	}
#endif
	if (-1 == chroot(root)) {
		dowarn("%s: chroot", root);
		goto out;
	} else if (-1 == chdir("/")) {
		dowarn("/: chdir");
		goto out;
	}
#if defined(__OpenBSD__) && OpenBSD >= 201605
	if (-1 == pledge("stdio cpath wpath", NULL)) {
		dowarn("pledge");
		goto out;
	}
#endif

	/* 
	 * Loop while we wait to get a thumbprint and token.
	 * We'll get this for each SAN request.
	 */
	for (;;) {
		if (0 == (op = readop(netsock, COMM_CHNG_OP))) 
			break;
		else if (LONG_MAX == op)
			goto out;

		/* 
		 * Read the thumbprint and token.
		 * The token is the filename, so store that in a vector
		 * of tokens that we'll later clean up.
		 */

		if (NULL == (thumb = readstr(netsock, COMM_THUMB)))
			goto out;
		else if (NULL == (tok = readstr(netsock, COMM_TOK)))
			goto out;

		/* Vector appending... */

		pp = realloc(fs, (fsz + 1) * sizeof(char *));
		if (NULL == pp) {
			dowarn("realloc");
			goto out;
		}
		fs = pp;
		fs[fsz] = tok;
		tok = NULL;
		fsz++;

		/* Create and write to our challenge file. */

		if (NULL == (f = fopen(fs[fsz - 1], "wx"))) {
			dowarn("%s", fs[fsz - 1]);
			goto out;
		} if (-1 == fprintf(f, "%s.%s", fs[fsz - 1], thumb)) {
			dowarn("%s", fs[fsz - 1]);
			goto out;
		} else if (-1 == fclose(f)) {
			dowarn("%s", fs[fsz - 1]);
			goto out;
		}

		free(thumb);
		thumb = NULL;

		dodbg("%s/%s: created", root, fs[fsz - 1]);
		fclose(f);
		f = NULL;

		/* Write our acknowledgement. */

		if ( ! writeop(netsock, COMM_CHNG_ACK, 1))
			goto out;
	}

	rc = 1;
out:
	if (NULL != f)
		fclose(f);
	for (i = 0; i < fsz; i++) {
		if (-1 == remove(fs[i]) && ENOENT != errno)
			dowarn("%s", fs[i]);
		free(fs[i]);
	}
	free(fs);
	free(thumb);
	free(tok);
	close(netsock);
	return(rc);
}
