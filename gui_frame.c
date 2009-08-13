/*
 * gui_frame.c - GUI, frame window
 *
 * Written 2009 by Werner Almesberger
 * Copyright 2009 by Werner Almesberger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


#include <string.h>
#include <gtk/gtk.h>

#include "util.h"
#include "error.h"
#include "dump.h"
#include "inst.h"
#include "obj.h"
#include "delete.h"
#include "unparse.h"
#include "gui_util.h"
#include "gui_style.h"
#include "gui_status.h"
#include "gui_tool.h"
#include "gui_canvas.h"
#include "gui.h"
#include "gui_frame.h"


int show_vars = 1;


/* ----- popup dispatcher -------------------------------------------------- */


static void *popup_data;


static void pop_up(GtkWidget *menu, GdkEventButton *event, void *data)
{
	popup_data = data;
	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
	    event->button, event->time);
}


/* ----- popup: frame ------------------------------------------------------ */


static GtkItemFactory *factory_frame;
static GtkWidget *popup_frame_widget;


static void popup_add_frame(void)
{
	struct frame *parent = popup_data;
	struct frame *new;

	new = zalloc_type(struct frame);
	new->name = unique("_");
	new->next = parent;
	new->prev = parent->prev;
	if (parent->prev)
		parent->prev->next = new;
	else
		frames = new;
	parent->prev = new;
	change_world();
}


static void popup_del_frame(void)
{
	struct frame *frame = popup_data;

	assert(frame != root_frame);
	delete_frame(frame);
	if (active_frame == frame)
		select_frame(root_frame);
	change_world();
}


/* @@@ merge with fpd.y */

static void popup_add_table(void)
{
	struct frame *frame = popup_data;
	struct table *table, **walk;

	table = zalloc_type(struct table);
	table->vars = zalloc_type(struct var);
	table->vars->name = unique("_");
	table->vars->frame = frame;
	table->vars->table = table;
	table->rows = zalloc_type(struct row);
	table->rows->table = table;
	table->rows->values = zalloc_type(struct value);
	table->rows->values->expr = parse_expr("0");
	table->rows->values->row = table->rows;
	table->active_row = table->rows;
	for (walk = &frame->tables; *walk; walk = &(*walk)->next);
	*walk = table;
	change_world();
}


static void popup_add_loop(void)
{
	struct frame *frame = popup_data;
	struct loop *loop, **walk;

	loop = zalloc_type(struct loop);
	loop->var.name = unique("_");
	loop->var.frame = frame;
	loop->from.expr = parse_expr("0");
	loop->to.expr = parse_expr("0");
	loop->next = NULL;
	for (walk = &frame->loops; *walk; walk = &(*walk)->next);
	*walk = loop;
	change_world();
}


static GtkItemFactoryEntry popup_frame_entries[] = {
	{ "/Add frame",		NULL,	popup_add_frame,	0, "<Item>" },
	{ "/sep0",		NULL,	NULL,		0, "<Separator>" },
	{ "/Add variable",	NULL,	popup_add_table,	0, "<Item>" },
	{ "/Add loop",		NULL,	popup_add_loop,		0, "<Item>" },
	{ "/sep1",		NULL,	NULL,		0, "<Separator>" },
	{ "/Delete frame",	NULL,	popup_del_frame,	0, "<Item>" },
	{ "/sep2",		NULL,	NULL,		0, "<Separator>" },
	{ "/Close",		NULL,	NULL,		0, "<Item>" },
	{ NULL }
};


static void pop_up_frame(struct frame *frame, GdkEventButton *event)
{
	gtk_widget_set_sensitive(
	    gtk_item_factory_get_item(factory_frame, "/Delete frame"),
	    frame != root_frame);
	pop_up(popup_frame_widget, event, frame);
}


/* ----- popup: single variable -------------------------------------------- */


static GtkItemFactory *factory_single_var;
static GtkWidget *popup_single_var_widget;



static void add_row_here(struct table *table, struct row **anchor)
{
	struct row *row;
	const struct value *walk;
	struct value *value;

	row = zalloc_type(struct row);
	row->table = table;
	/* @@@ future: adjust type */
	for (walk = table->rows->values; walk; walk = walk->next) {
		value = zalloc_type(struct value);
		value->expr = parse_expr("0");
		value->row = row;
		value->next = row->values;
		row->values = value;
	}
	row->next = *anchor;
	*anchor = row;
	change_world();
}


