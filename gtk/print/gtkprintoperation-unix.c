/* GTK - The GIMP Toolkit
 * gtkprintoperation-unix.c: Print Operation Details for Unix
 *                           and Unix-like platforms
 * Copyright (C) 2006, Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>

#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>
#include "gtkprivate.h"

#include "gtkprintoperation-private.h"
#include "gtkprintoperation-portal.h"

#include <cairo.h>
#ifdef CAIRO_HAS_PDF_SURFACE
#include <cairo-pdf.h>
#endif
#ifdef CAIRO_HAS_PS_SURFACE
#include <cairo-ps.h>
#endif
#include "gtkprintunixdialog.h"
#include "gtkpagesetupunixdialog.h"
#include "gtkprintbackendprivate.h"
#include "gtkprinter.h"
#include "gtkprintjob.h"
#include "gtkprintpreviewpane.h"

typedef struct
{
  GtkWindow *parent;        /* just in case we need to throw error dialogs */
  GMainLoop *loop;
  gboolean data_sent;

  /* Real printing (not preview) */
  GtkPrintJob *job;         /* the job we are sending to the printer */
  cairo_surface_t *surface;
  gulong job_status_changed_tag;


} GtkPrintOperationUnix;

typedef struct _PrinterFinder PrinterFinder;

typedef struct {
  GMainLoop *loop;
  GtkPrintPreviewResult result;
} PreviewRunData;

static void 
preview_pane_response_cb (GtkPrintPreviewPane *pane,
                          GtkPrintPreviewResult result,
                          gpointer user_data)
{
    PreviewRunData *pr = user_data;
    pr->result = result;
    if (pr->loop && g_main_loop_is_running(pr->loop))
        g_main_loop_quit(pr->loop);
}
// Add these forward declarations after the typedef struct sections
static cairo_status_t write_preview (void *closure, const unsigned char *data, unsigned int length);
static void close_preview (void *data);
static void printer_finder_free (PrinterFinder *finder);
static void find_printer        (const char    *printer,
                                 GFunc          func,
                                 gpointer       data);

static void
unix_start_page (GtkPrintOperation *op,
                 GtkPrintContext   *print_context,
                 GtkPageSetup      *page_setup)
{
  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c ->   unix_start_page   \n");
  GtkPrintOperationUnix *op_unix;
  GtkPaperSize *paper_size;
  cairo_surface_type_t type;
  double w, h;

  op_unix = op->priv->platform_data;

  paper_size = gtk_page_setup_get_paper_size (page_setup);

  w = gtk_paper_size_get_width (paper_size, GTK_UNIT_POINTS);
  h = gtk_paper_size_get_height (paper_size, GTK_UNIT_POINTS);

  type = cairo_surface_get_type (op_unix->surface);

  if ((op->priv->manual_number_up < 2) ||
      (op->priv->page_position % op->priv->manual_number_up == 0))
    {
      if (type == CAIRO_SURFACE_TYPE_PS)
        {
#ifdef CAIRO_HAS_PS_SURFACE
          cairo_ps_surface_set_size (op_unix->surface, w, h);
          cairo_ps_surface_dsc_begin_page_setup (op_unix->surface);
          switch (gtk_page_setup_get_orientation (page_setup))
            {
              case GTK_PAGE_ORIENTATION_PORTRAIT:
              case GTK_PAGE_ORIENTATION_REVERSE_PORTRAIT:
                cairo_ps_surface_dsc_comment (op_unix->surface, "%%PageOrientation: Portrait");
                break;

              case GTK_PAGE_ORIENTATION_LANDSCAPE:
              case GTK_PAGE_ORIENTATION_REVERSE_LANDSCAPE:
                cairo_ps_surface_dsc_comment (op_unix->surface, "%%PageOrientation: Landscape");
                break;
              default:
                break;
            }
#endif
         }
      else if (type == CAIRO_SURFACE_TYPE_PDF)
        {
#ifdef CAIRO_HAS_PDF_SURFACE
          if (!op->priv->manual_orientation)
            {
              w = gtk_page_setup_get_paper_width (page_setup, GTK_UNIT_POINTS);
              h = gtk_page_setup_get_paper_height (page_setup, GTK_UNIT_POINTS);
            }
          cairo_pdf_surface_set_size (op_unix->surface, w, h);
#endif
        }
    }
}

static void
unix_end_page (GtkPrintOperation *op,
               GtkPrintContext   *print_context)
{
  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c -> unix_end_page     \n");
  cairo_t *cr;

  cr = gtk_print_context_get_cairo_context (print_context);

  if ((op->priv->manual_number_up < 2) ||
      ((op->priv->page_position + 1) % op->priv->manual_number_up == 0) ||
      (op->priv->page_position == op->priv->nr_of_pages_to_print - 1))
    cairo_show_page (cr);
}

static void
op_unix_free (GtkPrintOperationUnix *op_unix)
{

  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c -> op_unix_free     \n");
  if (op_unix->job)
    {
      if (op_unix->job_status_changed_tag > 0)
        g_signal_handler_disconnect (op_unix->job,
                                     op_unix->job_status_changed_tag);
      g_object_unref (op_unix->job);
    }

  g_free (op_unix);
}

static char *
shell_command_substitute_file (const char *cmd,
                               const char *pdf_filename,
                               const char *settings_filename,
                               gboolean    *pdf_filename_replaced,
                               gboolean    *settings_filename_replaced)
{

  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c ->   shell_command_substitue_file   \n");
  const char *inptr, *start;
  GString *final;

  g_return_val_if_fail (cmd != NULL, NULL);
  g_return_val_if_fail (pdf_filename != NULL, NULL);
  g_return_val_if_fail (settings_filename != NULL, NULL);

  final = g_string_new (NULL);

  *pdf_filename_replaced = FALSE;
  *settings_filename_replaced = FALSE;

  start = inptr = cmd;
  while ((inptr = strchr (inptr, '%')) != NULL)
    {
      g_string_append_len (final, start, inptr - start);
      inptr++;
      switch (*inptr)
        {
          case 'f':
            g_string_append (final, pdf_filename);
            *pdf_filename_replaced = TRUE;
            break;

          case 's':
            g_string_append (final, settings_filename);
            *settings_filename_replaced = TRUE;
            break;

          case '%':
            g_string_append_c (final, '%');
            break;

          default:
            g_string_append_c (final, '%');
            if (*inptr)
              g_string_append_c (final, *inptr);
            break;
        }
      if (*inptr)
        inptr++;
      start = inptr;
    }
  g_string_append (final, start);

  return g_string_free (final, FALSE);
}


