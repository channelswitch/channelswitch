#include "memories_2.h"
#include "catf.h"
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#define BLOB(name) _binary_##name##_0_start
extern char BLOB(i4_module)[];
extern char BLOB(c4_source)[];

#define OUT(label) \
	{ \
		snprintf(e, e_sz, "%s: %d", __FILE__, __LINE__); \
		goto label; \
	}

static unsigned ulog2(unsigned n)
{
	unsigned i = 0;
	while((1 << i) - 1 < n) ++i;
	return i;
}

static int cat_data_memory(char **s, unsigned width, unsigned length,
		char *name, char *class)
{
	return catf(s,
		"reg [%1$d:0] memory_%2$s[0:%3$d];\n"
		"reg en_%2$s;\n"
		"reg [%4$d:0] addr_%2$s;\n"
		"reg [%1$d:0] in_%2$s;\n"
		"reg [%1$d:0] out_%2$s;\n"
		"always @(posedge clk) begin\n"
		"\tif(enable_data_memories) begin\n"
		"\t\tif(we_%5$s) memory_%2$s[addr_%2$s] <= in_%2$s;\n"
		"\t\telse out_%2$s <= memory_%2$s[addr_%2$s];\n"
		"\tend\n"
		"end\n"
		"\n",
		width - 1,
		name,
		length - 1,
		length > 1 ? ulog2(length - 1) - 1 : 0,
		class);
}

static unsigned mem_sz2(unsigned mem_sz, unsigned n_mem, unsigned period,
		unsigned i)
{
	return (n_mem * (mem_sz - 1) + i % n_mem) >= period ?
		mem_sz - 1 : mem_sz;
}

static char *cat_mem_ctl(
		unsigned inputs,
		unsigned period,
		unsigned n_mem,
		unsigned mem_sz,
		char write_class,
		char read_class)
{
	char *s = NULL;
	unsigned i;
	for(i = 0; i < inputs * n_mem; ++i) {
		unsigned a = i / n_mem, b = i % n_mem;
		if(catf(&s,
			"        if(`enabled(%5$d)) begin\n"
			"          en_%1$d_%2$d_%3$c <= 1;\n"
			"          in_%1$d_%2$d_%3$c <= "
			"inputs_2[`in_from(%5$d)];\n"
			"          addr_%1$d_%2$d_%3$c <= wraddr_%1$d_%2$d;\n"
			"          wraddr_%1$d_%2$d <= wraddr_%1$d_%2$d "
			"== %4$d ? 0 : wraddr_%1$d_%2$d + 1;\n"
			"        end\n"
			"        else begin\n"
			"          en_%1$d_%2$d_%3$c <= 0;\n"
			"        end\n",
			a,
			b,
			write_class,
			mem_sz2(mem_sz, n_mem, period, i) - 1,
			i,
			0) < 0) goto error;
	}
	for(i = 0; i < inputs * n_mem; ++i) {
		unsigned a = i / n_mem, b = i % n_mem;
		if(catf(&s,
			"        en_%1$d_%2$d_%3$c <= 1;\n"
			"        addr_%1$d_%2$d_%3$c <= `raddr(%1$d);\n",
			a, b, read_class, i) < 0) goto error;
	}

	return s;

error:
	free(s);
	return NULL;
}

