#include "vpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "catf.h"
#include <fcntl.h>

#define BLOB(name) _binary_##name##_0_start
extern char BLOB(tb_vpi)[];

#define OUT(label) \
	{ \
		snprintf(e, e_sz, "%s: %d", __FILE__, __LINE__); \
		goto label; \
	}

int vpi(
		char *vpi_c,
		char *e,
		unsigned e_sz)
{
	int ret = -1;

	char *c = NULL;
	if(catf(&c, BLOB(tb_vpi)) < 0) OUT(e_c);

	int fd = creat(vpi_c, 0666);
	if(fd < 0) {
		snprintf(e, e_sz, "Could not create %s: %s", vpi_c,
			strerror(errno));
		goto e_creat;
	}

	if(write(fd, c, strlen(c)) < 0) {
		snprintf(e, e_sz, "Could not write %s: %s", vpi_c,
			strerror(errno));
		goto e_write;
	}

	ret = 0;

e_write:
	close(fd);
e_creat:
	free(c);
e_c:
	return ret;
}
