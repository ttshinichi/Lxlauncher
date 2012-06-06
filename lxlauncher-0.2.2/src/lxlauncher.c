/*
 *      lxlauncher.c - Open source replace for EeePC Asus Launcher
 *
 *      Copyright 2008 PCMan <pcman.tw@gmail.com>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *      MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <menu-cache.h>

#include <errno.h>

#include <cairo.h>

#include "exo-wrap-table.h"
#include "working-area.h"
#include "misc.h"

#define CONFIG_FILE "lxlauncher/settings.conf"

#define DATA_DIR  PACKAGE_DATA_DIR"/lxlauncher"
#define BACKGROUND_DIR  PACKAGE_DATA_DIR"/lxlauncher/background"
#define ICON_DIR        PACKAGE_DATA_DIR"/lxlauncher/icons"

#define BUTTON_SIZE_FALLBACK 120
#define IMG_SIZE_FALLBACK 48

static GtkWidget* main_window;
static GtkWidget* notebook;
static int n_cols;
static GtkIconSize icon_size;

static Atom atom_NET_WORKAREA = None;

static MenuCache* menu_tree = NULL;
static MenuCacheDir* root_dir = NULL;

static int reload_handler = 0;
static gpointer reload_notify_id;

static GKeyFile *key_file;

static gint button_size;
static gint img_size;

static int area_width, area_height;		//added
static gchar* bg_img_path;		//added

typedef struct _PageData{
    MenuCacheDir* dir;
    char* dir_path;
    GtkWidget* page_vbox;
    GtkBox* go_up_bar;
    GtkWidget* table;
    GdkPixbuf* background;
}PageData;

static void on_app_btn_clicked( GtkButton* btn, MenuCacheApp* app )
{
    lxlauncher_execute_app( gdk_screen_get_default(),
                                        NULL, menu_cache_app_get_exec(app), 
                                        menu_cache_item_get_name(MENU_CACHE_ITEM(app)), NULL, 
                                        menu_cache_app_get_use_terminal(app),
                                        NULL );
}

static void on_menu_item_properties(GtkMenuItem* item, MenuCacheApp* app)
{
    /* FIXME: if the source desktop is in AppDir other then default
     * applications dirs, where should we store the user-specific file?
    */
    char* ifile = menu_cache_item_get_file_path(MENU_CACHE_ITEM(app));
    char* ofile = g_build_filename(g_get_user_data_dir(), "applications",
                                  menu_cache_item_get_file_basename(MENU_CACHE_ITEM(app)), NULL);
    char* argv[] = {
        "lxshortcut",
        "-i",
        ifile,
        "-o",
        ofile,
        NULL};
    g_spawn_async( NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL );
    g_free( ifile );
    g_free( ofile );
}

static gboolean on_app_btn_press_event(GtkWidget* btn, GdkEventButton* evt, MenuCacheApp* app)
{
    if( evt->button == 3 )  /* right */
    {
        char* tmp;
        GtkWidget* item;
        GtkWidget* p = gtk_menu_new();
        tmp = g_find_program_in_path("lxshortcut");
        if( tmp )
        {
            /*
            item = gtk_separator_menu_item_new();
            gtk_menu_shell_append(p, item);
            */
            item = gtk_menu_item_new_with_mnemonic(_("_Customize"));
            g_signal_connect(item, "activate", G_CALLBACK(on_menu_item_properties), app);
            gtk_menu_shell_append(GTK_MENU_SHELL(p), item);
            g_free(tmp);
        }
        else
        {
            /* FIXME: since currently Properties is the only menu item,
             *        we don't popup the menu if it's empty */
            gtk_widget_destroy(GTK_WIDGET(p));
            return FALSE;
        }
        g_signal_connect(p, "selection-done", G_CALLBACK(gtk_widget_destroy), NULL);

        gtk_widget_show_all(GTK_WIDGET(p));
        gtk_menu_popup(GTK_MENU(p), NULL, NULL, NULL, NULL, 0, evt->time);
        return TRUE;
    }
    return FALSE;
}

static void notebook_page_chdir( PageData* data, MenuCacheDir* dir );

static void on_dir_btn_clicked( GtkButton* btn, PageData* data )
{
    MenuCacheDir* dir = (MenuCacheDir*)g_object_get_data( G_OBJECT(btn), "dir" );
    notebook_page_chdir( data, dir );
}