static void
gtk_print_operation_unix_launch_preview (GtkPrintOperation *op,
                                         cairo_surface_t   *surface,
                                         GtkWindow         *parent,
                                         const char        *filename)
{
  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c -> gtk_print_operation_unix_launch_preview\n");
  GtkWidget *preview;
  GError *error = NULL;
  PreviewRunData pr = {0};

  cairo_surface_destroy (surface);

  preview = gtk_print_preview_pane_new (parent);
  g_print("%s is the file name that you want to see",filename);
  if (!gtk_print_preview_pane_load_pdf (GTK_PRINT_PREVIEW_PANE (preview), filename, &error))
  {
    g_warning ("Preview failed : %s ", error->message);
    g_clear_error (&error);
    return;
  }

  gtk_window_set_transient_for (GTK_WINDOW (preview), parent);
  gtk_window_set_modal (GTK_WINDOW (preview), TRUE);

  pr.loop = g_main_loop_new (NULL, FALSE);
  g_signal_connect (preview, "preview-finished", G_CALLBACK (preview_pane_response_cb), &pr);

  gtk_window_present (GTK_WINDOW (preview));
  g_main_loop_run (pr.loop);

  g_main_loop_unref (pr.loop);

  gtk_window_destroy (GTK_WINDOW (preview));

}

static void
unix_finish_send  (GtkPrintJob  *job,
                   gpointer      user_data,
                   const GError *error)
{

  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c ->   unix_finish_send   \n");
  GtkPrintOperation *op = (GtkPrintOperation *) user_data;
  GtkPrintOperationUnix *op_unix = op->priv->platform_data;

  if (error != NULL && op->priv->error == NULL)
    op->priv->error = g_error_copy (error);

  op_unix->data_sent = TRUE;

  if (op_unix->loop)
    g_main_loop_quit (op_unix->loop);

  g_object_unref (op);
}

static void
unix_end_run (GtkPrintOperation *op,
              gboolean           wait,
              gboolean           cancelled)
{

  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c ->  unix_end_run    \n");
  GtkPrintOperationUnix *op_unix = op->priv->platform_data;

    cairo_surface_finish (op_unix->surface);

  if (cancelled)
    return;


  if (wait)
    op_unix->loop = g_main_loop_new (NULL, FALSE);

  /* TODO: Check for error */
  if (op_unix->job != NULL)
    {
      g_object_ref (op);
      gtk_print_job_send (op_unix->job,
                          unix_finish_send,
                          op, NULL);
    }

  if (wait)
    {
      g_object_ref (op);
      if (!op_unix->data_sent)
        g_main_loop_run (op_unix->loop);
      g_main_loop_unref (op_unix->loop);
      op_unix->loop = NULL;
      g_object_unref (op);
    }
}

static void
job_status_changed_cb (GtkPrintJob       *job,
                       GtkPrintOperation *op)
{
  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c ->  job_status_changes_cb   \n");
  _gtk_print_operation_set_status (op, gtk_print_job_get_status (job), NULL);
}


static void
print_setup_changed_cb (GtkPrintUnixDialog *print_dialog,
                        GParamSpec         *pspec,
                        gpointer            user_data)
{

  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c ->  print_setup_changed_cb    \n");
  GtkPageSetup             *page_setup;
  GtkPrintSettings         *print_settings;
  GtkPrintOperation        *op = user_data;
  GtkPrintOperationPrivate *priv = op->priv;

  page_setup = gtk_print_unix_dialog_get_page_setup (print_dialog);
  print_settings = gtk_print_unix_dialog_get_settings (print_dialog);

  g_signal_emit_by_name (op,
                         "update-custom-widget",
                         priv->custom_widget,
                         page_setup,
                         print_settings);
}

static GtkWidget *
get_print_dialog (GtkPrintOperation *op,
                  GtkWindow         *parent)
{
  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c ->   get_print_dialog   \n");
  GtkPrintOperationPrivate *priv = op->priv;
  GtkWidget *pd, *label;
  const char *custom_tab_label;

  pd = gtk_print_unix_dialog_new (NULL, parent);

  gtk_print_unix_dialog_set_manual_capabilities (GTK_PRINT_UNIX_DIALOG (pd),
                                                 GTK_PRINT_CAPABILITY_PAGE_SET |
                                                 GTK_PRINT_CAPABILITY_COPIES |
                                                 GTK_PRINT_CAPABILITY_COLLATE |
                                                 GTK_PRINT_CAPABILITY_REVERSE |
                                                 GTK_PRINT_CAPABILITY_SCALE |
                                                 GTK_PRINT_CAPABILITY_PREVIEW |
                                                 GTK_PRINT_CAPABILITY_NUMBER_UP |
                                                 GTK_PRINT_CAPABILITY_NUMBER_UP_LAYOUT);

  if (priv->print_settings)
    gtk_print_unix_dialog_set_settings (GTK_PRINT_UNIX_DIALOG (pd),
                                        priv->print_settings);

  if (priv->default_page_setup)
    gtk_print_unix_dialog_set_page_setup (GTK_PRINT_UNIX_DIALOG (pd),
                                          priv->default_page_setup);

  gtk_print_unix_dialog_set_embed_page_setup (GTK_PRINT_UNIX_DIALOG (pd),
                                              priv->embed_page_setup);

  gtk_print_unix_dialog_set_current_page (GTK_PRINT_UNIX_DIALOG (pd),
                                          priv->current_page);

  gtk_print_unix_dialog_set_support_selection (GTK_PRINT_UNIX_DIALOG (pd),
                                               priv->support_selection);

  gtk_print_unix_dialog_set_has_selection (GTK_PRINT_UNIX_DIALOG (pd),
                                           priv->has_selection);

  g_signal_emit_by_name (op, "create-custom-widget",
                         &priv->custom_widget);

  if (priv->custom_widget)
    {
      custom_tab_label = priv->custom_tab_label;

      if (custom_tab_label == NULL)
        {
          custom_tab_label = g_get_application_name ();
          if (custom_tab_label == NULL)
            custom_tab_label = _("Application");
        }

      label = gtk_label_new (custom_tab_label);

      gtk_print_unix_dialog_add_custom_tab (GTK_PRINT_UNIX_DIALOG (pd),
                                            priv->custom_widget, label);

      g_signal_connect (pd, "notify::selected-printer", (GCallback) print_setup_changed_cb, op);
      g_signal_connect (pd, "notify::page-setup", (GCallback) print_setup_changed_cb, op);
    }

  return pd;
}

typedef struct
{
  GtkPrintOperation           *op;
  gboolean                     do_print;
  gboolean                     do_preview;
  GtkPrintOperationResult      result;
  GtkPrintOperationPrintFunc   print_cb;
  GDestroyNotify               destroy;
  GtkWindow                   *parent;
  GMainLoop                   *loop;
} PrintResponseData;

