#include "config.h"

#include <string.h>
#include <math.h>
#include <glib/gi18n-lib.h>
#include "gtkprintpreviewpane.h"
/*#include "gtkintl.h"*/
#include "gtkprivate.h"
#include "gtkbuilder.h"
#include <poppler/glib/poppler.h>

struct _GtkPrintPreviewPanePrivate
{
    /* UI Elements */
    GtkWidget *preview_drawing_area;
    GtkWidget *prev_page_button;
    GtkWidget *next_page_button;
    GtkWidget *page_label;
    GtkWidget *zoom_in_button;
    GtkWidget *zoom_out_button;
    GtkWidget *zoom_label;
    GtkWidget *print_button;
    GtkWidget *close_button;
    GtkWidget *scrolled_window;
    
    /* PDF Document */
    PopplerDocument *document;
    PopplerPage *current_page;
    gint current_page_num;
    gint total_pages;
    
    /* Display Properties */
    gdouble scale;
    gdouble page_width;
    gdouble page_height;
    
    /* File Path */
    gchar *pdf_file_path;
    
    /* Window Properties */
    gchar *title;
    gboolean modal;
};

enum {
    PREVIEW_FINISHED,  // Keeping the typo to match your header
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

G_DEFINE_TYPE_WITH_PRIVATE (GtkPrintPreviewPane, gtk_print_preview_pane, GTK_TYPE_WINDOW)

/* Forward declarations */
static void update_page_display (GtkPrintPreviewPane *pane);
static void update_navigation_buttons (GtkPrintPreviewPane *pane);
static void update_zoom_display (GtkPrintPreviewPane *pane);

/* Drawing area draw callback */
static void
preview_draw_func (GtkDrawingArea *area,
                   cairo_t        *cr,
                   int            width,
                   int            height,
                   gpointer       user_data)
{
    GtkPrintPreviewPane *pane = GTK_PRINT_PREVIEW_PANE (user_data);
    GtkPrintPreviewPanePrivate *priv = pane->priv;
    
    if (!priv->current_page)
        return;
    
    /* Clear Background */
    cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
    cairo_paint (cr);
    
    /* Draw page shadow */
    cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.3);
    cairo_rectangle (cr, 5, 5,
                     priv->page_width * priv->scale + 5,
                     priv->page_height * priv->scale + 5);
    cairo_fill (cr);
    
    /* Draw page background */
    cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
    cairo_rectangle (cr, 0, 0,
                     priv->page_width * priv->scale,
                     priv->page_height * priv->scale);
    cairo_fill (cr);
    
    /* Render PDF Page */
    cairo_save (cr);
    cairo_scale (cr, priv->scale, priv->scale);
    poppler_page_render (priv->current_page, cr);
    cairo_restore (cr);
}

/* Button callbacks */
static void
prev_page_clicked_cb (GtkButton *button,
                      gpointer   user_data)
{
    GtkPrintPreviewPane *pane = GTK_PRINT_PREVIEW_PANE (user_data);
    gtk_print_preview_pane_previous_page (pane);
}

static void
next_page_clicked_cb (GtkButton *button,
                      gpointer   user_data)
{
    GtkPrintPreviewPane *pane = GTK_PRINT_PREVIEW_PANE (user_data);
    gtk_print_preview_pane_next_page (pane);
}

static void
zoom_in_clicked_cb (GtkButton *button,
                    gpointer   user_data)
{
    GtkPrintPreviewPane *pane = GTK_PRINT_PREVIEW_PANE (user_data);
    gtk_print_preview_pane_zoom_in (pane);
}

static void
zoom_out_clicked_cb (GtkButton *button,
                     gpointer   user_data)
{
    GtkPrintPreviewPane *pane = GTK_PRINT_PREVIEW_PANE (user_data);
    gtk_print_preview_pane_zoom_out (pane);
}

static void
print_clicked_cb (GtkButton *button,
                  gpointer   user_data)
{
    GtkPrintPreviewPane *pane = GTK_PRINT_PREVIEW_PANE (user_data);
    g_signal_emit (pane, signals[PREVIEW_FINISHED], 0, GTK_PRINT_PREVIEW_RESULT_PRINT);
}

