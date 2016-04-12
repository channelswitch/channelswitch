#include "catf.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

int catf(char **buf, char *format, ...)
{
	char *buf1;
	va_list args;
	long len1;
	va_start(args, format);
	len1 = vsnprintf(NULL, 0, format, args);
	va_end(args);
	if(len1 < 0) goto error;
	unsigned long len0 = *buf ? strlen(*buf) : 0;
	buf1 = realloc(*buf, len0 + len1 + 1);
	if(!buf1) goto error;
	*buf = buf1;
	va_start(args, format);
	if(vsprintf(buf1 + len0, format, args) < 0) goto error;
	va_end(args);
	return 0;

error:	free(*buf);
	return -1;
}

