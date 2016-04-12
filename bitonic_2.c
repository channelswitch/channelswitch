#include "bitonic_2.h"
#include "catf.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#define BLOB(name) _binary_##name##_0_start
extern char BLOB(i6_module)[];
extern char BLOB(i6_swap)[];
extern char BLOB(i6_delay)[];
extern char BLOB(i6_swap_control)[];
extern char BLOB(i6_delay_control)[];
extern char BLOB(c6_source)[];

static unsigned ulog2(unsigned n)
{
	unsigned i = 0;
	while((1 << i) - 1 < n) ++i;
	return i;
}

static int bitonic_stage(
		unsigned inputs,
		unsigned period,
		unsigned x,
		unsigned skip,
		int (*swap)(void *user, unsigned x, unsigned y0, unsigned y1),
		int (*delay)(void *user, unsigned x, unsigned y, unsigned n),
		void *user)
{
	unsigned i, j;
	if(skip < inputs) {
		unsigned block = 2 * skip;
		for(i = 0; i < inputs / block; ++i)
		for(j = 0; j < skip; ++j) {
			if(swap(user, x, i * block + j,
					i * block + skip + j) < 0) return -1;
		}
	}
	else {
		for(i = 0; i < inputs; ++i) {
			if(delay(user, x, i, skip / inputs) < 0) return -1;
		}
	}
	return 0;
}

static int bitonic(
		unsigned inputs,
		unsigned period,
		int (*swap)(void *user, unsigned x, unsigned y0, unsigned y1),
		int (*delay)(void *user, unsigned x, unsigned y, unsigned n),
		void *user)
{
	unsigned x = 0;
	unsigned i, j;
	for(i = 0; i < ulog2(inputs * period) - 1; ++i) {
		j = i + 1;
		while(j--) {
			int status = bitonic_stage(
					inputs,
					period,
					x,	
					1 << j,
					swap,
					delay,
					user);
			if(status < 0) return -1;
			++x;
		}
	}
	return 0;
}

static int add_one(void *user, unsigned a, unsigned b, unsigned c)
{
	unsigned *ptr = user;
	++*ptr;
	return 0;
}

static int max_x(void *user, unsigned a, unsigned b, unsigned c)
{
	unsigned *ptr = user;
	if(a > *ptr) *ptr = a;
	return 0;
}

struct sorter_data {
	char **s;
	unsigned pattern_bit;
	unsigned type;
	unsigned inputs;
	unsigned period;
	unsigned data_delay;
};

#define OUT(label) \
	{ \
		snprintf(e, e_sz, "%s: %d", __FILE__, __LINE__); \
		goto label; \
	}

static int swap(void *user, unsigned x, unsigned y0, unsigned y1)
{
	struct sorter_data *data = user;
	if(y0 == 0) {
		if(catf(data->s, BLOB(i6_swap_control),
			x + 1,				// %1$d
			x,				// %2$d
			ulog2(data->period - 1) - 1,	// %3$d
			0) < 0) return -1;
		++data->data_delay;
	}
	return catf(data->s, BLOB(i6_swap),
			x,				// %1$d
			x + 1,				// %2$d
			y0,				// %3$d
			y1,				// %4$d
			data->pattern_bit++,		// %5$d
			data->type - 1,			// %6$d
			0);
}

static int delay(void *user, unsigned x, unsigned y, unsigned n)
{
	struct sorter_data *data = user;
	char addr[20];
	if(n > 1) {
		snprintf(addr, sizeof addr,
			"t_2[%2$d:0]", x, ulog2(n - 1) - 1);
	}
	else {
		snprintf(addr, sizeof addr, "0");
	}
	if(y == 0) {
		/* What t_2 is when delayer has run for n clock cycles. */
		unsigned t_when_first_out =
				(data->data_delay + n) % data->period;
		if(catf(data->s, BLOB(i6_delay_control),
			x + 1,				// %1$d
			x,				// %2$d
			t_when_first_out,		// %3$d
			ulog2(n - 1),			// %4$d
			ulog2(data->period - 1) - 1,	// %5$d
			0) < 0) return -1;
		data->data_delay += n + 1;
	}
	return catf(data->s, BLOB(i6_delay),
			x,				// %1$d
			x + 1,				// %2$d
			y,				// %3$d
			n,				// %4$d
			data->pattern_bit++,		// %5$d
			data->type - 1,			// %6$d
			n - 1,				// %7$d
			addr,				// %8$s
			0);
}

