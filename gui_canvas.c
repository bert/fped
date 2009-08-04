/*
 * gui_canvas.c - GUI, canvas
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
#include <gtk/gtk.h>

#include "obj.h"
#include "inst.h"
#include "gui_inst.h"
#include "gui_style.h"
#include "gui_status.h"
#include "gui_tools.h"
#include "gui.h"
#include "gui_canvas.h"


static struct draw_ctx ctx;
static struct coord curr_pos;
static struct coord user_origin = { 0, 0 };

static int dragging = 0;
static struct coord drag_start;


/* ----- status display ---------------------------------------------------- */


static void update_zoom(void)
{
	status_set_zoom("x%d", ctx.scale);
}


static void update_pos(struct coord pos)
{
	status_set_sys_x("X %5.2lf" , units_to_mm(pos.x));
	status_set_sys_y("Y %5.2lf" , units_to_mm(pos.y));
	status_set_user_x("x %5.2lf", units_to_mm(pos.x-user_origin.x));
	status_set_user_y("y %5.2lf", units_to_mm(pos.y-user_origin.y));
}


/* ----- coordinate system ------------------------------------------------- */


static void center(const struct bbox *this_bbox)
{
	struct bbox bbox;

	bbox = this_bbox ? *this_bbox : inst_get_bbox();
	ctx.center.x = (bbox.min.x+bbox.max.x)/2;
	ctx.center.y = (bbox.min.y+bbox.max.y)/2;
}


static void auto_scale(const struct bbox *this_bbox)
{
	struct bbox bbox;
	unit_type h, w;
	int sx, sy;
	float aw, ah;

	bbox = this_bbox ? *this_bbox : inst_get_bbox();
	aw = ctx.widget->allocation.width;
	ah = ctx.widget->allocation.height;
	h = bbox.max.x-bbox.min.x;
	w = bbox.max.y-bbox.min.y;
	aw -= 2*CANVAS_CLEARANCE;
	ah -= 2*CANVAS_CLEARANCE;
	if (aw < 1)
		aw = 1;
	if (ah < 1)
		ah = 1;
	sx = ceil(h/aw);
	sy = ceil(w/ah);
	ctx.scale = sx > sy ? sx : sy > 0 ? sy : 1;

	update_zoom();
}


/* ----- drawing ----------------------------------------------------------- */


void redraw(void)
{
	float aw, ah;

	aw = ctx.widget->allocation.width;
	ah = ctx.widget->allocation.height;
	gdk_draw_rectangle(ctx.widget->window, gc_bg, TRUE, 0, 0, aw, ah);

	inst_draw(&ctx);
}


/* ----- drag -------------------------------------------------------------- */


static void drag_left(struct coord pos)
{
	if (!dragging)
		return;
	if (hypot(pos.x-drag_start.x, pos.y-drag_start.y)/ctx.scale <
	    DRAG_MIN_R)
		return;
	tool_drag(&ctx, pos);
}


static void drag_middle(struct coord pos)
{
}


static gboolean motion_notify_event(GtkWidget *widget, GdkEventMotion *event,
    gpointer data)
{
	struct coord pos = canvas_to_coord(&ctx, event->x, event->y);

	curr_pos.x = event->x;
	curr_pos.y = event->y;
	if (event->state & GDK_BUTTON1_MASK)
		drag_left(pos);
	else
		tool_hover(&ctx, pos);
	if (event->state & GDK_BUTTON2_MASK)
		drag_middle(pos);
	update_pos(pos);
	return TRUE;
}


/* ----- button press and release ------------------------------------------ */


static gboolean button_press_event(GtkWidget *widget, GdkEventButton *event,
    gpointer data)
{
	struct coord pos = canvas_to_coord(&ctx, event->x, event->y);
	const struct inst *prev;

	switch (event->button) {
	case 1:
		if (dragging) {
			fprintf(stderr, "HUH ?!?\n");
			tool_cancel_drag(&ctx);
			dragging = 0;
		}
		if (tool_consider_drag(&ctx, pos)) {
			dragging = 1;
			drag_start = pos;
			break;
		}
		prev = selected_inst;
		inst_deselect();
		inst_select(&ctx, pos);
		if (prev != selected_inst)
			redraw();
		break;
	case 2:
		ctx.center = pos;
		redraw();
		break;
	}
	return TRUE;
}


static gboolean button_release_event(GtkWidget *widget, GdkEventButton *event,
    gpointer data)
{
	struct coord pos = canvas_to_coord(&ctx, event->x, event->y);

	if (dragging) {
		dragging = 0;
		if (hypot(pos.x-drag_start.x, pos.y-drag_start.y)/ctx.scale < 
		    DRAG_MIN_R)
			tool_cancel_drag(&ctx);
		else {
			if (tool_end_drag(&ctx, pos))
				change_world();
		}
	}
	return TRUE;
}


/* ----- zoom control ------------------------------------------------------ */