static void add_column_here(struct table *table, struct var **anchor)
{
	const struct var *walk;
	struct var *var;
	struct row *row;
	struct value *value;
	struct value **value_anchor;
	int n = 0, i;

	for (walk = table->vars; walk != *anchor; walk = walk->next)
		n++;
	var = zalloc_type(struct var);
	var->name = unique("_");
	var->frame = table->vars->frame;
	var->table = table;
	var->next = *anchor;
	*anchor = var;
	for (row = table->rows; row; row = row->next) {
		value_anchor = &row->values;
		for (i = 0; i != n; i++)
			value_anchor = &(*value_anchor)->next;
		value = zalloc_type(struct value);
		value->expr = parse_expr("0");
		value->row = row;
		value->next = *value_anchor;
		*value_anchor = value;
	}
	change_world();
}


static void popup_add_row(void)
{
	struct var *var = popup_data;

	add_row_here(var->table, &var->table->rows);
}


static void popup_add_column(void)
{
	struct var *var = popup_data;

	add_column_here(var->table, &var->next);
}


static void popup_del_table(void)
{
	struct var *var = popup_data;

	delete_table(var->table);
	change_world();
}


static GtkItemFactoryEntry popup_single_var_entries[] = {
	{ "/Add row",		NULL,	popup_add_row,		0, "<Item>" },
	{ "/Add column",	NULL,	popup_add_column,	0, "<Item>" },
	{ "/sep1",		NULL,	NULL,		0, "<Separator>" },
	{ "/Delete variable",	NULL,	popup_del_table,	0, "<Item>" },
	{ "/sep2",		NULL,	NULL,		0, "<Separator>" },
	{ "/Close",		NULL,	NULL,			0, "<Item>" },
	{ NULL }
};


static void pop_up_single_var(struct var *var, GdkEventButton *event)
{
	pop_up(popup_single_var_widget, event, var);
}


/* ----- popup: table variable --------------------------------------------- */


static GtkItemFactory *factory_table_var;
static GtkWidget *popup_table_var_widget;


static void popup_del_column(void)
{
	struct var *var = popup_data;
	const struct var *walk;
	int n = 0;

	for (walk = var->table->vars; walk != var; walk = walk->next)
		n++;
	delete_column(var->table, n);
	change_world();
}


static GtkItemFactoryEntry popup_table_var_entries[] = {
	{ "/Add row",		NULL,	popup_add_row,		0, "<Item>" },
	{ "/Add column",	NULL,	popup_add_column,	0, "<Item>" },
	{ "/sep1",		NULL,	NULL,		0, "<Separator>" },
	{ "/Delete table",	NULL,	popup_del_table,	0, "<Item>" },
	{ "/Delete column",	NULL,	popup_del_column,	0, "<Item>" },
	{ "/sep2",		NULL,	NULL,		0, "<Separator>" },
	{ "/Close",		NULL,	NULL,		0, "<Item>" },
	{ NULL }
};


static void pop_up_table_var(struct var *var, GdkEventButton *event)
{
	gtk_widget_set_sensitive(
	    gtk_item_factory_get_item(factory_table_var, "/Delete column"),
	    var->table->vars->next != NULL);
	pop_up(popup_table_var_widget, event, var);
}


/* ----- popup: table value ------------------------------------------------ */


static GtkItemFactory *factory_table_value;
static GtkWidget *popup_table_value_widget;


static void popup_add_column_by_value(void)
{
	struct value *value = popup_data;
	const struct value *walk;
	struct table *table = value->row->table;
	struct var *var = table->vars;

	for (walk = value->row->values; walk != value; walk = walk->next)
		var = var->next;
	add_column_here(table, &var->next);
}


static void popup_add_row_by_value(void)
{
	struct value *value = popup_data;

	add_row_here(value->row->table, &value->row->next);
}


static void popup_del_row(void)
{
	struct value *value = popup_data;
	struct table *table = value->row->table;

	delete_row(value->row);
	if (table->active_row == value->row)
		table->active_row = table->rows;
	change_world();
}


static void popup_del_column_by_value(void)
{
	struct value *value = popup_data;
	const struct value *walk;
	int n = 0;

	for (walk = value->row->values; walk != value; walk = walk->next)
		n++;
	delete_column(value->row->table, n);
	change_world();
}


static GtkItemFactoryEntry popup_table_value_entries[] = {
	{ "/Add row",		NULL,	popup_add_row_by_value,	0, "<Item>" },
	{ "/Add column",	NULL,	popup_add_column_by_value,
							0, "<Item>" },
	{ "/sep1",		NULL,	NULL,		0, "<Separator>" },
	{ "/Delete row",	NULL,	popup_del_row,		0, "<Item>" },
	{ "/Delete column",	NULL,	popup_del_column_by_value,
								0, "<Item>" },
	{ "/sep2",		NULL,	NULL,		0, "<Separator>" },
	{ "/Close",		NULL,	NULL,		0, "<Item>" },
	{ NULL }
};


