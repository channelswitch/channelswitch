#include "writeverilog_bitonic.h"
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

static unsigned ppot(unsigned n)
{
	unsigned i = 0;
	while(1) {
		if((1 << i) > n) return 1 << (i - 1);
		++i;
	}
}

#define BLOB(name) _binary_##name##_0_start
extern char BLOB(i1_module)[];
extern char BLOB(i1_swap)[];
extern char BLOB(i1_delay)[];
extern char BLOB(i1_swapper)[];
extern char BLOB(i1_delayer)[];

static char *getdir(unsigned green_bit, unsigned l, unsigned m, unsigned n)
{
	if((1 << green_bit) < m) {
		if(l & (1 << green_bit)) return "!(%3$s)";
		else return "%3$s";
	}
	else if((1 << green_bit) >= n) {
		return "%3$s";
	}
	else {
		return "t_%1$d[%2$d] ^ %3$s";
	}
}

int writeverilog_bitonic(
	char *filename,
	unsigned inputs,
	unsigned period,
	unsigned type,
	char *e,
	unsigned e_sz)
{
	int err = -1;

	int file = creat(filename, 0666);
	if(file < 0) {
		snprintf(e, e_sz, "Could not create %s: %s", filename,
			strerror(errno));
		goto err0;
	}

	unsigned n = npot(inputs * period);
	unsigned k = ppot(period);
	unsigned m = n / k;
	unsigned delay = 0;

	if(inputs != m || period != k) {
		snprintf(e, e_sz, "Input and period must be powers of two");
		goto err1;
	}

	unsigned adr_bits = ulog2(n);
	unsigned val_bits = type;
	unsigned time_bits = ulog2(k);
	unsigned i;

	int status;

	/* Default error. */
	snprintf(e, e_sz, "Malloc returned NULL");

	char *module_in_args = NULL;
	for(i = 0; i < inputs; ++i) {
		status = catf(&module_in_args,
			",\n\tin%d", i);
		if(status < 0) goto err3;
	}

	char *module_out_args = NULL;
	for(i = 0; i < inputs; ++i) {
		status = catf(&module_out_args,
			",\n\tout%d", i);
		if(status < 0) goto err4;
	}

	char *input_declarations = NULL;
	for(i = 0; i < inputs; ++i) {
		status = catf(&input_declarations,
			"input [%2$d:0] in%1$d;\n", i, val_bits - 1);
		if(status < 0) goto err5;
	}

	char *output_declarations = NULL;
	for(i = 0; i < inputs; ++i) {
		status = catf(&output_declarations,
			"output reg [%2$d:0] out%1$d;\n", i, val_bits - 1);
		if(status < 0) goto err6;
	}

	char *pattern_declarations = NULL;
	for(i = 0; i < m; ++i) {
		status = catf(&pattern_declarations,
			"reg [%2$d:0] patterna%1$d[0:%3$d];\n", i, adr_bits - 1,
			k - 1);
		if(status < 0) goto err7;
	}
	for(i = 0; i < m; ++i) {
		status = catf(&pattern_declarations,
			"reg [%2$d:0] patternb%1$d[0:%3$d];\n", i, adr_bits - 1,
			k - 1);
		if(status < 0) goto err7;
	}

	static char address_template[] = "reg [%3$d:0] address%1$d_%2$d;\n";
	static char value_template[] = "reg [%3$d:0] value%1$d_%2$d;\n";

	char *pipeline0 = NULL;
	for(i = 0; i < m; ++i) {
		status = catf(&pipeline0,
			address_template, 0, i, adr_bits - 1);
		if(status < 0) goto err8;
	}
	for(i = 0; i < m; ++i) {
		status = catf(&pipeline0,
			value_template, 0, i, val_bits - 1);
		if(status < 0) goto err8;
	}

	char *process = NULL;
	for(i = 0; i < ulog2(n) * (ulog2(n) + 1) / 2; ++i) {
		int process_err = -1;
		unsigned j;
		for(j = 0; j * (j + 1) / 2 <= i; ++j);
		unsigned next_triangular_number = j;
		unsigned degree = (j * (j + 1) / 2 - 1) - i;

		char *pipeline = NULL;
		for(j = 0; j < m; ++j) {
			status = catf(&pipeline,
				address_template, i + 1, j, adr_bits - 1);
			if(status < 0) goto process_err1;
		}
		for(j = 0; j < m; ++j) {
			status = catf(&pipeline,
				value_template, i + 1, j, val_bits - 1);
			if(status < 0) goto process_err1;
		}

		char *memory = NULL;
		static char memory_template[] =
			"reg [%3$d:0] memory%1$d_%2$d[0:%4$d];\n";
		unsigned skip = 1 << degree;
		unsigned delaytime = skip / m;
		for(j = 0; j < m; ++j) {
			status = catf(&memory,
				memory_template, i + 1, j,
				val_bits + adr_bits - 1, delaytime - 1);
			if(status < 0) goto process_err2;
		}

		char *reset_lines = NULL;
		for(j = 0; j < m; ++j) {
			status = catf(&reset_lines,
				"\t\taddress%1$d_%2$d <= 0;\n", i + 1, j);
			if(status < 0) goto process_err3;
		}

		for(j = 0; j < m; ++j) {
			status = catf(&reset_lines,
				"\t\tvalue%1$d_%2$d <= 0;\n", i + 1, j);
			if(status < 0) goto process_err3;
		}

		char *reset_memories = NULL;
		for(j = 0; j < m; ++j) {
			status = catf(&reset_memories,
				"\t\tfor(i = 0; i < %1$d; i = i + 1) "
				"memory%2$d_%3$d[i] <= 0;\n", delaytime, i + 1,
				j);
			if(status < 0) goto process_err4;
		}

		char *body = NULL;
		unsigned olddelay = delay;
		if(skip < m) {
			/* Swap */
			unsigned l;
			for(l = 0; l < m; ++l) {
				int swap_err = -1;
				if(l & skip) continue;
				/* Swap l with l + skip. */
				char *comparison = NULL;
				status = catf(&comparison,
					"address%1$d_%3$d < address%1$d_%2$d",
					i, l, l + skip);
				if(status < 0) goto swap_err0;

				char *sort = NULL;
				status = catf(&sort,
					getdir(next_triangular_number, l, m, n),
					i, next_triangular_number - ulog2(m),
					comparison);
				if(status < 0) goto swap_err1;

				status = catf(&body,
					BLOB(i1_swapper), sort, i + 1, i, l,
					l + skip);
				if(status < 0) goto swap_err2;

				swap_err = 0;

swap_err2:			free(sort);
swap_err1:			free(comparison);
swap_err0:			if(swap_err) goto process_err5;
			}
			++delay;
		}
		else {
			/* Delay */
			unsigned delaytime = skip / m;
			unsigned delaybits = ulog2(delaytime);
			unsigned l;
			for(l = 0; l < m; ++l) {
				int delay_err = -1;

				char *comparison = NULL;
				status = catf(&comparison,
					"address%1$d_%2$d < "
					"memory%3$d_%2$d[t_%1$d & %4$d]"
					"[%5$d:0]",
					i, l, i + 1, delaytime - 1,
					adr_bits - 1);
				if(status < 0) goto delay_err0;

				char *sort = NULL;
				status = catf(&sort,
					getdir(next_triangular_number, l, m, n),
					i, next_triangular_number - ulog2(m),
					comparison);
				if(status < 0) goto delay_err1;
				status = catf(&body, BLOB(i1_delayer), sort,
					i + 1, i, l, delaybits, delaytime - 1,
					adr_bits - 1, adr_bits + val_bits - 1,
					adr_bits);
				if(status < 0) goto delay_err2;

				delay_err = 0;

delay_err2:			free(sort);
delay_err1:			free(comparison);
delay_err0:			if(delay_err) goto process_err5;
			}
			delay += delaytime + 1;
		}

		status = catf(&process, skip >= m ? BLOB(i1_delay)
			: BLOB(i1_swap), i + 1, pipeline, memory, reset_lines,
			reset_memories, body, time_bits - 1, i,
			delay - olddelay - 1);
		if(status < 0) goto process_err5;

		process_err = 0;

process_err5:	free(body);
process_err4:	free(reset_memories);
process_err3:	free(reset_lines);
process_err2:	free(memory);
process_err1:	free(pipeline);
process_err0:	if(process_err) goto err8;
	}

	unsigned last_i = ulog2(n) * (ulog2(n) + 1) / 2;

	char *obuf = NULL;
	for(i = 0; i < m; ++i) {
		status = catf(&obuf, "reg [%1$d:0] obuf%2$d[0:6];\n",
			val_bits - 1, i);
		if(status < 0) goto err9;
	}

	char *control_reset = NULL;
	for(i = 0; i < m; ++i) {
		status = catf(&control_reset,
			"\t\taddress0_%1$d <= 0;\n", i);
		if(status < 0) goto err10;
	}
	for(i = 0; i < m; ++i) {
		status = catf(&control_reset,
			"\t\tvalue0_%1$d <= 0;\n", i);
		if(status < 0) goto err10;
	}
	for(i = 0; i < m; ++i) {
		status = catf(&control_reset,
			"\t\tfor(i = 0; i < %2$d; i = i + 1) "
			"patterna%1$d[i] <= i * %3$d + %1$d;\n", i, k, m);
		if(status < 0) goto err10;
	}
	for(i = 0; i < m; ++i) {
		status = catf(&control_reset,
			"\t\tfor(i = 0; i < %2$d; i = i + 1) "
			"patternb%1$d[i] <= 0;\n", i, k, m);
		if(status < 0) goto err10;
	}
	for(i = 0; i < m; ++i) {
		status = catf(&control_reset,
			"\t\tfor(i = 0; i < 6; i = i + 1) "
			"obuf%1$d[i] <= 0;\n", i);
		if(status < 0) goto err10;
	}
	for(i = 0; i < m; ++i) {
		status = catf(&control_reset,
			"\t\tout%1$d <= 0;\n", i);
		if(status < 0) goto err10;
	}

	char *read_in = NULL;
	for(i = 0; i < m; ++i) {
		status = catf(&read_in,
			"\t\t\taddress0_%1$d <= current_pattern ?\n"
			"\t\t\t\tpatternb%1$d[t] : patterna%1$d[t];\n", i);
		if(status < 0) goto err11;
	}
	for(i = 0; i < m; ++i) {
		status = catf(&read_in,
			"\t\t\tvalue0_%1$d <= in%1$d;\n", i);
		if(status < 0) goto err11;
	}

	char *shift_obuf = NULL;
	for(i = 0; i < m; ++i) {
		status = catf(&shift_obuf,
			"\t\t\tobuf%1$d[0] <= value%2$d_%1$d;\n", i, last_i);
		if(status < 0) goto err11;
	}
	for(i = 0; i < m; ++i) {
		status = catf(&shift_obuf,
			"\t\t\tfor(i = 1; i < 7; i = i + 1) "
			"obuf%1$d[i] <= obuf%1$d[i - 1];\n", i);
		if(status < 0) goto err11;
	}

	char *write_out = NULL;
	for(i = 0; i < m; ++i) {
		status = catf(&write_out,
			"\t\t\tout%1$d <= obuf%1$d[obuf_len - 1];\n", i);
		if(status < 0) goto err13;
	}

	char *pattern_cases = NULL;
	for(i = 0; i < m; ++i) {
		status = catf(&pattern_cases,
			"\t\t\t\tif(current_pattern) begin\n"
			"\t\t\t\t\tpatterna%1$d[pattern_pos][i] \n"
			"\t\t\t\t\t\t<= pattern_word[%3$d + i];\n"
			"\t\t\t\tend\n"
			"\t\t\t\telse begin\n"
			"\t\t\t\t\tpatternb%1$d[pattern_pos][i] \n"
			"\t\t\t\t\t\t<= pattern_word[%3$d + i];\n"
			"\t\t\t\tend\n",
			i, k, (m - 1 - i) * adr_bits, (i + 1) * adr_bits - 1);
		if(status < 0) goto err14;
	}

	unsigned max_zeroing = delay + k + 1;

	unsigned bits_per_word = adr_bits * m;
	unsigned bytes_per_word = (bits_per_word + 7) / 8;

	char *everything = NULL;
	status = catf(&everything, BLOB(i1_module), module_in_args,
		module_out_args, 2 * ulog2(n) - 1, time_bits - 1,
		input_declarations, output_declarations, pattern_declarations,
		pipeline0, process,
/*10-14*/	obuf, ulog2(npot(delay + 1)), control_reset, read_in, delay,
/*15-18*/	last_i, shift_obuf, write_out, ulog2(npot(max_zeroing)),
/*19-22*/	max_zeroing, pattern_cases, adr_bits + ulog2(m) - 1, adr_bits,
/*23-26*/	inputs - 1, val_bits, ulog2(inputs - 1), inputs,
/*27-28*/	bytes_per_word * 8 - 1, ulog2(bytes_per_word * 8) - 3 - 1,
/*29-..*/	bytes_per_word - 1, ulog2(k - 1) - 1, k - 1);
	if(status < 0) goto err_f;

	status = write(file, everything, strlen(everything));
	if(status < 0) {
		snprintf(e, e_sz, "Could not write %s: %s",
			filename, strerror(errno));
		goto err_f;
	}

	err = 0;

err_f:	free(everything);
err14:	free(pattern_cases);
err13:	free(write_out);
err12:	free(shift_obuf);
err11:	free(read_in);
err10:	free(control_reset);
err9:	free(obuf);
err8:	free(pipeline0);
err7:	free(pattern_declarations);
err6:	free(output_declarations);
err5:	free(input_declarations);
err4:	free(module_out_args);
err3:	free(module_in_args);
err1:	close(file);
err0:	return err;
}
