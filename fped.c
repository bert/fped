/*
 * fped.c - Footprint editor, main function
 *
 * Written 2009, 2010 by Werner Almesberger
 * Copyright 2009, 2010 by Werner Almesberger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "cpp.h"
#include "util.h"
#include "error.h"
#include "obj.h"
#include "inst.h"
#include "file.h"
#include "gui.h"
#include "delete.h"
#include "fpd.h"


char *save_file_name = NULL;


static void load_file(const char *name)
{
	if (file_exists(name) == 1) {
		reporter = report_parse_error;
		run_cpp_on_file(name);
	} else {
		scan_empty();
	}
	(void) yyparse();
}


static void usage(const char *name)
{
	fprintf(stderr,
"usage: %s [-k] [-p|-P] [cpp_option ...] [in_file [out_file]]\n\n"
"  -k          write KiCad output, then exit\n"
"  -p          write Postscript output, then exit\n"
"  -P          write Postscript output (full page), then exit\n"
"  cpp_option  -Idir, -Dname[=value], or -Uname\n"
    , name);
	exit(1);
}


int main(int argc, char **argv)
{
	char *name = *argv;
	char **fake_argv;
	char *args[2];
	int fake_argc;
	char opt[] = "-?";
	int error, batch;
	int batch_write_kicad = 0;
	int batch_write_ps = 0, batch_write_ps_fullpage = 0;
	int c;

	while ((c = getopt(argc, argv, "kpD:I:U:P")) != EOF)
		switch (c) {
		case 'k':
			batch_write_kicad = 1;
			break;
		case 'p':
			batch_write_ps = 1;
			break;
		case 'P':
			batch_write_ps_fullpage = 1;
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

	if (batch_write_ps && batch_write_ps_fullpage)
		usage(name);

	batch = batch_write_kicad || batch_write_ps || batch_write_ps_fullpage;

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

	if (batch_write_kicad)
		write_kicad();
	if (batch_write_ps)
		write_ps();
	if (batch_write_ps_fullpage)
		write_ps_fullpage();
	if (!batch) {
		error = gui_main();
		if (error)
			return error;
	}

	purge();
	inst_revert();
	obj_cleanup();
	unique_cleanup();

	return 0;
}