static void pop_up_table_value(struct value *value, GdkEventButton *event)
{
	gtk_widget_set_sensitive(
	    gtk_item_factory_get_item(factory_table_value, "/Delete row"),
	    value->row->table->rows->next != NULL);
	gtk_widget_set_sensitive(
	    gtk_item_factory_get_item(factory_table_value, "/Delete column"),
	    value->row->table->vars->next != NULL);
	pop_up(popup_table_value_widget, event, value);
}


/* ----- popup: loop ------------------------------------------------------- */


static GtkItemFactory *factory_loop_var;
static GtkWidget *popup_loop_var_widget;


static void popup_del_loop(void)
{
	struct loop *loop = popup_data;

	delete_loop(loop);
	change_world();
}


static GtkItemFactoryEntry popup_loop_var_entries[] = {
	{ "/Delete loop",	NULL,	popup_del_loop,		0, "<Item>" },
	{ "/sep2",		NULL,	NULL,		0, "<Separator>" },
	{ "/Close",		NULL,	NULL,		0, "<Item>" },
	{ NULL }
};


static void pop_up_loop_var(struct loop *loop, GdkEventButton *event)
{
	pop_up(popup_loop_var_widget, event, loop);
}


/* ----- make popups ------------------------------------------------------- */


static GtkWidget *make_popup(const char *name, GtkItemFactory **factory,
    GtkItemFactoryEntry *entries)
{
	GtkWidget *popup;
	int n;

	n = 0;
	for (n = 0; entries[n].path; n++);

	*factory = gtk_item_factory_new(GTK_TYPE_MENU, name, NULL);
	gtk_item_factory_create_items(*factory, n, entries, NULL);
	popup = gtk_item_factory_get_widget(*factory, name);
	return popup;
}


void make_popups(void)
{
	popup_frame_widget = make_popup("<FpedFramePopUp>",
	    &factory_frame, popup_frame_entries);
	popup_single_var_widget = make_popup("<FpedSingleVarPopUp>",
	    &factory_single_var, popup_single_var_entries);
	popup_table_var_widget = make_popup("<FpedTableVarPopUp>",
	    &factory_table_var, popup_table_var_entries);
	popup_table_value_widget = make_popup("<FpedTableValusPopUp>",
	    &factory_table_value, popup_table_value_entries);
	popup_loop_var_widget = make_popup("<FpedLoopVarPopUp>",
	    &factory_loop_var, popup_loop_var_entries);
}


/* ----- variable list ----------------------------------------------------- */


static void add_sep(GtkWidget *box, int size)
{
	GtkWidget *sep;
	GdkColor black = { 0, 0, 0, 0 };

	sep = gtk_drawing_area_new();
	gtk_box_pack_start(GTK_BOX(box), sep, FALSE, TRUE, size);
	gtk_widget_modify_bg(sep, GTK_STATE_NORMAL, &black);
}


/* ----- variable name editor ---------------------------------------------- */


static int find_var_in_frame(const struct frame *frame, const char *name)
{
	const struct table *table;
	const struct loop *loop;
	const struct var *var;

	for (table = frame->tables; table; table = table->next)
		for (var = table->vars; var; var = var->next)
			if (!strcmp(var->name, name))
				return 1;
	for (loop = frame->loops; loop; loop = loop->next)
		if (!strcmp(loop->var.name, name))
			return 1;
	return 0;
}


static int validate_var_name(const char *s, void *ctx)
{
	struct var *var = ctx;

	if (!is_id(s))
		return 0;
	return !find_var_in_frame(var->frame, s);
}


static void unselect_var(void *data)
{
	struct var *var = data;

	label_in_box_bg(var->widget, COLOR_VAR_PASSIVE);
}


static void edit_var(struct var *var)
{
	inst_select_outside(var, unselect_var);
	label_in_box_bg(var->widget, COLOR_VAR_EDITING);
	status_set_type_entry("name =");
	status_set_name("%s", var->name);
	edit_nothing();
	edit_unique(&var->name, validate_var_name, var);
}


/* ----- value editor ------------------------------------------------------ */


static void unselect_value(void *data)
{
	struct value *value = data;

	label_in_box_bg(value->widget,
	    value->row && value->row->table->active_row == value->row ?
	     COLOR_CHOICE_SELECTED : COLOR_EXPR_PASSIVE);
}