static GtkWidget* add_btn( GtkWidget* table, const char* text, GdkPixbuf* icon, const char* tip )
{
    GtkWidget *btn, *box, *img, *label;
    GtkRequisition req;
    GtkSettings *settings = gtk_widget_get_settings(GTK_WIDGET(main_window));
    gboolean enable_key=0;
    g_object_get(settings, "lxlauncher-enable-key", &enable_key,NULL);
    GtkBorder* inner_border;
    int w, fw, fp, btn_border;

    /* add the app to that page */
    btn = gtk_button_new();
    gtk_widget_set_size_request( btn, button_size, -1 );
    if (!enable_key)
#if GTK_CHECK_VERSION(2, 18, 0)
        gtk_widget_set_can_focus(btn, FALSE );
    gtk_widget_set_can_default(GTK_WIDGET(btn), FALSE );
#else
        GTK_WIDGET_UNSET_FLAGS(btn, GTK_CAN_FOCUS );
    GTK_WIDGET_UNSET_FLAGS(btn, GTK_CAN_DEFAULT );
#endif
    img = gtk_image_new_from_pixbuf( icon );

    box = gtk_vbox_new( FALSE, 2 );
    gtk_box_pack_start( GTK_BOX(box), img, FALSE, TRUE, 2 );

    label = gtk_label_new( text );
    gtk_widget_show( label );
    gtk_widget_set_size_request( label, button_size - 10, -1 );
    gtk_label_set_line_wrap_mode( GTK_LABEL(label), PANGO_WRAP_WORD_CHAR );
    gtk_label_set_line_wrap( GTK_LABEL(label), TRUE );
    gtk_label_set_justify( GTK_LABEL(label), GTK_JUSTIFY_CENTER );
    gtk_misc_set_alignment( GTK_MISC(label), 0.5, 0 );
    gtk_misc_set_padding( GTK_MISC(label), 0, 0 );
    gtk_box_pack_start( GTK_BOX(box), label, TRUE, TRUE, 0 );

    gtk_container_add( GTK_CONTAINER(btn), box );

    gtk_button_set_relief( GTK_BUTTON(btn), GTK_RELIEF_NONE );
    gtk_widget_set_size_request( btn, button_size, button_size );
    gtk_widget_show_all( btn );

    gtk_container_add( GTK_CONTAINER(table), btn );
    gtk_widget_realize( btn );

    gtk_widget_set_tooltip_text(btn, tip);

    /* Adjust the size of label and set line wrapping is needed.
     * FIXME: this is too dirty, and the effect is quite limited.
     * However, due to the unfortunate design flaws of gtk+, the
     * only way to overcome this might be implement our own label class.
     */

    /* get border of GtkButtons */
    gtk_widget_style_get(btn, "focus-line-width", &fw, "focus-padding", &fp, NULL);
    btn_border = 2 * (gtk_container_get_border_width(GTK_CONTAINER(btn)) + gtk_widget_get_style(btn)->xthickness + fw + fp);
    gtk_widget_style_get(btn, "inner-border", &inner_border, NULL);
    if( inner_border )
        btn_border += (inner_border->left + inner_border->right);
    /* padding of vbox should be added. */
    btn_border += 2 * gtk_container_get_border_width(GTK_CONTAINER(box));

    gtk_widget_size_request( label, &req );

    /* if the label is wider than button width, line-wrapping is needed. */
    if( req.width > (button_size - btn_border) )
    {
        gtk_widget_set_size_request( label, button_size - btn_border, -1 );
        gtk_label_set_line_wrap_mode( GTK_LABEL(label), PANGO_WRAP_WORD_CHAR );
        gtk_label_set_line_wrap( GTK_LABEL(label), TRUE );
    }

    gtk_widget_set_app_paintable( btn, TRUE );
#if GTK_CHECK_VERSION(3, 0, 0)
    GdkRGBA color;
    gdk_rgba_parse(&color, "none");
    gtk_widget_override_background_color ((GtkWidget*)btn, GTK_STATE_FLAG_NORMAL, &color);
#else
    gdk_window_set_back_pixmap( ((GtkWidget*)btn)->window, NULL, TRUE );
#endif
    return btn;
}

static void add_dir_btn( PageData* data, MenuCacheDir* dir )
{
    GdkPixbuf* icon;
    GtkWidget* btn;
    const char* icon_name = menu_cache_item_get_icon( MENU_CACHE_ITEM(dir) );
    if( !icon_name )
        icon_name = "folder";

    icon = lxlauncher_load_icon( icon_name, img_size, TRUE );

    btn = add_btn( data->table, menu_cache_item_get_name(MENU_CACHE_ITEM(dir)), icon, menu_cache_item_get_comment(MENU_CACHE_ITEM(dir)) );
    if( icon )
        g_object_unref( icon );

    g_object_set_data( G_OBJECT(btn), "dir", dir );
    g_signal_connect( btn, "clicked", G_CALLBACK(on_dir_btn_clicked), data );
}

