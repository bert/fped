/*
 * coord.c - Coordinate representation and basic operations
 *
 * Written 2009 by Werner Almesberger
 * Copyright 2009 by Werner Almesberger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


#include <math.h>

#include "coord.h"


/* ----- unit conversion --------------------------------------------------- */


double mm_to_mil(double mm, int exponent)
{
	return mm*pow(MIL_IN_MM, -exponent);
}


double mil_to_mm(double mil, int exponent)
{
	return mil*pow(MIL_IN_MM, exponent);
}

/* ----- vector operations ------------------------------------------------- */


struct coord normalize(struct coord v, unit_type len)
{
	double f;

	f = len/hypot(v.x, v.y);
	v.x *= f;
	v.y *= f;
	return v;
}


struct coord rotate(struct coord v, double angle)
{
	double rad = M_PI*angle/180.0;
	struct coord res;

	res.x = v.x*cos(rad)-v.y*sin(rad);
	res.y = v.y*cos(rad)+v.x*sin(rad);
	return res;
}


struct coord add_vec(struct coord a, struct coord b)
{
	a.x += b.x;
	a.y += b.y;
	return a;
}


struct coord sub_vec(struct coord a, struct coord b)
{
	a.x -= b.x;
	a.y -= b.y;
	return a;
}


struct coord neg_vec(struct coord v)
{
	v.x = -v.x; 
	v.y = -v.y;
	return v;
}


/* ----- distance calculations --------------------------------------------- */


unit_type dist_point(struct coord a, struct coord b)
{
	return hypot(a.x-b.x, a.y-b.y);
}


static unit_type dist_line_xy(unit_type px, unit_type py,
    unit_type ax, unit_type ay, unit_type bx, unit_type by)
{
	unit_type d_min, d;
	double a, f;

	d_min = hypot(ax-px, ay-py);
	d = hypot(bx-px, by-py);
	if (d < d_min)
		d_min = d;
	if (ax != bx || ay != by) {
		/*
		 * We make a the line vector from point B and b the vector from
		 * B to point P. Then we calculate the projection of b on a.
		 */
		ax -= bx;
		ay -= by;
		bx = px-bx;
		by = py-by;
		a = hypot(ax, ay);
		f = ((double) ax*bx+(double) ay*by)/a/a;
		if (f >= 0 && f <= 1) {
			bx -= f*ax;
			by -= f*ay;
			d = hypot(bx, by);
			if (d < d_min)
				d_min = d;
		}
	}
	return d_min;
}


unit_type dist_line(struct coord p, struct coord a, struct coord b)
{
	return dist_line_xy(p.x, p.y, a.x, a.y, b.x, b.y);
}


unit_type dist_rect(struct coord p, struct coord min, struct coord max)
{
	unit_type d_min, d;

	d_min = dist_line_xy(p.x, p.y, min.x, min.y, max.x, min.y);
	d = dist_line_xy(p.x, p.y, min.x, min.y, min.x, max.y);
	if (d < d_min)
		d_min = d;
	d = dist_line_xy(p.x, p.y, min.x, max.y, max.x, max.y);
	if (d < d_min)
		d_min = d;
	d = dist_line_xy(p.x, p.y, max.x, min.y, max.x, max.y);
	if (d < d_min)
		d_min = d;
	return d_min;
}


int inside_rect(struct coord p, struct coord min, struct coord max)
{
	if (p.x < min.x || p.x > max.x)
		return 0;
	if (p.y < min.y || p.y > max.y)
		return 0;
	return 1;
}


unit_type dist_circle(struct coord p, struct coord c, unit_type r)
{
	unit_type d;

	d = hypot(p.x-c.x, p.y-c.y);
	return fabs(d-r);
}