static void
close_clicked_cb (GtkButton *button,
                  gpointer   user_data)
{
    GtkPrintPreviewPane *pane = GTK_PRINT_PREVIEW_PANE (user_data);
    g_signal_emit (pane, signals[PREVIEW_FINISHED], 0, GTK_PRINT_PREVIEW_RESULT_CANCEL);
}

/* Update functions */
static void
update_page_display (GtkPrintPreviewPane *pane)
{
    GtkPrintPreviewPanePrivate *priv = pane->priv;
    
    if (!priv->current_page)
        return;
    
    /* Get page dimensions */
    poppler_page_get_size (priv->current_page, &priv->page_width, &priv->page_height);
    
    /* Update drawing area size */
    gtk_drawing_area_set_content_width (GTK_DRAWING_AREA (priv->preview_drawing_area),
                                        (int)(priv->page_width * priv->scale) + 10);
    gtk_drawing_area_set_content_height (GTK_DRAWING_AREA (priv->preview_drawing_area),
                                         (int)(priv->page_height * priv->scale) + 10);
    
    /* Update Page label */
    gchar *page_text = g_strdup_printf (_("Page %d of %d"),
                                        priv->current_page_num + 1,
                                        priv->total_pages);
    gtk_label_set_text (GTK_LABEL (priv->page_label), page_text);
    g_free (page_text);
    
    update_navigation_buttons (pane);
    gtk_widget_queue_draw (priv->preview_drawing_area);
}

static void
update_navigation_buttons (GtkPrintPreviewPane *pane)
{
    GtkPrintPreviewPanePrivate *priv = pane->priv;
    
    gtk_widget_set_sensitive (priv->prev_page_button, priv->current_page_num > 0);
    gtk_widget_set_sensitive (priv->next_page_button, priv->current_page_num < priv->total_pages - 1);
}

static void
update_zoom_display (GtkPrintPreviewPane *pane)
{
    GtkPrintPreviewPanePrivate *priv = pane->priv;
    
    gchar *zoom_text = g_strdup_printf ("%.0f%%", priv->scale * 100);
    gtk_label_set_text (GTK_LABEL (priv->zoom_label), zoom_text);
    g_free (zoom_text);
}

/* Window close request handler */
static gboolean
on_close_request (GtkWindow *window,
                  gpointer   user_data)
{
    GtkPrintPreviewPane *pane = GTK_PRINT_PREVIEW_PANE (window);
    g_signal_emit (pane, signals[PREVIEW_FINISHED], 0, GTK_PRINT_PREVIEW_RESULT_CANCEL);
    return TRUE; /* Prevent default close */
}

/* Load PDF document */
static gboolean
load_pdf_document (GtkPrintPreviewPane *pane,
                   const gchar         *pdf_file_path,
                   GError             **error)
{
    GtkPrintPreviewPanePrivate *priv = pane->priv;
    gchar *uri;
    
    /* Clean up existing document */
    if (priv->current_page)
    {
        g_object_unref (priv->current_page);
        priv->current_page = NULL;
    }
    
    if (priv->document)
    {
        g_object_unref (priv->document);
        priv->document = NULL;
    }
    
    /* Convert file path to URI */
    uri = g_filename_to_uri (pdf_file_path, NULL, error);
    if (!uri)
        return FALSE;
    
    /* Load Document */
    priv->document = poppler_document_new_from_file (uri, NULL, error);
    g_free (uri);
    
    if (!priv->document)
        return FALSE;
    
    /* Get document info */
    priv->total_pages = poppler_document_get_n_pages (priv->document);
    priv->current_page_num = 0;
    
    if (priv->total_pages > 0)
    {
        priv->current_page = poppler_document_get_page (priv->document, 0);
        update_page_display (pane);
    }
    
    return TRUE;
}

/* Class and instance initialization */
static void
gtk_print_preview_pane_finalize (GObject *object)
{
    GtkPrintPreviewPane *pane = GTK_PRINT_PREVIEW_PANE (object);
    GtkPrintPreviewPanePrivate *priv = pane->priv;
    
    if (priv->current_page)
        g_object_unref (priv->current_page);
    
    if (priv->document)
        g_object_unref (priv->document);
    
    g_free (priv->pdf_file_path);
    g_free (priv->title);
    
    G_OBJECT_CLASS (gtk_print_preview_pane_parent_class)->finalize (object);
}

