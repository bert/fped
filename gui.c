/*
 * gui.c - Editor GUI core
 *
 * Written 2009-2012 by Werner Almesberger
 * Copyright 2009-2012 by Werner Almesberger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


#include <locale.h>
#include <gtk/gtk.h>

#include "inst.h"
#include "file.h"
#include "gui_util.h"
#include "gui_style.h"
#include "gui_status.h"
#include "gui_canvas.h"
#include "gui_tool.h"
#include "gui_frame.h"
#include "gui.h"
#include "fped.h"

#include "icons/stuff.xpm"
#include "icons/stuff_off.xpm"
#include "icons/meas.xpm"
#include "icons/meas_off.xpm"
#include "icons/all.xpm"
#include "icons/all_off.xpm"
#include "icons/bright.xpm"
#include "icons/bright_off.xpm"


GtkWidget *root;
int show_all = 1;
int show_stuff = 1;
int show_meas = 1;
int show_bright = 0;


static GtkWidget *paned;
static GtkWidget *frames_box;
static GtkWidget *ev_stuff, *ev_meas, *ev_all, *ev_bright;
static GtkWidget *stuff_image[2], *meas_image[2], *all_image[2];
static GtkWidget *bright_image[2];

static void do_build_frames(void);


/* ----- save callbacks ---------------------------------------------------- */


static void save_as_fpd(void)
{
	GtkWidget *dialog;

	dialog = gtk_file_chooser_dialog_new("Save File",
	    NULL, GTK_FILE_CHOOSER_ACTION_SAVE,
	    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
	    GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT, NULL);
	gtk_file_chooser_set_do_overwrite_confirmation(
	    GTK_FILE_CHOOSER(dialog), TRUE);
	if (save_file_name)
		gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog),
		    save_file_name);
	if (gtk_dialog_run(GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
		save_file_name =
		    gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
		save_fpd();
		/* @@@ we may leak save_file_name */
		no_save = 0;
	}
	gtk_widget_destroy(dialog);
}


/* ----- view callbacks ---------------------------------------------------- */


static void swap_var_code(void)
{
	extern int show_vars;

	show_vars = !show_vars;
	change_world();
}


/* ----- menu bar ---------------------------------------------------------- */


static GtkItemFactoryEntry menu_entries[] = {
	{ "/File",		NULL,	NULL,		0, "<Branch>" },
	{ "/File/Save",		NULL,	save_fpd,	0, "<Item>" },
	{ "/File/Save as",	NULL,	save_as_fpd,	0, "<Item>" },
        { "/File/sep1",		NULL,	NULL,		0, "<Separator>" },
        { "/File/Write KiCad module",	NULL,	write_kicad,	0, "<Item>" },
        { "/File/Write PCB footprint",	NULL,	write_pcb,	0, "<Item>" },
        { "/File/Write Postscript",
				NULL,	write_ps,	0, "<Item>" },
        { "/File/sep2",		NULL,	NULL,		0, "<Separator>" },
        { "/File/Reload",	NULL,	reload,		0, "<Item>" },
        { "/File/sep3",		NULL,	NULL,		0, "<Separator>" },
        { "/File/Quit",		NULL,	gtk_main_quit,	0, "<Item>" },
	{ "/View",		NULL,	NULL,		0, "<Branch>" },
	{ "/View/Zoom in",	NULL,	zoom_in_center,	0, "<Item>" },
	{ "/View/Zoom out",	NULL,	zoom_out_center,0, "<Item>" },
	{ "/View/Zoom all",	NULL,	zoom_to_extents,0, "<Item>" },
	{ "/View/Zoom frame",	NULL,	zoom_to_frame,	0, "<Item>" },
	{ "/View/sep1",		NULL,	NULL,		0, "<Separator>" },
	{ "/View/Swap var&code",NULL,	swap_var_code,	0, "<Item>" },
};


static void make_menu_bar(GtkWidget *hbox)
{
	GtkItemFactory *factory;
	GtkWidget *bar;

	factory = gtk_item_factory_new(GTK_TYPE_MENU_BAR, "<FpedMenu>", NULL);
        gtk_item_factory_create_items(factory,
	    sizeof(menu_entries)/sizeof(*menu_entries), menu_entries, NULL);

	bar = gtk_item_factory_get_widget(factory, "<FpedMenu>");
	gtk_box_pack_start(GTK_BOX(hbox), bar, TRUE, TRUE, 0);

	gtk_widget_set_sensitive(
	    gtk_item_factory_get_item(factory, "/File/Save"), !no_save);
	gtk_widget_set_sensitive(
	    gtk_item_factory_get_item(factory, "/File/Reload"),
	    no_save && !!save_file_name);
}