int memories_2(
	char *v_filename,
	char *c_filename,
	unsigned inputs,
	unsigned period,
	unsigned type,
	char *e,
	unsigned e_sz)
{
	int ret = -1;

	unsigned i;

	unsigned mem_sz;
	unsigned n_mem;
	if(period >= inputs) {
		n_mem = inputs;
		mem_sz = (period - 1) / inputs + 1;
	}
	else {
		n_mem = period;
		mem_sz = 1;
	}
	unsigned time_bits = period > 1 ? ulog2(period - 1) : 1;
	/* Here's the control word format:
	 * First comes inputs * n_mem repetitions of the following:
	 *  - 1 bit, enable this memory during this clock cycle?
	 *  - log2(inputs - 1) bits, which input to read from?
	 * Then inputs repetitions of this:
	 *  - log2(n_mem - 1) bits, which memory to read from?
	 *  - log2(mem_sz - 1) bits, read address */
	unsigned aword_bits = 1 + ulog2(inputs - 1);
	unsigned bword_bits = ulog2(n_mem - 1) + ulog2(mem_sz - 1);
	unsigned control_bits =
		inputs * n_mem * aword_bits +
		inputs * bword_bits;
	unsigned bytes_in_word = (control_bits + 7) / 8;

	char *dynamic_ports = NULL;
	for(i = 0; i < inputs; ++i) {
		if(catf(&dynamic_ports, ",\n  in%1$d", i) < 0)
			OUT(e_dynamic_ports);
	}
	for(i = 0; i < inputs; ++i) {
		if(catf(&dynamic_ports, ",\n  out%1$d", i) < 0)
			OUT(e_dynamic_ports);
	}

	char *dynamic_declarations = NULL;
	for(i = 0; i < inputs; ++i) {
		if(catf(&dynamic_declarations, "input [%2$d:0] in%1$d;\n",
			i, type - 1) < 0) OUT(e_dynamic_declarations);
	}
	for(i = 0; i < inputs; ++i) {
		if(catf(&dynamic_declarations,
			"output wire [%2$d:0] out%1$d;\n",
			i, type - 1) < 0) OUT(e_dynamic_declarations);
	}
	for(i = 0; i < inputs; ++i) {
		if(catf(&dynamic_declarations,
			"assign inputs[%1$d] = in%1$d;\n",
			i) < 0) OUT(e_dynamic_declarations);
	}

	char *memories = NULL;
	for(i = 0; i < inputs * n_mem; ++i) {
		char buf[20];
		snprintf(buf, sizeof buf, "%d_%d_a", i / n_mem, i % n_mem);
		if(cat_data_memory(&memories, type,
			mem_sz2(mem_sz, n_mem, period, i), buf, "a") < 0)
			OUT(e_memories);
		snprintf(buf, sizeof buf, "%d_%d_b", i / n_mem, i % n_mem);
		if(cat_data_memory(&memories, type,
			mem_sz2(mem_sz, n_mem, period, i), buf, "b") < 0)
			OUT(e_memories);
	}
	for(i = 0; i < inputs * n_mem; ++i) {
		if(catf(&memories, "reg [%3$d:0] wraddr_%1$d_%2$d;\n",
			i / n_mem, i % n_mem,
			time_bits - 1) < 0) OUT(e_memories);
	}

	char *wraddr_reset = NULL;
	for(i = 0; i < inputs * n_mem; ++i) {
		if(catf(&wraddr_reset, "    wraddr_%1$d_%2$d <= 0;\n",
			i / n_mem, i % n_mem,
			time_bits - 1) < 0) OUT(e_wraddr_reset);
	}

	char *mem_is_0 = cat_mem_ctl(inputs, period, n_mem, mem_sz, 'a', 'b');
	if(!mem_is_0) OUT(e_mem_is_0);

	char *mem_is_1 = cat_mem_ctl(inputs, period, n_mem, mem_sz, 'b', 'a');
	if(!mem_is_1) OUT(e_mem_is_1);

	char *read_out = NULL;
	for(i = 0; i < inputs; ++i) {
		if(catf(&read_out,
			"      obuf[%1$d] <= memouts_%1$d[`which_mem(%1$d)];\n",
			i,
			0) < 0) OUT(e_read_out);
	}

	char *memouts = NULL;
	for(i = 0; i < inputs; ++i) {
		if(catf(&memouts,
			"wire [%2$d:0] memouts_%1$d[0:%3$d];\n",
			i,
			type - 1,
			n_mem - 1,
			0) < 0) OUT(e_memouts);
	}
	for(i = 0; i < inputs * n_mem; ++i) {
		unsigned a = i / n_mem, b = i % n_mem;
		if(catf(&memouts,
			"assign memouts_%1$d[%2$d] = mem_2 ? out_%1$d_%2$d_a "
			": out_%1$d_%2$d_b;\n",
			a, b) < 0) OUT(e_memouts);
	}
	for(i = 0; i < inputs; ++i) {
		if(catf(&memouts,
			"assign out%1$d = outputs[%1$d];\n",
			i,
			0) < 0) OUT(e_memouts);
	}

	char *generate_c_initial = NULL;
	for(i = 0; i < inputs * n_mem; ++i) {
		unsigned a = i / n_mem, b = i % n_mem;
		if(catf(&generate_c_initial,
			"      c_initial[%2$d] <= m_counter == %1$d;\n"
			"      c_initial[%3$d:%4$d] <= %5$d;\n",
			b,
			i * aword_bits,
			i * aword_bits + 1 + ulog2(inputs - 1) - 1,
			i * aword_bits + 1,
			a,
			0) < 0) OUT(e_generate_c_initial);
	}
	for(i = 0; i < inputs; ++i) {
		unsigned skip = inputs * n_mem * aword_bits;
		if(catf(&generate_c_initial,
			"      c_initial[%1$d:%2$d] <= m_counter;\n"
			"      c_initial[%3$d:%4$d] <= raddr_counter;\n",
			skip + i * bword_bits + ulog2(n_mem - 1) - 1,
			skip + i * bword_bits,
			skip + i * bword_bits + ulog2(n_mem - 1) +
				ulog2(mem_sz - 1) - 1,
			skip + i * bword_bits + ulog2(n_mem - 1),
			0) < 0) OUT(e_generate_c_initial);
	}

	char *v = NULL;
	if(catf(&v, BLOB(i4_module),
		dynamic_ports,					// %1$s
		dynamic_declarations,				// %2$s
		time_bits - 1,					// %3$d
		type - 1,					// %4$d
		inputs - 1,					// %5$d
		control_bits - 1,				// %6$d
		period - 1,					// %7$d
		time_bits - 1,					// %8$d
		bytes_in_word * 8 - 1,				// %9$d
		bytes_in_word * 8 - 8,				// %10$d
		bytes_in_word - 1,				// %11$d
		memories,					// %12$s
		mem_is_0,					// %13$s
		mem_is_1,					// %14$s
		wraddr_reset,					// %15$s
		15 * inputs - 1,				// %16$d
		read_out,					// %17$s
		memouts,					// %18$s
		n_mem - 1,					// %19$d
		ulog2(n_mem - 1),				// %20$d
		generate_c_initial,				// %21$s
		ulog2(mem_sz - 1) - 1,				// %22$d
		inputs,						// %23$d
		aword_bits,					// %24$d
		ulog2(inputs - 1),				// %25$d
		inputs * n_mem * aword_bits +
			ulog2(n_mem - 1) +
			ulog2(mem_sz - 1) - 1,			// %26$d
		bword_bits,					// %27$d
		inputs * n_mem * aword_bits + ulog2(n_mem - 1),	// %28$d
		inputs * n_mem * aword_bits +
			ulog2(n_mem - 1) - 1,			// %29$d
		inputs * n_mem * aword_bits,			// %30$d
		bytes_in_word > 1 ?
			ulog2(bytes_in_word - 1) - 1 : 0,	// %31$d
		ulog2(period - 1),				// %32$d
		0) < 0) OUT(e_v);

	int fd = creat(v_filename, 0666);
	if(fd < 0) {
		snprintf(e, e_sz, "Could not create %s: %s",
			v_filename, strerror(errno));
		goto e_creat_v;
	}

	if(write(fd, v, strlen(v)) < 0) {
		snprintf(e, e_sz, "Could not write to %s: %s",
			v_filename, strerror(errno));
		goto e_write_v;
	}

	ret = 0;

e_write_v:
	close(fd);
e_creat_v:
	free(v);
e_generate_c_initial:
	free(generate_c_initial);
e_memouts:
	free(memouts);
e_read_out:
	free(read_out);
e_mem_is_1:
	free(mem_is_1);
e_mem_is_0:
	free(mem_is_0);
e_wraddr_reset:
	free(wraddr_reset);
e_memories:
	free(memories);
e_dynamic_declarations:
	free(dynamic_declarations);
e_dynamic_ports:
	free(dynamic_ports);
e_v:

	if(ret) return ret;
	ret = -1;

	char *c = NULL;
	if(catf(&c, BLOB(c4_source),
			inputs,					// %1$d
			period,					// %2$d
			0) < 0) OUT(e_c);

	int c_fd = creat(c_filename, 0666);
	if(c_fd < 0) {
		snprintf(e, e_sz, "Could not create %s: %s",
			c_filename, strerror(errno));
		goto e_open_c;
	}

	if(write(c_fd, c, strlen(c)) < 0) {
		snprintf(e, e_sz, "Could not write to %s: %s",
			c_filename, strerror(errno));
		goto e_write_c;
	}

	ret = 0;

	close(c_fd);
e_write_c:
	free(c);
e_open_c:
e_c:
	return ret;
}
