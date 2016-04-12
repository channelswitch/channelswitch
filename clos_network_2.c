#include "clos_network_2.h"
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include "catf.h"

#define BLOB(name) _binary_##name##_0_start
extern char BLOB(i5_module)[];
extern char BLOB(c5_source)[];

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

static void switch_info(unsigned inputs, unsigned *n_switches, unsigned *depth)
{
	if(inputs == 0) {
		*depth = 0;
		*n_switches = 0;
	}
	else if(inputs == 1) {
		*depth = 0;
		*n_switches = 0;
	}
	else if(inputs == 2) {
		*depth = 1;
		*n_switches = 1;
	}
	else {
		unsigned a, b;
		a = inputs / 2;
		b = a + inputs % 2;
		unsigned a1, b1;
		unsigned depth_a, depth_b;
		switch_info(a, &a1, &depth_a);
		switch_info(b, &b1, &depth_b);
		*depth = 2 + (depth_a > depth_b ? depth_a : depth_b);
		*n_switches = 2 * (inputs / 2) + a1 + b1;
	}
}

static unsigned get_depth(unsigned inputs)
{
	if(inputs == 0) return 0;
	else if(inputs == 1) return 0;
	else return 2 * ulog2(inputs - 1) - 1;
}

static void get_span(
		unsigned inputs,
		unsigned x,
		unsigned y,
		unsigned *start_out,
		unsigned *len_out,
		unsigned *is_pipeline_out)
{
	unsigned i;
	unsigned start, len, stride;
	start = 0;
	len = inputs;
	if(x > get_depth(inputs) / 2) x = get_depth(inputs) - x - 1;
	for(i = 0; i < x; ++i) {
		if(get_depth(inputs) / 2 - x < get_depth(len) / 2) {
			if(y - start < len / 2) {
				len /= 2;
			}
			else {
				start += len / 2;
				len = len / 2 + len % 2;
			}
		}
	}
	*start_out = start;
	*len_out = len;
	*is_pipeline_out = get_depth(inputs) / 2 - x > get_depth(len) / 2;
}

static unsigned get_connected_input(
		unsigned y,
		unsigned prev_start,
		unsigned prev_len,
		unsigned prev_was_pipeline)
{
	if(prev_was_pipeline) return y;

	if(y - prev_start < prev_len / 2) {
		return prev_start + (y - prev_start) * 2;
	}
	else {
		if(prev_len % 2 && prev_start + prev_len - 1 == y) return y;
		return prev_start + (y - (prev_start + prev_len / 2)) * 2 + 1;
	}
}

static unsigned get_input_to(
		unsigned inputs,
		unsigned x,
		unsigned y)
{
	unsigned start, len, is_pipeline;
	get_span(inputs, x, y, &start, &len, &is_pipeline);

	if(x <= get_depth(inputs) / 2) {
		/* Left side. Span to the left always >= current span. */
		unsigned prev_start, prev_len, prev_was_pipeline;
		if(x == 0) {
			prev_start = 0;
			prev_len = inputs;
			prev_was_pipeline = 1;
		}
		else {
			get_span(inputs, x - 1, y, &prev_start, &prev_len,
				&prev_was_pipeline);
		}

		if(prev_was_pipeline) {
			return y;
		}
		else if(start == prev_start) {
			/* We are in top half of left span. */
			return prev_start + (y - start) * 2;
		}
		else {
			/* We are in bottom half of left span. */
			if((y - prev_start) / 2 * 2 + 1 >= prev_len) return y;
			return prev_start + (y - start) * 2 + 1;
		}
	}
	else {
		/* Right side. Divide current span in two as per usual rules. */
		if(is_pipeline) {
			return y;
		}
		else if((y - start) / 2 * 2 + 1 >= len) {
			return y;
		}
		else if((y - start) % 2 == 0) {
			/* Even */
			return start + (y - start) / 2;
		}
		else {
			/* Odd */
			return start + len / 2 + (y - start - 1) / 2;
		}
	}
}