static void add_app_btn( GtkWidget* table, MenuCacheApp* app )
{
    GdkPixbuf* icon;
    GtkWidget* btn;
    const char* icon_name = menu_cache_item_get_icon( MENU_CACHE_ITEM(app) );

    if( !icon_name )
        icon_name = "application-x-executable";

    icon = lxlauncher_load_icon( icon_name, img_size, TRUE );

    btn = add_btn( table, menu_cache_item_get_name(MENU_CACHE_ITEM(app)), icon, menu_cache_item_get_comment(MENU_CACHE_ITEM(app)) );

    if( icon )
        g_object_unref( icon );
    g_signal_connect( btn, "clicked", G_CALLBACK(on_app_btn_clicked), app );
    g_signal_connect( btn, "button-press-event", G_CALLBACK(on_app_btn_press_event), app );
}

#if GTK_CHECK_VERSION(3, 0, 0)
static gboolean on_viewport_draw( GtkWidget* w, cairo_t *cr1, gpointer data )
{
    GtkWidget* scroll = gtk_widget_get_parent(w);
	GtkStyleContext *style = gtk_widget_get_style_context(scroll);

    cairo_t *cr;

    cr = gdk_cairo_create (gtk_widget_get_window(w));
    cairo_rectangle(cr, 0, 0, 0, 0 );
    cairo_set_source_rgb(cr, 184.0/256, 215.0/256, 235.0/256);
    cairo_fill(cr);

	gtk_render_background(style,cr,0,0,
			gtk_widget_get_allocated_width(scroll),
			gtk_widget_get_allocated_height(scroll));


    cairo_destroy(cr);
    return FALSE;

}
#else
static gboolean on_viewport_expose( GtkWidget* w, GdkEventExpose* evt, gpointer data )
{
    GObjectClass* oc = G_OBJECT_GET_CLASS(w);
    GtkWidgetClass* wc = (GtkWidgetClass*)g_type_class_peek_parent( oc );
/*
    GdkPixmap* pixmap = (GdkPixmap*)data;
*/
    if( GTK_WIDGET_DRAWABLE(w) && evt->window == ((GtkViewport*)w)->bin_window )
    {
        cairo_t *cr;
        cairo_pattern_t* pat;

        GtkWidget* scroll = gtk_widget_get_parent(w);
        GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll));

        cr = gdk_cairo_create (evt->window);
        //pat = cairo_pattern_create_linear( 0, gtk_adjustment_get_value(vadj), 0, w->allocation.height + gtk_adjustment_get_value(vadj) );
        //cairo_pattern_add_color_stop_rgb( pat, 0, 1, 1, 1);
        //cairo_pattern_add_color_stop_rgb( pat, 1.0, ((gdouble)184/256), ((gdouble)215/256), ((gdouble)235/256));
//        cairo_rectangle(cr, 0, 0, w->allocation.width, w->allocation.height );
//        cairo_rectangle(cr, evt->area.x, evt->area.y, evt->area.width, evt->area.height );
        cairo_rectangle(cr, evt->area.x, evt->area.y, evt->area.width, evt->area.height );
        //cairo_set_source(cr, pat);
        cairo_set_source_rgb(cr, 184.0/256, 215.0/256, 235.0/256);
        cairo_fill(cr);
        //cairo_pattern_destroy(pat);
        cairo_destroy(cr);

/*
        GdkGC* gc = gdk_gc_new(evt->window);

        gdk_gc_set_tile( gc, pixmap );
        gdk_gc_set_fill( gc, GDK_TILED );
        gdk_gc_set_ts_origin( gc, 0, 0 );

        gdk_draw_rectangle( evt->window, gc, TRUE, evt->area.x, evt->area.y, evt->area.width, evt->area.height );

        gdk_gc_unref( gc );
*/
    }

    // call handler of tha parent GtkContainer class to propagate the event to children

    (* wc->expose_event) (w, evt);
    return TRUE;
}
#endif

static gboolean on_scroll( GtkAdjustment* adj, PageData* data )
{
    // Dirty hacks used to force pseudo-transparent background
    gtk_widget_queue_draw( data->table );
    return TRUE;
}