static void edit_value(struct value *value)
{
	inst_select_outside(value, unselect_value);
	label_in_box_bg(value->widget, COLOR_EXPR_EDITING);
	edit_nothing();
	edit_expr(&value->expr);
}


/* ----- activator --------------------------------------------------------- */


static GtkWidget *add_activator(GtkWidget *hbox, int active,
    gboolean (*cb)(GtkWidget *widget, GdkEventButton *event, gpointer data),
    gpointer user, const char *fmt, ...)
{
	GtkWidget *label;
	va_list ap;
	char buf[100];

	va_start(ap, fmt);
	vsprintf(buf, fmt, ap);
	va_end(ap);
	label = label_in_box_new(buf);
	gtk_misc_set_padding(GTK_MISC(label), 2, 2);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0);
	label_in_box_bg(label,
	    active ? COLOR_CHOICE_SELECTED : COLOR_CHOICE_UNSELECTED);
	gtk_box_pack_start(GTK_BOX(hbox), box_of_label(label),
	    FALSE, FALSE, 2);
	g_signal_connect(G_OBJECT(box_of_label(label)),
	    "button_press_event", G_CALLBACK(cb), user);
	return label;
}


/* ----- assignments ------------------------------------------------------- */


static gboolean assignment_var_select_event(GtkWidget *widget,
    GdkEventButton *event, gpointer data)
{
	struct var *var = data;

	switch (event->button) {
	case 1:
		edit_nothing();
		edit_var(var);
		break;
	case 3:
		pop_up_single_var(var, event);
		break;
	}
	return TRUE;
}


static gboolean assignment_value_select_event(GtkWidget *widget,
    GdkEventButton *event, gpointer data)
{
	struct value *value = data;

	switch (event->button) {
	case 1:
		edit_nothing();
		edit_value(value);
		break;
	}
	return TRUE;
}


static void build_assignment(GtkWidget *vbox, struct frame *frame,
    struct table *table)
{
	GtkWidget *hbox, *field;
	char *expr;

	if (!table->vars || table->vars->next)
		return;
	if (!table->rows || table->rows->next)
		return;

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

	field = label_in_box_new(table->vars->name);
	gtk_box_pack_start(GTK_BOX(hbox), box_of_label(field), FALSE, FALSE, 0);
	label_in_box_bg(field, COLOR_VAR_PASSIVE);
	table->vars->widget = field;
	g_signal_connect(G_OBJECT(box_of_label(field)),
	    "button_press_event",
	    G_CALLBACK(assignment_var_select_event), table->vars);

	gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(" = "),
	    FALSE, FALSE, 0);

	expr = unparse(table->rows->values->expr);
	field = label_in_box_new(expr);
	free(expr);
	gtk_box_pack_start(GTK_BOX(hbox), box_of_label(field), FALSE, FALSE, 0);
	label_in_box_bg(field, COLOR_EXPR_PASSIVE);
	table->rows->values->widget = field;
	g_signal_connect(G_OBJECT(box_of_label(field)),
	    "button_press_event",
	    G_CALLBACK(assignment_value_select_event), table->rows->values);
}


/* ----- tables ------------------------------------------------------------ */


static void select_row(struct row *row)
{
	struct table *table = row->table;
	struct value *value;

	for (value = table->active_row->values; value; value = value->next)
		label_in_box_bg(value->widget, COLOR_ROW_UNSELECTED);
	table->active_row = row;
	for (value = table->active_row->values; value; value = value->next)
		label_in_box_bg(value->widget, COLOR_ROW_SELECTED);
}


static gboolean table_var_select_event(GtkWidget *widget,
    GdkEventButton *event, gpointer data)
{
	struct var *var = data;

	switch (event->button) {
	case 1:
		edit_nothing();
		edit_var(var);
		break;
	case 3:
		pop_up_table_var(var, event);
		break;
	}
	return TRUE;
}


static gboolean table_value_select_event(GtkWidget *widget,
    GdkEventButton *event, gpointer data)
{
	struct value *value = data;

	switch (event->button) {
	case 1:
		if (!value->row ||
		    value->row->table->active_row == value->row) {
			edit_nothing();
			edit_value(value);
		} else {
			select_row(value->row);
			change_world();
		}
		break;
	case 3:
		pop_up_table_value(value, event);
		break;
	}
	return TRUE;
}