static void
gtk_print_preview_pane_class_init (GtkPrintPreviewPaneClass *class)
{
    GObjectClass *object_class = G_OBJECT_CLASS (class);
    
    object_class->finalize = gtk_print_preview_pane_finalize;
    
    /* Signals */
    signals[PREVIEW_FINISHED] =
        g_signal_new (I_("preview-finished"),
                      G_TYPE_FROM_CLASS (class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (GtkPrintPreviewPaneClass, preview_finished),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      G_TYPE_INT);
}

static void
gtk_print_preview_pane_init (GtkPrintPreviewPane *pane)
{
    GtkPrintPreviewPanePrivate *priv;
    GtkBuilder *builder;
    GError *error = NULL;
    GtkWidget *content_box;
    GtkWidget *toolbar;
    GtkWidget *button_box;
    
    pane->priv = gtk_print_preview_pane_get_instance_private (pane);
    priv = pane->priv;
    
    /* Initialize properties */
    priv->scale = 1.0;
    priv->current_page_num = 0;
    priv->total_pages = 0;
    priv->title = g_strdup (_("Print Preview"));
    priv->modal = TRUE;
    
    /* Create UI programmatically (since GtkBuilder resource loading may differ) */
    content_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child (GTK_WINDOW (pane), content_box);
    
    /* Create toolbar */
    toolbar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start (toolbar, 6);
    gtk_widget_set_margin_end (toolbar, 6);
    gtk_widget_set_margin_top (toolbar, 6);
    gtk_widget_set_margin_bottom (toolbar, 6);
    gtk_box_append (GTK_BOX (content_box), toolbar);
    
    /* Navigation buttons */
    priv->prev_page_button = gtk_button_new_with_label (_("Previous"));
    priv->next_page_button = gtk_button_new_with_label (_("Next"));
    priv->page_label = gtk_label_new (_("Page 1 of 1"));
    
    gtk_box_append (GTK_BOX (toolbar), priv->prev_page_button);
    gtk_box_append (GTK_BOX (toolbar), priv->next_page_button);
    gtk_box_append (GTK_BOX (toolbar), priv->page_label);
    
    /* Zoom controls */
    priv->zoom_out_button = gtk_button_new_with_label (_("Zoom Out"));
    priv->zoom_in_button = gtk_button_new_with_label (_("Zoom In"));
    priv->zoom_label = gtk_label_new ("100%");
    
    gtk_box_append (GTK_BOX (toolbar), priv->zoom_out_button);
    gtk_box_append (GTK_BOX (toolbar), priv->zoom_in_button);
    gtk_box_append (GTK_BOX (toolbar), priv->zoom_label);
    
    /* Action buttons */
    button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    priv->print_button = gtk_button_new_with_label (_("Print"));
    priv->close_button = gtk_button_new_with_label (_("Close"));
    
    gtk_box_append (GTK_BOX (button_box), priv->print_button);
    gtk_box_append (GTK_BOX (button_box), priv->close_button);
    gtk_box_append (GTK_BOX (toolbar), button_box);
    
    /* Create scrolled window for drawing area */
    priv->scrolled_window = gtk_scrolled_window_new ();
    gtk_widget_set_vexpand (priv->scrolled_window, TRUE);
    gtk_widget_set_hexpand (priv->scrolled_window, TRUE);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (priv->scrolled_window),
                                    GTK_POLICY_AUTOMATIC,
                                    GTK_POLICY_AUTOMATIC);
    gtk_box_append (GTK_BOX (content_box), priv->scrolled_window);
    
    /* Create drawing area */
    priv->preview_drawing_area = gtk_drawing_area_new ();
    gtk_drawing_area_set_content_width (GTK_DRAWING_AREA (priv->preview_drawing_area), 400);
    gtk_drawing_area_set_content_height (GTK_DRAWING_AREA (priv->preview_drawing_area), 600);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (priv->scrolled_window),
                                   priv->preview_drawing_area);
    
    /* Set up drawing area */
    gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (priv->preview_drawing_area),
                                    preview_draw_func,
                                    pane,
                                    NULL);
    
    /* Connect button signals */
    g_signal_connect (priv->prev_page_button, "clicked", G_CALLBACK (prev_page_clicked_cb), pane);
    g_signal_connect (priv->next_page_button, "clicked", G_CALLBACK (next_page_clicked_cb), pane);
    g_signal_connect (priv->zoom_in_button, "clicked", G_CALLBACK (zoom_in_clicked_cb), pane);
    g_signal_connect (priv->zoom_out_button, "clicked", G_CALLBACK (zoom_out_clicked_cb), pane);
    g_signal_connect (priv->print_button, "clicked", G_CALLBACK (print_clicked_cb), pane);
    g_signal_connect (priv->close_button, "clicked", G_CALLBACK (close_clicked_cb), pane);
    
    /* Connect window close signal */
    g_signal_connect (pane, "close-request", G_CALLBACK (on_close_request), NULL);
    
    /* Set up window properties */
    gtk_window_set_title (GTK_WINDOW (pane), priv->title);
    gtk_window_set_default_size (GTK_WINDOW (pane), 900, 700);
    gtk_window_set_modal (GTK_WINDOW (pane), priv->modal);
    
    /* Initialize zoom display */
    update_zoom_display (pane);
}