static gboolean toggle_all(GtkWidget *widget, GdkEventButton *event,
    gpointer data)
{
	switch (event->button) {
	case 1:
		show_all = !show_all;
		set_image(ev_all, all_image[show_all]);
		inst_deselect();
		redraw();
		break;
	}
        return TRUE;
}


static gboolean toggle_stuff(GtkWidget *widget, GdkEventButton *event,
    gpointer data)
{
	switch (event->button) {
	case 1:
		show_stuff = !show_stuff;
		set_image(ev_stuff, stuff_image[show_stuff]);
		inst_deselect();
		redraw();
		break;
	}
        return TRUE;
}


static gboolean toggle_meas(GtkWidget *widget, GdkEventButton *event,
    gpointer data)
{
	switch (event->button) {
	case 1:
		show_meas = !show_meas;
		set_image(ev_meas, meas_image[show_meas]);
		inst_deselect();
		redraw();
		break;
	}
        return TRUE;
}


static gboolean toggle_bright(GtkWidget *widget, GdkEventButton *event,
    gpointer data)
{
	switch (event->button) {
	case 1:
		show_bright = !show_bright;
		set_image(ev_bright, bright_image[show_bright]);
		inst_deselect();
		redraw();
		break;
	}
        return TRUE;
}


static void make_tool_bar(GtkWidget *hbox, GdkDrawable *drawable)
{
	GtkWidget *bar;

	bar = gtk_toolbar_new();
	gtk_box_pack_end(GTK_BOX(hbox), bar, TRUE, TRUE, 0);
	//gtk_box_pack_end(GTK_BOX(hbox), bar, FALSE, FALSE, 0);
	gtk_toolbar_set_style(GTK_TOOLBAR(bar), GTK_TOOLBAR_ICONS);

	ev_all = tool_button(bar, drawable, NULL, NULL, toggle_all, NULL);
	ev_stuff = tool_button(bar, drawable, NULL, NULL, toggle_stuff, NULL);
	ev_meas = tool_button(bar, drawable, NULL, NULL, toggle_meas, NULL);
	ev_bright = tool_button(bar, drawable, NULL, NULL, toggle_bright, NULL);

	stuff_image[0] = gtk_widget_ref(make_image(drawable, xpm_stuff_off,
	    "Show vectors and frame references (disabled)"));
	stuff_image[1] = gtk_widget_ref(make_image(drawable, xpm_stuff,
	    "Show vectors and frame references (enabled)"));
	meas_image[0] = gtk_widget_ref(make_image(drawable, xpm_meas_off,
	    "Show measurements (disabled)"));
	meas_image[1] = gtk_widget_ref(make_image(drawable, xpm_meas,
	    "Show measurements (enabled)"));
	all_image[0] = gtk_widget_ref(make_image(drawable, xpm_all_off,
	    "Show all frames (currently showing only the active frame)"));
	all_image[1] = gtk_widget_ref(make_image(drawable, xpm_all,
	    "Show all frames (enabled)"));
	bright_image[0] = gtk_widget_ref(make_image(drawable, xpm_bright_off,
	    "Highlight elements (disabled)"));
	bright_image[1] = gtk_widget_ref(make_image(drawable, xpm_bright,
	    "Highlight elements (enabled)"));

	set_image(ev_stuff, stuff_image[show_stuff]);
	set_image(ev_meas, meas_image[show_meas]);
	set_image(ev_all, all_image[show_all]);
	set_image(ev_bright, bright_image[show_bright]);
}


static void cleanup_tool_bar(void)
{
	g_object_unref(stuff_image[0]);
	g_object_unref(stuff_image[1]);
	g_object_unref(meas_image[0]);
	g_object_unref(meas_image[1]);
	g_object_unref(all_image[0]);
	g_object_unref(all_image[1]);
	g_object_unref(bright_image[0]);
	g_object_unref(bright_image[1]);
}


static void make_top_bar(GtkWidget *vbox)
{
	GtkWidget *hbox;

	hbox = gtk_hbox_new(FALSE, 0);
	make_menu_bar(hbox);
	make_tool_bar(hbox, root->window);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
}