// Dirty hacks used to reduce unnecessary redrew during scroll
static gboolean on_scroll_change_val( GtkRange* scroll, GtkScrollType type, gdouble value, PageData* data )
{

    GtkAdjustment* adj = gtk_range_get_adjustment(scroll);

    if( type == GTK_SCROLL_JUMP )
    {
        if( ABS( gtk_adjustment_get_value(adj) - value ) < button_size / 2 )
        {
            if( gtk_adjustment_get_value(adj) > value )    // upward
            {
                if( (gtk_adjustment_get_value(adj) - gtk_adjustment_get_lower(adj)) < button_size / 2 )
                {
                    gtk_adjustment_set_value( adj, gtk_adjustment_get_lower(adj) );
                }
            }
            else // downward
            {
                if( (gtk_adjustment_get_upper(adj) - gtk_adjustment_get_value(adj)) < button_size / 2 )
                {
                    gtk_adjustment_set_value( adj, gtk_adjustment_get_lower(adj) );
                }
            }
            return TRUE;
        }
    }
    return FALSE;
}

static char* menu_dir_to_path( MenuCacheDir* dir )
{
    if( menu_cache_item_get_parent(MENU_CACHE_ITEM(dir)) == root_dir )
        return g_strdup( menu_cache_item_get_name(MENU_CACHE_ITEM(dir)) );
    else
    {
        char* parent = menu_dir_to_path( menu_cache_item_get_parent(MENU_CACHE_ITEM(dir)) );
        char* ret;
        ret = g_strconcat( parent, " > ", menu_cache_item_get_name(MENU_CACHE_ITEM(dir)), NULL );
        g_free( parent );
        return ret;
    }
    return NULL;
}

static void create_notebook_pages();

static PageData* notebook_page_from_dir( MenuCacheDir* dir )
{
    int i, n;
    GtkWidget* page;
    MenuCacheDir* top = dir;
    PageData* page_data;

    // get toplevel parent dir
    while( menu_cache_item_get_parent(MENU_CACHE_ITEM(top)) != root_dir )
        top = menu_cache_item_get_parent(MENU_CACHE_ITEM(top));

    n = gtk_notebook_get_n_pages( GTK_NOTEBOOK(notebook) );
    for( i = 0; i < n; ++i )
    {

        MenuCacheDir* top2;
        page = gtk_notebook_get_nth_page( GTK_NOTEBOOK(notebook), i );
        page_data = (PageData*)g_object_get_data(G_OBJECT(page), "page");
        top2 = page_data->dir;
        while( menu_cache_item_get_parent(MENU_CACHE_ITEM(top2)) != root_dir )
            top2 = menu_cache_item_get_parent(MENU_CACHE_ITEM(top2));

        if( top == top2 )
            return page_data;
    }
    return NULL;
}

static gboolean reload_apps()
{
    int i;
    PageData* page_data;
    GtkWidget* page;

    int n = gtk_notebook_get_n_pages( GTK_NOTEBOOK(notebook) );
    char** page_paths = g_new0(char*, n + 1);
    int cur_page = gtk_notebook_get_current_page(GTK_NOTEBOOK(notebook));

    for( i = 0; i < n; ++i )
    {
        page = gtk_notebook_get_nth_page( GTK_NOTEBOOK(notebook), 0 );
        page_data = (PageData*)g_object_get_data( G_OBJECT(page), "page" );
        page_paths[i] = g_strdup(page_data->dir_path);
        gtk_notebook_remove_page( GTK_NOTEBOOK(notebook), 0 );
    }

    // rebuild every pages
    create_notebook_pages();

    for( i = 0; i < n; ++i )
    {
        MenuCacheDir* dir = menu_cache_get_dir_from_path( menu_tree, page_paths[i] );

        if( dir )
        {
            MenuCacheDir* top = dir;
            // get toplevel parent dir

            while( menu_cache_item_get_parent(MENU_CACHE_ITEM(top)) != root_dir )
                top = menu_cache_item_get_parent(MENU_CACHE_ITEM(top));

            // find notebook page containing the top dir
            page_data = notebook_page_from_dir( top );

            if( ! page_data )
                continue;

            if( page_data->dir != dir )
                notebook_page_chdir( page_data, dir );

            if( i == cur_page )
                gtk_notebook_set_current_page( GTK_NOTEBOOK(notebook), cur_page );
        }
    }
    g_strfreev( page_paths );
    return FALSE;
}

