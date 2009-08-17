/*
 * obj.c - Object definition model
 *
 * Written 2009 by Werner Almesberger
 * Copyright 2009 by Werner Almesberger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "util.h"
#include "error.h"
#include "expr.h"
#include "meas.h"
#include "inst.h"
#include "obj.h"


#define	DEFAULT_SILK_WIDTH	make_mil(15)	/* @@@ */

#define	MAX_ITERATIONS	1000	/* abort "loop"s at this limit */


char *pkg_name = NULL;
struct frame *frames = NULL;
struct frame *root_frame = NULL;
struct frame *active_frame = NULL;
void *instantiation_error = NULL;


static int generate_frame(struct frame *frame, struct coord base,
    const struct frame *parent, struct obj *frame_ref, int active);


struct num eval_unit(const struct expr *expr, const struct frame *frame);
/*static*/ struct num eval_unit(const struct expr *expr, const struct frame *frame)
{
	struct num d;

	d = eval_num(expr, frame);
	if (!is_undef(d) && to_unit(&d))
		return d;
	fail_expr(expr);
	return undef;
}


static struct num eval_unit_default(const struct expr *expr,
    const struct frame *frame, struct num def)
{
	if (expr)
		return eval_unit(expr, frame);
	to_unit(&def);
	return def;
}


static int generate_vecs(struct frame *frame, struct coord base)
{
	struct coord vec_base;
	struct vec *vec;
	struct num x, y;

	for (vec = frame->vecs; vec; vec = vec->next) {
		x = eval_unit(vec->x, frame);
		if (is_undef(x))
			goto error;
		y = eval_unit(vec->y, frame);
		if (is_undef(y))
			goto error;
		vec_base = vec->base ? vec->base->pos : base;
		vec->pos = vec_base;
		vec->pos.x += x.n;
		vec->pos.y += y.n;
		if (!inst_vec(vec, vec_base))
			goto error;
		meas_post(vec, vec->pos);
	}
	return 1;

error:
	instantiation_error = vec;
	return 0;
}


static int generate_objs(struct frame *frame, struct coord base, int active)
{
	struct obj *obj;
	char *name;
	int ok;
	struct num width;

	for (obj = frame->objs; obj; obj = obj->next)
		switch (obj->type) {
		case ot_frame:
			if (!generate_frame(obj->u.frame.ref,
			    obj->base ? obj->base->pos : base, frame, obj,
			    active && obj->u.frame.ref->active_ref == obj))
				return 0;
			break;
		case ot_line:
			width = eval_unit_default(obj->u.line.width, frame,
			    DEFAULT_SILK_WIDTH);
			if (is_undef(width))
				goto error;
			if (!inst_line(obj, obj->base ? obj->base->pos : base,
			    obj->u.line.other ? obj->u.line.other->pos : base,
			    width.n))
				goto error;
			break;
		case ot_rect:
			width = eval_unit_default(obj->u.rect.width, frame,
			    DEFAULT_SILK_WIDTH);
			if (is_undef(width))
				goto error;
			if (!inst_rect(obj, obj->base ? obj->base->pos : base,
			    obj->u.rect.other ? obj->u.rect.other->pos : base,
			    width.n))
				goto error;
			break;
		case ot_pad:
			name = expand(obj->u.pad.name, frame);
			if (!name)
				goto error;
			ok = inst_pad(obj, name,
			    obj->base ? obj->base->pos : base,
			    obj->u.pad.other ? obj->u.pad.other->pos : base);
			free(name);
			if (!ok)
				goto error;
			break;
		case ot_arc:
			width = eval_unit_default(obj->u.arc.width, frame,
			    DEFAULT_SILK_WIDTH);
			if (is_undef(width))
				goto error;
			if (!inst_arc(obj, obj->base ? obj->base->pos : base,
			    obj->u.arc.start ? obj->u.arc.start->pos : base,
			    obj->u.arc.end ? obj->u.arc.end->pos : base,
			    width.n))
				goto error;
			break;
		case ot_meas:
			break;
		default:
			abort();
		}
	return 1;

error:
	instantiation_error = obj;
	return 0;
}


static int generate_items(struct frame *frame, struct coord base, int active)
{
	char *s;
	int ok;

	if (!frame->name) {
		s = expand(pkg_name, frame);
		inst_select_pkg(s);
		free(s);
	}
	inst_begin_active(active && frame == active_frame);
	ok = generate_vecs(frame, base) && generate_objs(frame, base, active);
	inst_end_active();
	return ok;
}


static int run_loops(struct frame *frame, struct loop *loop,
    struct coord base, int active)
{
	struct num from, to;
	int n;

	if (!loop)
		return generate_items(frame, base, active);
	from = eval_num(loop->from.expr, frame);
	if (is_undef(from)) {
		fail_expr(loop->from.expr);
		instantiation_error = loop;
		return 0;
	}
	if (!is_dimensionless(from)) {
		fail("incompatible type for start value");
		fail_expr(loop->from.expr);
		instantiation_error = loop;
		return 0;
	}

	to = eval_num(loop->to.expr, frame);
	if (is_undef(to)) {
		fail_expr(loop->to.expr);
		instantiation_error = loop;
		return 0;
	}
	if (!is_dimensionless(to)) {
		fail("incompatible type for end value");
		fail_expr(loop->to.expr);
		instantiation_error = loop;
		return 0;
	}

	assert(!loop->initialized);
	loop->curr_value = from.n;
	loop->initialized = 1;

	n = 0;
	for (; loop->curr_value <= to.n; loop->curr_value += 1) {
		if (n >= MAX_ITERATIONS) {
			fail("%s: too many iterations (%d)", loop->var.name,
			    MAX_ITERATIONS);
			instantiation_error = loop;
			goto fail;
		}
		if (!run_loops(frame, loop->next, base,
		    active && loop->active == n))
			goto fail;
		n++;
	}
	loop->initialized = 0;
	loop->iterations = n;
	return 1;

fail:
	loop->initialized = 0;
	return 0;
}


static int iterate_tables(struct frame *frame, struct table *table,
    struct coord base, int active)
{
	if (!table)
		return run_loops(frame, frame->loops, base, active);
	for (table->curr_row = table->rows; table->curr_row;
	    table->curr_row = table->curr_row->next)
		if (!iterate_tables(frame, table->next, base,
		    active && table->active_row == table->curr_row))
			return 0;
	return 1;
}


static int generate_frame(struct frame *frame, struct coord base,
    const struct frame *parent, struct obj *frame_ref, int active)
{
	int ok;

	/*
	 * We ensure during construction that frames can never recurse.
	 */
	inst_begin_frame(frame_ref, frame, base,
	    active && parent == active_frame,
	    active && frame == active_frame);
	frame->curr_parent = parent;
	ok = iterate_tables(frame, frame->tables, base, active);
	inst_end_frame(frame);
	return ok;
}


int instantiate(void)
{
	struct coord zero = { 0, 0 };
	int ok;

	meas_start();
	inst_start();
	instantiation_error = NULL;
	ok = generate_frame(root_frame, zero, NULL, NULL, 1);
	if (ok)
		ok = instantiate_meas();
	if (ok)
		inst_commit();
	else
		inst_revert();
	return ok;
}