static void
print_response_data_free (gpointer data)
{

  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c ->  print_response_data_free   \n");
  PrintResponseData *rdata = data;

  g_object_unref (rdata->op);
  g_free (rdata);
}

 static void
finish_print (PrintResponseData *rdata,
              GtkPrinter        *printer,
              GtkPageSetup      *page_setup,
              GtkPrintSettings  *settings,
              gboolean           page_setup_set)
{
  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c -> finish_print\n");
  GtkPrintOperation *op = rdata->op;
  GtkPrintOperationPrivate *priv = op->priv;
  GtkPrintJob *job;
  double top, bottom, left, right;

  if (rdata->do_print)
    {
      gtk_print_operation_set_print_settings (op, settings);
      priv->print_context = _gtk_print_context_new (op);

      if (gtk_print_settings_get_number_up (settings) < 2)
        {
          if (printer &&
              (gtk_printer_get_hard_margins_for_paper_size (
                   printer,
                   gtk_page_setup_get_paper_size (page_setup),
                   &top, &bottom, &left, &right) ||
               gtk_printer_get_hard_margins (printer,
                                             &top, &bottom, &left, &right)))
            _gtk_print_context_set_hard_margins (priv->print_context,
                                                 top, bottom, left, right);
        }
      else
        {
          _gtk_print_context_set_hard_margins (priv->print_context, 0, 0, 0, 0);
        }

      if (page_setup != NULL &&
          (gtk_print_operation_get_default_page_setup (op) == NULL || page_setup_set))
        gtk_print_operation_set_default_page_setup (op, page_setup);

      _gtk_print_context_set_page_setup (priv->print_context, page_setup);

      GtkPrintOperationUnix *op_unix = g_new0 (GtkPrintOperationUnix, 1);
      priv->platform_data = op_unix;
      op_unix->parent = rdata->parent;

      priv->start_page = unix_start_page;
      priv->end_page   = unix_end_page;
      priv->end_run    = unix_end_run;

      if (rdata->do_preview)
        {
          /* ---- PREVIEW BRANCH ---- */
          g_print("inside teh preview branch \n");
          double dpi_x = 72.0, dpi_y = 72.0;
          char *preview_filename = NULL;

          op_unix->surface =
              _gtk_print_operation_platform_backend_create_preview_surface
                (op, page_setup, &dpi_x, &dpi_y, &preview_filename);
          g_print("got the material \n");
          if (!op_unix->surface)
            {
              g_free (preview_filename);
              rdata->result = GTK_PRINT_OPERATION_RESULT_ERROR;
            }
          else
            {
              cairo_t *cr = cairo_create (op_unix->surface);
              gtk_print_context_set_cairo_context (priv->print_context, cr,
                                                   dpi_x, dpi_y);
              cairo_destroy (cr);
              g_print("after the cairo_destroy funciton\n");

              /* Store filename in private data so unix_end_run can use it */
              priv->export_filename = preview_filename;

              /* DO NOT launch preview here – let unix_end_run do it */
            }
            g_print("completed the if\n");
        }
      else
        {
          g_print("inside the else \n");
          /* ---- REAL PRINT BRANCH ---- */
          job = gtk_print_job_new (priv->job_name, printer,
                                   settings, page_setup);
          op_unix->job = job;
          gtk_print_job_set_track_print_status (job, priv->track_print_status);

          op_unix->surface = gtk_print_job_get_surface (job, &priv->error);
          if (!op_unix->surface)
            {
              rdata->result = GTK_PRINT_OPERATION_RESULT_ERROR;
              rdata->do_print = FALSE;
              goto out;
            }

          cairo_t *cr = cairo_create (op_unix->surface);
          gtk_print_context_set_cairo_context (priv->print_context, cr, 72, 72);
          cairo_destroy (cr);

          _gtk_print_operation_set_status (op, gtk_print_job_get_status (job), NULL);

          op_unix->job_status_changed_tag =
            g_signal_connect (job, "status-changed",
                              G_CALLBACK (job_status_changed_cb), op);

          priv->print_pages         = gtk_print_job_get_pages (job);
          priv->page_ranges         = gtk_print_job_get_page_ranges (job,
                                                                     &priv->num_page_ranges);
          priv->manual_num_copies   = gtk_print_job_get_num_copies (job);
          priv->manual_collation    = gtk_print_job_get_collate (job);
          priv->manual_reverse      = gtk_print_job_get_reverse (job);
          priv->manual_page_set     = gtk_print_job_get_page_set (job);
          priv->manual_scale        = gtk_print_job_get_scale (job);
          priv->manual_orientation  = gtk_print_job_get_rotate (job);
          priv->manual_number_up    = gtk_print_job_get_n_up (job);
          priv->manual_number_up_layout = gtk_print_job_get_n_up_layout (job);
        }
        g_print("completed \n");
    }

out:
  g_print("outside \n");
  if (rdata->print_cb)
    rdata->print_cb (op, rdata->parent, rdata->do_print, rdata->result);
  g_print("outside2 \n");
  if (rdata->destroy)
    rdata->destroy (rdata);
  g_print("outside 3\n");
}
static void
preview_pane_cleanup_cb(GtkWidget *widget, gpointer user_data)
{
  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c -> preview_pane_cleanup_cb\n");
    char *filename = user_data;
    g_print("Preview pane destroyed. Deleting temp file: %s\n", filename);
    g_unlink(filename);
    g_free(filename);
}

static void
launch_poppler_preview_pane(GtkWindow *parent, char *pdf_filename)
{
  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c -> launch_poppler_preview_pane\n");
    GtkWidget *preview;
    GError *error = NULL;
    PreviewRunData pr = {0};

    preview = gtk_print_preview_pane_new(parent);

    if (!gtk_print_preview_pane_load_pdf(GTK_PRINT_PREVIEW_PANE(preview), 
                                         pdf_filename, &error))
    {
        g_warning("Preview failed to load PDF: %s", error->message);
        g_clear_error(&error);
        g_unlink(pdf_filename);
        g_free(pdf_filename);
        return;
    }

    gtk_window_set_transient_for(GTK_WINDOW(preview), parent);
    gtk_window_set_modal(GTK_WINDOW(preview), TRUE);
    
    // Make sure the window can be destroyed
    gtk_window_set_destroy_with_parent(GTK_WINDOW(preview), FALSE);
    
    // Connect to response signal if your preview pane emits it
    pr.loop = g_main_loop_new(NULL, FALSE);
    g_signal_connect(preview, "preview-finished", 
                     G_CALLBACK(preview_pane_response_cb), &pr);
    
    // Connect cleanup for when window is actually destroyed
    g_signal_connect(preview, "destroy", 
                     G_CALLBACK(preview_pane_cleanup_cb), pdf_filename);

    gtk_window_present(GTK_WINDOW(preview));
    
    // Run a nested main loop to handle events for the preview
    g_main_loop_run(pr.loop);
    g_main_loop_unref(pr.loop);
    
    // Destroy the preview window after the loop exits
    gtk_window_destroy(GTK_WINDOW(preview));
}