static void zoom_in(struct coord pos)
{
	if (ctx.scale < 2)
		return;
	ctx.scale /= 2;
	ctx.center.x = (ctx.center.x+pos.x)/2;
	ctx.center.y = (ctx.center.y+pos.y)/2;
	update_zoom();
	redraw();
}


static void zoom_out(struct coord pos)
{
	struct bbox bbox;

	bbox = inst_get_bbox();
	bbox.min = translate(&ctx, bbox.min);
	bbox.max = translate(&ctx, bbox.max);
	if (bbox.min.x >= 0 && bbox.max.y >= 0 &&
	    bbox.max.x < ctx.widget->allocation.width &&
	    bbox.min.y < ctx.widget->allocation.height)
		return;
	ctx.scale *= 2;
	ctx.center.x = 2*ctx.center.x-pos.x;
	ctx.center.y = 2*ctx.center.y-pos.y;
	update_zoom();
	redraw();
}


static gboolean scroll_event(GtkWidget *widget, GdkEventScroll *event,
    gpointer data)
{
	struct coord pos = canvas_to_coord(&ctx, event->x, event->y);

	switch (event->direction) {
	case GDK_SCROLL_UP:
		zoom_in(pos);
		break;
	case GDK_SCROLL_DOWN:
		zoom_out(pos);
		break;
	default:
		/* ignore */;
	}
	return TRUE;
}


/* ----- keys -------------------------------------------------------------- */


static gboolean key_press_event(GtkWidget *widget, GdkEventKey *event,
    gpointer data)
{
	struct coord pos = canvas_to_coord(&ctx, curr_pos.x, curr_pos.y);

	switch (event->keyval) {
	case ' ':
		user_origin = pos;
		update_pos(pos);
		break;
	case '+':
	case '=':
		zoom_in(pos);
		break;
	case '-':
		zoom_out(pos);
		break;
	case '*':
		center(NULL);
		auto_scale(NULL);
		redraw();
		break;
	case '#':
		center(&active_frame_bbox);
		auto_scale(&active_frame_bbox);
		redraw();
		break;
	case '.':
		ctx.center = pos;
		redraw();
		break;
	}
	return TRUE;
}


/* ----- expose event ------------------------------------------------------ */


static gboolean expose_event(GtkWidget *widget, GdkEventExpose *event,
    gpointer data)
{
	static int first = 1;
	if (first) {
		init_canvas();
		first = 0;
	}
	redraw();
	return TRUE;
}


/* ----- enter/leave ------------------------------------------------------- */


static gboolean enter_notify_event(GtkWidget *widget, GdkEventCrossing *event,
    gpointer data)
{
	gtk_widget_grab_focus(widget);
	return TRUE;
}


static gboolean leave_notify_event(GtkWidget *widget, GdkEventCrossing *event,
    gpointer data)
{
	if (dragging)
		tool_cancel_drag(&ctx);
	tool_dehover(&ctx);
	dragging = 0;
	return TRUE;
}


/* ----- canvas setup ------------------------------------------------------ */


/*
 * Note that we call init_canvas twice: first to make sure we'll make it safely
 * through select_frame, and the second time to set the geometry for the actual
 * screen.
 */

void init_canvas(void)
{
	center(NULL);
	auto_scale(NULL);
}


GtkWidget *make_canvas(void)
{
	GtkWidget *canvas;
	GdkColor black = { 0, 0, 0, 0 };

	/* Canvas */

	canvas = gtk_drawing_area_new();
	gtk_widget_modify_bg(canvas, GTK_STATE_NORMAL, &black);

	g_signal_connect(G_OBJECT(canvas), "motion_notify_event",
	    G_CALLBACK(motion_notify_event), NULL);
	g_signal_connect(G_OBJECT(canvas), "button_press_event",
	    G_CALLBACK(button_press_event), NULL);
	g_signal_connect(G_OBJECT(canvas), "button_release_event",
	    G_CALLBACK(button_release_event), NULL);
	g_signal_connect(G_OBJECT(canvas), "scroll_event",
	    G_CALLBACK(scroll_event), NULL);

	GTK_WIDGET_SET_FLAGS(canvas, GTK_CAN_FOCUS);

	g_signal_connect(G_OBJECT(canvas), "key_press_event",
	    G_CALLBACK(key_press_event), NULL);

	g_signal_connect(G_OBJECT(canvas), "expose_event",
	    G_CALLBACK(expose_event), NULL);
	g_signal_connect(G_OBJECT(canvas), "enter_notify_event",
	    G_CALLBACK(enter_notify_event), NULL);
	g_signal_connect(G_OBJECT(canvas), "leave_notify_event",
	    G_CALLBACK(leave_notify_event), NULL);

	gtk_widget_set_events(canvas,
	    GDK_EXPOSE | GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK |
	    GDK_KEY_PRESS_MASK |
	    GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
	    GDK_SCROLL |
	    GDK_POINTER_MOTION_MASK);

	ctx.widget = canvas;

	return canvas;
}
