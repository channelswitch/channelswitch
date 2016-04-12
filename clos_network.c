#include "clos_network.h"
#include "catf.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#define BLOB(name) _binary_##name##_0_start
extern char BLOB(i3_module)[];
extern char BLOB(c3_source)[];

/* Finds the smallest number of bits neccesary to represent n. */
static unsigned ulog2(unsigned n)
{
	unsigned i = 0;
	while((1 << i) - 1 < n) ++i;
	return i;
}

static void get_n_switches(unsigned inputs, unsigned *n_switches, unsigned *depth)
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
		get_n_switches(a, &a1, &depth_a);
		get_n_switches(b, &b1, &depth_b);
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

static void switch_info(
		unsigned inputs,
		unsigned x,
		unsigned y)
{
	unsigned start, len, is_pipeline;
	get_span(inputs, x, y, &start, &len, &is_pipeline);
	if(is_pipeline || (y - start) / 2 * 2 + 1 >= len) {
		/* Is delay. */
		printf("%d,%d <= %d,%d\n", x, y, x - 1, get_input_to(inputs, x, y));
	}
	else {
		/* Is switch. */
		unsigned a, b;
		a = get_input_to(inputs, x, start + (y - start) / 2 * 2);
		b = get_input_to(inputs, x, start + (y - start) / 2 * 2 + 1);
		if(y == start + (y - start) / 2 * 2 + 1) {
			unsigned tmp = a;
			a = b;
			b = tmp;
		}
		printf("%d,%d <= %d,%d alt %d,%d\n", x, y, x - 1, a, x - 1, b);
	}
}

static int append_pattern_shifts(
		char **s,
		unsigned inputs,
		unsigned period,
		unsigned x,
		char *pattern_bank,
		unsigned *pattern_pos)
{
	unsigned y;
	for(y = 0; y < inputs; ++y) {
		unsigned start, len, is_pipeline;
		get_span(inputs, x, y, &start, &len, &is_pipeline);

		if(is_pipeline || (y - start) / 2 * 2 + 1 >= len) {
			/* No pattern */
			continue;
		}

		/* Only once per 2x2 switch */
		unsigned start0 = start + (y - start) / 2 * 2;
		if(y != start0) continue;

		if(catf(s, "          for(i = 0; i < %1$d; i = i + 1) "
			"%3$s[%2$d + i] <= %3$s[%2$d + (i + 1) %% %1$d];\n",
			period, *pattern_pos, pattern_bank) < 0) goto err0;
		*pattern_pos += period;
	}
	return 0;

err0:
	return -1;
}

static char *create_as_benes(
		unsigned inputs,
		unsigned period,
		char *memory,
		char *input,
		unsigned fill_delay,
		unsigned *pattern_pos)
{
	unsigned depth = get_depth(inputs);
	unsigned x, y;

	char *s = NULL;

	unsigned pattern_pos_1 = *pattern_pos;

	for(x = 0; x < depth; ++x)  {
		if(catf(&s,
			"      if(active[%1$d]) begin\n",
			x + fill_delay) < 0) goto err0;
		for(y = 0; y < inputs; ++y) {
			unsigned start, len, is_pipeline;
			get_span(inputs, x, y, &start, &len, &is_pipeline);

			if(is_pipeline || (y - start) / 2 * 2 + 1 >= len) {
				/* Only delay if is pipeline or last stage when
				 * len is odd. */
				if(catf(&s,
					"        %1$s[%2$d] <= %3$s[%4$d];\n",
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
					"        %1$s[%2$d] <= `to_swap(%6$d, "
					"%7$d) ? %3$s[%5$d] : %3$s[%4$d];\n",
					memory, x * inputs + y,
					x == 0 ? input : memory,
					(x == 0 ? 0 : (x - 1) * inputs) +
						a,
					(x == 0 ? 0 : (x - 1) * inputs) +
						b,
					*pattern_pos,
					x + fill_delay,
					0) < 0) goto err0;
				if(y == start0 + 1) {
					*pattern_pos += period;
				}
			}

		}
		if(catf(&s, "      end\n") < 0) goto err0;
	}

	return s;
err0:
	free(s);
	return NULL;
}

static int cat_block_memory(char **s, unsigned width, unsigned length,
		char *name)
{
	if(catf(s,"reg [%1$d:0] memory_%2$s[0:%3$d];\n"
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
		"end\n",
		width - 1, name, length - 1, ulog2(length - 1) - 1) < 0) return -1;

	return 0;
}