static void
run_isolated_preview(GtkPrintOperation *op, GtkPrintUnixDialog *dialog)
{
  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c -> run_isolated_preview\n");
    GtkPrintOperationPrivate *priv = op->priv;
    GtkPrintSettings *settings = NULL;
    GtkPageSetup *page_setup = NULL;
    char *preview_filename = NULL;
    char *temp_filename = NULL;
    cairo_surface_t *preview_surface = NULL;
    GtkPrintContext *temp_context = NULL;
    cairo_t *cr = NULL;
    int fd = -1;

    g_print("--- Starting isolated preview generation ---\n");

    // Get current settings - these return references owned by the dialog
    settings = gtk_print_unix_dialog_get_settings(dialog);
    page_setup = gtk_print_unix_dialog_get_page_setup(dialog);
    
    if (!settings || !page_setup)
    {
        g_warning("Failed to get settings or page setup");
        goto cleanup;
    }

    // Create temporary filename template
    temp_filename = g_strdup("gtk-preview-XXXXXX.pdf");
    fd = g_file_open_tmp(temp_filename, &preview_filename, NULL);
    g_free(temp_filename);

    if (fd < 0)
    {
        g_warning("Failed to create temp preview file");
        goto cleanup;
    }

    // Get paper dimensions
    double w = gtk_page_setup_get_paper_width(page_setup, GTK_UNIT_POINTS);
    double h = gtk_page_setup_get_paper_height(page_setup, GTK_UNIT_POINTS);
    
    g_print("Paper dimensions: %.2f x %.2f points\n", w, h);
    
    // Validate dimensions
    if (w <= 0 || h <= 0)
    {
        g_warning("Invalid paper dimensions: %.2f x %.2f", w, h);
        goto cleanup;
    }

    // Close the file descriptor before creating the surface
    close(fd);
    fd = -1;

    // Create PDF surface using filename
    preview_surface = cairo_pdf_surface_create(preview_filename, w, h);
    
    if (cairo_surface_status(preview_surface) != CAIRO_STATUS_SUCCESS)
    {
        g_warning("Failed to create PDF surface: %s", 
                  cairo_status_to_string(cairo_surface_status(preview_surface)));
        goto cleanup;
    }

    cr = cairo_create(preview_surface);
    if (cairo_status(cr) != CAIRO_STATUS_SUCCESS)
    {
        g_warning("Failed to create cairo context: %s",
                  cairo_status_to_string(cairo_status(cr)));
        goto cleanup;
    }

    // Create temporary print context
    temp_context = _gtk_print_context_new(op);
    if (!temp_context)
    {
        g_warning("Failed to create print context");
        goto cleanup;
    }
    
    gtk_print_context_set_cairo_context(temp_context, cr, 72.0, 72.0);
    _gtk_print_context_set_page_setup(temp_context, page_setup);

    // Emit signals with error checking
    g_signal_emit_by_name(op, "begin-print", temp_context);

    // Determine pages to preview
    int start_page = 0;
    int end_page = priv->nr_of_pages;
    
    if (end_page <= 0)
    {
        g_warning("Invalid number of pages: %d", end_page);
        goto cleanup;
    }
    
    GtkPrintPages print_pages = gtk_print_settings_get_print_pages(settings);
    if (print_pages == GTK_PRINT_PAGES_CURRENT)
    {
        start_page = gtk_print_unix_dialog_get_current_page(dialog);
        if (start_page < 0 || start_page >= end_page)
            start_page = 0;
        end_page = start_page + 1;
    }

    g_print("Previewing pages %d to %d\n", start_page, end_page - 1);

    // Draw pages
    for (int i = start_page; i < end_page; i++)
    {
        g_signal_emit_by_name(op, "draw-page", temp_context, i);
        cairo_show_page(cr);
    }

    g_signal_emit_by_name(op, "end-print", temp_context);

    // Ensure all data is written
    cairo_surface_flush(preview_surface);
    
    // Destroy cairo context before finishing surface
    if (cr)
    {
        cairo_destroy(cr);
        cr = NULL;
    }
    
    cairo_surface_finish(preview_surface);

    // Check if file was created successfully
    if (!g_file_test(preview_filename, G_FILE_TEST_EXISTS))
    {
        g_warning("Preview PDF file was not created");
        goto cleanup;
    }

    // Launch preview - transfers ownership of filename
    launch_poppler_preview_pane(GTK_WINDOW(dialog), preview_filename);
    preview_filename = NULL; // Prevent double-free

cleanup:
    g_print("--- Cleaning up temporary preview resources ---\n");
    
    if (cr)
        cairo_destroy(cr);
    if (preview_surface)
        cairo_surface_destroy(preview_surface);
    if (fd >= 0)
        close(fd);
    if (temp_context)
        g_object_unref(temp_context);
    
    // DO NOT unref settings and page_setup - we don't own these references!
    // The dialog owns them and will handle their lifecycle
    // if (settings)
    //     g_object_unref(settings);
    // if (page_setup)
    //     g_object_unref(page_setup);
    
    if (preview_filename)
    {
        g_unlink(preview_filename);
        g_free(preview_filename);
    }
}