static void build_table(GtkWidget *vbox, struct frame *frame,
    struct table *table)
{
	GtkWidget *tab, *field;
	GtkWidget *evbox, *align;
	struct var *var;
	struct row *row;
	struct value *value;
	int n_vars = 0, n_rows = 0;
	char *expr;
	GdkColor col;

	for (var = table->vars; var; var = var->next)
		n_vars++;
	for (row = table->rows; row; row = row->next)
		n_rows++;

	if (n_vars == 1 && n_rows == 1)
		return;

	evbox = gtk_event_box_new();
	align = gtk_alignment_new(0, 0, 0, 0);
	gtk_container_add(GTK_CONTAINER(align), evbox);
	gtk_box_pack_start(GTK_BOX(vbox), align, FALSE, FALSE, 0);

	tab = gtk_table_new(n_rows+1, n_vars, FALSE);
	gtk_container_add(GTK_CONTAINER(evbox), tab);
	col = get_color(COLOR_VAR_TABLE_SEP);
	gtk_widget_modify_bg(GTK_WIDGET(evbox),
	    GTK_STATE_NORMAL, &col);

	gtk_table_set_row_spacings(GTK_TABLE(tab), 1);
	gtk_table_set_col_spacings(GTK_TABLE(tab), 1);

	n_vars = 0;
	for (var = table->vars; var; var = var->next) {
		field = label_in_box_new(var->name);
		gtk_table_attach_defaults(GTK_TABLE(tab), box_of_label(field),
		    n_vars, n_vars+1, 0, 1);
		label_in_box_bg(field, COLOR_VAR_PASSIVE);
		g_signal_connect(G_OBJECT(box_of_label(field)),
		    "button_press_event",
		    G_CALLBACK(table_var_select_event), var);
		var->widget = field;
		n_vars++;
	}
	n_rows = 0;
	for (row = table->rows; row; row = row->next) {
		n_vars = 0;
		for (value = row->values; value; value = value->next) {
			expr = unparse(value->expr);
			field = label_in_box_new(expr);
			free(expr);
			gtk_table_attach_defaults(GTK_TABLE(tab),
			    box_of_label(field),
			    n_vars, n_vars+1,
			    n_rows+1, n_rows+2);
			label_in_box_bg(field, table->active_row == row ?
			    COLOR_ROW_SELECTED : COLOR_ROW_UNSELECTED);
			g_signal_connect(G_OBJECT(box_of_label(field)),
			    "button_press_event",
			    G_CALLBACK(table_value_select_event), value);
			value->widget = field;
			n_vars++;
		}
		n_rows++;
	}
}


/* ----- loops ------------------------------------------------------------- */


static gboolean loop_var_select_event(GtkWidget *widget,
    GdkEventButton *event, gpointer data)
{
	struct loop *loop = data;

	switch (event->button) {
	case 1:
		edit_nothing();
		edit_var(&loop->var);
		break;
	case 3:
		pop_up_loop_var(loop, event);
		break;
	}
	return TRUE;
}


static gboolean loop_from_select_event(GtkWidget *widget,
    GdkEventButton *event, gpointer data)
{
	struct loop *loop = data;

	switch (event->button) {
	case 1:
		edit_nothing();
		edit_value(&loop->from);
		break;
	}
	return TRUE;
}


static gboolean loop_to_select_event(GtkWidget *widget,
    GdkEventButton *event, gpointer data)
{
	struct loop *loop = data;

	switch (event->button) {
	case 1:
		edit_nothing();
		edit_value(&loop->to);
		break;
	}
	return TRUE;
}


static gboolean loop_select_event(GtkWidget *widget, GdkEventButton *event,
    gpointer data)
{
	struct loop *loop = data;

	switch (event->button) {
	case 1:
		loop->active =
		    (long) gtk_object_get_data(GTK_OBJECT(widget), "value");
		change_world();
		break;
	}
	return TRUE;
}


static gboolean loop_scroll_event(GtkWidget *widget, GdkEventScroll *event,
    gpointer data)
{
	struct loop *loop = data;

	switch (event->direction) {
	case GDK_SCROLL_UP:
		if (loop->active < loop->iterations-1) {
			loop->active++;
			change_world();
		}
		break;
	case GDK_SCROLL_DOWN:
		if (loop->active) {
			loop->active--;
			change_world();
		}
		break;
	default:
		/* ignore */;
	}
	return TRUE;
}


static void build_loop(GtkWidget *vbox, struct frame *frame,
    struct loop *loop)
{
	GtkWidget *hbox, *field, *label;
	char *expr;
	int i;

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