static char *create_as_benes(
		unsigned inputs,
		unsigned period,
		char *memory,
		char *input)
{
	unsigned depth = get_depth(inputs);
	unsigned x, y;

	char *s = NULL;

	if(inputs == 1) {
		if(catf(&s, "      %1$s[0] <= %2$s[0];\n",
			memory, input) < 0) goto err0;
		return s;
	}

	unsigned pattern_pos = 0;
	for(x = 0; x < depth; ++x)  {
		for(y = 0; y < inputs; ++y) {
			unsigned start, len, is_pipeline;
			get_span(inputs, x, y, &start, &len, &is_pipeline);

			if(is_pipeline || (y - start) / 2 * 2 + 1 >= len) {
				/* Only delay if is pipeline or last stage when
				 * len is odd. */
				if(catf(&s,
					"      %1$s[%2$d] <= %3$s[%4$d];\n",
					memory, x * inputs + y,
					x == 0 ? input : memory,
					(x == 0 ? 0 : (x - 1) * inputs) +
						get_input_to(inputs, x, y),
					0) < 0) goto err0;
			}
			else {
				unsigned start0 = start + (y - start) / 2 * 2;
				unsigned a, b;
				a = get_input_to(inputs, x, start0);
				b = get_input_to(inputs, x, start0 + 1);
				if(y == start0 + 1) {
					unsigned tmp = a;
					a = b;
					b = tmp;
				}
				if(catf(&s,
					"      %1$s[%2$d] <= `to_swap(%6$d, "
					"%7$d) ? %3$s[%5$d] : %3$s[%4$d];\n",
					memory,
					x * inputs + y,
					x == 0 ? input : memory,
					(x == 0 ? 0 : (x - 1) * inputs) +
						a,
					(x == 0 ? 0 : (x - 1) * inputs) +
						b,
					pattern_pos,
					x,
					0) < 0) goto err0;
				if(y == start0 + 1) {
					pattern_pos += 1;
				}
			}

		}
	}

	return s;
err0:
	free(s);
	return NULL;
}

static int cat_pattern_memory(char **s, unsigned width, unsigned length,
		char *name)
{
	return catf(s,"reg [%1$d:0] memory_%2$s[0:%3$d];\n"
		"reg en_%2$s;\n"
		"reg we_%2$s;\n"
		"reg [%4$d:0] addr_%2$s;\n"
		"reg [%1$d:0] in_%2$s;\n"
		"reg [%1$d:0] out_%2$s;\n"
		"always @(posedge clk) begin\n"
		"\tif(en_%2$s) begin\n"
		"\t\tif(we_%2$s) memory_%2$s[addr_%2$s] <= in_%2$s;\n"
		"\t\telse out_%2$s <= memory_%2$s[addr_%2$s];\n"
		"\tend\n"
		"end\n"
		"\n",
		width - 1, name, length - 1, ulog2(length - 1) - 1);
}

static int cat_data_memory(char **s, unsigned width, unsigned length,
		char *name, char *class)
{
	return catf(s,"reg [%1$d:0] memory_%2$s[0:%3$d];\n"
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
		width - 1, name, length - 1, ulog2(length - 1) - 1, class);
}

static char *create_half_a_memory_stage(unsigned inputs, char *write_to,
		char *read_from)
{
	char *str = NULL;
	unsigned i;
	for(i = 0; i < inputs; ++i) {
		if(catf(&str,
			"        in_%1$d_%2$s <= `in_data(%1$d);\n"
			"        addr_%1$d_%2$s <= `write_addr;\n",
			i,
			write_to) < 0) goto error;
	}
	for(i = 0; i < inputs; ++i) {
		if(catf(&str,
			"        addr_%1$d_%2$s <= `read_addr(%1$d);\n",
			i,
			read_from) < 0) goto error;
	}

	return str;

error:
	free(str);
	return NULL;
}