/* Public API Implementation */

GtkWidget *
gtk_print_preview_pane_new (GtkWindow *parent)
{
    g_print("yash kumar kasaudhan-> gtkprintpreviewpane.c -> gtk_print_preview_pane_new\n");
    GtkPrintPreviewPane *pane;
    
    pane = g_object_new (GTK_TYPE_PRINT_PREVIEW_PANE, NULL);
    
    if (parent)
        gtk_window_set_transient_for (GTK_WINDOW (pane), parent);
    
    return GTK_WIDGET (pane);
}

gboolean
gtk_print_preview_pane_load_pdf (GtkPrintPreviewPane *pane,
                                 const gchar         *pdf_file_path,
                                 GError             **error)
{
    g_print("yash kumar kasaudhan: gtkprintpreviewpane.c -> gtk_print_preview_pane_load_pdf\n");
    GtkPrintPreviewPanePrivate *priv;
    
    g_return_val_if_fail (GTK_IS_PRINT_PREVIEW_PANE (pane), FALSE);
    g_return_val_if_fail (pdf_file_path != NULL, FALSE);
    
    priv = pane->priv;
    
    g_free (priv->pdf_file_path);
    priv->pdf_file_path = g_strdup (pdf_file_path);
    
    return load_pdf_document (pane, pdf_file_path, error);
}

void
gtk_print_preview_pane_set_modal (GtkPrintPreviewPane *pane,
                                  gboolean             modal)
{   
    g_print("yash kumar kasaudhan: gtkprintpreviewpane.c  -> gtk_print_preview_pane_set_modal\n");
    GtkPrintPreviewPanePrivate *priv;
    
    g_return_if_fail (GTK_IS_PRINT_PREVIEW_PANE (pane));
    
    priv = pane->priv;
    priv->modal = modal;
    
    gtk_window_set_modal (GTK_WINDOW (pane), modal);
}

void
gtk_print_preview_pane_set_title (GtkPrintPreviewPane *pane,
                                  const gchar         *title)
{
    g_print("yash kumar kasaudhan: gtkprintpreviewpane.c -> gtk_print_preview_pane_set_title\n");
    GtkPrintPreviewPanePrivate *priv;
    
    g_return_if_fail (GTK_IS_PRINT_PREVIEW_PANE (pane));
    
    priv = pane->priv;
    
    g_free (priv->title);
    priv->title = g_strdup (title ? title : _("Print Preview By Yash"));
    
    gtk_window_set_title (GTK_WINDOW (pane), priv->title);
}

const gchar *
gtk_print_preview_pane_get_title (GtkPrintPreviewPane *pane)
{
    g_print("yash kumar kasaudhan: gtkprintpreviewpane.c -. gtk_print_preview_pane_get_title\n");
    GtkPrintPreviewPanePrivate *priv;
    
    g_return_val_if_fail (GTK_IS_PRINT_PREVIEW_PANE (pane), NULL);
    
    priv = pane->priv;
    return priv->title;
}

