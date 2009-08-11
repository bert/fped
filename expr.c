/*
 * expr.c - Expressions and values
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
#include <math.h>

#include "util.h"
#include "error.h"
#include "obj.h"
#include "unparse.h"
#include "expr.h"


struct num undef = { .type = nt_none };


/* ----- error reporting --------------------------------------------------- */


void fail_expr(const struct expr *expr)
{
	char *s;

	s = unparse(expr);
	fail("in \"%s\" at line %d", s, expr->lineno);
	free(s);
}


/* ----- unit conversion --------------------------------------------------- */


const char *str_unit(struct num n)
{
	if (n.exponent == 0)
		return "";
	if (n.type == nt_mm) {
		switch (n.exponent) {
		case -2:
			return "mm^-2";
		case -1:
			return "mm^-1";
		case 1:
			return "mm";
		case 2:
			return "mm^2";
		default:
			abort();
		}
	}
	if (n.type == nt_mil) {
		switch (n.exponent) {
		case -2:
			return "mil^(-2)";
		case -1:
			return "mil^(-1)";
		case 1:
			return "mil";
		case 2:
			return "mil^2";
		default:
			abort();
		}
	}
	abort();
}


int to_unit(struct num *n)
{
	if (!is_distance(*n)) {
		fail("%s^%d is not a distance",
		    n->type == nt_mm ? "mm" : n->type == nt_mil ? "mil" : "?",
		    n->exponent);
		return 0;
	}
	switch (n->type) {
	case nt_mil:
		n->n = mil_to_units(n->n);
		break;
	case nt_mm:
		n->n = mm_to_units(n->n);
		break;
	default:
		abort();
	}
	return 1;
}


/* ----- primary expressions ----------------------------------------------- */


struct num op_string(const struct expr *self, const struct frame *frame)
{
	fail("cannot evaluate string");
	return undef;
}


struct num op_num(const struct expr *self, const struct frame *frame)
{
	return self->u.num;
}


struct num eval_var(const struct frame *frame, const char *name)
{
	const struct table *table;
	const struct loop *loop;
	const struct value *value;
	struct var *var;
	struct num res;

	for (table = frame->tables; table; table = table->next) {
		value = table->curr_row->values;
		for (var = table->vars; var; var = var->next) {
			if (var->name == name) {
				if (var->visited) {
					fail("recursive evaluation through "
					    "\"%s\"", name);
					return undef;
				}
				var->visited = 1;
				res = eval_num(value->expr, frame);
				var->visited = 0;
				return res;
				
			}
			value = value->next;
		}
	}
	for (loop = frame->loops; loop; loop = loop->next)
		if (loop->var.name == name) {
			if (!loop->initialized) {
				fail("uninitialized loop \"%s\"", name);
				return undef;
			}
			return make_num(loop->curr_value);
		}
	if (frame->curr_parent)
		return eval_var(frame->curr_parent, name);
	return undef;
}


static const char *eval_string_var(const struct frame *frame, const char *name)
{
	const struct table *table;
	const struct loop *loop;
	const struct value *value;
	struct var *var;
	const char *res;

	for (table = frame->tables; table; table = table->next) {
		value = table->curr_row->values;
		for (var = table->vars; var; var = var->next) {
			if (var->name == name) {
				if (var->visited)
					return NULL;
				var->visited = 1;
				res = eval_str(value->expr, frame);
				var->visited = 0;
				return res;
				
			}
			value = value->next;
		}
	}
	for (loop = frame->loops; loop; loop = loop->next)
		if (loop->var.name == name)
			return NULL;
	if (frame->curr_parent)
		return eval_string_var(frame->curr_parent, name);
	return NULL;
}


struct num op_var(const struct expr *self, const struct frame *frame)
{
	struct num res;

	res = eval_var(frame, self->u.var);
	if (is_undef(res))
		fail("undefined variable \"%s\"", self->u.var);
	return res;
}


/* ----- arithmetic -------------------------------------------------------- */


static struct num compatible_sum(struct num *a, struct num *b)
{
	struct num res;

	if (a->type != b->type) {
		if (a->type == nt_mil) {
			a->type = nt_mm;
			a->n = mil_to_mm(a->n, a->exponent);
		}
		if (b->type == nt_mil) {
			b->type = nt_mm;
			b->n = mil_to_mm(b->n, a->exponent);
		}
	}
	if (a->exponent != b->exponent) {
		fail("incompatible exponents (%d, %d)",
		    a->exponent, b->exponent);
		return undef;
	}
	res.type = a->type;
	res.exponent = a->exponent;
	res.n = 0; /* keep gcc happy */
	return res;
}


static struct num compatible_mult(struct num *a, struct num *b,
    int exponent)
{
	struct num res;

	if (a->type != b->type) {
		if (a->type == nt_mil) {
			a->type = nt_mm;
			a->n = mil_to_mm(a->n, a->exponent);
		}
		if (b->type == nt_mil) {
			b->type = nt_mm;
			b->n = mil_to_mm(b->n, b->exponent);
		}
	}
	res.type = a->type;
	res.exponent = exponent;
	res.n = 0; /* keep gcc happy */
	return res;
}