int bitonic_2(
		char *verilog_filename,
		char *c_filename,
		unsigned inputs,
		unsigned period,
		unsigned type,
		char *e,
		unsigned e_sz)
{
	int ret = -1;

	if(1 << ulog2(inputs - 1) != inputs) {
		snprintf(e, e_sz, "Inputs must be a power of two");
		goto e_args;
	}

	if(1 << ulog2(period - 1) != period) {
		snprintf(e, e_sz, "Period must be a power of two");
		goto e_args;
	}

	/* switch.v */

	unsigned i;
	unsigned n_switches = 0;
	bitonic(inputs, period, add_one, add_one, &n_switches);
	unsigned stages = 0;
	bitonic(inputs, period, max_x, max_x, &stages);
	stages += 1;
	unsigned control_bits = n_switches;
	unsigned bytes_in_word = (control_bits + 7) / 8;
	unsigned biw_bits = ulog2(bytes_in_word - 1);
	unsigned t_bits = ulog2(period - 1);

	char *variable_ports = NULL;
	for(i = 0; i < inputs; ++i) {
		if(catf(&variable_ports, ",\n  in%1$d", i) < 0)
			OUT(e_variable_ports);
	}
	for(i = 0; i < inputs; ++i) {
		if(catf(&variable_ports, ",\n  out%1$d", i) < 0)
			OUT(e_variable_ports);
	}

	char *variable_declarations = NULL;
	for(i = 0; i < inputs; ++i) {
		if(catf(&variable_declarations,
			"input [%2$d:0] in%1$d;\n",
			i, type - 1) < 0)
			OUT(e_variable_declarations);
	}
	for(i = 0; i < inputs; ++i) {
		if(catf(&variable_declarations,
			"assign inputs[%1$d] = in%1$d;\n",
			i, type - 1) < 0)
			OUT(e_variable_declarations);
	}
	for(i = 0; i < inputs; ++i) {
		if(catf(&variable_declarations,
			"output wire [%2$d:0] out%1$d;\n",
			i, type - 1) < 0)
			OUT(e_variable_declarations);
	}
	for(i = 0; i < inputs; ++i) {
		if(catf(&variable_declarations,
			"assign out%1$d = outputs[%1$d];\n",
			i, type - 1) < 0)
			OUT(e_variable_declarations);
	}

	char *sorter = NULL;
	struct sorter_data sorter_data;
	sorter_data.s = &sorter;
	sorter_data.pattern_bit = 0;
	sorter_data.type = type;
	sorter_data.inputs = inputs;
	sorter_data.period = period;
	sorter_data.data_delay = 0;
	if(bitonic(inputs, period, swap, delay, &sorter_data)
		< 0) OUT(e_sorter);

	char *write_obuf = NULL;
	for(i = 0; i < inputs; ++i) {
		if(catf(&write_obuf, "      obuf[%1$d] <= node_%2$d_%1$d;\n",
				i,
				stages) < 0) OUT(e_write_obuf);
	}

	char *node_0 = NULL;
	for(i = 0; i < inputs; ++i) {
		if(catf(&node_0, "reg [%1$d:0] node_0_%2$d;\n",
				type - 1,
				i) < 0) OUT(e_node_0);
	}

	char *write_node_0 = NULL;
	for(i = 0; i < inputs; ++i) {
		if(catf(&write_node_0, "      node_0_%2$d <= inputs_1[%2$d];\n",
				type - 1,
				i) < 0) OUT(e_write_node_0);
	}

	char *v = NULL;
	if(catf(&v, BLOB(i6_module),
		variable_ports,					// %1$s
		variable_declarations,				// %2$s
		control_bits - 1,				// %3$d
		period - 1,					// %4$d
		type - 1,					// %5$d
		inputs,						// %6$d
		ulog2(period - 1) - 1,				// %7$d
		sorter,						// %8$s
		biw_bits ? biw_bits - 1 : 0,			// %9$d
		8 * bytes_in_word - 1,				// %10$d
		t_bits ? t_bits - 1 : 0,			// %11$d
		bytes_in_word - 1,				// %12$d
		stages - 1,					// %13$d
		7 * inputs - 1,					// %14$d
		write_obuf,					// %15$s
		stages,						// %16$d
		node_0,						// %17$s
		write_node_0,					// %18$s
		inputs - 1,					// %19$d
		0) < 0) OUT(e_v);

	int fd = creat(verilog_filename, 0666);
	if(fd < 0) {
		snprintf(e, e_sz, "Could not create %s: %s", verilog_filename,
			strerror(errno));
		goto e_creat_v;
	}

	if(write(fd, v, strlen(v)) < 0) {
		snprintf(e, e_sz, "Could not write to %s: %s", verilog_filename,
			strerror(errno));
		goto e_write_v;
	}

	ret = 0;

e_write_v:
	close(fd);
e_creat_v:
	free(v);
e_v:
e_write_node_0:
	free(write_node_0);
e_node_0:
	free(node_0);
e_write_obuf:
	free(write_obuf);
e_sorter:
	free(sorter);
e_variable_declarations:
	free(variable_declarations);
e_variable_ports:
	free(variable_ports);

	/* generate_pattern_data.c */

	if(ret) goto e_args;
	ret = -1;

	char *c = NULL;
	if(catf(&c, BLOB(c6_source),
		inputs,
		period,
		control_bits,
		0) < 0) OUT(e_c);

	fd = creat(c_filename, 0666);
	if(fd < 0) {
		snprintf(e, e_sz, "Could not create %s: %s", c_filename,
			strerror(errno));
		goto e_creat_c;
	}

	if(write(fd, c, strlen(c)) < 0) {
		snprintf(e, e_sz, "Could not write to %s: %s", c_filename,
			strerror(errno));
		goto e_write_c;
	}

	ret = 0;

e_write_c:
	close(fd);
e_creat_c:
	free(c);
e_c:
e_args:
	return ret;
}
