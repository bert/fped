/*
 * gnuplot.c - Dump objects in gnuplot 2D format
 *
 * Written 2011 by Werner Almesberger
 * Copyright 2011 by Werner Almesberger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


#include <stdio.h>
#include <string.h>

#include "coord.h"
#include "inst.h"
#include "gnuplot.h"


#define	ARC_STEP	0.1	/* @@@ make configurable */


static void recurse_id(FILE *file, const struct inst *inst)
{
	if (inst->obj->frame->name) {
		recurse_id(file, inst->outer);
		fprintf(file, "/%s", inst->obj->frame->name);
	}
}


static void identify(FILE *file, const struct inst *inst)
{
	fprintf(file, "#%%id=");
	recurse_id(file, inst);
	fprintf(file, "\n");
}


static void gnuplot_line(FILE *file, const struct inst *inst)
{
	double xa, ya, xb, yb;

	xa = units_to_mm(inst->base.x);
	ya = units_to_mm(inst->base.y);
	xb = units_to_mm(inst->u.rect.end.x);
	yb = units_to_mm(inst->u.rect.end.y);

	identify(file, inst);
	fprintf(file, "#%%r=%f\n%f %f\n%f %f\n\n",
	    units_to_mm(inst->u.rect.width), xa, ya, xb, yb);
}


static void gnuplot_rect(FILE *file, const struct inst *inst)
{
	double xa, ya, xb, yb;

	xa = units_to_mm(inst->base.x);
	ya = units_to_mm(inst->base.y);
	xb = units_to_mm(inst->u.rect.end.x);
	yb = units_to_mm(inst->u.rect.end.y);

	identify(file, inst);
	fprintf(file, "#%%r=%f\n", units_to_mm(inst->u.rect.width));
	fprintf(file, "%f %f\n", xa, ya);
	fprintf(file, "%f %f\n", xa, yb);
	fprintf(file, "%f %f\n", xb, yb);
	fprintf(file, "%f %f\n", xb, ya);
	fprintf(file, "%f %f\n\n", xa, ya);
}


static void gnuplot_circ(FILE *file, const struct inst *inst)
{
	double cx, cy, r;
	double a;
	int n, i;

	cx = units_to_mm(inst->base.x);
	cy = units_to_mm(inst->base.y);
	r = units_to_mm(inst->u.arc.r);

	identify(file, inst);
	fprintf(file, "#%%r=%f\n", units_to_mm(inst->u.arc.width));

	n = ceil(2*r*M_PI/ARC_STEP);
	if (n < 2)
		n = 2;

	for (i = 0; i <= n; i++) {
		a = 2*M_PI/n*i;
		fprintf(file, "%f %f\n", cx+r*sin(a), cy+r*cos(a));
	}
	fprintf(file, "\n");
}


static void gnuplot_arc(FILE *file, const struct inst *inst)
{
	double cx, cy, r;
	double a, tmp;
	int n, i;

	cx = units_to_mm(inst->base.x);
	cy = units_to_mm(inst->base.y);
	r = units_to_mm(inst->u.arc.r);

	a = inst->u.arc.a2-inst->u.arc.a1;
	while (a <= 0)
		a += 360;
	while (a > 360)
		a =- 360;

	n = ceil(2*r*M_PI/360*a/ARC_STEP);
	if (n < 2)
		n = 2;

	for (i = 0; i <= n; i++) {
		tmp = (inst->u.arc.a1+a/n*i)*M_PI/180;
		fprintf(file, "%f %f\n", cx+r*cos(tmp), cy+r*sin(tmp));
	}

	fprintf(file, "\n");
}


static void gnuplot_inst(FILE *file, enum inst_prio prio,
    const struct inst *inst)
{
	switch (prio) {
	case ip_pad_copper:
	case ip_pad_special:
		/* complain ? */
		break;
	case ip_hole:
		/* complain ? */
		break;
	case ip_line:
		gnuplot_line(file, inst);
		break;
	case ip_rect:
		gnuplot_rect(file, inst);
		break;
	case ip_circ:
		gnuplot_circ(file, inst);
		break;
	case ip_arc:
		gnuplot_arc(file, inst);
		break;
	default:
		/*
		 * Don't try to export vectors, frame references, or
		 * measurements.
		 */
		break;
	}
}


static void gnuplot_package(FILE *file, const struct pkg *pkg)
{
	enum inst_prio prio;
	const struct inst *inst;

	/*
	 * Package name
	 */
	fprintf(file, "# %s\n", pkg->name);

	FOR_INST_PRIOS_UP(prio) {
		for (inst = pkgs->insts[prio]; inst; inst = inst->next)
			gnuplot_inst(file, prio, inst);
		for (inst = pkg->insts[prio]; inst; inst = inst->next)
			gnuplot_inst(file, prio, inst);
	}

	fprintf(file, "\n");
}


int gnuplot(FILE *file, const char *one)
{
	const struct pkg *pkg;

	for (pkg = pkgs; pkg; pkg = pkg->next)
		if (pkg->name)
			if (!one || !strcmp(pkg->name, one))
				gnuplot_package(file, pkg);

	fflush(file);
	return !ferror(file);
}