	field = label_in_box_new(loop->var.name);
	gtk_box_pack_start(GTK_BOX(hbox), box_of_label(field), FALSE, FALSE, 0);
	label_in_box_bg(field, COLOR_VAR_PASSIVE);
	if (instantiation_error == loop)
		label_in_box_fg(field, COLOR_ITEM_ERROR);
	g_signal_connect(G_OBJECT(box_of_label(field)),
	    "button_press_event",
	    G_CALLBACK(loop_var_select_event), loop);
	loop->var.widget = field;

	gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(" = "),
	    FALSE, FALSE, 0);

	expr = unparse(loop->from.expr);
	field = label_in_box_new(expr);
	free(expr);
	gtk_box_pack_start(GTK_BOX(hbox), box_of_label(field), FALSE, FALSE, 0);
	label_in_box_bg(field, COLOR_EXPR_PASSIVE);
	g_signal_connect(G_OBJECT(box_of_label(field)),
	    "button_press_event",
	    G_CALLBACK(loop_from_select_event), loop);
	loop->from.widget = field;

	gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(" ... "),
	    FALSE, FALSE, 0);

	expr = unparse(loop->to.expr);
	field = label_in_box_new(expr);
	free(expr);
	gtk_box_pack_start(GTK_BOX(hbox), box_of_label(field), FALSE, FALSE, 0);
	label_in_box_bg(field, COLOR_EXPR_PASSIVE);
	g_signal_connect(G_OBJECT(box_of_label(field)),
	    "button_press_event",
	    G_CALLBACK(loop_to_select_event), loop);
	loop->to.widget = field;

	gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(" ("),
	    FALSE, FALSE, 0);

	for (i = 0; i != loop->iterations; i++) {
		label = add_activator(hbox, loop->active == i,
		    loop_select_event, loop, "%d", i);
		gtk_object_set_data(GTK_OBJECT(box_of_label(label)), "value",
		    (gpointer) (long) i);

		g_signal_connect(G_OBJECT(box_of_label(label)),
		    "scroll_event",
		    G_CALLBACK(loop_scroll_event), loop);
	}

	gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(")"),
	    FALSE, FALSE, 0);
}


/* ----- the list of variables, tables, and loops -------------------------- */


static GtkWidget *build_vars(struct frame *frame)
{
	GtkWidget *vbox;
	struct table *table;
	struct loop *loop;

	vbox = gtk_vbox_new(FALSE, 0);
	for (table = frame->tables; table; table = table->next) {
		add_sep(vbox, 3);
		build_assignment(vbox, frame, table);
		build_table(vbox, frame, table);
	}
	for (loop = frame->loops; loop; loop = loop->next) {
		add_sep(vbox, 3);
		build_loop(vbox, frame, loop);
	}
	return vbox;
}


/* ----- items ------------------------------------------------------------- */


static void set_item_color(struct inst *inst, const char *color)
{
	GtkWidget *label;

	if (inst->vec)
		label = inst->vec->list_widget;
	else
		label = inst->obj->list_widget;
	label_in_box_bg(box_of_label(label), color);
}


void gui_frame_select_inst(struct inst *inst)
{
	set_item_color(inst, COLOR_ITEM_SELECTED);
}


void gui_frame_deselect_inst(struct inst *inst)
{
	set_item_color(inst, COLOR_ITEM_NORMAL);
}


static gboolean item_select_vec(GtkWidget *widget, GdkEventButton *event,
     gpointer data)
{
	struct vec *vec = data;

	switch (event->button) {
	case 1:
		inst_select_vec(vec);
		redraw();
		break;
	}
	return TRUE;
}


static gboolean item_select_obj(GtkWidget *widget, GdkEventButton *event,
     gpointer data)
{
	struct obj *obj = data;

	switch (event->button) {
	case 1:
		inst_select_obj(obj);
		redraw();
		break;
	}
	return TRUE;
}


static GtkWidget *item_label(GtkWidget *tab, char *s, int col, int row,
    gboolean (*cb)(GtkWidget *widget, GdkEventButton *event, gpointer data),
    gpointer data)
{
	GtkWidget *label;

	label = label_in_box_new(s);
	gtk_misc_set_padding(GTK_MISC(label), 0, 0);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0);
	gtk_widget_modify_font(label, item_list_font);
	gtk_table_attach_defaults(GTK_TABLE(tab), box_of_label(label),
	    col, col+1, row, row+1);
	label_in_box_bg(box_of_label(label), COLOR_ITEM_NORMAL);

	if (cb)
		g_signal_connect(G_OBJECT(box_of_label(label)),
		    "button_press_event", G_CALLBACK(cb), data);

	free(s);
	return label;
}