static int cat_pattern_task(char **s, char *name, unsigned number,
		unsigned inputs, unsigned period)
{
	unsigned depth, n_switches;
	get_n_switches(inputs, &n_switches, &depth);
	unsigned mem_addr_bits = period > 1 ? ulog2(period - 1) : 1;
	if(catf(s,
		"always @(posedge clk or posedge reset) \n"
		"begin : %1$s_task\n"
		"  integer i;\n"
		"  integer j;\n"
		"  integer k;\n"
		"  if(reset) begin\n"
		"    for(i = 0; i < %2$d; i = i + 1) %1$s[i] <= 0;\n"
		"    for(i = 0; i < %3$d; i = i + 1) begin\n"
		"      for(j = 0; j < %4$d; j = j + 1) begin\n"
		"        for(k = 0; k < %5$d; k = k + 1) begin\n"
		"          %1$s[%2$d + i * %4$d * %5$d + j * %5$d + k] <= "
		"((%4$d - 1 - j) >> k) & 1;\n"
		"        end\n"
		"      end\n"
		"    end\n"
		"    for(i = 0; i < %2$d; i = i + 1) %1$s[%6$d + i] <= 0;\n"
		"  end\n"
		"  else begin\n"
		"    if(pattern_valid && !updating_pattern && "
		"pattern_at[0] != %8$d) begin\n"
		"      for(i = 0; i < 8; i = i + 1) %1$s[i] <= "
		"pattern_data[i];\n"
		"      for(i = 8; i < %7$d; i = i + 1) %1$s[i] <= "
		"%1$s[i - 8];\n"
		"    end\n"
		"    else begin\n"
		"      if(in_valid) begin\n",
		name, n_switches * period, inputs, period,
		mem_addr_bits,
		n_switches * period + inputs * period * mem_addr_bits,
		2 * n_switches * period + inputs * period * mem_addr_bits,
		number) < 0) return -1;
	unsigned pos = 0;
	unsigned i;
	for(i = 0; i < depth; ++i) {
		if(catf(s, "        if(active[%1$d] && "
			"pattern_at[%1$d] == %2$d) begin\n",
			i, number) < 0) return -1;
		if(append_pattern_shifts(s, inputs, period, i, name,
			&pos) < 0) return -1;
		if(catf(s, "        end\n") < 0) return -1;
	}
	if(catf(s,
		"        /* The middle pattern is read one clock cycle in "
		"advance because of\n"
		"         * memory delays. */\n"
		"        if(memory_counter == %3$d ?\n"
		"            active[%2$d] && pattern_at[%2$d] == %4$d :\n"
		"            active[%1$d] && pattern_at[%1$d] == %4$d) begin\n",
		depth + 1, depth, period - 1, number) < 0) return -1;
	for(i = 0; i < inputs; ++i) {
		if(catf(s,
			"          for(i = 0; i < %2$d; i = i + 1) begin\n"
			"            for(j = 0; j < %3$d; j = j + 1) begin\n"
			"              %1$s[%4$d + i * %3$d + j] <= "
			"%1$s[%4$d + ((i + %5$d) %% %2$d) * %3$d + j];\n"
			"            end\n"
			"          end\n",
			name, period, mem_addr_bits, pos,
			period - 1) < 0) return -1;
		pos += period * mem_addr_bits;
	}
	if(catf(s,
		"        end\n"
		"      end\n"
		"      if(run_egress) begin\n") < 0) return -1;
	for(i = 0; i < depth; ++i) {
		if(catf(s, "        if(active[%1$d] && "
			"pattern_at[%1$d] == %2$d) begin\n",
			depth + 2 + i, number) < 0) return -1;
		if(append_pattern_shifts(s, inputs, period, i, name,
			&pos) < 0) return -1;
		if(catf(s, "        end\n") < 0) return -1;
	}
	if(catf(s,
		"      end\n"
		"    end\n"
		"  end\n"
		"end\n"
		"\n") < 0) return -1;
	return 0;
}

