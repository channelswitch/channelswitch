#include "writecode_bitonic.h"
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
extern char BLOB(c1_source)[];

int writecode_bitonic(
	char *c_filename,
	unsigned inputs,
	unsigned period,
	unsigned type,
	char *e,
	unsigned e_sz)
{
	int err = -1;

	int source = creat(c_filename, 0666);
	if(source < 0) {
		snprintf(e, e_sz, "Could not create %s: %s", c_filename,
			strerror(errno));
		goto err0;
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

	char *source_all = NULL;
	status = catf(&source_all, BLOB(c1_source),
		inputs, period, n, adr_bits);
	if(status < 0) {
		snprintf(e, e_sz, "Out of memory");
		goto err2;
	}

	status = write(source, source_all, strlen(source_all));
	if(status < 0) {
		snprintf(e, e_sz, "Could not write %s: %s", c_filename,
			strerror(errno));
		goto err2;
	}

	err = 0;

err2:	free(source_all);
err1:	close(source);
err0:	return err;
}