static void
handle_print_response (GtkWidget *dialog,
                       int        response,
                       gpointer   data)
{
    g_print("yash kumar kasaudhan: gtkprintoperation-unix.c -> handle_print_response\n");
    GtkPrintUnixDialog *pd = GTK_PRINT_UNIX_DIALOG (dialog);
    PrintResponseData *rdata = data;
    GtkPrintSettings *settings = NULL;
    GtkPageSetup *page_setup = NULL;
    GtkPrinter *printer = NULL;
    gboolean page_setup_set = FALSE;

    g_print("handle_print_response --> after the variable\n");
    
    switch (response)
    {
        case GTK_RESPONSE_OK:
            g_print("handle_print_response --> in the if GTK_RESPONSE_OK\n");
            printer = gtk_print_unix_dialog_get_selected_printer(GTK_PRINT_UNIX_DIALOG(pd));

            rdata->result = GTK_PRINT_OPERATION_RESULT_APPLY;
            rdata->do_preview = FALSE;
            if (printer != NULL)
                rdata->do_print = TRUE;
            
            settings = gtk_print_unix_dialog_get_settings(GTK_PRINT_UNIX_DIALOG(pd));
            page_setup = gtk_print_unix_dialog_get_page_setup(GTK_PRINT_UNIX_DIALOG(pd));
            page_setup_set = gtk_print_unix_dialog_get_page_setup_set(GTK_PRINT_UNIX_DIALOG(pd));

            gtk_print_operation_set_print_settings(rdata->op, settings);
            g_signal_emit_by_name(rdata->op, "custom-widget-apply", rdata->op->priv->custom_widget);
            
            if (rdata->loop)
                g_main_loop_quit(rdata->loop);
                
            finish_print(rdata, printer, page_setup, settings, page_setup_set);
            
            if (settings)
                g_object_unref(settings);
                
            gtk_window_destroy(GTK_WINDOW(pd));
            break;

        case GTK_RESPONSE_APPLY:
            g_print("handle_print_response --> in the GTK_RESPONSE_APPLY\n");
            /* print preview - but now isolated */
            run_isolated_preview(rdata->op, pd);
            /* Dialog stays open */
            break;

        case GTK_RESPONSE_CANCEL:
        case GTK_RESPONSE_DELETE_EVENT:
            g_print("handle_print_response --> Handling Cancel\n");
            rdata->result = GTK_PRINT_OPERATION_RESULT_CANCEL;
            if (rdata->loop)
                g_main_loop_quit(rdata->loop);
            gtk_window_destroy(GTK_WINDOW(pd));
            break;
            
        default:
            g_print("handle_print_response --> Unknown response: %d\n", response);
            break;
    }
    
    g_print("handle_print_response --> completed\n");
}

// static void
// handle_print_response (GtkWidget *dialog,
//                        int        response,
//                        gpointer   data)
// {

//   g_print("yash kumar kasaudhan: gtkprintoperation-unix.c -> handle_print_response     \n");
//   GtkPrintUnixDialog *pd = GTK_PRINT_UNIX_DIALOG (dialog);
//   PrintResponseData *rdata = data;
//   GtkPrintSettings *settings = NULL;
//   GtkPageSetup *page_setup = NULL;
//   GtkPrinter *printer = NULL;
//   gboolean page_setup_set = FALSE;

//   g_print("handle_print_response --> after the variable \n");
//   if (response == GTK_RESPONSE_OK)
//     {
//       g_print("handle_print_response --> in the if GTK_RESPONSE_OK \n");
//       printer = gtk_print_unix_dialog_get_selected_printer (GTK_PRINT_UNIX_DIALOG (pd));

//       rdata->result = GTK_PRINT_OPERATION_RESULT_APPLY;
//       rdata->do_preview = FALSE;
//       if (printer != NULL)
//         rdata->do_print = TRUE;
//     }
//   else if (response == GTK_RESPONSE_APPLY)
//     {
//       g_print("handle_print_response --> in teh GTK_RESPONSE_APPLY \n");
//       /* print preview */
//       rdata->result = GTK_PRINT_OPERATION_RESULT_APPLY;
//       rdata->do_preview = TRUE;
//       rdata->do_print = TRUE;

//       rdata->op->priv->action = GTK_PRINT_OPERATION_ACTION_PREVIEW;
//     }
//   g_print("handle_print_response --> after if and else if \n");
//   if (rdata->do_print)
//     {
//       g_print("handle_print_response --> inside the rdata->do_print \n");
//       settings = gtk_print_unix_dialog_get_settings (GTK_PRINT_UNIX_DIALOG (pd));
//       page_setup = gtk_print_unix_dialog_get_page_setup (GTK_PRINT_UNIX_DIALOG (pd));
//       page_setup_set = gtk_print_unix_dialog_get_page_setup_set (GTK_PRINT_UNIX_DIALOG (pd));

//       /* Set new print settings now so that custom-widget options
//        * can be added to the settings in the callback
//        */
//       gtk_print_operation_set_print_settings (rdata->op, settings);
//       g_signal_emit_by_name (rdata->op, "custom-widget-apply", rdata->op->priv->custom_widget);
//     }
//     g_print("handle_print_response --> if(rdata->doprint) \n");
//   if ( rdata->loop)
//     g_main_loop_quit (rdata->loop);
// g_print("handle_print_response --> after rdata->loop\n");
// finish_print(rdata, printer, page_setup, settings, page_setup_set);
// g_print("handle_print_response --> after finisH_pritn \n");
// if (settings)
//     g_object_unref(settings);
// g_print("handle_print_response --> after settings \n");
// /* ONLY destroy when we are NOT previewing */
// if (!rdata->do_preview)
//     gtk_window_destroy(GTK_WINDOW(pd));
//   g_print("handle_print_response --> after the destroy \n");

// }




static void
found_printer (GtkPrinter        *printer,
               PrintResponseData *rdata)
{

  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c ->  found_printer    \n");
  GtkPrintOperation *op = rdata->op;
  GtkPrintOperationPrivate *priv = op->priv;
  GtkPrintSettings *settings = NULL;
  GtkPageSetup *page_setup = NULL;

  if (rdata->loop)
    g_main_loop_quit (rdata->loop);

  if (printer != NULL)
    {
      rdata->result = GTK_PRINT_OPERATION_RESULT_APPLY;

      rdata->do_print = TRUE;

      if (priv->print_settings)
        settings = gtk_print_settings_copy (priv->print_settings);
      else
        settings = gtk_print_settings_new ();

      gtk_print_settings_set_printer (settings,
                                      gtk_printer_get_name (printer));

      if (priv->default_page_setup)
        page_setup = gtk_page_setup_copy (priv->default_page_setup);
      else
        page_setup = gtk_page_setup_new ();
  }

  finish_print (rdata, printer, page_setup, settings, FALSE);

  if (settings)
    g_object_unref (settings);

  if (page_setup)
    g_object_unref (page_setup);
}

static void
gtk_print_operation_unix_run_dialog_async (GtkPrintOperation          *op,
                                           gboolean                    show_dialog,
                                           GtkWindow                  *parent,
                                           GtkPrintOperationPrintFunc  print_cb)
{
  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c ->  gtk_print_opreation_unix_run_dialog_async    \n");
  GtkWidget *pd;
  PrintResponseData *rdata;
  const char *printer_name;

  rdata = g_new (PrintResponseData, 1);
  rdata->op = g_object_ref (op);
  rdata->do_print = FALSE;
  rdata->do_preview = FALSE;
  rdata->result = GTK_PRINT_OPERATION_RESULT_CANCEL;
  rdata->print_cb = print_cb;
  rdata->parent = parent;
  rdata->loop = NULL;
  rdata->destroy = print_response_data_free;

  if (show_dialog)
    {
      pd = get_print_dialog (op, parent);
      gtk_window_set_modal (GTK_WINDOW (pd), TRUE);

      g_signal_connect (pd, "response",
                        G_CALLBACK (handle_print_response), rdata);

      gtk_window_present (GTK_WINDOW (pd));
    }
  else
    {
      printer_name = NULL;
      if (op->priv->print_settings)
        printer_name = gtk_print_settings_get_printer (op->priv->print_settings);

      find_printer (printer_name, (GFunc) found_printer, rdata);
    }
}

