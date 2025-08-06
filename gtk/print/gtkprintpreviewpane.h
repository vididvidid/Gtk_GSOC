#ifndef __GTK_PRINT_PREVIEW_PANE_H__
#define __GTK_PRINT_PREVIEW_PANE_H__

#if !defined (__GTK_H_INSIDE__) && !defined (GTK_COMPILATION)
#error "Only <gtk/gtk.h> can be included directly."
#endif

#include <gtk/gtk.h>

#ifdef HAVE_POPPLER
#include <poppler.h>
#endif

G_BEGIN_DECLS

#define GTK_TYPE_PRINT_PREVIEW_PANE                 (gtk_print_preview_pane_get_type ())
#define GTK_PRINT_PREVIEW_PANE(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), GTK_TYPE_PRINT_PREVIEW_PANE, GtkPrintPreviewPane))
#define GTK_PRINT_PREVIEW_PANE_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST  ((klass), GTK_TYPE_PRINT_PREVIEW_PANE, GtkPrintPreviewPaneClass))
#define GTK_IS_PRINT_PREVIEW_PANE(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GTK_TYPE_PRINT_PREVIEW_PANE))
#define GTK_IS_PRINT_PREVIEW_PANE_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), GTK_TYPE_PRINT_PREVIEW_PANE))
#define GTK_PRINT_PREVIEW_PANE_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS  ((obj), GTK_TYPE_PRINT_PREVIEW_PANE, GtkPrintPreviewPaneClass))

typedef struct _GtkPrintPreviewPane GtkPrintPreviewPane;
typedef struct _GtkPrintPreviewPaneClass GtkPrintPreviewPaneClass;
typedef struct _GtkPrintPreviewPanePrivate GtkPrintPreviewPanePrivate;

typedef enum {
    GTK_PRINT_PREVIEW_RESULT_PRINT,
    GTK_PRINT_PREVIEW_RESULT_CANCEL
} GtkPrintPreviewResult;

struct _GtkPrintPreviewPane
{
    GtkDialog parent_instance;
    GtkPrintPreviewPanePrivate *priv;
};

struct _GtkPrintPreviewPaneClass
{
    GtkDialogClass parent_class;
    
    /* Signals */
    void (*preview_finished) (GtkPrintPreviewPane *pane, GtkPrintPreviewResult result);
    
    /* Padding for future expansion */
    void (*_gtk_reserved1) (void);
    void (*_gtk_reserved2) (void);
    void (*_gtk_reserved3) (void);
    void (*_gtk_reserved4) (void);
};

/* Public API Functions */
GDK_AVAILABLE_IN_ALL
GType               gtk_print_preview_pane_get_type         (void) G_GNUC_CONST;

GDK_AVAILABLE_IN_ALL
GtkWidget *         gtk_print_preview_pane_new              (GtkWindow *parent);

GDK_AVAILABLE_IN_ALL
gboolean            gtk_print_preview_pane_load_pdf         (GtkPrintPreviewPane *pane, 
                                                            const gchar *pdf_file_path,
                                                            GError **error);

GDK_AVAILABLE_IN_ALL
void                gtk_print_preview_pane_set_modal        (GtkPrintPreviewPane *pane, 
                                                            gboolean modal);

/* Additional functions that might be useful */
GDK_AVAILABLE_IN_ALL
void                gtk_print_preview_pane_set_title        (GtkPrintPreviewPane *pane,
                                                            const gchar *title);

GDK_AVAILABLE_IN_ALL
const gchar *       gtk_print_preview_pane_get_title        (GtkPrintPreviewPane *pane);

GDK_AVAILABLE_IN_ALL
void                gtk_print_preview_pane_zoom_in          (GtkPrintPreviewPane *pane);

GDK_AVAILABLE_IN_ALL
void                gtk_print_preview_pane_zoom_out         (GtkPrintPreviewPane *pane);

GDK_AVAILABLE_IN_ALL
void                gtk_print_preview_pane_zoom_fit_width   (GtkPrintPreviewPane *pane);

GDK_AVAILABLE_IN_ALL
void                gtk_print_preview_pane_zoom_fit_page    (GtkPrintPreviewPane *pane);

GDK_AVAILABLE_IN_ALL
void                gtk_print_preview_pane_set_zoom         (GtkPrintPreviewPane *pane,
                                                            gdouble zoom);

GDK_AVAILABLE_IN_ALL
gdouble             gtk_print_preview_pane_get_zoom         (GtkPrintPreviewPane *pane);

GDK_AVAILABLE_IN_ALL
void                gtk_print_preview_pane_goto_page        (GtkPrintPreviewPane *pane,
                                                            gint page_number);

GDK_AVAILABLE_IN_ALL
gint                gtk_print_preview_pane_get_current_page (GtkPrintPreviewPane *pane);

GDK_AVAILABLE_IN_ALL
gint                gtk_print_preview_pane_get_n_pages      (GtkPrintPreviewPane *pane);

GDK_AVAILABLE_IN_ALL
void                gtk_print_preview_pane_next_page        (GtkPrintPreviewPane *pane);

GDK_AVAILABLE_IN_ALL
void                gtk_print_preview_pane_previous_page    (GtkPrintPreviewPane *pane);

GDK_AVAILABLE_IN_ALL
gboolean            gtk_print_preview_pane_can_go_next      (GtkPrintPreviewPane *pane);

GDK_AVAILABLE_IN_ALL
gboolean            gtk_print_preview_pane_can_go_previous  (GtkPrintPreviewPane *pane);

G_END_DECLS

#endif /*__GTK_PRINT_PREVIEW_PANE_H__ */