void on_menu_tree_changed( MenuCache *tree, gpointer  user_data )
{
    // some changes happened in applications dirs
    // reload is needed
    // g_debug( "file changed" );
    menu_cache_item_unref(MENU_CACHE_ITEM(root_dir));
    root_dir = menu_cache_ref(menu_cache_get_root_dir(tree));
    reload_apps();
}

GdkFilterReturn evt_filter(GdkXEvent *xevt, GdkEvent *evt, gpointer data)
{
    XEvent *xev = (XEvent*)xevt;
    // working area is changed
    if(xev->type == PropertyNotify && xev->xproperty.atom == atom_NET_WORKAREA )
    {
        GdkRectangle working_area;
#if GTK_CHECK_VERSION(2,24,0)
        get_working_area( gdk_window_get_screen(evt->any.window), &working_area );
#else
        get_working_area( gdk_drawable_get_screen(evt->any.window), &working_area );
#endif
        gtk_window_move( GTK_WINDOW(main_window), working_area.x, working_area.y );
        gtk_window_resize( GTK_WINDOW(main_window), working_area.width, working_area.height );
    }
    return GDK_FILTER_CONTINUE;
}

static gboolean window_delete(GtkWidget *window, gpointer data)
{
    return TRUE;
}

#define skip_spaces( str ) \
    while( *str == ' ' || *str == '\t' ) ++str;

static char* get_line( char** buf )
{
    char *ret, *pbuf;
    ret = pbuf = *buf;

    while( *pbuf && *pbuf != '\n' )
        ++pbuf;
    if( G_LIKELY(*pbuf == '\n') )
    {
        *pbuf = '\0';
        *buf = pbuf + 1;
    }
    else
        *buf = NULL;

    return ret;
}

static void notebook_page_chdir( PageData* data, MenuCacheDir* dir )
{
    GSList* l;
    char* dir_path;
    MenuCacheDir* parent_dir;

    if(data->dir)
        menu_cache_item_unref(MENU_CACHE_ITEM(data->dir));
    data->dir = menu_cache_item_ref(MENU_CACHE_ITEM(dir));
    g_free(data->dir_path);
    data->dir_path = menu_cache_dir_make_path( dir );

    // destroy old buttons
    gtk_container_foreach( GTK_CONTAINER(data->table), gtk_widget_destroy, NULL );

    gtk_container_forall( GTK_CONTAINER(data->go_up_bar), gtk_widget_destroy, NULL );

    if( G_UNLIKELY( !dir ) )
        return;

    for( l = menu_cache_dir_get_children(dir); l; l = l->next )
    {
        MenuCacheItem* item = (MenuCacheItem*)l->data;
        MenuCacheType type = menu_cache_item_get_type(item);

        if( type == MENU_CACHE_TYPE_DIR )
            add_dir_btn( data, (MenuCacheDir*)item );
        else if( type == MENU_CACHE_TYPE_APP )
        {
            if( ! menu_cache_app_get_is_visible(MENU_CACHE_APP(item), SHOW_IN_LXDE) )
                continue;
            add_app_btn( data->table, (MenuCacheApp*)item );
        }
    }

    parent_dir = menu_cache_item_get_parent((MenuCacheItem*)dir);

    if( parent_dir != root_dir )   // if dir has parent, not top-level group
    {
        GtkWidget* label;
        char* text = g_strdup_printf( _("Go back to \"%s\""), menu_cache_item_get_name(MENU_CACHE_ITEM(parent_dir)) );
        GtkWidget* btn = gtk_button_new_with_label( text );
        g_free( text );

        dir_path = menu_dir_to_path( dir );

        label = gtk_label_new( dir_path );
        g_free( dir_path );

        gtk_misc_set_alignment( GTK_MISC(label), 0.0, 0.5 );
        g_object_set_data( G_OBJECT(btn), "dir", parent_dir );
        g_signal_connect( btn, "clicked", on_dir_btn_clicked, data );
        gtk_button_set_image( GTK_BUTTON(btn), gtk_image_new_from_stock( GTK_STOCK_GO_UP, GTK_ICON_SIZE_BUTTON ) );

        gtk_box_pack_start( data->go_up_bar, label, TRUE, TRUE, 4 );
        gtk_box_pack_start( data->go_up_bar, btn, FALSE, TRUE, 2 );
        gtk_widget_show_all( GTK_WIDGET(data->go_up_bar) );
    }
    else
    {
        gtk_widget_hide( GTK_WIDGET(data->go_up_bar) );
    }
}