static GtkWidget *build_items(struct frame *frame)
{
	GtkWidget *vbox, *hbox, *tab;
	struct order *order, *item;
	struct vec *vec;
	struct obj *obj;
	int n;
	char *s, *t;

	n = 0;
	for (vec = frame->vecs; vec; vec = vec->next)
		n++;
	for (obj = frame->objs; obj; obj = obj->next)
		if (obj->type != ot_meas)
			n++;

	vbox = gtk_vbox_new(FALSE, 0);
	add_sep(vbox, 3);

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

	tab = gtk_table_new(n, 2, FALSE);
	gtk_box_pack_start(GTK_BOX(hbox), tab, FALSE, FALSE, 0);

	order = order_frame(frame);
	n = 0;
	for (item = order; item->vec || item->obj; item++) {
		if (item->obj) {
			s = print_obj(item->obj, item->vec);
			item->obj->list_widget = item_label(tab, s, 1, n,
			    item_select_obj, item->obj);
			if (item->obj == instantiation_error)
				label_in_box_fg(item->obj->list_widget,
				    COLOR_ITEM_ERROR);
		} else {
			s = print_label(item->vec);
			t = stralloc_printf("%s: ", s);
			free(s);
			item_label(tab, t, 0, n, NULL, NULL);

			s = print_vec(item->vec);
			item->vec->list_widget = item_label(tab, s, 1, n,
			    item_select_vec, item->vec);
			if (item->vec == instantiation_error)
				label_in_box_fg(item->vec->list_widget,
				    COLOR_ITEM_ERROR);
		}
		n++;
        }
        free(order);

	return vbox;
}


static GtkWidget *build_meas(struct frame *frame)
{
	GtkWidget *vbox, *hbox, *tab;
	struct obj *obj;
	int n;
	char *s;

	n = 0;
	for (obj = frame->objs; obj; obj = obj->next)
		if (obj->type == ot_meas)
			n++;

	vbox = gtk_vbox_new(FALSE, 0);
	add_sep(vbox, 3);

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

	tab = gtk_table_new(n, 2, FALSE);
	gtk_box_pack_start(GTK_BOX(hbox), tab, FALSE, FALSE, 0);

	n = 0;
	for (obj = frame->objs; obj; obj = obj->next) {
		if (obj->type != ot_meas)
			continue;
		s = print_meas(obj);
		obj->list_widget = item_label(tab, s, 0, n,
		    item_select_obj, obj);
		if (obj == instantiation_error)
			label_in_box_fg(obj->list_widget, COLOR_ITEM_ERROR);
		n++;
        }

	return vbox;
}


/* ----- part name --------------------------------------------------------- */


static int validate_part_name(const char *s, void *ctx)
{
	if (!*s)
		return 0;
	while (*s)
		if (!is_id_char(*s++, 0))
			return 0;
	return 1;
}

static void unselect_part_name(void *data)
{
	GtkWidget *widget = data;

	label_in_box_bg(widget, COLOR_PART_NAME);
}


static gboolean part_name_edit_event(GtkWidget *widget, GdkEventButton *event,
    gpointer data)
{
	switch (event->button) {
	case 1:
		inst_select_outside(widget, unselect_part_name);
		label_in_box_bg(widget, COLOR_PART_NAME_EDITING);
		status_set_type_entry("part =");
		status_set_name("%s", part_name);
		edit_nothing();
		edit_name(&part_name, validate_part_name, NULL);
		break;
	}
	return TRUE;
}


static GtkWidget *build_part_name(void)
{
	GtkWidget *label;

	label = label_in_box_new(part_name);
	gtk_misc_set_padding(GTK_MISC(label), 2, 2);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0);

	label_in_box_bg(label, COLOR_PART_NAME);

	g_signal_connect(G_OBJECT(box_of_label(label)),
	    "button_press_event", G_CALLBACK(part_name_edit_event), NULL);

	return box_of_label(label);
}


/* ----- frame labels ------------------------------------------------------ */


static int validate_frame_name(const char *s, void *ctx)
{
	struct frame *f;

	if (!is_id(s))
		return 0;
	for (f = frames; f; f = f->next)
		if (f->name && !strcmp(f->name, s))
			return 0;
	return 1;
}


static void unselect_frame(void *data)
{
	struct frame *frame= data;

	/*
	 * "unselect" means in this context that the selection has moved
	 * elsewhere. However, this does not necessarily change the frame.
	 * (And, in fact, since we rebuild the frame list anyway, the color
	 * change here doesn't matter if selecting a different frame.)
	 * So we revert from "editing" to "selected".
	 */
	label_in_box_bg(frame->label, COLOR_FRAME_SELECTED);
}