#ifdef CAIRO_HAS_PDF_SURFACE
static cairo_status_t
write_preview (void                *closure,
               const unsigned char *data,
               unsigned int         length)
{
  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c ->  write_preveiw    \n");
  int fd = GPOINTER_TO_INT (closure);
  gssize written;

  while (length > 0)
    {
      written = write (fd, data, length);

      if (written == -1)
        {
          if (errno == EAGAIN || errno == EINTR)
            continue;

          return CAIRO_STATUS_WRITE_ERROR;
        }

      data += written;
      length -= written;
    }

  return CAIRO_STATUS_SUCCESS;
}

static void
close_preview (void *data)
{
  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c ->  close_preview    \n");
  int fd = GPOINTER_TO_INT (data);

  close (fd);
}

static cairo_surface_t *
gtk_print_operation_unix_create_preview_surface (GtkPrintOperation *op,
                                                 GtkPageSetup      *page_setup,
                                                 double            *dpi_x,
                                                 double            *dpi_y,
                                                 char             **target)
{
  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c ->  gtk_print_operation_unix_create_preview_surface    \n");
  char *filename;
  int fd;
  GtkPaperSize *paper_size;
  double w, h;
  cairo_surface_t *surface;
  static cairo_user_data_key_t key;

  filename = g_build_filename (g_get_tmp_dir (), "previewXXXXXX.pdf", NULL);
  fd = g_mkstemp (filename);
  g_print("inside the create_prevewi surface fd\n");
  if (fd < 0)
    {
      g_print("insed the if fd<0\n");
      g_free (filename);
      return NULL;
    }

  *target = filename;

  paper_size = gtk_page_setup_get_paper_size (page_setup);
  w = gtk_paper_size_get_width (paper_size, GTK_UNIT_POINTS);
  h = gtk_paper_size_get_height (paper_size, GTK_UNIT_POINTS);

  *dpi_x = *dpi_y = 72;
  g_print("after the dpi \n");
  surface = cairo_pdf_surface_create_for_stream (write_preview, GINT_TO_POINTER (fd), w, h);
    g_print("after teh surface \n");
  cairo_surface_set_user_data (surface, &key, GINT_TO_POINTER (fd), close_preview);
g_print("after the function that sets the user_data to cairo surface\n");
  return surface;
}
#endif

static void
gtk_print_operation_unix_preview_start_page (GtkPrintOperation *op,
                                             cairo_surface_t   *surface,
                                             cairo_t           *cr)
{

  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c ->   gtk_print_operation_unix_preview_start_page   \n");
}

static void
gtk_print_operation_unix_preview_end_page (GtkPrintOperation *op,
                                           cairo_surface_t   *surface,
                                           cairo_t           *cr)
{

  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c ->   gtk_print_operation_unix_preview_end_page   \n");
  cairo_show_page (cr);
}

static void
gtk_print_operation_unix_resize_preview_surface (GtkPrintOperation *op,
                                                 GtkPageSetup      *page_setup,
                                                 cairo_surface_t   *surface)
{
  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c ->   gtk_print_operation_unix_resize_preview_surface   \n");
#ifdef CAIRO_HAS_PDF_SURFACE
  double w, h;

  w = gtk_page_setup_get_paper_width (page_setup, GTK_UNIT_POINTS);
  h = gtk_page_setup_get_paper_height (page_setup, GTK_UNIT_POINTS);
  cairo_pdf_surface_set_size (surface, w, h);
#endif
}

static GtkPrintOperationResult
gtk_print_operation_unix_run_dialog (GtkPrintOperation *op,
                                     gboolean           show_dialog,
                                     GtkWindow         *parent,
                                     gboolean          *do_print)
 {
  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c ->   gtk_print_operation_unix_run_dialog   \n");
  GtkWidget *pd;
  PrintResponseData rdata;
  const char *printer_name;

  rdata.op = op;
  rdata.do_print = FALSE;
  rdata.do_preview = FALSE;
  rdata.result = GTK_PRINT_OPERATION_RESULT_CANCEL;
  rdata.print_cb = NULL;
  rdata.destroy = NULL;
  rdata.parent = parent;
  rdata.loop = NULL;

  if (show_dialog)
    {
      pd = get_print_dialog (op, parent);
      gtk_window_set_modal (GTK_WINDOW (pd), TRUE);

      g_signal_connect (pd, "response",
                        G_CALLBACK (handle_print_response), &rdata);

      gtk_window_present (GTK_WINDOW (pd));

      rdata.loop = g_main_loop_new (NULL, FALSE);
      g_main_loop_run (rdata.loop);
      g_main_loop_unref (rdata.loop);
      rdata.loop = NULL;
    }
  else
    {
      printer_name = NULL;
      if (op->priv->print_settings)
        printer_name = gtk_print_settings_get_printer (op->priv->print_settings);

      rdata.loop = g_main_loop_new (NULL, FALSE);
      find_printer (printer_name,
                    (GFunc) found_printer, &rdata);

      g_main_loop_run (rdata.loop);
      g_main_loop_unref (rdata.loop);
      rdata.loop = NULL;
    }

  *do_print = rdata.do_print;

  return rdata.result;
}


typedef struct
{
  GtkPageSetup         *page_setup;
  GtkPageSetupDoneFunc  done_cb;
  gpointer              data;
  GDestroyNotify        destroy;
  GMainLoop            *loop;
} PageSetupResponseData;

static void
page_setup_data_free (gpointer data)
{
  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c ->  page_setup_data_free    \n");
  PageSetupResponseData *rdata = data;

  if (rdata->page_setup)
    g_object_unref (rdata->page_setup);

  g_free (rdata);
}

static void
handle_page_setup_response (GtkWidget *dialog,
                            int        response,
                            gpointer   data)
{
  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c ->   handle_page_setup_response   \n");
  GtkPageSetupUnixDialog *psd;
  PageSetupResponseData *rdata = data;

  if (rdata->loop)
    g_main_loop_quit (rdata->loop);

  psd = GTK_PAGE_SETUP_UNIX_DIALOG (dialog);
  if (response == GTK_RESPONSE_OK)
    rdata->page_setup = gtk_page_setup_unix_dialog_get_page_setup (psd);

  gtk_window_destroy (GTK_WINDOW (dialog));

  if (rdata->done_cb)
    rdata->done_cb (rdata->page_setup, rdata->data);

  if (rdata->destroy)
    rdata->destroy (rdata);
}

