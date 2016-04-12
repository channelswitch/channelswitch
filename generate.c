#include "writecode_bitonic.h"
#include "writeverilog_bitonic.h"
#include "writeverilog_memories.h"
#include "clos_network.h"
#include "clos_network_2.h"
#include "memories_2.h"
#include "bitonic_2.h"
#include "writetestbench.h"
#include "testbench_2.h"
#include "vpi.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

#define DEFAULT_INPUTS 4
#define DEFAULT_PERIOD 4
#define DEFAULT_DATATYPE 32
#define DEFAULT_TYPE 5
#define N_TYPES 6

struct args {
	unsigned help;
	char *o;
	int inputs, period, datatype, type;
};

static int parse_options(
		struct args *a,
		int argc,
		char **argv,
		char *e,
		unsigned e_sz)
{
	unsigned stop_options_processing = 0;
	unsigned i, j;
	for(i = 1; i < argc; ++i) {
		if(stop_options_processing || argv[i][0] != '-') {
			if(!a->o) a->o = argv[i];
			else goto unrecognized_option;
		}
		else if(!strcmp(argv[i], "-")) {
			goto unrecognized_option;
		}
		else if(!strcmp(argv[i], "--")) {
			stop_options_processing = 1;
		}
		/* All long options without arguments here. */
		else if(!strcmp(argv[i], "--help")) {
			a->help = 1;
		}
		else if(argv[i][1] == '-') {
			/* Either a long option with an argument or an
			 * unrecognized option. */
			unsigned i1 = i;
			char *name = argv[i] + 2;
			char *arg = strchr(name, '=');
			if(arg) *arg++ = '\0';
			else if(i + 1 < argc) arg = argv[++i1];

			if(strcmp(name, "inputs")
					&& strcmp(name, "period")
					&& strcmp(name, "data")
					&& strcmp(name, "type"))
				goto unrecognized_option;

			if(!arg) goto requires_argument;

			char *end;
			long l = strtol(arg, &end, 10);
			if(end == arg || *end != '\0' || l < 0 || l > INT_MAX)
				goto requires_argument;
			int n = l;

			if(!strcmp(name, "inputs")) a->inputs = n;
			else if(!strcmp(name, "period")) a->period = n;
			else if(!strcmp(name, "data")) a->datatype = n;
			else if(!strcmp(name, "type")) a->type = n;
			else goto unrecognized_option;
			i = i1;
		}
		else {
			/* One or more short options. */
			unsigned i1 = i;
			for(j = 1; argv[i][j]; ++j) {
				int n;
				if(strchr("ipdt", argv[i][j])) {
					if(i1 + 1 >= argc)
						goto requires_argument_short;
					char *end;
					long l = strtol(argv[i1 + 1], &end, 10);
					if(end == argv[i1 + 1]
							|| *end != '\0'
							|| l < 0
							|| l > INT_MAX)
						goto requires_argument_short;
					n = l;
					++i1;
				}

				if(argv[i][j] == 'h') a->help = 1;
				else if(argv[i][j] == 'i') a->inputs = n;
				else if(argv[i][j] == 'p') a->period = n;
				else if(argv[i][j] == 'd') a->datatype = n;
				else if(argv[i][j] == 't') a->type = n;
				else goto unrecognized_option_short;
			}
			i = i1;
		}
	}
	return 0;

unrecognized_option_short:
	argv[i][1] = argv[i][j];
	argv[i][2] = '\0';
unrecognized_option:
	snprintf(e, e_sz, "Unrecognized option: %s", argv[i]);
	return -1;

requires_argument_short:
	argv[i][1] = argv[i][j];
	argv[i][2] = '\0';
requires_argument:
	snprintf(e, e_sz, "Option %s requires an integer argument", argv[i]);
	return -1;
}

#define STR1(s) #s
#define STR(s) STR1(s)

static int cata(char **out, char *a, char *b)
{
	char *out1;
	out1 = malloc(strlen(a) + strlen(b) + 1);
	if(!out1) return -1;

	strcpy(out1, a);
	strcat(out1, b);

	*out = out1;
	return 0;
}

