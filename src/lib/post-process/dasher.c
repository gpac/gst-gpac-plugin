/*
 *			GPAC - Multimedia Framework C SDK
 *
 *			Authors: Deniz Ugur, Romain Bouqueau, Sohaib Larbi
 *			Copyright (c) Motion Spell
 *				All rights reserved
 *
 *  This file is part of the GPAC/GStreamer wrapper
 *
 *  This GPAC/GStreamer wrapper is free software; you can redistribute it
 *  and/or modify it under the terms of the GNU Affero General Public License
 *  as published by the Free Software Foundation; either version 3, or (at
 *  your option) any later version.
 *
 *  This GPAC/GStreamer wrapper is distributed in the hope that it will be
 *  useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Affero General Public License for more details.
 *
 *  You should have received a copy of the GNU Affero General Public
 *  License along with this library; see the file LICENSE.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include "common.h"
#include "gpacmessages.h"
#include "lib/memio.h"
#include "lib/signals.h"

#include <gio/gio.h>
#include <gpac/mpd.h>
#include <gpac/network.h>

GST_DEBUG_CATEGORY_STATIC(gpac_dasher);
#define GST_CAT_DEFAULT gpac_dasher

typedef struct
{
  gchar* name;        // Name of the file
  GFile* file;        // GFile object for the file (optional)
  GOutputStream* out; // Output stream for the file
} FileAbstract;

typedef struct
{
  // current file being processed
  FileAbstract* main_file;
  FileAbstract* llhls_file; // for low-latency HLS chunks

  gchar* llhas_template;
  gboolean is_manifest;
  guint32 dash_state;
  const gchar* dst; // destination file path
} DasherCtx;

void
dasher_ctx_init(void** process_ctx)
{
  *process_ctx = g_new0(DasherCtx, 1);
  DasherCtx* ctx = (DasherCtx*)*process_ctx;

  GST_DEBUG_CATEGORY_INIT(
    gpac_dasher, "gpacdasherpp", 0, "GPAC dasher post-processor");
}

void
dasher_free_file(FileAbstract* file)
{
  if (file) {
    if (file->out) {
      g_output_stream_close(file->out, NULL, NULL);
      g_object_unref(file->out);
    }
    if (file->file) {
      g_object_unref(file->file);
    }
    g_free(file->name);
    g_free(file);
  }
}

void
dasher_ctx_free(void* process_ctx)
{
  DasherCtx* ctx = (DasherCtx*)process_ctx;

  // Free the main file if it exists
  dasher_free_file(ctx->main_file);
  dasher_free_file(ctx->llhls_file);

  // Free the llhas template if it exists
  if (ctx->llhas_template)
    g_free(ctx->llhas_template);

  // Free the context
  g_free(ctx);
}

GF_Err
dasher_configure_pid(GF_Filter* filter, GF_FilterPid* pid)
{
  GPAC_MemOutPIDContext* ctx =
    (GPAC_MemOutPIDContext*)gf_filter_pid_get_udta(pid);
  DasherCtx* dasher_ctx = (DasherCtx*)ctx->private_ctx;
  GPAC_MemOutPIDFlags udta_flags = gf_filter_pid_get_udta_flags(pid);

  // We'll transfer the data over signals
  udta_flags |= GPAC_MEMOUT_PID_FLAG_DONT_CONSUME;
  gf_filter_pid_set_udta_flags(pid, udta_flags);

  const GF_PropertyValue* p =
    gf_filter_pid_get_property(pid, GF_PROP_PID_IS_MANIFEST);
  if (p && p->value.uint)
    dasher_ctx->is_manifest = TRUE;
  else
    dasher_ctx->dash_state = 1;

  // Get the destination path
  GF_PropertyValue dst;
  Bool found = gf_filter_get_arg(filter, "dst", &dst);
  if (found)
    dasher_ctx->dst = g_strdup(dst.value.string);

  GPAC_MemOutPrivateContext* fctx =
    (GPAC_MemOutPrivateContext*)gf_filter_pid_get_alias_udta(pid);
  if (fctx) {
    dasher_ctx->dst = fctx->dst;
  } else {
    fctx = (GPAC_MemOutPrivateContext*)gf_filter_get_udta(filter);
    dasher_ctx->dst = fctx->dst;
  }

  return GF_OK;
}

Bool
dasher_process_event(GF_Filter* filter, const GF_FilterEvent* evt)
{
  if (evt->base.type == GF_FEVT_FILE_DELETE) {
    GPAC_MemIoContext* io_ctx =
      (GPAC_MemIoContext*)gf_filter_get_rt_udta(filter);
    GPAC_MemOutPIDContext* ctx =
      (GPAC_MemOutPIDContext*)gf_filter_pid_get_udta(evt->base.on_pid);
    DasherCtx* dasher_ctx = (DasherCtx*)ctx->private_ctx;

    GST_TRACE_OBJECT(io_ctx->sess->element,
                     "Received file delete event for PID %s: %s",
                     gf_filter_pid_get_name(evt->base.on_pid),
                     evt->file_del.url);

    gboolean sent = gpac_signal_try_emit(io_ctx->sess->element,
                                         GPAC_SIGNAL_DASHER_DELETE_SEGMENT,
                                         evt->file_del.url,
                                         NULL);

    if (!sent) {
      GFile* file = g_file_new_for_path(evt->file_del.url);
      GError* error = NULL;
      if (!g_file_delete(file, NULL, &error)) {
        GST_ELEMENT_WARNING(io_ctx->sess->element,
                            RESOURCE,
                            FAILED,
                            (NULL),
                            ("Failed to delete file %s: %s",
                             evt->file_del.url,
                             error ? error->message : "Unknown error"));
        if (error)
          g_error_free(error);
      }
      g_object_unref(file);
    }

    return GF_TRUE;
  }
  return GF_FALSE;
}

void
dasher_open_close_file(GF_Filter* filter,
                       GF_FilterPid* pid,
                       const gchar* name,
                       gboolean is_llhls)
{
  GPAC_MemIoContext* io_ctx = (GPAC_MemIoContext*)gf_filter_get_rt_udta(filter);
  GPAC_MemOutPIDContext* ctx =
    (GPAC_MemOutPIDContext*)gf_filter_pid_get_udta(pid);
  DasherCtx* dasher_ctx = (DasherCtx*)ctx->private_ctx;

  // Get a pointer to the requested file
  FileAbstract** file = NULL;
  if (is_llhls) {
    file = &dasher_ctx->llhls_file;
  } else {
    file = &dasher_ctx->main_file;
  }

  // If the file is already open, close it
  if (*file) {
    GST_TRACE_OBJECT(io_ctx->sess->element,
                     "Closing file for PID %s: %s",
                     gf_filter_pid_get_name(pid),
                     (*file)->name);
    if ((*file)->out)
      g_output_stream_close((*file)->out, NULL, NULL);
    if ((*file)->file) {
      g_object_unref((*file)->out);
      g_object_unref((*file)->file);
    }

    g_free((*file)->name);
    g_free(*file);
    *file = NULL;
  }

  if (!name)
    return; // No file to open

  GST_TRACE_OBJECT(io_ctx->sess->element,
                   "Opening new file for PID %s: %s",
                   gf_filter_pid_get_name(pid),
                   name);

  // Create a new file
  *file = g_new0(FileAbstract, 1);
  (*file)->name = g_strdup(name);

  // Decide on the file flags
  gboolean has_os = FALSE;
  if (dasher_ctx->is_manifest) {
    if (g_strcmp0(name, dasher_ctx->dst) == 0) {
      has_os = gpac_signal_try_emit(io_ctx->sess->element,
                                    GPAC_SIGNAL_DASHER_MANIFEST,
                                    (*file)->name,
                                    &(*file)->out);
    } else {
      has_os = gpac_signal_try_emit(io_ctx->sess->element,
                                    GPAC_SIGNAL_DASHER_MANIFEST_VARIANT,
                                    (*file)->name,
                                    &(*file)->out);
    }
  } else {
    if (g_strcmp0(name, dasher_ctx->dst) == 0) {
      has_os = gpac_signal_try_emit(io_ctx->sess->element,
                                    GPAC_SIGNAL_DASHER_SEGMENT_INIT,
                                    (*file)->name,
                                    &(*file)->out);
    } else {
      has_os = gpac_signal_try_emit(io_ctx->sess->element,
                                    GPAC_SIGNAL_DASHER_SEGMENT,
                                    (*file)->name,
                                    &(*file)->out);
    }
  }

  if (!has_os) {
    // Create a GFile and GOutputStream for the file
    (*file)->file = g_file_new_for_path((*file)->name);

    GError* error = NULL;
    (*file)->out = G_OUTPUT_STREAM(g_file_replace(
      (*file)->file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, &error));
    if (!(*file)->out) {
      GST_ELEMENT_ERROR(io_ctx->sess->element,
                        STREAM,
                        FAILED,
                        (NULL),
                        ("Failed to open output stream for file %s: %s",
                         (*file)->name,
                         error ? error->message : "Unknown error"));
      g_error_free(error);
      g_object_unref((*file)->file);

      // Reset the current file pointer
      g_free((*file)->name);
      g_free(*file);
      *file = NULL;
      return;
    }
  }
}

void
dasher_setup_file(GF_Filter* filter, GF_FilterPid* pid)
{
  GPAC_MemOutPIDContext* ctx =
    (GPAC_MemOutPIDContext*)gf_filter_pid_get_udta(pid);
  DasherCtx* dasher_ctx = (DasherCtx*)ctx->private_ctx;
  const char* dst = dasher_ctx->dst;

  const GF_PropertyValue* p =
    gf_filter_pid_get_property(pid, GF_PROP_PID_OUTPATH);
  if (p && p->value.string) {
    dasher_open_close_file(filter, pid, p->value.string, FALSE);
    return;
  }

  if (dasher_ctx->dst) {
    dasher_open_close_file(filter, pid, dasher_ctx->dst, FALSE);
  } else {
    p = gf_filter_pid_get_property(pid, GF_PROP_PID_FILEPATH);
    if (!p)
      p = gf_filter_pid_get_property(pid, GF_PROP_PID_URL);
    if (p && p->value.string)
      dasher_open_close_file(filter, pid, p->value.string, FALSE);
  }
}

GF_Err
dasher_ensure_file(GF_Filter* filter, GF_FilterPid* pid, gboolean is_llhls)
{
  GPAC_MemIoContext* io_ctx = (GPAC_MemIoContext*)gf_filter_get_rt_udta(filter);
  GPAC_MemOutPIDContext* ctx =
    (GPAC_MemOutPIDContext*)gf_filter_pid_get_udta(pid);
  DasherCtx* dasher_ctx = (DasherCtx*)ctx->private_ctx;

  FileAbstract** file = NULL;
  if (is_llhls) {
    file = &dasher_ctx->llhls_file;
  } else {
    file = &dasher_ctx->main_file;
  }

  // If we don't have a file, set it up
  if (G_UNLIKELY(!(*file))) {
    GST_ELEMENT_ERROR(
      io_ctx->sess->element,
      STREAM,
      FAILED,
      (NULL),
      ("Failed to ensure file for PID %s", gf_filter_pid_get_name(pid)));
    return GF_IO_ERR;
  }

  if (G_UNLIKELY(!(*file)->out)) {
    GST_ELEMENT_ERROR(io_ctx->sess->element,
                      STREAM,
                      FAILED,
                      (NULL),
                      ("No output stream for file %s",
                       (*file)->name ? (*file)->name : "unknown"));
    gf_filter_abort(filter);
    return GF_IO_ERR;
  }

  return GF_OK;
}

GF_Err
dasher_write_data(GF_Filter* filter,
                  GF_FilterPid* pid,
                  FileAbstract* file,
                  const u8* data,
                  u32 size)
{
  GPAC_MemIoContext* io_ctx = (GPAC_MemIoContext*)gf_filter_get_rt_udta(filter);
  GPAC_MemOutPIDContext* ctx =
    (GPAC_MemOutPIDContext*)gf_filter_pid_get_udta(pid);
  DasherCtx* dasher_ctx = (DasherCtx*)ctx->private_ctx;

  if (!file || !file->out) {
    GST_ELEMENT_ERROR(io_ctx->sess->element,
                      STREAM,
                      FAILED,
                      (NULL),
                      ("No output stream for file %s",
                       file && file->name ? file->name : "unknown"));
    return GF_IO_ERR;
  }

  gssize bytes_written =
    g_output_stream_write(file->out, data, size, NULL, NULL);
  if (bytes_written < 0) {
    GST_ELEMENT_ERROR(io_ctx->sess->element,
                      STREAM,
                      FAILED,
                      (NULL),
                      ("Failed to write data to output stream for file %s",
                       file->name ? file->name : "unknown"));
    return GF_IO_ERR;
  }

  if (bytes_written < (gssize)size) {
    GST_ELEMENT_WARNING(io_ctx->sess->element,
                        STREAM,
                        FAILED,
                        (NULL),
                        ("Partial write to output stream for file %s, "
                         "expected: %" G_GSIZE_FORMAT
                         ", written: %" G_GSSIZE_FORMAT,
                         file->name ? file->name : "unknown",
                         (gssize)size,
                         bytes_written));
  }

  GST_TRACE_OBJECT(io_ctx->sess->element,
                   "Wrote %s, size: %" G_GSIZE_FORMAT,
                   file->name ? file->name : "unknown",
                   (gssize)size);

  return GF_OK;
}

GF_Err
dasher_post_process(GF_Filter* filter, GF_FilterPid* pid, GF_FilterPacket* pck)
{
  GPAC_MemIoContext* io_ctx = (GPAC_MemIoContext*)gf_filter_get_rt_udta(filter);
  GPAC_MemOutPIDContext* ctx =
    (GPAC_MemOutPIDContext*)gf_filter_pid_get_udta(pid);
  DasherCtx* dasher_ctx = (DasherCtx*)ctx->private_ctx;
  const GF_PropertyValue* fname;
  const GF_PropertyValue* p;

  if (!pck) {
    if (gf_filter_pid_is_eos(pid) && !gf_filter_pid_is_flush_eos(pid)) {
      dasher_open_close_file(filter, pid, NULL, FALSE);
      dasher_open_close_file(filter, pid, NULL, TRUE);
    }
    return GF_OK; // No packet to process
  }

  // Check the packet framing
  Bool start;
  Bool end;
  gf_filter_pck_get_framing(pck, &start, &end);
  if (dasher_ctx->dash_state) {
    p = gf_filter_pck_get_property(pck, GF_PROP_PCK_FILENUM);
    if (p) {
      p = gf_filter_pck_get_property(pck, GF_PROP_PCK_FILENAME);
      if (p && p->value.string)
        start = GF_TRUE;
    }

    p = gf_filter_pck_get_property(pck, GF_PROP_PCK_EODS);
    if (p && p->value.boolean)
      end = GF_TRUE;
  }

  if (start) {
    // Previous file has ended, move to the next file
    if (dasher_ctx->main_file)
      dasher_open_close_file(filter, pid, NULL, FALSE);

    const GF_PropertyValue* ext;
    const GF_PropertyValue* fnum;
    const GF_PropertyValue* rel;
    Bool explicit_overwrite = GF_FALSE;
    const char* name = NULL;
    fname = ext = NULL;
    // file num increased per packet, open new file
    fnum = gf_filter_pck_get_property(pck, GF_PROP_PCK_FILENUM);
    if (fnum) {
      fname = gf_filter_pid_get_property(pid, GF_PROP_PID_OUTPATH);
      ext = gf_filter_pid_get_property(pid, GF_PROP_PID_FILE_EXT);
      if (!fname)
        name = dasher_ctx->dst;
    }
    // filename change at packet start, open new file
    if (!fname)
      fname = gf_filter_pck_get_property(pck, GF_PROP_PCK_FILENAME);
    if (fname)
      name = fname->value.string;

    if (name) {
      dasher_open_close_file(filter, pid, name, FALSE);
    } else if (!dasher_ctx->main_file) {
      dasher_setup_file(filter, pid);
    }

    fname = gf_filter_pck_get_property(pck, GF_PROP_PCK_LLHAS_TEMPLATE);
    if (fname) {
      if (dasher_ctx->llhas_template)
        g_free(dasher_ctx->llhas_template);
      dasher_ctx->llhas_template = g_strdup(fname->value.string);
    }
  }

  // Get the data
  u32 size;
  const u8* data = gf_filter_pck_get_data(pck, &size);
  if (G_UNLIKELY(!data || !size)) {
    GST_ELEMENT_WARNING(
      io_ctx->sess->element,
      STREAM,
      FAILED,
      (NULL),
      ("Received empty packet for PID %s\n", gf_filter_pid_get_name(pid)));
    return GF_OK;
  }

  // We must be actively working on a file by now
  gpac_return_if_fail(dasher_ensure_file(filter, pid, FALSE));

  // If we are in low-latency HLS mode, we need to handle the llhas chunks
  p = gf_filter_pck_get_property(pck, GF_PROP_PCK_LLHAS_FRAG_NUM);
  if (p) {
    char* llhas_chunkname = gf_mpd_resolve_subnumber(
      dasher_ctx->llhas_template, dasher_ctx->main_file->name, p->value.uint);
    dasher_open_close_file(
      filter, pid, llhas_chunkname, TRUE); // Open the llhls file
    gf_free(llhas_chunkname);

    // Ensure the file is set up for llhls
    gpac_return_if_fail(dasher_ensure_file(filter, pid, TRUE));
  }

  // Write the data to the output stream
  gpac_return_if_fail(
    dasher_write_data(filter, pid, dasher_ctx->main_file, data, size));
  if (dasher_ctx->llhls_file) {
    // Write to the llhls file if it exists
    gpac_return_if_fail(
      dasher_write_data(filter, pid, dasher_ctx->llhls_file, data, size));
  }

  // Close the output stream
  if (end && dasher_ctx->is_manifest)
    dasher_open_close_file(filter, pid, NULL, FALSE);

  return GF_OK;
}

GPAC_FilterPPRet
dasher_consume(GF_Filter* filter, GF_FilterPid* pid, void** outptr)
{
  // We don't output any buffers directly
  return GPAC_FILTER_PP_RET_NULL;
}
