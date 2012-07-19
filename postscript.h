/*
 * postscript.h - Dump objects in Postscript
 *
 * Written 2009-2012 by Werner Almesberger
 * Copyright 2009-2012 by Werner Almesberger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


#ifndef POSTSCRIPT_H
#define POSTSCRIPT_H

#include <stdio.h>


struct postscript_params {
        double zoom;		/* 0 for auto-zoom */
	double max_width;	/* in fped units; 0 for paper width */
	double max_height;	/* in fped units; 0 for paper height */
        int show_pad_names;
        int show_stuff;         /* vecs and frames */
        int label_vecs;
        int show_meas;
	int show_key;
} postscript_params;


int postscript(FILE *file, const char *one);
int postscript_fullpage(FILE *file, const char *one);

#endif /* !POSTSCRIPT_H */