struct num op_minus(const struct expr *self, const struct frame *frame)
{
	struct num res;

	res = eval_num(self->u.op.a, frame);
	if (!is_undef(res))
		res.n = -res.n;
	return res;
}


#define	BINARY						\
	struct num a, b, res;				\
							\
	a = eval_num(self->u.op.a, frame);		\
	if (is_undef(a))				\
		return undef;				\
	b = eval_num(self->u.op.b, frame);		\
	if (is_undef(b))				\
		return undef;


struct num op_add(const struct expr *self, const struct frame *frame)
{
	BINARY;
	res = compatible_sum(&a, &b);
	if (is_undef(res))
		return undef;
	res.n = a.n+b.n;
	return res;
}


struct num op_sub(const struct expr *self, const struct frame *frame)
{
	BINARY;
	res = compatible_sum(&a, &b);
	if (is_undef(res))
		return undef;
	res.n = a.n-b.n;
	return res;
}


struct num op_mult(const struct expr *self, const struct frame *frame)
{
	BINARY;
	res = compatible_mult(&a, &b, a.exponent+b.exponent);
	res.n = a.n*b.n;
	return res;
}


struct num op_div(const struct expr *self, const struct frame *frame)
{
	BINARY;
	if (!b.n) {
		fail("Division by zero");
		return undef;
	}
	res = compatible_mult(&a, &b, a.exponent-b.exponent);
	res.n = a.n/b.n;
	return res;
}


/* ----- expression construction ------------------------------------------- */


struct expr *new_op(op_type op)
{
	struct expr *expr;

	expr = alloc_type(struct expr);
	expr->op = op;
	expr->lineno = lineno;
	return expr;
}


struct expr *binary_op(op_type op, struct expr *a, struct expr *b)
{
	struct expr *expr;

	expr = new_op(op);
	expr->u.op.a = a;
	expr->u.op.b = b;
	return expr;
}


const char *eval_str(const struct expr *expr, const struct frame *frame)
{
	if (expr->op == op_string)
		return expr->u.str;
	if (expr->op == op_var)
		return eval_string_var(frame, expr->u.var);
	return NULL;
}


struct num eval_num(const struct expr *expr, const struct frame *frame)
{
	return expr->op(expr, frame);
}


/* ----- string expansion -------------------------------------------------- */


char *expand(const char *name, const struct frame *frame)
{
	int len = strlen(name);
	char *buf = alloc_size(len+1);
	char num_buf[100]; /* enough :-) */
	const char *s, *s0;
	char *var;
	const char *var_unique, *value_string;
	struct num value;
	int i, value_len;

	i = 0;
	for (s = name; *s; s++) {
		if (*s != '$') {
			buf[i++] = *s;
			continue;
		}
		s0 = ++s;
		if (*s != '{') {
			while (is_id_char(*s, s == s0))
				s++;
			if (s == s0)
				goto invalid;
			var = strnalloc(s0, s-s0);
			len -= s-s0+1;
			s--;
		} else {
			s++;
			while (*s != '}') {
				if (!*s) {
					fail("unfinished \"${...}\"");
					goto fail;
				}
				if (!is_id_char(*s, s == s0+1))
					goto invalid;
				s++;
			}
			var = strnalloc(s0+1, s-s0-1);
			len -= s-s0+2;
		}
		if (!frame)
			continue;
		var_unique = unique(var);
		free(var);
		value_string = eval_string_var(frame, var_unique);
		if (value_string)
			value_len = strlen(value_string);
		else {
			value = eval_var(frame, var_unique);
			if (is_undef(value)) {
				fail("undefined variable \"%s\"", var_unique);
				goto fail;
			}
			value_len = snprintf(num_buf, sizeof(num_buf), "%lg%s",
			    value.n, str_unit(value));
			value_string = num_buf;
		}
		len += value_len;
		buf = realloc(buf, len+1);
		if (!buf)
			abort();
		strcpy(buf+i, value_string);
		i += value_len;
	}
	buf[i] = 0;
	return buf;

invalid:
	fail("invalid character in variable name");
fail:
	free(buf);
	return NULL;
}


/* ----- make a number -----------------------------------------------------*/


struct expr *new_num(struct num num)
{
	struct expr *expr;

	expr = new_op(op_num);
	expr->u.num = num;
	return expr;
}


/* ----- expression-only parser -------------------------------------------- */


void scan_expr(const char *s);
int yyparse(void);


struct expr *expr_result;


struct expr *parse_expr(const char *s)
{
	scan_expr(s);
	return yyparse() ? NULL : expr_result;
}


static void vacate_op(struct expr *expr)
{
	if (expr->op == op_num || expr->op == op_string ||
	    expr->op == op_var)
		return;
	if (expr->op == op_minus) {
		free_expr(expr->u.op.a);
		return;
	}
	if (expr->op == op_add || expr->op == op_sub ||
	    expr->op == op_mult || expr->op == op_div) {
		free_expr(expr->u.op.a);
		free_expr(expr->u.op.b);
		return;
	}
	abort();
}


void free_expr(struct expr *expr)
{
	vacate_op(expr);
	free(expr);
}
