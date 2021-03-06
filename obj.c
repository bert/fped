/*
 * obj.c - Object definition model
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
#include <string.h>
#include <assert.h>

#include "util.h"
#include "error.h"
#include "expr.h"
#include "bitset.h"
#include "meas.h"
#include "inst.h"
#include "hole.h"
#include "overlap.h"
#include "layer.h"
#include "delete.h"
#include "fpd.h"
#include "obj.h"


#define	DEFAULT_SILK_WIDTH	make_mil(15)	/* @@@ */
#define	DEFAULT_OFFSET		make_mil(0)	/* @@@ */

#define	MAX_ITERATIONS	1000	/* abort "loop"s at this limit */


char *pkg_name = NULL;
struct frame *frames = NULL;
struct frame *active_frame = NULL;
void *instantiation_error = NULL;
enum allow_overlap allow_overlap = ao_none;
int holes_linked = 1;


static struct bitset *frame_set; /* frames visited in "call chain" */


/* ----- Searching --------------------------------------------------------- */


/*
 * @@@ Known bug: we should compare all parameters of an instance, not just the
 * object's base or the vectors end.
 */

static int found = 0;
static int search_suspended = 0;
static const struct vec *find_vec = NULL;
static const struct obj *find_obj = NULL;
static struct coord find_pos;


static void suspend_search(void)
{
	search_suspended++;
}

static void resume_search(void)
{
	assert(search_suspended > 0);
	search_suspended--;
}


static struct coord get_pos(const struct inst *inst)
{
	return inst->obj ? inst->base : inst->u.vec.end;
}


void find_inst(const struct inst *inst)
{
	struct coord pos;

	if (search_suspended)
		return;
	if (find_vec != inst->vec)
		return;
	if (find_obj != inst->obj)
		return;
	pos = get_pos(inst);
	if (pos.x != find_pos.x || pos.y != find_pos.y)
		return;
	found++;
}


void search_inst(const struct inst *inst)
{
	find_vec = inst->vec;
	find_obj = inst->obj;
	find_pos = get_pos(inst);
}


/* ----- Get the list of anchors of an object ------------------------------ */


int obj_anchors(struct obj *obj, struct vec ***anchors)
{
	anchors[0] = &obj->base;
	switch (obj->type) {
	case ot_frame:
		return 1;
	case ot_rect:
	case ot_line:
		anchors[1] = &obj->u.rect.other;
		return 2;
	case ot_pad:
		anchors[1] = &obj->u.pad.other;
		return 2;
	case ot_hole:
		anchors[1] = &obj->u.hole.other;
		return 2;
	case ot_meas:
		anchors[1] = &obj->u.meas.high;
		return 2;
	case ot_arc:
		/*
		 * Put end point first so that this is what we grab if dragging
		 * a circle (thereby turning it into an arc).
		 */
		anchors[1] = &obj->u.arc.end;
		anchors[2] = &obj->u.arc.start;
		return 3;
	default:
		abort();
	}
}


/* ----- Instantiation ----------------------------------------------------- */


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


static int recurse_vec(const char *name, const struct frame *frame,
    struct coord *res)
{
	const struct vec *v;

	if (!frame)
		return 0;
	for (v = frame->vecs; v; v = v->next)
		if (v->name == name) {
			*res = v->pos;
			return 1;
		}
	return recurse_vec(name, frame->curr_parent, res);
}


static int resolve_vec(const struct vec *vec, struct coord base_pos,
    const struct frame *frame, struct coord *res)
{
	const char *name = (const char *) vec;

	if (!vec) {
		*res = base_pos;
		return 1;
	}
	if (!*name) {
		*res = vec->pos;
		return 1;
	}
	if (recurse_vec(name, frame->curr_parent, res))
		return 1;
	fail("unknown vector \"%s\"", name);
	return 0;
}