static void edit_frame(struct frame *frame)
{
	inst_select_outside(frame, unselect_frame);
	label_in_box_bg(frame->label, COLOR_FRAME_EDITING);
	status_set_type_entry("name =");
	status_set_name("%s", frame->name);
	edit_nothing();
	edit_unique(&frame->name, validate_frame_name, frame);
}


void select_frame(struct frame *frame)
{
	if (active_frame)
		label_in_box_bg(active_frame->label, COLOR_FRAME_UNSELECTED);
	active_frame = frame;
	tool_frame_update();
	change_world();
}


static gboolean frame_select_event(GtkWidget *widget, GdkEventButton *event,
    gpointer data)
{
	struct frame *frame = data;

	switch (event->button) {
	case 1:
		if (active_frame != frame)
			select_frame(frame);
		else {
			if (active_frame->name) {
				edit_nothing();
				edit_frame(frame);
			}
		}
		break;
	case 3:
		pop_up_frame(frame, event);
		break;
	}
	return TRUE;
}


static GtkWidget *build_frame_label(struct frame *frame)
{
	GtkWidget *label;

	label = label_in_box_new(frame->name ? frame->name : "(root)");
	gtk_misc_set_padding(GTK_MISC(label), 2, 2);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0);

	label_in_box_bg(label, active_frame == frame ?
	    COLOR_FRAME_SELECTED : COLOR_FRAME_UNSELECTED);

	g_signal_connect(G_OBJECT(box_of_label(label)),
	    "button_press_event", G_CALLBACK(frame_select_event), frame);
	frame->label = label;

	return box_of_label(label);
}


/* ----- frame references -------------------------------------------------- */


static gboolean frame_ref_select_event(GtkWidget *widget, GdkEventButton *event,
    gpointer data)
{
	struct obj *obj = data;

	switch (event->button) {
	case 1:
		obj->u.frame.ref->active_ref = data;
		change_world();
		break;
	}
	return TRUE;
}


static GtkWidget *build_frame_refs(const struct frame *frame)
{
	GtkWidget *hbox;
	struct obj *obj;

	hbox = gtk_hbox_new(FALSE, 0);
	for (obj = frame->objs; obj; obj = obj->next)
		if (obj->type == ot_frame && obj->u.frame.ref == active_frame)
			add_activator(hbox,
			    obj == obj->u.frame.ref->active_ref,
			    frame_ref_select_event, obj,
			    "%d", obj->u.frame.lineno);
	return hbox;
}


/* ----- frames ------------------------------------------------------------ */


void build_frames(GtkWidget *vbox)
{
	struct frame *frame;
	GtkWidget *hbox, *tab, *label, *refs, *vars, *items, *meas;
	int n = 0;

	destroy_all_children(GTK_CONTAINER(vbox));
	for (frame = frames; frame; frame = frame->next)
		n++;

	hbox = gtk_hbox_new(FALSE, 0);

	tab = gtk_table_new(n*2+3, 2, FALSE);
	gtk_table_set_row_spacings(GTK_TABLE(tab), 1);
	gtk_table_set_col_spacings(GTK_TABLE(tab), 1);

	gtk_box_pack_start(GTK_BOX(hbox), tab, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

	label = build_part_name();
	gtk_table_attach_defaults(GTK_TABLE(tab), label, 0, 1, 0, 1);

	n = 0;
	for (frame = root_frame; frame; frame = frame->prev) {
		label = build_frame_label(frame);
		gtk_table_attach_defaults(GTK_TABLE(tab), label,
		    0, 1, n*2+1, n*2+2);

		refs = build_frame_refs(frame);
		gtk_table_attach_defaults(GTK_TABLE(tab), refs,
		    1, 2, n*2+1, n*2+2);

		if (show_vars) {
			vars = build_vars(frame);
			gtk_table_attach_defaults(GTK_TABLE(tab), vars,
			    1, 2, n*2+2, n*2+3);
		} else {
			items = build_items(frame);
			gtk_table_attach_defaults(GTK_TABLE(tab), items,
			    1, 2, n*2+2, n*2+3);
		}

		n++;
	}

	if (!show_vars) {
		meas = build_meas(root_frame);
		gtk_table_attach_defaults(GTK_TABLE(tab), meas,
		    1, 2, n*2+2, n*2+3);
	}

	gtk_widget_show_all(hbox);
}