int clos_network(
		char *verilog_filename,
		char *c_filename,
		unsigned inputs,
		unsigned period,
		unsigned type,
		char *e,
		unsigned e_sz)
{
	int err = -1;

#define OUT(label) \
	{ \
		snprintf(e, e_sz, "%s: %d", __FILE__, __LINE__); \
		goto label; \
	}

	unsigned i;

	char *variable_ports = NULL;
	for(i = 0; i < inputs; ++i) {
		if(catf(&variable_ports, ",\n  in%1$d", i) < 0)
			OUT(error_variable_ports);
	}
	for(i = 0; i < inputs; ++i) {
		if(catf(&variable_ports, ",\n  out%1$d", i) < 0)
			OUT(error_variable_ports);
	}

	char *input_declarations = NULL;
	for(i = 0; i < inputs; ++i) {
		if(catf(&input_declarations, "input [%2$d:0] in%1$d;\n",
			i, type - 1) < 0) OUT(error_input_declarations);
	}
	for(i = 0; i < inputs; ++i) {
		if(catf(&input_declarations, "output [%2$d:0] out%1$d;\n",
			i, type - 1) < 0) OUT(error_input_declarations);
	}

	char *assign_inputs = NULL;
	for(i = 0; i < inputs; ++i) {
		if(catf(&assign_inputs, "assign inputs[%1$d] = in%1$d;\n",
			i) < 0) OUT(error_assign_inputs);
	}
	for(i = 0; i < inputs; ++i) {
		if(catf(&assign_inputs, "assign out%1$d = outputs[%1$d];\n",
			i) < 0) OUT(error_assign_inputs);
	}

	unsigned n_switches;
	unsigned switch_depth;
	get_n_switches(inputs, &n_switches, &switch_depth);

	unsigned mem_addr_bits = period > 1 ? ulog2(period - 1) : 1;
	unsigned total_pattern_bits =
		n_switches * period +
		inputs * period * mem_addr_bits +
		n_switches * period;
	unsigned bytes = (total_pattern_bits + 7) / 8;

	unsigned pattern_pos = 0;
	char *ingress = create_as_benes(
		inputs, period, "ingress", "inputs", 0, &pattern_pos);
	if(!ingress) OUT(error_ingress);

	pattern_pos += inputs * period * mem_addr_bits;
	char *egress = create_as_benes(
		inputs, period, "egress", "inputs3", switch_depth + 2,
		&pattern_pos);
	if(!egress) OUT(error_egress);

	char *middle_memories = NULL;
	for(i = 0; i < inputs; ++i) {
		char name[20];
		snprintf(name, sizeof name, "%d_a", i);
		if(cat_block_memory(&middle_memories,
			type, period, name) < 0) OUT(error_middle_memories);
		snprintf(name, sizeof name, "%d_b", i);
		if(cat_block_memory(&middle_memories,
			type, period, name) < 0) OUT(error_middle_memories);
	}

	char *middle_read = NULL;
	if(catf(&middle_read, "        if(middle_bank) begin\n") < 0)
		OUT(error_middle_read);
	for(i = 0; i < inputs; ++i) {
		if(catf(&middle_read,
			"          en_%1$d_b <= 1;\n"
			"          we_%1$d_b <= 1;\n"
			"          addr_%1$d_b <= memory_counter;\n"
			"          in_%1$d_b <= ingress[%2$d];\n",
			i, (switch_depth - 1) * inputs + i,
			0) < 0) OUT(error_middle_read);
	}
	if(catf(&middle_read, "        end\n"
		"        else begin\n") < 0) OUT(error_middle_read);
	for(i = 0; i < inputs; ++i) {
		if(catf(&middle_read,
			"          en_%1$d_a <= 1;\n"
			"          we_%1$d_a <= 1;\n"
			"          addr_%1$d_a <= memory_counter;\n"
			"          in_%1$d_a <= ingress[%2$d];\n",
			i, (switch_depth - 1) * inputs + i,
			0) < 0) OUT(error_middle_read);
	}
	if(catf(&middle_read, "        end\n") < 0) OUT(error_middle_read);

	char *middle_write = NULL;
	if(catf(&middle_write, "        if(middle_bank) begin\n") < 0)
		OUT(error_middle_write);
	for(i = 0; i < inputs; ++i) {
		if(catf(&middle_write,
			"          en_%1$d_a <= 1;\n"
			"          we_%1$d_a <= 0;\n"
			"          addr_%1$d_a <= raddr_%1$d;\n",
			i, (switch_depth - 1) * inputs + i,
			0) < 0) OUT(error_middle_write);
	}
	if(catf(&middle_write, "        end\n"
		"        else begin\n") < 0) OUT(error_middle_write);
	for(i = 0; i < inputs; ++i) {
		if(catf(&middle_write,
			"          en_%1$d_b <= 1;\n"
			"          we_%1$d_b <= 0;\n"
			"          addr_%1$d_b <= raddr_%1$d;\n",
			i, (switch_depth - 1) * inputs + i,
			0) < 0) OUT(error_middle_write);
	}
	if(catf(&middle_write, "        end\n") < 0) OUT(error_middle_write);

	char *raddr_decls = NULL;
	for(i = 0; i < inputs; ++i) {
		if(catf(&raddr_decls,
			"reg [%2$d:0] raddr_%1$d;\n",
			i, mem_addr_bits - 1,
			0) < 0) OUT(error_raddr_decls);
	}

	char *raddr_updates = NULL;
	for(i = 0; i < inputs; ++i) {
		if(catf(&raddr_updates,
			"        raddr_%1$d <= read_addr(%1$d, rpattern_1);\n",
			i, 0) < 0) OUT(error_raddr_updates);
	}

	char *clear_memory_enable = NULL;
	for(i = 0; i < inputs; ++i) {
		if(catf(&clear_memory_enable,
			"      en_%1$d_a <= 0;\n"
			"      en_%1$d_b <= 0;\n",
			i) < 0) OUT(error_clear_memory_enable);
	}

	char *memories_reset = NULL;
	for(i = 0; i < inputs; ++i) {
		if(catf(&memories_reset,
			"    en_%1$d_a <= 0;\n"
			"    en_%1$d_b <= 0;\n",
			i) < 0) OUT(error_memories_reset);
	}

	char *inputs3 = NULL;
	for(i = 0; i < inputs; ++i) {
		if(catf(&inputs3,
			"assign inputs3[%1$d] = read_memory ? out_%1$d_a : "
			"out_%1$d_b;\n",
			i) < 0) OUT(error_inputs3);
	}

	char *pattern_tasks = NULL;
	if(cat_pattern_task(&pattern_tasks, "xx", 0, inputs, period) < 0)
		OUT(error_pattern_tasks);
	if(cat_pattern_task(&pattern_tasks, "yy", 1, inputs, period) < 0)
		OUT(error_pattern_tasks);

	char *v = NULL;
	if(catf(&v, BLOB(i3_module),
		variable_ports,					// %1$s
		mem_addr_bits - 1,				// %2$s
		input_declarations,				// %3$s
		assign_inputs,					// %4$s
		type - 1,					// %5$d
		0,						// %6$d
		switch_depth,					// %7$d
		inputs,						// %8$d
		n_switches,					// %9$d
		period, 					// %10$d
		ulog2(switch_depth),				// %11$d
		switch_depth,					// %12$d
		ulog2((n_switches * period + 7) / 8 - 1) - 1,	// %13$d
		(n_switches * period + 7) / 8 - 1,		// %14$d
		ulog2(2 * period - 1) - 1,			// %15$d
		ulog2(switch_depth * 2 + period + 1) - 1,	// %16$d
		switch_depth * 2 + period + 1,			// %17$d
		mem_addr_bits,					// %18$d
		bytes - 1,					// %19$d
		bytes > 1 ? ulog2(bytes - 1) - 1 : 0,		// %20$d
		total_pattern_bits,				// %21$d
		ingress,					// %22$s
		0,						// %23$d
		egress,						// %24$s
		middle_memories,				// %25$s
		middle_read,					// %26$s
		middle_write,					// %27$s
		n_switches * period,				// %28$d
		period * mem_addr_bits,				// %29$d
		raddr_decls,					// %30$s
		raddr_updates,					// %31$s
		2 * switch_depth + 2,				// %32$d
		clear_memory_enable,				// %33$s
		memories_reset,					// %34$s
		NULL,						// %35$s
		inputs3,					// %36$s
		pattern_tasks,					// %37$s
		0) < 0) OUT(error_v);

	int v_fd = creat(verilog_filename, 0666);
	if(v_fd < 0) {
		snprintf(e, e_sz, "Could not create %s: %s", verilog_filename,
			strerror(errno));
		goto error_opening_v;
	}

	if(write(v_fd, v, strlen(v)) < 0) {
		snprintf(e, e_sz, "Could not write to %s: %s",
			verilog_filename, strerror(errno));
		goto error_writing_v;
	}

	err = 0;

error_writing_v:
	close(v_fd);
error_opening_v:
	free(v);
error_v:
error_pattern_tasks:
	free(pattern_tasks);
error_inputs3:
	free(inputs3);
error_memories_reset:
	free(memories_reset);
error_clear_memory_enable:
	free(clear_memory_enable);
error_raddr_updates:
	free(raddr_updates);
error_raddr_decls:
	free(raddr_decls);
error_middle_write:
	free(middle_write);
error_middle_read:
	free(middle_read);
error_middle_memories:
	free(middle_memories);
	free(egress);
error_egress:
	free(ingress);
error_ingress:
	free(assign_inputs);
error_assign_inputs:
	free(input_declarations);
error_input_declarations:
	free(variable_ports);
error_variable_ports:
	if(err) goto error_in_v;

	err = -1;

	char *c = NULL;
	if(catf(&c, BLOB(c3_source),
			inputs,					// %1$d
			period,					// %2$d
			0) < 0) OUT(error_c);

	int c_fd = creat(c_filename, 0666);
	if(c_fd < 0) {
		snprintf(e, e_sz, "Could not create %s: %s", c_filename,
			strerror(errno));
		goto error_opening_c;
	}

	if(write(c_fd, c, strlen(c)) < 0) {
		snprintf(e, e_sz, "Could not write to %s: %s",
			c_filename, strerror(errno));
		goto error_writing_c;
	}

	err = 0;

error_writing_c:
	close(c_fd);
error_opening_c:
	free(c);
error_c:
error_in_v:
	return err;
}