static void page_data_free( PageData* data )
{
    if( data->background )
        g_object_unref( data->background );
    if(data->dir)
        menu_cache_item_unref( MENU_CACHE_ITEM(data->dir) );
    g_free(data->dir_path);
    g_free( data );
}

static void create_notebook_pages()
{
    GSList* l;

    // build pages for toplevel groups
    for( l = menu_cache_dir_get_children(root_dir); l; l = l->next )
    {
        GtkWidget* *viewport;
        GtkAdjustment* adj;
        GtkWidget* scroll = gtk_scrolled_window_new(NULL, NULL);
        GtkWidget* page_vbox = gtk_vbox_new(FALSE, 0);
        GtkWidget* table = exo_wrap_table_new(TRUE);
        GtkWidget* label;
        GtkWidget* image;
        GtkWidget* go_up_bar = gtk_hbox_new( FALSE, 2 );
        GdkPixbuf* pixbuf=NULL;
        gchar* file;
        PageData* page_data;
        MenuCacheDir* dir = (MenuCacheDir*)l->data;

        if( G_UNLIKELY( menu_cache_item_get_type((MenuCacheItem*)dir) != MENU_CACHE_TYPE_DIR ) )
            continue;

        page_data = g_new0( PageData, 1 );
        g_object_set_data_full( G_OBJECT(page_vbox), "page", page_data, page_data_free );

        label = gtk_hbox_new( FALSE, 2 );

        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC );

        // Very bad dirty hacks used to force gtk+ to draw transparent background
        GtkRange* range = gtk_scrolled_window_get_vscrollbar(GTK_SCROLLED_WINDOW(scroll));
        //gtk_range_set_update_policy( range, GTK_UPDATE_DELAYED );
        g_signal_connect( range, "change-value", G_CALLBACK(on_scroll_change_val), page_data );
        adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll));
#if GTK_CHECK_VERSION(2, 14, 0)
        gtk_adjustment_set_step_increment(adj, button_size / 3);
        gtk_adjustment_set_page_increment(adj, button_size / 2);
#else
        adj->step_increment = button_size / 3;
        adj->page_increment = button_size / 2;
#endif
        gtk_adjustment_changed( adj );
        g_signal_connect( adj, "value-changed", G_CALLBACK(on_scroll), page_data );

        // create label
        image = gtk_image_new_from_icon_name( menu_cache_item_get_icon(MENU_CACHE_ITEM(dir)), GTK_ICON_SIZE_MENU );

        gtk_box_pack_start( GTK_BOX(label), image, FALSE, TRUE, 2 );
        gtk_box_pack_start( GTK_BOX(label), gtk_label_new( menu_cache_item_get_name(MENU_CACHE_ITEM(dir)) ), FALSE, TRUE, 2 );
        gtk_widget_show_all(label);

        // gtk_container_set_border_width( page_vbox, 4 );
        exo_wrap_table_set_col_spacing( EXO_WRAP_TABLE(table), 8 );

        viewport = gtk_viewport_new( NULL, NULL );
        gtk_container_add( GTK_CONTAINER(viewport), table );
        gtk_container_add( GTK_CONTAINER(scroll), GTK_WIDGET(viewport) );
        gtk_widget_show_all( scroll );

        gtk_box_pack_start( GTK_BOX(page_vbox), go_up_bar, FALSE, TRUE, 0 );
        gtk_box_pack_start( GTK_BOX(page_vbox), scroll, TRUE, TRUE, 0 );
        gtk_widget_show_all( page_vbox );

        gtk_notebook_append_page( GTK_NOTEBOOK(notebook), page_vbox, label );

        // set background
        gtk_widget_set_app_paintable( GTK_WIDGET(viewport), TRUE );
#if GTK_CHECK_VERSION(3, 0, 0)
        g_signal_connect( viewport, "draw", G_CALLBACK(on_viewport_draw), NULL );