static int generate_vecs(struct frame *frame, struct coord base_pos)
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
		if (!resolve_vec(vec->base, base_pos, frame, &vec_base))
			goto error;
		vec->pos = vec_base;
		vec->pos.x += x.n;
		vec->pos.y += y.n;
		if (!inst_vec(vec, vec_base))
			goto error;
		meas_post(vec, vec->pos, frame_set);
	}
	return 1;

error:
	instantiation_error = vec;
	return 0;
}


static int generate_objs(struct frame *frame, struct coord base_pos,
    int active)
{
	struct obj *obj;
	char *name;
	int ok;
	struct num width, offset;
	struct coord base, other, start, end;

	for (obj = frame->objs; obj; obj = obj->next) {
		if (obj->type != ot_meas)
			if (!resolve_vec(obj->base, base_pos, frame, &base))
				goto error;
		switch (obj->type) {
		case ot_frame:
			if (!generate_frame(obj->u.frame.ref, base, frame, obj,
			    active && obj->u.frame.ref->active_ref == obj))
				return 0;
			break;
		case ot_line:
			if (!resolve_vec(obj->u.line.other, base_pos, frame,
			    &other))
				goto error;
			width = eval_unit_default(obj->u.line.width, frame,
			    DEFAULT_SILK_WIDTH);
			if (is_undef(width))
				goto error;
			if (!inst_line(obj, base, other, width.n))
				goto error;
			break;
		case ot_rect:
			if (!resolve_vec(obj->u.rect.other, base_pos, frame,
			    &other))
				goto error;
			width = eval_unit_default(obj->u.rect.width, frame,
			    DEFAULT_SILK_WIDTH);
			if (is_undef(width))
				goto error;
			if (!inst_rect(obj, base, other, width.n))
				goto error;
			break;
		case ot_pad:
			if (!resolve_vec(obj->u.pad.other, base_pos, frame,
			    &other))
				goto error;
			name = expand(obj->u.pad.name, frame);
			if (!name)
				goto error;
			ok = inst_pad(obj, name, base, other);
			free(name);
			if (!ok)
				goto error;
			break;
		case ot_hole:
			if (!resolve_vec(obj->u.hole.other, base_pos, frame,
			    &other))
				goto error;
			if (!inst_hole(obj, base, other))
				goto error;
			break;
		case ot_arc:
			if (!resolve_vec(obj->u.arc.start, base_pos, frame,
			    &start))
				goto error;
			if (!resolve_vec(obj->u.arc.end, base_pos, frame,
			    &end))
				goto error;
			width = eval_unit_default(obj->u.arc.width, frame,
			    DEFAULT_SILK_WIDTH);
			if (is_undef(width))
				goto error;
			if (!inst_arc(obj, base, start, end, width.n))
				goto error;
			break;
		case ot_meas:
			assert(frame == frames);
			offset = eval_unit_default(obj->u.meas.offset, frame,
			    DEFAULT_OFFSET);
			if (is_undef(offset))
				goto error;
			inst_meas_hint(obj, offset.n);
			break;
		case ot_iprint:
			dbg_print(obj->u.iprint.expr, frame);
			break;
		default:
			abort();
		}
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

	if (frame == frames) {
		s = expand(pkg_name, frame);
		/* s is NULL if expansion failed */
		inst_select_pkg(s ? s : "_", active);
		free(s);
	}
	inst_begin_active(active && frame == active_frame);
	ok = generate_vecs(frame, base) && generate_objs(frame, base, active);
	inst_end_active();
	return ok;
}


static int match_keys(struct frame *frame, struct coord base, int active)
{
	const struct table *table;
	const struct var *var;
	const struct value *value;
	int res;

	for (table = frame->tables; table; table = table->next) {
		value = table->curr_row->values;
		for (var = table->vars; var; var = var->next) {
			if (var->key) {
				res = var_eq(frame, var->name, value->expr);
				if (!res)
					return 1;
				if (res < 0)
					return 0;
			}
			value = value->next;
		}
	}
	return generate_items(frame, base, active);
}


static int run_loops(struct frame *frame, struct loop *loop,
    struct coord base, int active)
{
	struct num from, to;
	int n;
	int found_before, ok;

	if (!loop)
		return match_keys(frame, base, active);
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
		found_before = found;
		if (loop->found == loop->active)
			suspend_search();
		ok = run_loops(frame, loop->next, base,
		    active && loop->active == n);
		if (loop->found == loop->active)
			resume_search();
		if (!ok)
			goto fail;
		if (found_before != found)
			loop->found = n;
		n++;
	}
	loop->initialized = 0;
	loop->curr_value = UNDEF;
	if (active) {
		loop->n = from.n;
		loop->iterations = n;
	}
	return 1;

fail:
	loop->initialized = 0;
	return 0;
}