static GtkWidget *
get_page_setup_dialog (GtkWindow        *parent,
                       GtkPageSetup     *page_setup,
                       GtkPrintSettings *settings)
{
  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c ->  get_page_setup_dialog    \n");
  GtkWidget *dialog;

  dialog = gtk_page_setup_unix_dialog_new (NULL, parent);
  if (page_setup)
    gtk_page_setup_unix_dialog_set_page_setup (GTK_PAGE_SETUP_UNIX_DIALOG (dialog),
                                               page_setup);
  gtk_page_setup_unix_dialog_set_print_settings (GTK_PAGE_SETUP_UNIX_DIALOG (dialog),
                                                 settings);

  return dialog;
}

/**
 * gtk_print_run_page_setup_dialog:
 * @parent: (nullable): transient parent
 * @page_setup: (nullable): an existing `GtkPageSetup`
 * @settings: a `GtkPrintSettings`
 *
 * Runs a page setup dialog, letting the user modify the values from @page_setup.
 *
 * If the user cancels the dialog, the returned `GtkPageSetup` is identical
 * to the passed in @page_setup, otherwise it contains the modifications
 * done in the dialog.
 *
 * Note that this function may use a recursive mainloop to show the page
 * setup dialog. See [func@Gtk.print_run_page_setup_dialog_async] if this is
 * a problem.
 *
 * Returns: (transfer full): a new `GtkPageSetup`
 */
GtkPageSetup *
gtk_print_run_page_setup_dialog (GtkWindow        *parent,
                                 GtkPageSetup     *page_setup,
                                 GtkPrintSettings *settings)
{
  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c -> gtk_print_run_page_setup_dialog    \n");
  GtkWidget *dialog;
  PageSetupResponseData rdata;

  rdata.page_setup = NULL;
  rdata.done_cb = NULL;
  rdata.data = NULL;
  rdata.destroy = NULL;
  rdata.loop = g_main_loop_new (NULL, FALSE);

  dialog = get_page_setup_dialog (parent, page_setup, settings);

  g_signal_connect (dialog, "response",
                    G_CALLBACK (handle_page_setup_response),
                    &rdata);

  gtk_window_present (GTK_WINDOW (dialog));

  g_main_loop_run (rdata.loop);
  g_main_loop_unref (rdata.loop);
  rdata.loop = NULL;

  if (rdata.page_setup)
    return rdata.page_setup;
  else if (page_setup)
    return gtk_page_setup_copy (page_setup);
  else
    return gtk_page_setup_new ();
}

/**
 * gtk_print_run_page_setup_dialog_async:
 * @parent: (nullable): transient parent
 * @page_setup: (nullable): an existing `GtkPageSetup`
 * @settings: a `GtkPrintSettings`
 * @done_cb: (scope async): a function to call when the user saves
 *    the modified page setup
 * @data: user data to pass to @done_cb
 *
 * Runs a page setup dialog, letting the user modify the values from @page_setup.
 *
 * In contrast to [func@Gtk.print_run_page_setup_dialog], this function  returns
 * after showing the page setup dialog on platforms that support this, and calls
 * @done_cb from a signal handler for the ::response signal of the dialog.
 */
void
gtk_print_run_page_setup_dialog_async (GtkWindow            *parent,
                                       GtkPageSetup         *page_setup,
                                       GtkPrintSettings     *settings,
                                       GtkPageSetupDoneFunc  done_cb,
                                       gpointer              data)
{
  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c -> gtk_print_run_page_setup_dialog_async     \n");
  GtkWidget *dialog;
  PageSetupResponseData *rdata;

  dialog = get_page_setup_dialog (parent, page_setup, settings);
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);

  rdata = g_new (PageSetupResponseData, 1);
  rdata->page_setup = NULL;
  rdata->done_cb = done_cb;
  rdata->data = data;
  rdata->destroy = page_setup_data_free;
  rdata->loop = NULL;

  g_signal_connect (dialog, "response",
                    G_CALLBACK (handle_page_setup_response), rdata);

  gtk_window_present (GTK_WINDOW (dialog));
 }

struct _PrinterFinder
{
  gboolean found_printer;
  gboolean scheduled_callback;
  GFunc func;
  gpointer data;
  char *printer_name;
  GList *backends;
  guint timeout_tag;
  GtkPrinter *printer;
  GtkPrinter *default_printer;
  GtkPrinter *first_printer;
};

static gboolean
find_printer_idle (gpointer data)
{

  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c ->  find_printer_idle    \n");
  PrinterFinder *finder = data;
  GtkPrinter *printer;

  if (finder->printer != NULL)
    printer = finder->printer;
  else if (finder->default_printer != NULL)
    printer = finder->default_printer;
  else if (finder->first_printer != NULL)
    printer = finder->first_printer;
  else
    printer = NULL;

  finder->func (printer, finder->data);

  printer_finder_free (finder);

  return G_SOURCE_REMOVE;
}

static void
schedule_finder_callback (PrinterFinder *finder)
{
  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c ->  schedule_finder_callback    \n");
  g_assert (!finder->scheduled_callback);
  g_idle_add (find_printer_idle, finder);
  finder->scheduled_callback = TRUE;
}

static void
printer_added_cb (GtkPrintBackend *backend,
                  GtkPrinter      *printer,
                  PrinterFinder   *finder)
{
  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c ->  printer_added_cb    \n");
  if (finder->found_printer)
    return;

  /* FIXME this skips "Print to PDF" - is this intentional ? */
  if (gtk_printer_is_virtual (printer))
    return;

  if (finder->printer_name != NULL &&
      strcmp (gtk_printer_get_name (printer), finder->printer_name) == 0)
    {
      finder->printer = g_object_ref (printer);
      finder->found_printer = TRUE;
    }
  else if (finder->default_printer == NULL &&
           gtk_printer_is_default (printer))
    {
      finder->default_printer = g_object_ref (printer);
      if (finder->printer_name == NULL)
        finder->found_printer = TRUE;
    }
  else
    {
      if (finder->first_printer == NULL)
        finder->first_printer = g_object_ref (printer);
    }

  if (finder->found_printer)
    schedule_finder_callback (finder);
}

static void
printer_list_done_cb (GtkPrintBackend *backend,
                      PrinterFinder   *finder)
{
  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c ->  printer_list_done_cb    \n");
  finder->backends = g_list_remove (finder->backends, backend);

  g_signal_handlers_disconnect_by_func (backend, printer_added_cb, finder);
  g_signal_handlers_disconnect_by_func (backend, printer_list_done_cb, finder);

  gtk_print_backend_destroy (backend);
  g_object_unref (backend);

  /* If there are no more backends left after removing ourselves from the list
   * above, then we're finished.
   */
  if (finder->backends == NULL && !finder->found_printer)
    schedule_finder_callback (finder);
}