#else
/*
        file = g_build_filename( BACKGROUND_DIR, groups[i].background, NULL );
        pixbuf = gdk_pixbuf_new_from_file( file, NULL );
        g_free( file );
*/
/*        GdkPixmap* pixmap;
        GdkGC *pixmap_gc=NULL;
        if( pixbuf )
        {
            pixmap = gdk_pixmap_new( gdk_get_default_root_window(), gdk_pixbuf_get_width(pixbuf), gdk_pixbuf_get_height(pixbuf), -1 );
            pixmap_gc = gdk_gc_new(pixmap);
            gdk_pixbuf_render_to_drawable(pixbuf, pixmap, pixmap_gc,
                                    0, 0, 0, 0,
                                    gdk_pixbuf_get_width(pixbuf),
                                    gdk_pixbuf_get_height(pixbuf),
                                    GDK_RGB_DITHER_NORMAL, 0, 0 );
            g_object_unref( pixbuf );

            g_object_weak_ref( G_OBJECT(viewport), (GWeakNotify)g_object_unref, pixmap );
            g_object_unref(pixmap_gc);
        }
        g_signal_connect( viewport, "expose_event", G_CALLBACK(on_viewport_expose), pixmap );
*/
		GdkPixmap* background;	//start add
		GtkStyle* style;
		//char* file_path;
		
		//file_path = "/home/kid/Pictures/background1.JPG";
		bg_img_path = g_key_file_get_string(key_file, "Main", "BG_IMG_PATH", NULL);
		pixbuf = gdk_pixbuf_new_from_file_at_scale(bg_img_path, area_width, area_height, FALSE, NULL);
		if(pixbuf){
	    	gdk_pixbuf_render_pixmap_and_mask (pixbuf, &background, NULL, 0);
	    	style = gtk_style_new ();
	    	style->bg_pixmap[0] = background;
	    	gtk_widget_set_style (GTK_WIDGET(viewport), GTK_STYLE(style));
		}
		else g_signal_connect( viewport, "expose_event", G_CALLBACK(on_viewport_expose), NULL );	//end add
#endif

        page_data->page_vbox = page_vbox;
        page_data->go_up_bar = go_up_bar;
        page_data->table = table;

        notebook_page_chdir( page_data, dir );
    }
}

gchar* get_xdg_config_file(const char *name) {
    const gchar *user_dir = g_get_user_config_dir();
    const gchar **system_dirs = g_get_system_config_dirs();
    const gchar **dir;
    gchar *file;

    file = g_build_filename(user_dir, name, NULL);
    if (g_file_test(file, G_FILE_TEST_EXISTS) == TRUE)
    	return file;
    free(file);

    for (dir = system_dirs; *dir; ++dir ) {
        file = g_build_filename(*dir, name, NULL);
        if (g_file_test(file, G_FILE_TEST_EXISTS) == TRUE)
            return file;
	    free(file);
    }
    return NULL;
}

static void on_menu_cache_reload(MenuCache* mc, gpointer user_data)
{
    GMainLoop* mainloop = (GMainLoop*)user_data;
    g_main_loop_quit(mainloop);
}

MenuCache* _menu_cache_lookup_sync( const char* menu_name )
{
    MenuCache* mc = menu_cache_lookup(menu_name);
    /* ensure that the menu cache is loaded */
    if(! menu_cache_get_root_dir(mc)) /* if it's not yet loaded */
    {
        GMainLoop* mainloop = g_main_loop_new(NULL, FALSE);
        gpointer notify_id = menu_cache_add_reload_notify(mc, on_menu_cache_reload, mainloop);
        g_main_loop_run(mainloop);
        g_main_loop_unref(mainloop);
        menu_cache_remove_reload_notify(mc, notify_id);
    }
    return mc;
}

int main(int argc, char** argv)
{
    int i;
    GdkRectangle working_area;
    GList* l;
    gboolean enable_key=0;
    GtkSettings *settings;
    const gchar* prefix = g_getenv("XDG_MENU_PREFIX");

#ifdef ENABLE_NLS
    bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);
#endif

    key_file = g_key_file_new();

    GError *error = NULL;
    gchar *config_file = get_xdg_config_file(CONFIG_FILE);
    if (config_file &&
        g_key_file_load_from_file(key_file,
                      config_file,
                      G_KEY_FILE_NONE,
                      &error)) {
    	printf("Loaded %s\n", config_file);
    }
    else {
    	perror("Error loading " CONFIG_FILE);
    }
    if (config_file)
	free(config_file);

    gtk_init( &argc, &argv );

    // Add application specific properties
    gtk_settings_install_property(
                             g_param_spec_boolean("lxlauncher-enable-key",
                            _("Enable key navigation"),
                            _("Allow users to use up/down/left/right/tab/enter keys to operate the lxlauncher"),
                            FALSE,
                            G_PARAM_READWRITE));
    // set up themes for notebook
    gchar* gtkrc_file = get_xdg_config_file("lxlauncher/gtkrc");
    if (gtkrc_file) {
        gtk_rc_parse(gtkrc_file);
    	free(gtkrc_file);
    }

    button_size = g_key_file_get_integer(key_file, "Main", "BUTTON_SIZE", NULL);
    img_size = g_key_file_get_integer(key_file, "Main", "IMG_SIZE", NULL);

    // to prevent from going without configure file
    if(!button_size)
        button_size = BUTTON_SIZE_FALLBACK;
    if(!img_size)
        img_size = IMG_SIZE_FALLBACK;

    icon_size = gtk_icon_size_register( "ALIcon", img_size, img_size );

    main_window = gtk_window_new( GTK_WINDOW_TOPLEVEL );
    gtk_window_move( GTK_WINDOW(main_window), 0, 0 );
    gtk_window_set_skip_pager_hint( GTK_WINDOW(main_window), TRUE );
    gtk_window_set_skip_taskbar_hint( GTK_WINDOW(main_window), TRUE );

    gtk_widget_realize( main_window );