void
gtk_print_preview_pane_zoom_in (GtkPrintPreviewPane *pane)
{
    g_print("yash kumar kasaudhan: gtkprintpreviewpane.c -> gtk_print_preview_pane_zoom_in\n");
    GtkPrintPreviewPanePrivate *priv;
    
    g_return_if_fail (GTK_IS_PRINT_PREVIEW_PANE (pane));
    
    priv = pane->priv;
    
    if (priv->scale < 3.0)
    {
        priv->scale *= 1.2;
        update_page_display (pane);
        update_zoom_display (pane);
    }
}

void
gtk_print_preview_pane_zoom_out (GtkPrintPreviewPane *pane)
{
    g_print("yash kumar kasaudhan: gtkprintpreviewpane.c -> gtk_print_preview_pane_zoom_out\n");
    GtkPrintPreviewPanePrivate *priv;
    
    g_return_if_fail (GTK_IS_PRINT_PREVIEW_PANE (pane));
    
    priv = pane->priv;
    
    if (priv->scale > 0.2)
    {
        priv->scale /= 1.2;
        update_page_display (pane);
        update_zoom_display (pane);
    }
}

void
gtk_print_preview_pane_zoom_fit_width (GtkPrintPreviewPane *pane)
{
    g_print("yash kumar kasaudhan: gtkprintpreviewpane.c -> gtk_print_preview_pane_zoom_fit_width\n");
    GtkPrintPreviewPanePrivate *priv;
    gint widget_width;
    
    g_return_if_fail (GTK_IS_PRINT_PREVIEW_PANE (pane));
    
    priv = pane->priv;
    
    if (!priv->current_page)
        return;
    
    /* Get the available width from the scrolled window */
    widget_width = gtk_widget_get_width (priv->scrolled_window);
    
    if (widget_width > 0 && priv->page_width > 0)
    {
        priv->scale = (widget_width - 20) / priv->page_width; /* 20px margin */
        update_page_display (pane);
        update_zoom_display (pane);
    }
}

void
gtk_print_preview_pane_zoom_fit_page (GtkPrintPreviewPane *pane)
{
    g_print("yash kumar kasaudhan: gtkprintpreviewpane.c -> gtk_print_preview_pane_zoom_fit_page\n");
    GtkPrintPreviewPanePrivate *priv;
    gint widget_width, widget_height;
    gdouble scale_x, scale_y;
    
    g_return_if_fail (GTK_IS_PRINT_PREVIEW_PANE (pane));
    
    priv = pane->priv;
    
    if (!priv->current_page)
        return;
    
    /* Get the available size from the scrolled window */
    widget_width = gtk_widget_get_width (priv->scrolled_window);
    widget_height = gtk_widget_get_height (priv->scrolled_window);
    
    if (widget_width > 0 && widget_height > 0 && 
        priv->page_width > 0 && priv->page_height > 0)
    {
        scale_x = (widget_width - 20) / priv->page_width;   /* 20px margin */
        scale_y = (widget_height - 20) / priv->page_height; /* 20px margin */
        
        priv->scale = MIN (scale_x, scale_y);
        update_page_display (pane);
        update_zoom_display (pane);
    }
}

void
gtk_print_preview_pane_set_zoom (GtkPrintPreviewPane *pane,
                                 gdouble              zoom)
{
    g_print("yash kumar kasaudhan: gtkprintpreviewpane.c -> gtk_print_preview_pane_set_zoom\n");
    GtkPrintPreviewPanePrivate *priv;
    
    g_return_if_fail (GTK_IS_PRINT_PREVIEW_PANE (pane));
    g_return_if_fail (zoom > 0.0);
    
    priv = pane->priv;
    
    priv->scale = CLAMP (zoom, 0.1, 5.0);
    update_page_display (pane);
    update_zoom_display (pane);
}

gdouble
gtk_print_preview_pane_get_zoom (GtkPrintPreviewPane *pane)
{
    g_print("yash kumar kasaudhan: gtkprintpreviewpane.c -> gtk_print_preview_pane_get_zoom\n");
    GtkPrintPreviewPanePrivate *priv;
    
    g_return_val_if_fail (GTK_IS_PRINT_PREVIEW_PANE (pane), 1.0);
    
    priv = pane->priv;
    return priv->scale;
}