static int iterate_tables(struct frame *frame, struct table *table,
    struct coord base, int active)
{
	int found_before, ok;

	if (!table)
		return run_loops(frame, frame->loops, base, active);
	for (table->curr_row = table->rows; table->curr_row;
	    table->curr_row = table->curr_row->next) {
		found_before = found;
		if (table->found_row == table->active_row)
			suspend_search();
		ok = iterate_tables(frame, table->next, base,
		    active && table->active_row == table->curr_row);
		if (table->found_row == table->active_row)
			resume_search();
		if (!ok)
			return 0;
		if (found_before != found)
			table->found_row = table->curr_row;
	}
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
	bitset_set(frame_set, frame->n);
	frame->curr_parent = parent;
	ok = iterate_tables(frame, frame->tables, base, active);
	inst_end_frame(frame);
	bitset_clear(frame_set, frame->n);
	frame->curr_parent = NULL;
	return ok;
}


static void reset_all_loops(void)
{
	const struct frame *frame;
	struct loop *loop;

	for (frame = frames; frame; frame = frame->next)
		for (loop = frame->loops; loop; loop = loop->next)
			loop->iterations = 0;
}


static void reset_found(void)
{
	struct frame *frame;
	struct table *table;
	struct loop *loop;

	for (frame = frames; frame; frame = frame->next) {
		for (table = frame->tables; table; table = table->next)
			table->found_row = NULL;
		for (loop = frame->loops; loop; loop = loop->next)
			loop->found = -1;
		frame->found_ref = NULL;
	}
}


/*
 * Note: we don't use frame->found_ref yet. Instead, we adjust the frame
 * references with activate_item in inst.c
 */

static void activate_found(void)
{
	struct frame *frame;
	struct table *table;
	struct loop *loop;

	for (frame = frames; frame; frame = frame->next) {
		for (table = frame->tables; table; table = table->next)
			if (table->found_row)
				table->active_row = table->found_row;
		for (loop = frame->loops; loop; loop = loop->next)
			if (loop->found != -1)
				loop->active = loop->found;
		if (frame->found_ref)
			frame->active_ref = frame->found_ref;
	}
}


static int enumerate_frames(void)
{
	struct frame *frame;
	int n = 0;

	for (frame = frames; frame; frame = frame->next)
		frame->n = n++;
	return n;
}


int instantiate(void)
{
	struct coord zero = { 0, 0 };
	int n_frames;
	int ok;

	meas_start();
	inst_start();
	n_frames = enumerate_frames();
	frame_set = bitset_new(n_frames);
	instantiation_error = NULL;
	reset_all_loops();
	reset_found();
	found = 0;
	search_suspended = 0;
	ok = generate_frame(frames, zero, NULL, NULL, 1);
	if (ok && (find_vec || find_obj) && found)
		activate_found();
	find_vec = NULL;
	find_obj = NULL;
	if (ok)
		ok = link_holes(holes_linked);
	if (ok)
		ok = refine_layers(allow_overlap);
	if (ok)
		ok = instantiate_meas(n_frames);
	if (ok)
		inst_commit();
	else
		inst_revert();
	bitset_free(frame_set);
	return ok;
}


/* ----- deallocation ------------------------------------------------------ */


void obj_cleanup(void)
{
	free(pkg_name);
	while (frames) {
		delete_frame(frames);
		destroy();
	}
}