#if GTK_CHECK_VERSION(2, 14, 0)
    gdk_window_set_keep_below( gtk_widget_get_window(main_window), TRUE );
    //gdk_window_set_decorations( main_window->window );
    gdk_window_set_type_hint( gtk_widget_get_window(main_window), GDK_WINDOW_TYPE_HINT_DESKTOP );
#else
    gdk_window_set_keep_below( main_window->window, TRUE );
    //gdk_window_set_decorations( main_window->window );
    gdk_window_set_type_hint( main_window->window, GDK_WINDOW_TYPE_HINT_DESKTOP );
#endif
    gtk_window_set_position( GTK_WINDOW(main_window), GTK_WIN_POS_NONE );
    //gtk_window_set_gravity(GDK_GRAVITY_STATIC );

    /* Remove resize grip with GTK3
    */
#if GTK_CHECK_VERSION(3, 0, 0)
    gtk_window_set_has_resize_grip(GTK_WINDOW(main_window), FALSE);
#else
#endif

    g_signal_connect(main_window, "delete-event", G_CALLBACK(window_delete), NULL);

    atom_NET_WORKAREA = XInternAtom( GDK_DISPLAY_XDISPLAY(gdk_display_get_default()), "_NET_WORKAREA", True);
    XSelectInput(GDK_DISPLAY_XDISPLAY(gdk_display_get_default()), GDK_WINDOW_XID(gtk_widget_get_root_window(main_window)), PropertyChangeMask );
    gdk_window_add_filter( gtk_widget_get_root_window(main_window), evt_filter, NULL );

    notebook = gtk_notebook_new();
    settings = gtk_widget_get_settings(GTK_WIDGET(main_window));
    g_object_get(settings, "lxlauncher-enable-key", &enable_key,NULL);
    
    if (!enable_key)
#if GTK_CHECK_VERSION(2, 18, 0)
        gtk_widget_set_can_focus(notebook, FALSE );
#else
        GTK_WIDGET_UNSET_FLAGS(notebook, GTK_CAN_FOCUS );
#endif
    gtk_container_add( (GtkContainer*)main_window, notebook );
    
    if ((prefix == NULL) || (strcmp(prefix, "lxde-") == 0))
        g_setenv("XDG_MENU_PREFIX", "lxlauncher-", TRUE);

    menu_tree = _menu_cache_lookup_sync( "applications.menu" );
    if(!menu_tree)
    {
        g_print("Unable to load application menu\n");
        return 1;
    }
    root_dir = menu_cache_item_ref(MENU_CACHE_ITEM(menu_cache_get_root_dir( menu_tree )));
    reload_notify_id = menu_cache_add_reload_notify( menu_tree, on_menu_tree_changed, NULL );

    //create_notebook_pages();	//removed

    get_working_area( gtk_widget_get_screen(main_window), &working_area );
    gtk_window_move( GTK_WINDOW(main_window), working_area.x, working_area.y );
    gtk_window_resize( GTK_WINDOW(main_window), working_area.width, working_area.height );
    
    area_width = working_area.width;	//added
    area_height = working_area.height;	//added
    
    create_notebook_pages();	//moved from above

    gtk_widget_show_all( main_window );
    gtk_main();

    gdk_window_remove_filter( gtk_widget_get_root_window(main_window), evt_filter, NULL );
    menu_cache_remove_reload_notify( menu_tree, reload_notify_id );
    menu_cache_item_unref( MENU_CACHE_ITEM(root_dir) );
    menu_cache_unref( menu_tree );

    return 0;
}
