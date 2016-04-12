#include "writeverilog_memories.h"
#include <stdio.h>
#include <stdlib.h>
#include "catf.h"
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#define BLOB(name) _binary_##name##_0_start
extern char BLOB(i2_module)[];
extern char BLOB(c2_source)[];
extern char BLOB(i2_pattern)[];

/* Finds the smallest number of bits neccesary to represent n. */
static unsigned ulog2(unsigned n)
{
	unsigned i = 0;
	while((1 << i) - 1 < n) ++i;
	return i;
}

static unsigned mem_sz2(unsigned mem_sz, unsigned n_mem, unsigned period,
		unsigned i)
{
	return (n_mem * (mem_sz - 1) + i % n_mem) >= period ?
		mem_sz - 2 : mem_sz - 1;
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
		"end\n"
		"\n",
		width - 1,
		name,
		length - 1,
		length > 1 ? ulog2(length - 1) - 1 : 0) < 0) return -1;

	return 0;
}

struct info {
	unsigned inputs;
	unsigned period;
	unsigned n_mem;
	unsigned mem_sz;
	unsigned bits_per_cycle;
	unsigned bits_per_memory;
	unsigned bits_per_input;
	unsigned input_bits;
	unsigned bits_per_cycle_output;
	unsigned bits_per_output;
	unsigned output_bits;
	unsigned total_bits;
};

/*
 * Pattern contains:
 * Input:
 *  Once per input:
 *   Once per submemory (n_mem):
 *    Once per cycle in period:
 *     1 bit - to write to this memory this cycle?
 *     log2(inputs) bits - Which input to take value from?
 * Output:
 *  Once per input:
 *   Once per cycle in period:
 *    log2(n_mem) bits - Which memory to read from
 *    log2(mem_sz) bits - Address to read from
 */

static int cat_pattern_reg(char **s, char *name, unsigned number,
		struct info *info)
{
	if(catf(s,
		BLOB(i2_pattern),
		name,
		number,
		info->total_bits - 1,
		info->inputs,
		info->n_mem,
		info->period,
		ulog2(info->inputs - 1),
		ulog2(info->n_mem - 1),
		ulog2(info->mem_sz - 1),
		info->total_bits,
		info->bits_per_cycle,
		info->input_bits,
		info->bits_per_output,
		info->bits_per_cycle_output) < 0) return -1;
	return 0;
}

static int cat_read(char **s, struct info *info, char ch)
{
	unsigned i, j;
	for(i = 0; i < info->inputs; ++i)
	for(j = 0; j < info->n_mem; ++j) {
		if(catf(s,
			"        we_%1$d_%2$d_%3$c <= 1;\n"
			"        en_%1$d_%2$d_%3$c <= enable(%1$d, %2$d);\n"
			"        addr_%1$d_%2$d_%3$c <= "
			"write_counter_%1$d_%2$d;\n"
			"        m_in_tmp = m_in(%1$d, %2$d);\n"
			"        in_%1$d_%2$d_%3$c <= in[m_in_tmp];\n"
			"        if(enable(%1$d, %2$d)) begin\n"
			"          if(write_counter_%1$d_%2$d == %4$d) "
			"write_counter_%1$d_%2$d <= 0;\n"
			"          else write_counter_%1$d_%2$d <= "
			"write_counter_%1$d_%2$d + 1;\n"
			"        end\n",
			i, j, ch, mem_sz2(info->mem_sz, info->n_mem,
				info->period, j)) < 0) return -1;
	}
	return 0;
}

static int cat_write(char **s, struct info *info, char ch)
{
	unsigned i, j;
	for(i = 0; i < info->inputs; ++i)
	for(j = 0; j < info->n_mem; ++j) {
		if(catf(s,
			"        we_%1$d_%2$d_%3$c <= 0;\n"
			"        en_%1$d_%2$d_%3$c <= 1;\n"
			"        addr_%1$d_%2$d_%3$c <= address(%1$d);\n",
			i, j, ch) < 0) return -1;
	}
	return 0;
}