int main(int argc, char **argv)
{
	int err = 1;
	char e[4096];

	struct args args;
	args.help = 0;
	args.o = NULL;
	args.inputs = -1;
	args.period = -1;
	args.datatype = -1;
	args.type = -1;
	if(parse_options(&args, argc, argv, e, sizeof e) < 0) goto err0;

	if(args.help) {
		printf(
			"Usage: %s [OPTION]... [OUTDIR]\n"
			"\n"
			"Generate Verilog HDL for a switch. The following "
			"files will be generated and\nplaced in the output "
			"directory (which must exist):\n"
			"  switch.v                 Verilog code for the "
			"switch.\n"
			"  generate_pattern_data.c  C source to generate "
			"pattern data for the switch.\n"
			"  generate_pattern_data.h  Header file for the C "
			"source.\n"
			"  testbench.v              A testbench for the "
			"hardware.\n"
			"  vpi.c                    VPI interface to generate_"
			"pattern_data(). Used by\n"
			"                           the testbench.\n"
			"\n"
			"Options:\n"
			"  -h,--help      Print this help text.\n"
			"  -i,--inputs=N  The number of parallel inputs to the "
			"switch. The default is " STR(DEFAULT_INPUTS) ".\n"
			"  -p,--period=N  The period of the switch. The "
			"default is " STR(DEFAULT_PERIOD) ".\n"
			"  -d,--data=N    The datatype in bits. The "
			"default is " STR(DEFAULT_DATATYPE) ".\n"
			"  -t,--type=N    The type of switch.\n"
			"                   1: Bitonic sorter. Limited to "
			"power-of-two inputs and period.\n"
			"                   2: Parallel memories.\n"
			"                   3: Clos network.\n"
			"                   4: Parallel memories*.\n"
			"                   5: Clos network*.\n"
			"                   6: Bitonic but not crap.\n"
			"                 * Number 4 to 6 store their pattern "
			"in a memory while 2 and 3\n"
			"                 use shift registers. The default is "
			STR(DEFAULT_TYPE) ".\n",
			argv[0]);
		err = 0;
		goto err0;
	}

	if(!args.o) args.o = ".";
	if(args.inputs < 0) args.inputs = DEFAULT_INPUTS;
	if(args.period < 0) args.period = DEFAULT_PERIOD;
	if(args.datatype < 0) args.datatype = DEFAULT_DATATYPE;
	if(args.type < 0) args.type = DEFAULT_TYPE;
	else if(args.type < 1 || args.type > N_TYPES) {
		snprintf(e, sizeof e, "The type must be between 1 and %d",
			N_TYPES);
		goto err0;
	}

	if(args.inputs < 1) {
		snprintf(e, sizeof e,
			"The number of inputs must be at least 1");
		goto err0;
	}
	if(args.period < 1) {
		snprintf(e, sizeof e,
			"The period must be at least 1");
		goto err0;
	}

	char *switch_v, *pattern_c, *pattern_h, *testbench_v, *vpi_c;
	if(cata(&switch_v, args.o, "/switch.v") < 0) goto err0;
	if(cata(&pattern_c, args.o, "/generate_pattern_data.c") < 0) goto err1;
	if(cata(&pattern_h, args.o, "/generate_pattern_data.h") < 0) goto err2;
	if(cata(&testbench_v, args.o, "/testbench.v") < 0) goto err3;
	if(cata(&vpi_c, args.o, "/vpi.c") < 0) goto err4;

	if(args.type == 1) {
		if(writeverilog_bitonic(switch_v, args.inputs, args.period,
				args.datatype, e, sizeof e) < 0) {
			goto err5;
		}

		if(writecode_bitonic(pattern_c, args.inputs, args.period,
				args.datatype, e, sizeof e) < 0) {
			goto err5;
		}
	}
	else if(args.type == 2) {
		if(writeverilog_memories(switch_v, pattern_c, args.inputs,
				args.period, args.datatype, e, sizeof e) < 0) {
			goto err5;
		}
	}
	else if(args.type == 3) {
		if(clos_network(switch_v, pattern_c, args.inputs, args.period,
				args.datatype, e, sizeof e) < 0) {
			goto err5;
		}
	}
	else if(args.type == 4) {
		if(memories_2(switch_v, pattern_c, args.inputs, args.period,
				args.datatype, e, sizeof e) < 0) {
			goto err5;
		}
	}
	else if(args.type == 5) {
		if(clos_network_2(switch_v, pattern_c, args.inputs, args.period,
				args.datatype, e, sizeof e) < 0) {
			goto err5;
		}
	}
	else if(args.type == 6) {
		if(bitonic_2(switch_v, pattern_c, args.inputs, args.period,
				args.datatype, e, sizeof e) < 0) {
			goto err5;
		}
	}

	if(writeheader(pattern_h, args.inputs, args.period, args.datatype,
			e, sizeof e) < 0) {
		goto err5;
	}

	if(vpi(vpi_c, e, sizeof e) < 0) goto err5;

	if(args.type == 6) {
		if(testbench_2(testbench_v, args.inputs, args.period,
				args.datatype, e, sizeof e) < 0) goto err5;
	}
	else {
		if(writetestbench(testbench_v, args.inputs, args.period,
				args.datatype, e, sizeof e) < 0) {
			goto err5;
		}
	}

	err = 0;

err5:	free(vpi_c);
err4:	free(testbench_v);
err3:	free(pattern_h);
err2:	free(pattern_c);
err1:	free(switch_v);
err0:	if(err) {
		fprintf(stderr, "Error: %s.\n", e);
	}
	return err;
}
