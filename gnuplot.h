/*
 * gnuplot.h - Dump objects in gnuplot 2D format
 *
 * Written 2011 by Werner Almesberger
 * Copyright 2011 by Werner Almesberger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


#ifndef GNUPLOT_H
#define GNUPLOT_H

#include <stdio.h>


int gnuplot(FILE *file, const char *one);

#endif /* !GNUPLOT_H */