static int cat_case(char **s, struct info *info, char ch)
{
	unsigned i, j;
	for(i = 0; i < info->inputs; ++i) {
		if(catf(s,
			"        case(which_mem_%1$d_0)\n",
			i) < 0) return -1;
		for(j = 0; j < info->n_mem; ++j) {
			if(catf(s,
				"          %1$d: obuf_%2$d[0] <= "
				"out_%2$d_%1$d_%3$c;\n",
				j, i, ch) < 0) return -1;
		}
		if(catf(s,
			"        endcase\n",
			i) < 0) return -1;
	}
	return 0;
}

int writeverilog_memories(
		char *filename,
		char *filename1,
		unsigned inputs,
		unsigned period,
		unsigned type,
		char *e,
		unsigned e_sz)
{
	int err = -1;

	if(inputs >= period) {
		snprintf(e, e_sz, "This one currently doesn't work when"
			"inputs >= period");
		return -1;
	}

	int fd = creat(filename, 0666);
	if(fd < 0) {
		snprintf(e, e_sz, "Could not create %s: %s",
			filename, strerror(errno));
		goto ecreat;
	}

#define ERR(label) \
	{ \
		snprintf(e, e_sz, "%s: %d", __FILE__, __LINE__); \
		goto label; \
	}

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

	unsigned n_mem_bound = n_mem > 1 ? ulog2(n_mem - 1) - 1 : 0;
	unsigned inputs_bound = inputs > 1 ? ulog2(inputs - 1) - 1 : 0;
	unsigned mem_sz_bound = mem_sz > 1 ? ulog2(mem_sz - 1) - 1 : 0;

	struct info info;
	info.inputs = inputs;
	info.period = period;
	info.n_mem = n_mem;
	info.mem_sz = mem_sz;
	info.bits_per_cycle = 1 + ulog2(inputs - 1);
	info.bits_per_memory = period * info.bits_per_cycle;
	info.bits_per_input = n_mem * info.bits_per_memory;
	info.input_bits = inputs * info.bits_per_input;
	info.bits_per_cycle_output = ulog2(n_mem - 1) + ulog2(mem_sz - 1);
	info.bits_per_output = period * info.bits_per_cycle_output;
	info.output_bits = inputs * info.bits_per_output;
	info.total_bits = info.input_bits + info.output_bits;
	if(info.total_bits < 8) info.total_bits = 8;

	char *dynamic_declarations = NULL;

	unsigned i, j;
	for(i = 0; i < inputs; ++i) {
		if(catf(&dynamic_declarations, ",\n  in%d", i) < 0) ERR(edecl);
	}
	for(i = 0; i < inputs; ++i) {
		if(catf(&dynamic_declarations, ",\n  out%d", i) < 0) ERR(edecl);
	}

	char *dynamic_definitions = NULL;
	for(i = 0; i < inputs; ++i) {
		if(catf(&dynamic_definitions, "input [%d:0] in%d;\n",
			type - 1, i) < 0) ERR(edef);
	}
	for(i = 0; i < inputs; ++i) {
		if(catf(&dynamic_definitions, "output reg [%d:0] out%d;\n",
			type - 1, i) < 0) ERR(edef);
	}

	char *memories = NULL;
	for(i = 0; i < inputs * n_mem; ++i) {
		char name[20];
		snprintf(name, sizeof name, "%1$d_%2$d_a",
			i / n_mem, i % n_mem);
		if(cat_block_memory(&memories,
			type,
			mem_sz2(mem_sz, n_mem, period, i) + 1,
			name) < 0) ERR(emem);
		snprintf(name, sizeof name, "%1$d_%2$d_b",
			i / n_mem, i % n_mem);
		if(cat_block_memory(&memories,
			type,
			mem_sz2(mem_sz, n_mem, period, i) + 1,
			name) < 0) ERR(emem);
	}
	if(cat_pattern_reg(&memories, "xx", 0, &info) < 0) ERR(emem);
	if(cat_pattern_reg(&memories, "yy", 1, &info) < 0) ERR(emem);

	for(i = 0; i < inputs; ++i)
	for(j = 0; j < n_mem; ++j) {
		if(catf(&memories, "reg [%3$d:0] write_counter_%1$d_%2$d;\n",
			i, j, mem_sz > 1 ? ulog2(mem_sz - 1) - 1 : 0) < 0,
			0) ERR(emem);
	}
	for(i = 0; i < inputs; ++i) {
		if(catf(&memories, "reg [%2$d:0] which_mem_%1$d_1;\n"
			"reg [%2$d:0] which_mem_%1$d_0;\n",
			i, n_mem > 1 ? ulog2(n_mem - 1) - 1 : 0) < 0,
			0) ERR(emem);
	}

	char *assign_in = NULL;
	for(i = 0; i < inputs; ++i) {
		if(catf(&assign_in, "assign in[%1$d] = in%1$d;\n", i) < 0)
			ERR(eassig);
	}

	unsigned obuf_len = 7;
	char *obuf = NULL;
	for(i = 0; i < inputs; ++i) {
		if(catf(&obuf, "reg [%2$d:0] obuf_%1$d[0:%3$d];\n",
			i, type - 1, obuf_len - 1) < 0) ERR(eobuf);
	}

	char *work = NULL;
	if(catf(&work,
		"      /* Input side */\n"
		"      if(half == 0) begin\n") < 0) ERR(e_work);
	if(cat_read(&work, &info, 'a') < 0) ERR(e_work);
	if(cat_write(&work, &info, 'b') < 0) ERR(e_work);
	if(catf(&work,
		"      end\n"
		"      else begin\n") < 0) ERR(e_work);
	if(cat_write(&work, &info, 'a') < 0) ERR(e_work);
	if(cat_read(&work, &info, 'b') < 0) ERR(e_work);
	if(catf(&work,
		"      end\n") < 0) ERR(e_work);

	char *get_out = NULL;
	if(catf(&get_out,
		"      if(prev_half_0 == 0) begin\n",
		i) < 0)
		ERR(ego);
	if(cat_case(&get_out, &info, 'b') < 0) ERR(ego);
	if(catf(&get_out,
		"      end\n"
		"      else begin\n",
		i) < 0)
		ERR(ego);
	if(cat_case(&get_out, &info, 'a') < 0) ERR(ego);
	if(catf(&get_out,
		"      end\n",
		i) < 0)
		ERR(ego);
	for(i = 0; i < inputs; ++i) {
		if(catf(&get_out,
			"      for(i = 0; i < 6; i = i + 1) "
			"obuf_%1$d[i + 1] <= obuf_%1$d[i];\n",
			i) < 0)
			ERR(ego);
	}

	char *buf_out = NULL;
	for(i = 0; i < inputs; ++i) {
		if(catf(&buf_out,
			"      out%1$d <= obuf_%1$d[obuf_len - 1];\n",
			i) < 0)
			ERR(ebo);
	}

	char *memories_reset = NULL;
	for(i = 0; i < inputs; ++i)
	for(j = 0; j < n_mem; ++j) {
		if(catf(&memories_reset,
			"    en_%1$d_%2$d_a <= 0;\n"
			"    en_%1$d_%2$d_b <= 0;\n",
			i, j) < 0) ERR(error_memories_reset);
	}
	for(i = 0; i < inputs; ++i)
	for(j = 0; j < n_mem; ++j) {
		if(catf(&memories_reset,
			"    write_counter_%1$d_%2$d <= 0;\n",
			i, j) < 0) ERR(error_memories_reset);
	}

	char *memories_disable = NULL;
	for(i = 0; i < inputs; ++i)
	for(j = 0; j < n_mem; ++j) {
		if(catf(&memories_disable,
			"      en_%1$d_%2$d_a <= 0;\n"
			"      en_%1$d_%2$d_b <= 0;\n",
			i, j) < 0) ERR(error_memories_disable);
	}

	char *pipeline = NULL;
	for(i = 0; i < inputs; ++i) {
		if(catf(&pipeline,
			"    which_mem_%1$d_0 <= which_mem_%1$d_1;\n",
			i) < 0) ERR(e_pipeline);
	}
	for(i = 0; i < inputs; ++i) {
		if(catf(&pipeline,
			"    which_mem_%1$d_1 <= which_mem(%1$d);\n",
			i) < 0) ERR(e_pipeline);
	}

	char *v_all = NULL;
	if(catf(&v_all, BLOB(i2_module),
		dynamic_declarations,			// %1$s
		period > 1 ? ulog2(period - 1) - 1 : 0,	// %2$d
		dynamic_definitions,			// %3$s
		memories,				// %4$s
		ulog2(2 * period - 1) - 1,		// %5$d
		period,					// %6$d
		period - 1,				// %7$d
		work,					// %8$s
		n_mem - 1,				// %9$d
		info.n_mem,				// %10$d
		info.bits_per_memory,			// %11$d
		type - 1,				// %12$d
		inputs - 1,				// %13$d
		assign_in,				// %14$d
		ulog2(mem_sz - 1),			// %15$d
		mem_sz > 1 ? ulog2(mem_sz - 1) - 1 : 0,	// %16$d
		ulog2(inputs - 1),			// %17$d
		inputs > 1 ? ulog2(inputs - 1) - 1 : 0,	// %18$d
		get_out,				// %19$s
		buf_out,				// %20$s
		obuf,					// %21$s
		obuf_len - 1,				// %22$d
		ulog2(obuf_len) - 1,			// %23$d
		ulog2((info.total_bits + 7) / 8) - 1,	// %24$d
		(info.total_bits + 7) / 8 - 1,		// %25$d
		memories_reset,				// %26$s
		memories_disable,			// %27$s
		n_mem > 1 ? ulog2(n_mem - 1) - 1 : 0,	// %28$d
		info.input_bits,			// %29$d
		info.bits_per_output,			// %30$d
		ulog2(n_mem - 1),			// %31$d
		pipeline,				// %32$s
		info.total_bits,			// %33$d
		0) < 0) ERR(ev_all);

	if(write(fd, v_all, strlen(v_all)) < 0) {
		snprintf(e, e_sz, "Could not write %s: %s", filename,
			strerror(errno));
		goto ewrite;
	}

	int fd1 = creat(filename1, 0666);
	if(fd1 < 0) {
		snprintf(e, e_sz, "Could not create %s: %s",
			filename1, strerror(errno));
		goto ecrea1;
	}

	char *c_all = NULL;
	if(catf(&c_all, BLOB(c2_source),
		(info.total_bits + 7) / 8,		// %1$d
		info.inputs,				// %2$d
		info.n_mem,				// %3$d
		info.period,				// %4$d
		period - 1,				// %5$d
		n_mem - 1,				// %6$d
		inputs * period,			// %7$d
		info.bits_per_input,			// %8$d
		info.bits_per_memory,			// %9$d
		info.bits_per_cycle,			// %10$d
		ulog2(inputs - 1),			// %11$d
		info.input_bits,			// %12$d
		info.bits_per_output,			// %13$d
		info.bits_per_cycle_output,		// %14$d
		ulog2(n_mem - 1),			// %15$d
		ulog2(mem_sz - 1),			// %16$d
		info.total_bits,			// %17$d
		0) < 0) ERR(ec_all);

	if(write(fd1, c_all, strlen(c_all)) < 0) {
		snprintf(e, e_sz, "Could not write %s: %s", filename1,
			strerror(errno));
		goto ewrit1;
	}

	err = 0;

ewrit1:
ec_all:	free(c_all);
	close(fd1);
ecrea1:
ewrite:	free(v_all);
ev_all:
e_pipeline:
	free(pipeline);
error_memories_disable:
	free(memories_disable);
error_memories_reset:
	free(memories_reset);
ebo:	free(buf_out);
ego:	free(get_out);
e_work:	free(work);
eobuf:	free(obuf);
eassig:	free(assign_in);
emem:	free(memories);
edef:	free(dynamic_definitions);
edecl:	free(dynamic_declarations);
	close(fd);
ecreat:	return err;
}
