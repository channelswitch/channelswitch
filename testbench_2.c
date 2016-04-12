#include "testbench_2.h"
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
		if((1 << i) > n) return i;
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
extern char BLOB(tb2_module)[];

#define OUT(label) \
	{ \
		snprintf(e, e_sz, "%s: %d", __FILE__, __LINE__); \
		goto label; \
	}

int testbench_2(
		char *testbench_v,
		unsigned inputs,
		unsigned period,
		unsigned type,
		char *e,
		unsigned e_sz)
{
	int err = -1;

	int file = creat(testbench_v, 0666);
	if(file < 0) {
		snprintf(e, e_sz, "Could not create %s: %s", testbench_v,
			strerror(errno));
		goto err0;
	}

	unsigned m, k;
	m = inputs;
	k = period;
	unsigned n = m * k;
	unsigned delay = 0;

	char adr[20];
	sprintf(adr, "[%d:0]", ulog2(n) - 1);

	char val[20];
	sprintf(val, "[%d:0]", type - 1);

	char time_type[20];
	sprintf(time_type, "[%d:0]", period > 1 ? ulog2(period - 1) - 1 : 0);

	/* Default error. */
	snprintf(e, e_sz, "Out of memory");

	int status;

	char *inargs = NULL;
	unsigned i;
	for(i = 0; i < m; ++i) {
		status = catf(&inargs,
			"reg %2$s in%1$d;\n", i, val);
		if(status < 0) goto err3;
	}

	char *outargs = NULL;
	for(i = 0; i < m; ++i) {
		status = catf(&outargs,
			"wire %2$s out%1$d;\n", i, val);
		if(status < 0) goto err4;
	}

	char *module_in = NULL;
	for(i = 0; i < m; ++i) {
		status = catf(&module_in,
			",\n\tin%1$d", i);
		if(status < 0) goto err5;
	}

	char *module_out = NULL;
	for(i = 0; i < m; ++i) {
		status = catf(&module_out,
			",\n\tout%1$d", i);
		if(status < 0) goto err6;
	}

	char *in_args = NULL;
	for(i = 0; i < m; ++i) {
		status = catf(&in_args,
			"\t\t\tget_input();\n"
			"\t\t\tin%1$d = input_value;\n", i);
		if(status < 0) goto err7;
	}

	char *out_args = NULL;
	for(i = 0; i < m; ++i) {
		status = catf(&out_args,
			"\t\t\tget_output();\n"
			"\t\t\tif(out%1$d != output_value && out%1$d != 0) "
			"begin\n"
			"\t\t\t\t$error(\"Mismatch\\n\");\n"
			"\t\t\t\t$stop();\n"
			"\t\t\tend\n", i);
		if(status < 0) goto err8;
	}

	char *write_inputs = NULL;
	for(i = 0; i < m; ++i) {
		status = catf(&write_inputs,
			"\tin%1$d <= $random(seed_in) & ((1 << %2$d) - 1);\n",
			i, type);
		if(status < 0) goto err9;
	}

	char *check_outputs = NULL;
	for(i = 0; i < m; ++i) {
		status = catf(&check_outputs,
			"\tif(out%1$d !== output0[output_ptr]) valid0 = 0;\n"
			"\tif(out%1$d !== output1[output_ptr]) valid1 = 0;\n"
			"\toutput_ptr = output_ptr + 1;\n",
			i, (1 << type) - 1);
		if(status < 0) goto err10;
	}

	char *everything = NULL;
	unsigned test_reps = 1000 * period + 100 * n;
	if(test_reps < 10000) test_reps = 10000;
	status = catf(&everything, BLOB(tb2_module),
		inargs,
		outargs,
		module_in,
		module_out,
		time_type,
		in_args,
		out_args,
		0,
		n,
		type,
		m,
		ulog2(n),
		test_reps,
		50 * k,
		write_inputs,
		check_outputs);
	if(status < 0) goto err11;

	status = write(file, everything, strlen(everything));
	if(status < 0) {
		snprintf(e, e_sz, "Could not write %s: %s", testbench_v,
			strerror(errno));
		goto err11;
	}

	err = 0;

err11:	free(everything);
err10:	free(check_outputs);
err9:	free(write_inputs);
err8:	free(out_args);
err7:	free(in_args);
err6:	free(module_out);
err5:	free(module_in);
err4:	free(outargs);
err3:	free(inargs);
err2:	close(file);
err0:	return err;
}