int clos_network_2(
	char *verilog_filename,
	char *c_filename,
	unsigned inputs,
	unsigned period,
	unsigned type,
	char *e,
	unsigned e_sz)
{
	int err = -1;

	if(period < 4) {
		snprintf(e, e_sz, "Period cannot be less than 4");
		return -1;
	}

	unsigned i;

	unsigned switch_depth, n_switches;
	switch_info(inputs, &n_switches, &switch_depth);
	if(!switch_depth) switch_depth = 1;
	unsigned t_bits = period > 1 ? ulog2(period - 1) - 1 : 0;
	unsigned initial_pattern_t = period - switch_depth % period;
	unsigned total_delay = switch_depth + period + switch_depth;
	unsigned fill_bits = ulog2(total_delay) - 1;
	unsigned period_bits = ulog2(period - 1) - 1;
	unsigned adr_bits = ulog2(period - 1);
	unsigned control_word_bits =
		n_switches +
		inputs * adr_bits +
		n_switches;
	unsigned bytes_in_word = (control_word_bits + 7) / 8;

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
			"output wire [%2$d:0] out%1$d;\n",
			i, type - 1) < 0)
			OUT(e_variable_declarations);
	}

	char *memories = NULL;
	if(cat_pattern_memory(&memories, control_word_bits, period, "xx")
		< 0) OUT(e_memories);
	if(cat_pattern_memory(&memories, control_word_bits, period, "yy")
		< 0) OUT(e_memories);
	for(i = 0; i < inputs; ++i) {
		char buf[10];
		snprintf(buf, sizeof buf, "%d_a", i);
		if(cat_data_memory(&memories, type, period, buf, "a") < 0)
			OUT(e_memories);
		snprintf(buf, sizeof buf, "%d_b", i);
		if(cat_data_memory(&memories, type, period, buf, "b") < 0)
			OUT(e_memories);
	}

	char *assign_outputs = NULL;
	for(i = 0; i < inputs; ++i) {
		if(catf(&assign_outputs, "assign out%1$d = outputs[%1$d];\n",
			i) < 0) OUT(e_assign_outputs);
	}
	for(i = 0; i < inputs; ++i) {
		if(catf(&assign_outputs, "assign inputs[%1$d] = in%1$d;\n",
			i) < 0) OUT(e_assign_outputs);
	}

	char *ingress = create_as_benes(
		inputs, period, "ingress", "inputs_2");
	if(!ingress) OUT(e_ingress);

	char *middle_a = create_half_a_memory_stage(inputs, "a", "b");
	if(!middle_a) OUT(e_middle_a);

	char *middle_b = create_half_a_memory_stage(inputs, "b", "a");
	if(!middle_b) OUT(e_middle_b);

	char *egress = create_as_benes(inputs, period, "egress", "mem_out");
	if(!egress) OUT(e_egress);

	char *assign_mem_out = NULL;
	for(i = 0; i < inputs; ++i) {
		if(catf(&assign_mem_out,
			"assign mem_out[%1$d] = "
			"mem_state_2 ? out_%1$d_a : out_%1$d_b;\n",
			i) < 0) OUT(e_assign_mem_out);
	}

	char *unrolled_for_loop = NULL;
	for(i = 0; i < inputs; ++i) {
		unsigned bit_offset = n_switches + i * ulog2(period - 1);
		if(catf(&unrolled_for_loop,
			"    c_initial[%1$d:%2$d] <= c_initial_counter;\n",
			bit_offset + ulog2(period - 1) - 1,
			bit_offset) < 0) OUT(e_unrolled_for_loop);
	}

	char *v = NULL;
	if(catf(&v, BLOB(i5_module),
		variable_ports,					// %1$s
		switch_depth - 1,				// %2$d
		switch_depth,					// %3$d
		t_bits,						// %4$d
		initial_pattern_t,				// %5$d
		period - 1,					// %6$d
		total_delay,					// %7$d
		fill_bits,					// %8$d
		inputs,						// %9$d
		type - 1,					// %10$d
		15 * inputs - 1,				// %11$d
		period_bits,					// %12$d
		period - 1,					// %13$d
		control_word_bits - 1,				// %14$d
		bytes_in_word - 1,				// %15$d
		bytes_in_word > 1 ?
			ulog2(bytes_in_word - 1) - 1 :
			0,					// %16$d
		memories,					// %17$s
		inputs * switch_depth - 1,			// %18$d
		variable_declarations,				// %19$s
		inputs - 1,					// %20$d
		assign_outputs,					// %21$s
		(switch_depth - 1) * inputs,			// %22$d
		ingress,					// %23$s
		middle_a,					// %24$s
		middle_b,					// %25$s
		n_switches,					// %26$d
		ulog2(period - 1),				// %27$d
		egress,						// %28$s
		assign_mem_out,					// %29$s
		n_switches + ulog2(period - 1) * inputs,	// %30$d
		unrolled_for_loop,				// %31$s
		bytes_in_word * 8 - 1,				// %32$d
		bytes_in_word * 8 - 8,				// %33$d
		0) < 0) OUT(e_v)

	int v_fd = creat(verilog_filename, 0666);
	if(v_fd < 0) {
		snprintf(e, e_sz, "Could not create %s: %s", verilog_filename,
			strerror(errno));
		goto e_open_v;
	}

	if(write(v_fd, v, strlen(v)) < 0) {
		snprintf(e, e_sz, "Could not write to %s: %s",
			verilog_filename, strerror(errno));
		goto e_write_v;
	}

	err = 0;

e_write_v:
	close(v_fd);
e_open_v:
	free(v);
e_v:
e_unrolled_for_loop:
	free(unrolled_for_loop);
e_assign_mem_out:
	free(assign_mem_out);
	free(egress);
e_egress:
	free(middle_b);
e_middle_b:
	free(middle_a);
e_middle_a:
	free(ingress);
e_ingress:
e_assign_outputs:
	free(assign_outputs);
e_memories:
	free(memories);
e_variable_declarations:
	free(variable_declarations);
e_variable_ports:
	free(variable_ports);

	if(err) return err;
	err = -1;

	char *c = NULL;
	if(catf(&c, BLOB(c5_source),
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

	err = 0;

	close(c_fd);
e_write_c:
	free(c);
e_open_c:
e_c:
	return err;
}
