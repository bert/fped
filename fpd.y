%{
/*
 * fpd.y - FootPrint Definition language
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

#include "util.h"
#include "error.h"
#include "expr.h"
#include "obj.h"


extern struct expr *expr_result;

static struct frame *curr_frame;
static struct table *curr_table;
static struct row *curr_row;

static struct frame *last_frame = NULL;
static struct vec *last_vec = NULL;

static struct table **next_table;
static struct loop **next_loop;
static struct vec **next_vec;
static struct obj **next_obj;

static int n_vars, n_values;


static struct frame *find_frame(const char *name)
{
	struct frame *f;

	for (f = frames; f; f = f->next)
		if (f->name == name)
			return f;
	return NULL;
}


static struct vec *find_vec(const char *name)
{
	struct vec *v;

	for (v = curr_frame->vecs; v; v = v->next)
		if (v->name == name)
			return v;
	return NULL;
}


static void set_frame(struct frame *frame)
{
	curr_frame = frame;
	next_table = &frame->tables;
	next_loop = &frame->loops;
	next_vec = &frame->vecs;
	next_obj = &frame->objs;
	last_vec = NULL;
}


static void make_var(const char *id, struct expr *expr)
{
	struct table *table;

	table = zalloc_type(struct table);
	table->vars = zalloc_type(struct var);
	table->vars->name = id;
	table->vars->frame = curr_frame;
	table->rows = zalloc_type(struct row);
	table->rows->values = zalloc_type(struct value);
	table->rows->values->expr = expr;
	table->rows->values->row = table->rows;
	*next_table = table;
	next_table = &table->next;
}


static void make_loop(const char *id, struct expr *from, struct expr *to)
{
	struct loop *loop;

	loop = alloc_type(struct loop);
	loop->var.name = id;
	loop->var.next = NULL;
	loop->var.frame = curr_frame;
	loop->from.expr = from;
	loop->from.row = NULL;
	loop->from.next = NULL;
	loop->to.expr = to;
	loop->to.row = NULL;
	loop->to.next = NULL;
	loop->next = NULL;
	loop->active = 0;
	loop->initialized = 0;
	*next_loop = loop;
	next_loop = &loop->next;
}


static struct obj *new_obj(enum obj_type type)
{
	struct obj *obj;

	obj = alloc_type(struct obj);
	obj->type = type;
	obj->next = NULL;
	return obj;
}


%}


%union {
	struct num num;
	char *str;
	const char *id;
	struct expr *expr;
	struct frame *frame;
	struct table *table;
	struct var *var;
	struct row *row;
	struct value *value;
	struct vec *vec;
	struct obj *obj;
};


%token		START_FPD START_EXPR
%token		TOK_SET TOK_LOOP TOK_FRAME TOK_TABLE TOK_VEC
%token		TOK_PAD TOK_RECT TOK_LINE TOK_CIRC TOK_ARC TOK_MEAS

%token	<num>	NUMBER
%token	<str>	STRING
%token	<id>	ID LABEL

%type	<table>	table
%type	<var>	vars var
%type	<row>	rows
%type	<value>	row value
%type	<vec>	vec base
%type	<obj>	obj
%type	<expr>	expr opt_expr add_expr mult_expr unary_expr primary_expr

%%

all:
	START_FPD fpd
	| START_EXPR expr
		{
			expr_result = $2;
		}
	;

fpd:
		{
			root_frame = zalloc_type(struct frame);
			set_frame(root_frame);
		}
	frame_defs frame_items
		{
			root_frame->prev = last_frame;
			if (last_frame)
				last_frame->next = root_frame;
			else
				frames = root_frame;
		}
	;

frame_defs:
	| frame_defs frame_def
	;

frame_def:
	TOK_FRAME ID '{'
		{
			if (find_frame($2)) {
				yyerrorf("duplicate frame \"%s\"", $2);
				YYABORT;
			}
			curr_frame = zalloc_type(struct frame);
			curr_frame->name = $2;
			set_frame(curr_frame);
			curr_frame->prev = last_frame;
			if (last_frame)
				last_frame->next = curr_frame;
			else
				frames = curr_frame;
			last_frame = curr_frame;
		}
	    frame_items '}'
		{
			set_frame(root_frame);
		}
	;

frame_items:
	| frame_item frame_items
	;

frame_item:
	table
	| TOK_SET ID '=' expr
		{
			make_var($2, $4);
		}
	| TOK_LOOP ID '=' expr ',' expr
		{
			make_loop($2, $4, $6);
		}
	| vec
	| LABEL vec
		{
			if (find_vec($1)) {
				yyerrorf("duplicate vector \"%s\"", $1);
				YYABORT;
			}
			$2->name = $1;
		}
	| obj
		{
			*next_obj = $1;
			next_obj = &$1->next;
		}
	;

table:
	TOK_TABLE
		{
			$<table>$ = zalloc_type(struct table);
			*next_table = $<table>$;
			curr_table = $<table>$;
			n_vars = 0;
		}
	    '{' vars '}' rows
		{
			$$ = $<table>2;
			$$->vars = $4;
			$$->rows = $6;
			$$->active = 0;
			next_table = &$$->next;
		}
	;

vars:
	var
		{
			$$ = $1;
		}
	| vars ',' var
		{
			struct var **walk;

			$$ = $1;
			for (walk = &$$; *walk; walk = &(*walk)->next);
			*walk = $3;
		}
	;

var:
	ID
		{
			$$ = alloc_type(struct var);
			$$->name = $1;
			$$->frame = curr_frame;
			$$->next = NULL;
			n_vars++;
		}
	;
	
	
rows:
		{
			$$ = NULL;
		}
	| '{'
		{
			$<row>$ = alloc_type(struct row);
			$<row>$->table = curr_table;
			curr_row = $<row>$;;
			n_values = 0;
		}
	    row '}'
		{
			if (n_vars != n_values) {
				yyerrorf("table has %d variables but row has "
				    "%d values", n_vars, n_values);
				YYABORT;
			}
			$<row>2->values = $3;
		}
	    rows
		{
			$$ = $<row>2;
			$$->next = $6;
		}
	;

row:
	value
		{
			$$ = $1;
		}
	| row ',' value
		{
			struct value **walk;

			$$ = $1;
			for (walk = &$$; *walk; walk = &(*walk)->next);
			*walk = $3;
		}
	;

value:
	expr
		{
			$$ = alloc_type(struct value);
			$$->expr = $1;
			$$->row = curr_row;
			$$->next = NULL;
			n_values++;
		}
	;

vec:
	TOK_VEC base '(' expr ',' expr ')'
		{
			$$ = alloc_type(struct vec);
			$$->name = NULL;
			$$->base = $2;
			if ($2)
				$2->n_refs++;
			$$->x = $4;
			$$->y = $6;
			$$->n_refs = 0;
			$$->frame = curr_frame;
			$$->next = NULL;
			last_vec = $$;
			*next_vec = $$;
			next_vec = &$$->next;
		}
	;

base:
	'@'
		{
			$$ = NULL;
		}
	| '.'
		{
			$$ = last_vec;
			if (!$$) {
				yyerrorf(". without predecessor");
				YYABORT;
			}
		}
	| ID
		{
			$$ = find_vec($1);
			if (!$$) {
				yyerrorf("unknown vector \"%s\"", $1);
				YYABORT;
			}
		}
	;

obj:
	TOK_PAD STRING base base
		{
			$$ = new_obj(ot_pad);
			$$->base = $3;
			$$->u.pad.name = $2;
			$$->u.pad.other = $4;
		}
	| TOK_RECT base base opt_expr
		{
			$$ = new_obj(ot_rect);
			$$->base = $2;
			$$->u.rect.other = $3;
			$$->u.rect.width = $4;
		}
	| TOK_LINE base base opt_expr
		{
			$$ = new_obj(ot_line);
			$$->base = $2;
			$$->u.line.other = $3;
			$$->u.line.width = $4;
		}
	| TOK_CIRC base base opt_expr
		{
			$$ = new_obj(ot_arc);
			$$->base = $2;
			$$->u.arc.start = $3;
			$$->u.arc.end = $3;
			$$->u.arc.width = $4;
		}
	| TOK_ARC base base base opt_expr
		{
			$$ = new_obj(ot_arc);
			$$->base = $2;
			$$->u.arc.start = $3;
			$$->u.arc.end = $4;
			$$->u.arc.width = $5;
		}
	| TOK_MEAS base base expr
		{
			$$ = new_obj(ot_meas);
			$$->base = $2;
			$$->u.meas.other = $3;
			$$->u.meas.offset = $4;
		}
	| TOK_FRAME ID base
		{
			$$ = new_obj(ot_frame);
			$$->base = $3;
			$$->u.frame = find_frame($2);
			if (!$$->u.frame) {
				yyerrorf("unknown frame \"%s\"", $2);
				YYABORT;
			}
		}
	;

opt_expr:
		{
			$$ = NULL;
		}
	| expr
		{
			$$ = $1;
		}
	;

expr:
	add_expr
		{
			$$ = $1;
		}
	;

add_expr:
	mult_expr
		{
			$$ = $1;
		}
	| add_expr '+' mult_expr
		{
			$$ = binary_op(op_add, $1, $3);
		}
	| add_expr '-' mult_expr
		{
			$$ = binary_op(op_sub, $1, $3);
		}
	;

mult_expr:
	unary_expr
		{
			$$ = $1;
		}
	| mult_expr '*' unary_expr
		{
			$$ = binary_op(op_mult, $1, $3);
		}
	| mult_expr '/' unary_expr
		{
			$$ = binary_op(op_div, $1, $3);
		}
	;

unary_expr:
	primary_expr
		{
			$$ = $1;
		}
	| '-' primary_expr
		{
			$$ = binary_op(op_minus, $2, NULL);
		}
	;

primary_expr:
	NUMBER
		{
			$$ = new_op(op_num);
			$$->u.num = $1;
		}
	| ID
		{
			$$ = new_op(op_var);
			$$->u.var = $1;
		}
	| '(' expr ')'
		{
			$$ = $2;
		}
	;
