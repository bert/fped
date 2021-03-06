/*
 * fped.c - Footprint editor, main function
 *
 * Written 2009-2012 by Werner Almesberger
 * Copyright 2009-2012 by Werner Almesberger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "cpp.h"
#include "util.h"
#include "error.h"
#include "obj.h"
#include "inst.h"
#include "file.h"
#include "postscript.h"
#include "dump.h"
#include "gui.h"
#include "delete.h"
#include "fpd.h"
#include "fped.h"


char *save_file_name = NULL;
int no_save = 0;


static void load_file(const char *name)
{
	FILE *file;
	char line[sizeof(MACHINE_GENERATED)];

	file = fopen(name, "r");
	if (file) {
		if (!fgets(line, sizeof(line), file)) {
			if (ferror(file)) {
				perror(name);
				exit(1);
			}
			*line = 0;
		}
		no_save = strcmp(line, MACHINE_GENERATED);
		fclose(file);
		reporter = report_parse_error;
		run_cpp_on_file(name);
	} else {
		if (errno != ENOENT) {
			perror(name);
			exit(1);
		}
		scan_empty();
	}
	(void) yyparse();
}


void reload(void)
{
	struct frame *old_frames;

	/* @@@ this needs more work */
	purge();
	old_frames = frames;
	scan_file();
	load_file(save_file_name);
	if (!instantiate())
		frames = old_frames;
	change_world();
}


static void usage(const char *name)
{
	fprintf(stderr,
"usage: %s [batch_mode] [cpp_option ...] [in_file [out_file]]\n\n"
"Batch mode options:\n"
"  -g [-1 package]\n"
"              write gnuplot output, then exit\n"
"  -k          write KiCad output, then exit\n"
"  -p          write Postscript output, then exit\n"
"  -P [-K] [-s scale] [-1 package]\n"
"              write Postscript output (full page), then exit\n"
"  -T          test mode. Load file, then exit\n"
"  -T -T       test mode. Load file, dump to stdout, then exit\n\n"
"Common options:\n"
"  -1 name     output only the specified package\n"
"  -K          show the pad type key\n"
"  -s scale    scale factor for -P (default: auto-scale)\n"
"  -s [width]x[heigth]\n"
"              auto-scale to fit within specified box. Dimensions in mm.\n"
"  cpp_option  -Idir, -Dname[=value], or -Uname\n"
    , name);
	exit(1);
}


static int parse_scaling(const char *arg)
{
	const char *x;
	char *end;

	x = strchr(arg, 'x');
	if (!x) {
		postscript_params.zoom = strtod(arg, &end);
		return !*end;
	}
	if (x != arg) {
		postscript_params.max_width = mm_to_units(strtod(arg, &end));
		if (*end != 'x')
			return 0;
	}
	if (x[1]) {
		postscript_params.max_height = mm_to_units(strtod(x+1, &end));
		if (*end)
			return 0;
	}
	return 1;
}


int main(int argc, char **argv)
{
	enum {
		batch_none = 0,
		batch_kicad,
		batch_ps,
		batch_ps_fullpage,
		batch_gnuplot,
		batch_test
	} batch = batch_none;
	char *name = *argv;
	char **fake_argv;
	char *args[2];
	int fake_argc;
	char opt[] = "-?";
	int error;
	int test_mode = 0;
	const char *one = NULL;
	int c;

	while ((c = getopt(argc, argv, "1:gkps:D:I:KPTU:")) != EOF)
		switch (c) {
		case '1':
			one = optarg;
			break;
		case 'g':
			if (batch)
				usage(*argv);
			batch = batch_gnuplot;
			break;
		case 'k':
			if (batch)
				usage(*argv);
			batch = batch_kicad;
			break;
		case 'p':
			if (batch)
				usage(*argv);
			batch = batch_ps;
			break;
		case 'P':
			if (batch)
				usage(*argv);
			batch = batch_ps_fullpage;
			break;
		case 'K':
			postscript_params.show_key = 1;
			break;
		case 's':
			if (batch != batch_ps_fullpage)
				usage(*argv);
			if (!parse_scaling(optarg))
				usage(*argv);
			break;
		case 'T':
			batch = batch_test;
			test_mode++;
			break;
		case 'D':
		case 'U':
		case 'I':
			opt[1] = c;
			add_cpp_arg(opt);
			add_cpp_arg(optarg);
			break;
		default:
			usage(name);
		}

	if (one && batch != batch_ps && batch != batch_ps_fullpage &&
	    batch != batch_gnuplot)
		usage(name);
	if (postscript_params.show_key && batch != batch_ps_fullpage)
		usage(name);

	if (!batch) {
		args[0] = name;
		args[1] = NULL;
		fake_argc = 1;
		fake_argv = args;
		error = gui_init(&fake_argc, &fake_argv);
		if (error)
			return error;
	}

	switch (argc-optind) {
	case 0:
		scan_empty();
		(void) yyparse();
		break;
	case 1:
		load_file(argv[optind]);
		save_file_name = argv[optind];
		break;
	case 2:
		load_file(argv[optind]);
		save_file_name = argv[optind+1];
		if (!strcmp(save_file_name, "-"))
			save_file_name = NULL;
		break;
	default:
		usage(name);
	}

	if (!pkg_name)
		pkg_name = stralloc("_");

	reporter = report_to_stderr;
	if (!instantiate())
		return 1;

	switch (batch) {
	case batch_none:
		error = gui_main();
		if (error)
			return error;
		break;
	case batch_kicad:
		write_kicad();
		break;
	case batch_ps:
		write_ps(one);
		break;
	case batch_ps_fullpage:
		write_ps_fullpage(one);
		break;
	case batch_gnuplot:
		write_gnuplot(one);
		break;
	case batch_test:
		if (test_mode > 1)
			dump(stdout, NULL);
		break;
	default:
		abort();
	}

	purge();
	inst_revert();
	obj_cleanup();
	unique_cleanup();

	return 0;
}