/* ----- central screen area ----------------------------------------------- */


static void resize_frames_area(GtkWidget *widget, GtkAllocation *allocation,
    gpointer user_data)
{
	static int width = 0;

	if (allocation->width == width)
		return;
	width = allocation->width;
	do_build_frames();
}


static void make_center_area(GtkWidget *vbox)
{
	GtkWidget *hbox, *frames_area;//, *paned;
	GtkWidget *tools;

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);

	paned = gtk_hpaned_new();
	gtk_box_pack_start(GTK_BOX(hbox), paned, TRUE, TRUE, 0);

	/* Frames */

	frames_area = gtk_scrolled_window_new(NULL, NULL);
	gtk_paned_add1(GTK_PANED(paned), frames_area);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(frames_area),
	    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_size_request(frames_area,
	    DEFAULT_FRAME_AREA_WIDTH, DEFAULT_FRAME_AREA_HEIGHT);

	frames_box = gtk_vbox_new(FALSE, 0);
	build_frames(frames_box, DEFAULT_FRAME_AREA_WIDTH);

	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(frames_area),
	    frames_box);

	g_signal_connect(G_OBJECT(frames_area), "size-allocate",
	    G_CALLBACK(resize_frames_area), NULL);

	/* Canvas */

	gtk_paned_add2(GTK_PANED(paned), make_canvas());

	/* Icon bar */

	tools = gui_setup_tools(root->window);
	gtk_box_pack_end(GTK_BOX(hbox), tools, FALSE, FALSE, 0);
}


/* ----- GUI construction -------------------------------------------------- */


static void do_build_frames(void)
{
	int width;

	width = gtk_paned_get_position(GTK_PANED(paned));
	build_frames(frames_box, width > 0 ? width : DEFAULT_FRAME_AREA_WIDTH);
}


void change_world(void)
{
	struct bbox before, after;
	int reachable_is_active;

	inst_deselect();
	status_begin_reporting();
	before = inst_get_bbox(NULL);
	reachable_is_active = reachable_pkg && reachable_pkg == active_pkg;
	instantiate();
	if (reachable_is_active && reachable_pkg &&
	     reachable_pkg != active_pkg) {
		active_pkg = reachable_pkg;
		instantiate();
	}
	after = inst_get_bbox(NULL);
	label_in_box_bg(active_frame->label, COLOR_FRAME_SELECTED);
	do_build_frames();
	if (after.min.x < before.min.x || after.min.y < before.min.y ||
	    after.max.x > before.max.x || after.max.y > before.max.y)
		zoom_to_extents();
	else
		redraw();
}


void change_world_reselect(void)
{
	struct obj *selected_obj;

	/*
	 * We can edit an object even if it's not selected if it was picked
	 * via the item view. inst_select_obj tries to find an instance, but
	 * if there's never been a successful instantiation since creation of
	 * the object or if the object is unreachable for some other reason,
	 * then selected_inst will be NULL.
	 */
	if (!selected_inst) {
		change_world();
		return;
	}
	selected_obj = selected_inst->obj;
	change_world();
	inst_select_obj(selected_obj);
}


static void make_screen(GtkWidget *window)
{
	GtkWidget *vbox;

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(window), vbox);

	make_top_bar(vbox);
	make_center_area(vbox);
	make_status_area(vbox);
}


int gui_init(int *argc, char ***argv)
{
	gtk_init(argc, argv);
	setlocale(LC_ALL, "C"); /* damage control */
	return 0;
}


int gui_main(void)
{
	root = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_position(GTK_WINDOW(root), GTK_WIN_POS_CENTER);
	gtk_window_set_default_size(GTK_WINDOW(root), 620, 460);
	if (*VERSION)
		gtk_window_set_title(GTK_WINDOW(root),
		    "fped (rev " VERSION ")");
	else
		gtk_window_set_title(GTK_WINDOW(root), "fped");

	/* get root->window */
	gtk_widget_show_all(root);

	g_signal_connect(G_OBJECT(root), "destroy",
	    G_CALLBACK(gtk_main_quit), NULL);

	make_screen(root);

	gtk_widget_show_all(root);

	gui_setup_style(root->window);
	init_canvas();
	edit_nothing();
	select_frame(frames);
	make_popups();

	gtk_main();

	gui_cleanup_style();
	gui_cleanup_tools();
	cleanup_tool_bar();
	cleanup_status_area();

	return 0;
}