static void
find_printer_init (PrinterFinder   *finder,
                   GtkPrintBackend *backend)
{
  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c ->  find_printer_init    \n");
  GList *list;
  GList *node;

  list = gtk_print_backend_get_printer_list (backend);

  node = list;
  while (node != NULL)
    {
      printer_added_cb (backend, node->data, finder);
      node = node->next;

      if (finder->found_printer)
        break;
    }

  g_list_free (list);

  if (gtk_print_backend_printer_list_is_done (backend))
    {
      printer_list_done_cb (backend, finder);
    }
  else
    {
      g_signal_connect (backend, "printer-added",
                        (GCallback) printer_added_cb,
                        finder);
      g_signal_connect (backend, "printer-list-done",
                        (GCallback) printer_list_done_cb,
                        finder);
    }

}

static void
printer_finder_free (PrinterFinder *finder)
{
  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c -> printer_finder_free     \n");
  GList *l;

  g_free (finder->printer_name);

  if (finder->printer)
    g_object_unref (finder->printer);

  if (finder->default_printer)
    g_object_unref (finder->default_printer);

  if (finder->first_printer)
    g_object_unref (finder->first_printer);

  for (l = finder->backends; l != NULL; l = l->next)
    {
      GtkPrintBackend *backend = l->data;
      g_signal_handlers_disconnect_by_func (backend, printer_added_cb, finder);
      g_signal_handlers_disconnect_by_func (backend, printer_list_done_cb, finder);
      gtk_print_backend_destroy (backend);
      g_object_unref (backend);
    }

  g_list_free (finder->backends);

  g_free (finder);
}

static void
find_printer (const char *printer,
              GFunc        func,
              gpointer     data)
{
  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c ->  find_printer    \n");
  GList *node, *next;
  PrinterFinder *finder;

  finder = g_new0 (PrinterFinder, 1);

  finder->printer_name = g_strdup (printer);
  finder->func = func;
  finder->data = data;

  finder->backends = NULL;
  if (g_module_supported ())
    finder->backends = gtk_print_backend_load_modules ();

  if (finder->backends == NULL)
    {
      schedule_finder_callback (finder);
      return;
    }

  for (node = finder->backends; !finder->found_printer && node != NULL; node = next)
    {
      next = node->next;
      find_printer_init (finder, GTK_PRINT_BACKEND (node->data));
    }
}

GtkPrintOperationResult
_gtk_print_operation_platform_backend_run_dialog (GtkPrintOperation *op,
                                                  gboolean           show_dialog,
                                                  GtkWindow         *parent,
                                                  gboolean          *do_print)
{
  // GdkDisplay *display;
 g_print("yash kumar kasaudhan: gtkprintoperation-unix.c -> _gtk_print_operation_platform_backend_run_dialog\n");
  return gtk_print_operation_unix_run_dialog(op,show_dialog,parent,do_print);
  // if (parent)
  //   display = gtk_widget_get_display (GTK_WIDGET (parent));
  // else
  //   display = gdk_display_get_default ();

  // if (gdk_display_should_use_portal (display, PORTAL_PRINT_INTERFACE, 0))
  //   return gtk_print_operation_portal_run_dialog (op, show_dialog, parent, do_print);
  // else
  //   return gtk_print_operation_unix_run_dialog (op, show_dialog, parent, do_print);
}

void
_gtk_print_operation_platform_backend_run_dialog_async (GtkPrintOperation          *op,
                                                        gboolean                    show_dialog,
                                                        GtkWindow                  *parent,
                                                        GtkPrintOperationPrintFunc  print_cb)
{
  // GdkDisplay *display;
  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c -> _gtk_print_operation_platform_backend_run_dialog_async\n");
  gtk_print_operation_unix_run_dialog_async(op,show_dialog,parent,print_cb);
  // if (parent)
  //   display = gtk_widget_get_display (GTK_WIDGET (parent));
  // else
  //   display = gdk_display_get_default ();

  // if (gdk_display_should_use_portal (display, PORTAL_PRINT_INTERFACE, 0))
  //   gtk_print_operation_portal_run_dialog_async (op, show_dialog, parent, print_cb);
  // else
  //   gtk_print_operation_unix_run_dialog_async (op, show_dialog, parent, print_cb);
}

void
_gtk_print_operation_platform_backend_launch_preview (GtkPrintOperation *op,
                                                      cairo_surface_t   *surface,
                                                      GtkWindow         *parent,
                                                      const char        *filename)
{
  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c ->  _gtk_print_operation_platform_backend_launch_preview    \n");
  // GdkDisplay *display;

  // if (parent)
  //   display = gtk_widget_get_display (GTK_WIDGET (parent));
  // else
  //   display = gdk_display_get_default ();

  // if (gdk_display_should_use_portal (display, PORTAL_PRINT_INTERFACE, 0))
  //   gtk_print_operation_portal_launch_preview (op, surface, parent, filename);
  // else
    gtk_print_operation_unix_launch_preview (op, surface, parent, filename);
}

cairo_surface_t *
_gtk_print_operation_platform_backend_create_preview_surface (GtkPrintOperation *op,
                                                              GtkPageSetup      *page_setup,
                                                              double            *dpi_x,
                                                              double            *dpi_y,
                                                              char             **target)
{
  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c ->  _gtk_print_operation_platform_backend_create_preview_surface    \n");
#ifdef CAIRO_HAS_PDF_SURFACE
  return gtk_print_operation_unix_create_preview_surface (op, page_setup, dpi_x, dpi_y, target);
#else
  return NULL;
#endif
}

void
_gtk_print_operation_platform_backend_resize_preview_surface (GtkPrintOperation *op,
                                                              GtkPageSetup      *page_setup,
                                                              cairo_surface_t   *surface)
{
  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c ->  _gtk_print_operation_platform_backend_resize_preview_surface    \n");
  gtk_print_operation_unix_resize_preview_surface (op, page_setup, surface);
}

void
_gtk_print_operation_platform_backend_preview_start_page (GtkPrintOperation *op,
                                                          cairo_surface_t   *surface,
                                                          cairo_t           *cr)
{
  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c ->  _gtk_print_opreation_platform_backend_preview_start_page    \n");
  gtk_print_operation_unix_preview_start_page (op, surface, cr);
}

void
_gtk_print_operation_platform_backend_preview_end_page (GtkPrintOperation *op,
                                                        cairo_surface_t   *surface,
                                                        cairo_t           *cr)
{
  g_print("yash kumar kasaudhan: gtkprintoperation-unix.c ->   _gtk_print_operation_platform_backend_preview_END_PAGE   \n");
  gtk_print_operation_unix_preview_end_page (op, surface, cr);
}