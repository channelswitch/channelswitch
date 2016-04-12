#include "writeheader.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "catf.h"
#include <fcntl.h>

static unsigned ulog2(unsigned n)
{
	unsigned i = 0;
	while(1) {
		if((1 << i) >= n) return i;
		++i;
	}
}

static unsigned npot(unsigned n)
{
	unsigned i = 0;
	while(1) {
		if((1 << i) >= n) return 1 << i;
		++i;
	}
}

#define BLOB(name) _binary_##name##_0_start
extern char BLOB(c_header)[];

static char *copy_string(char *str, unsigned long sz)
{
	char *out = malloc(sz + 1);
	if(!out) return NULL;
	memcpy(out, str, sz);
	out[sz] = '\0';
	return out;
}

int writeheader(
	char *h_filename,
	unsigned inputs,
	unsigned period,
	unsigned type,
	char *e,
	unsigned e_sz)
{
	int err = -1;

	int header = creat(h_filename, 0666);
	if(header < 0) {
		snprintf(e, e_sz, "Could not create %s: %s", h_filename,
			strerror(errno));
		goto err1;
	}

	unsigned m, k;
	m = npot(inputs);
	k = npot(period);
	unsigned n = m * k;
	unsigned delay = 0;

	unsigned adr_bits = ulog2(n);
	unsigned val_bits = type;
	unsigned time_bits = ulog2(k);
	unsigned i;

	int status;

	status = write(header, BLOB(c_header), strlen(BLOB(c_header)));
	if(status < 0) {
		snprintf(e, e_sz, "Could not write %s: %s", h_filename,
			strerror(errno));
		goto err1;
	}

	err = 0;

err1:	close(header);
err0:	return err;
}