void
gtk_print_preview_pane_goto_page (GtkPrintPreviewPane *pane,
                                  gint                 page_number)
{
    g_print("yash kumar kasaudhan: gtkprintpreviewpane.c -> gtk_print_preview_pane_goto_page\n");
    GtkPrintPreviewPanePrivate *priv;
    
    g_return_if_fail (GTK_IS_PRINT_PREVIEW_PANE (pane));
    
    priv = pane->priv;
    
    if (!priv->document)
        return;
    
    page_number = CLAMP (page_number, 0, priv->total_pages - 1);
    
    if (page_number != priv->current_page_num)
    {
        priv->current_page_num = page_number;
        
        if (priv->current_page)
            g_object_unref (priv->current_page);
        
        priv->current_page = poppler_document_get_page (priv->document, priv->current_page_num);
        update_page_display (pane);
    }
}

gint
gtk_print_preview_pane_get_current_page (GtkPrintPreviewPane *pane)
{
    g_print("yash kumar kasaudhan: gtkprintpreviewpane.c -> gtk_print_preview_pane_get_current_page\n");
    GtkPrintPreviewPanePrivate *priv;
    
    g_return_val_if_fail (GTK_IS_PRINT_PREVIEW_PANE (pane), 0);
    
    priv = pane->priv;
    return priv->current_page_num;
}

gint
gtk_print_preview_pane_get_n_pages (GtkPrintPreviewPane *pane)
{
    g_print("yash kumar kasaudhan: gtkprintpreviewpane.c -> gtk_print_preview_pane_get_n_pages\n");
    GtkPrintPreviewPanePrivate *priv;
    
    g_return_val_if_fail (GTK_IS_PRINT_PREVIEW_PANE (pane), 0);
    
    priv = pane->priv;
    return priv->total_pages;
}

void
gtk_print_preview_pane_next_page (GtkPrintPreviewPane *pane)
{
    g_print("yash kumar kasaudhan: gtkprintpreviewpane.c -> gtk_print_preview_pane_next_page\n");
    GtkPrintPreviewPanePrivate *priv;
    
    g_return_if_fail (GTK_IS_PRINT_PREVIEW_PANE (pane));
    
    priv = pane->priv;
    
    if (priv->current_page_num < priv->total_pages - 1)
    {
        gtk_print_preview_pane_goto_page (pane, priv->current_page_num + 1);
    }
}

void
gtk_print_preview_pane_previous_page (GtkPrintPreviewPane *pane)
{
    g_print("yash kumar kasaudhan: gtkprintpreviewpane.c -> gtk_print_preview_pane_previous_page\n");
    GtkPrintPreviewPanePrivate *priv;
    
    g_return_if_fail (GTK_IS_PRINT_PREVIEW_PANE (pane));
    
    priv = pane->priv;
    
    if (priv->current_page_num > 0)
    {
        gtk_print_preview_pane_goto_page (pane, priv->current_page_num - 1);
    }
}

gboolean
gtk_print_preview_pane_can_go_next (GtkPrintPreviewPane *pane)
{
    g_print("yash kumar kasaudhan: gtkprintpreviewpane.c -> gtk_print_preview_pane_can_go_next\n");
    GtkPrintPreviewPanePrivate *priv;
    
    g_return_val_if_fail (GTK_IS_PRINT_PREVIEW_PANE (pane), FALSE);
    
    priv = pane->priv;
    return (priv->current_page_num < priv->total_pages - 1);
}

gboolean
gtk_print_preview_pane_can_go_previous (GtkPrintPreviewPane *pane)
{
    g_print("yash kumar kasaudhan: gtkprintpreviewpane.c -> gtk_print_preview_pane_can_go_previous\n");
    GtkPrintPreviewPanePrivate *priv;
    
    g_return_val_if_fail (GTK_IS_PRINT_PREVIEW_PANE (pane), FALSE);
    
    priv = pane->priv;
    return (priv->current_page_num > 0);
}
