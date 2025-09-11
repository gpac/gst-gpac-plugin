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

#include "lib/memio.h"
#include "gpacmessages.h"
#include "lib/caps.h"
#include "lib/pid.h"
#include "post-process/common.h"
#include "post-process/registry.h"
#include <gst/video/video-event.h>

static GF_Err
gpac_default_memin_process_cb(GF_Filter* filter);

static Bool
gpac_default_memin_process_event_cb(GF_Filter* filter,
                                    const GF_FilterEvent* evt);

static GF_Err
gpac_default_memout_initialize_cb(GF_Filter* filter);

static GF_Err
gpac_default_memout_process_cb(GF_Filter* filter);

static Bool
gpac_default_memout_process_event_cb(GF_Filter* filter,
                                     const GF_FilterEvent* evt);

static Bool
gpac_default_memout_use_alias_cb(GF_Filter* filter,
                                 const char* url,
                                 const char* mime);

static GF_FilterProbeScore
gpac_default_memout_probe_url_cb(const char* url, const char* mime);

static GF_Err
gpac_default_memout_configure_pid_cb(GF_Filter* filter,
                                     GF_FilterPid* PID,
                                     Bool is_remove);

static const GF_FilterCapability DefaultMemOutCaps[] = {
  CAP_UINT(GF_CAPS_INPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_FILE),
  CAP_STRING(GF_CAPS_INPUT, GF_PROP_PID_FILE_EXT, "*"),
};

#define OFFS(_n) #_n, offsetof(GPAC_MemOutPrivateContext, _n)
static const GF_FilterArgs MemoutArgs[] = {
  { OFFS(dst),
    "location of destination resource",
    GF_PROP_NAME,
    NULL,
    NULL,
    0 },
  { OFFS(ext), "file extension", GF_PROP_NAME, NULL, NULL, 0 },
  { 0 }
};

GF_FilterRegister MemOutRegister = {
  .name = "memout",
  .args = MemoutArgs,
  .private_size = sizeof(GPAC_MemOutPrivateContext),
  .max_extra_pids = -1,
  .priority = -1,
  .flags =
    GF_FS_REG_FORCE_REMUX | GF_FS_REG_TEMP_INIT | GF_FS_REG_EXPLICIT_ONLY,
  .caps = DefaultMemOutCaps,
  .nb_caps = G_N_ELEMENTS(DefaultMemOutCaps),
  .initialize = gpac_default_memout_initialize_cb,
  .process = gpac_default_memout_process_cb,
  .process_event = gpac_default_memout_process_event_cb,
  .use_alias = gpac_default_memout_use_alias_cb,
  .probe_url = gpac_default_memout_probe_url_cb,
  .configure_pid = gpac_default_memout_configure_pid_cb,
};

GF_Err
gpac_memio_new(GPAC_SessionContext* sess, GPAC_MemIoDirection dir)
{
  GF_Err e = GF_OK;
  GF_Filter* memio = NULL;

  if (dir == GPAC_MEMIO_DIR_IN) {
    memio = sess->memin = gf_fs_new_filter(sess->session, "memin", 0, &e);
    if (!sess->memin) {
      GST_ELEMENT_ERROR(sess->element,
                        LIBRARY,
                        INIT,
                        (NULL),
                        ("Failed to create memin filter"));
      return e;
    }
    gf_filter_set_process_ckb(memio, gpac_default_memin_process_cb);
    gf_filter_set_process_event_ckb(memio, gpac_default_memin_process_event_cb);
  } else {
    gf_fs_add_filter_register(sess->session, &MemOutRegister);
    gchar* filter_name = "memout";
    gboolean link_to_last_filter = sess->params && sess->params->is_single;

    // Try to retrieve a destination
    const gchar* dst = NULL;
    if (sess->destination) {
      // If a destination is provided, use it
      dst = sess->destination;
      link_to_last_filter = TRUE;
    } else if (sess->params && sess->params->info &&
               sess->params->info->destination) {
      // If the session parameters have a destination, use it
      dst = sess->params->info->destination;
    }

    // If we have a specific destination, use it
    if (dst)
      filter_name = g_strdup_printf("memout:dst=%s", dst);

    memio = sess->memout = gf_fs_load_filter(sess->session, filter_name, &e);
    if (!sess->memout) {
      GST_ELEMENT_ERROR(
        sess->element, LIBRARY, INIT, (NULL), ("Failed to load memout filter"));
      return e;
    }

    // If we are in single filter mode (explicit destination), we explicitly
    // connect to the last loaded filter to avoid connecting to the memin filter
    // unnecessarily
    if (link_to_last_filter) {
      u32 count = gf_fs_get_filters_count(sess->session);
      GF_Filter* filter = gf_fs_get_filter(sess->session, count - 2);
      if (filter) {
        // Connect the memout filter to the last filter
        if (gf_filter_set_source(memio, filter, NULL) != GF_OK) {
          GST_ELEMENT_ERROR(sess->element,
                            LIBRARY,
                            INIT,
                            (NULL),
                            ("Failed to connect memout filter to the last "
                             "loaded filter %s",
                             gf_filter_get_name(filter)));
          return GF_BAD_PARAM;
        }
      } else {
        GST_ELEMENT_ERROR(sess->element,
                          LIBRARY,
                          INIT,
                          (NULL),
                          ("No filters found in the session"));
        return GF_BAD_PARAM;
      }
    }
  }

  // Set the runtime user data
  GPAC_MemIoContext* rt_udta = g_new0(GPAC_MemIoContext, 1);
  if (!rt_udta) {
    GST_ELEMENT_ERROR(sess->element,
                      LIBRARY,
                      INIT,
                      (NULL),
                      ("Failed to allocate memory for runtime user data"));
    return GF_OUT_OF_MEM;
  }
  gpac_return_if_fail(gf_filter_set_rt_udta(memio, rt_udta));
  rt_udta->dir = dir;
  rt_udta->global_offset = GST_CLOCK_TIME_NONE;
  rt_udta->sess = sess;

  return e;
}

void
gpac_memio_free(GPAC_SessionContext* sess)
{
  if (sess->memin)
    gf_free(gf_filter_get_rt_udta(sess->memin));

  if (sess->memout)
    gf_free(gf_filter_get_rt_udta(sess->memout));
}

void
gpac_memio_assign_queue(GPAC_SessionContext* sess,
                        GPAC_MemIoDirection dir,
                        GQueue* queue)
{
  GPAC_MemIoContext* rt_udta = gf_filter_get_rt_udta(
    dir == GPAC_MEMIO_DIR_IN ? sess->memin : sess->memout);
  if (!rt_udta) {
    GST_ELEMENT_ERROR(sess->element,
                      LIBRARY,
                      FAILED,
                      (NULL),
                      ("Failed to get runtime user data"));
    return;
  }
  rt_udta->queue = queue;
}

void
gpac_memio_set_eos(GPAC_SessionContext* sess, gboolean eos)
{
  if (!sess->memin)
    return;

  GPAC_MemIoContext* rt_udta = gf_filter_get_rt_udta(sess->memin);
  if (!rt_udta) {
    GST_ELEMENT_ERROR(sess->element,
                      LIBRARY,
                      FAILED,
                      (NULL),
                      ("Failed to get runtime user data"));
    return;
  }
  rt_udta->eos = eos;
}

gboolean
gpac_memio_set_gst_caps(GPAC_SessionContext* sess, GstCaps* caps)
{
  if (!sess->memout)
    return TRUE;

  // Save the current caps
  guint cur_nb_caps = 0;
  const GF_FilterCapability* current_caps =
    gf_filter_get_caps(sess->memout, &cur_nb_caps);

  // Convert the caps to GF_FilterCapability
  guint new_nb_caps = 0;
  GF_FilterCapability* gf_caps = gpac_gstcaps_to_gfcaps(caps, &new_nb_caps);
  if (!gf_caps) {
    GST_ELEMENT_ERROR(sess->element,
                      STREAM,
                      FAILED,
                      (NULL),
                      ("Failed to convert the caps to GF_FilterCapability"));
    return FALSE;
  }

  // Set the capabilities
  if (gf_filter_override_caps(sess->memout, gf_caps, new_nb_caps) != GF_OK) {
    GST_ELEMENT_ERROR(sess->element,
                      STREAM,
                      FAILED,
                      (NULL),
                      ("Failed to set the caps on the memory output filter, "
                       "reverting to the previous caps"));
    gf_free(gf_caps);
    gf_filter_override_caps(sess->memout, current_caps, cur_nb_caps);
    return FALSE;
  }

  // Reconnect the pipeline
  u32 count = gf_fs_get_filters_count(sess->session);
  for (u32 i = 0; i < count; i++) {
    GF_Filter* filter = gf_fs_get_filter(sess->session, i);
    gf_filter_reconnect_output(filter, NULL);
  }

  return TRUE;
}

GPAC_FilterPPRet
gpac_memio_consume(GPAC_SessionContext* sess, void** outptr)
{
  if (!sess->memout)
    return GPAC_FILTER_PP_RET_NULL;

  // Context
  guint32 pid_to_consume = 0;
  GF_FilterPid* best_ipid = NULL;
  GPAC_MemOutPIDContext* best_pctx = NULL;
  GPAC_FilterPPRet ret = GPAC_FILTER_PP_RET_INVALID;

  // Find the PID to consume
  for (u32 i = 0; i < gf_filter_get_ipid_count(sess->memout); i++) {
    GF_FilterPid* ipid = gf_filter_get_ipid(sess->memout, i);
    GPAC_MemOutPIDContext* pctx =
      (GPAC_MemOutPIDContext*)gf_filter_pid_get_udta(ipid);
    GPAC_MemOutPIDFlags udta_flags = gf_filter_pid_get_udta_flags(ipid);

    // If we should not consume this PID, call the consume callback
    // and continue to the next one
    if (udta_flags & GPAC_MEMOUT_PID_FLAG_DONT_CONSUME) {
      ret |= pctx->entry->consume(sess->memout, ipid, NULL);
      continue;
    }

    // Check if there are more than one PID to consume
    if (pid_to_consume > 0) {
      GST_ELEMENT_ERROR(sess->element,
                        STREAM,
                        FAILED,
                        (NULL),
                        ("Multiple PIDs to consume, this is not supported by "
                         "the memout filter"));
      return GPAC_FILTER_PP_RET_ERROR;
    }

    // We can consume this PID
    pid_to_consume++;
    best_ipid = ipid;
    best_pctx = pctx;
  }

  if (!best_ipid) {
    *outptr = NULL;
    // No PID to consume
    return ret == GPAC_FILTER_PP_RET_INVALID
             ? GPAC_FILTER_PP_RET_EMPTY
             : ret; // If we have a signal, return it
  }

  // We can consume the PID
  ret |= best_pctx->entry->consume(sess->memout, best_ipid, outptr);
  return ret;
}

void
gpac_memio_set_global_offset(GPAC_SessionContext* sess,
                             const GstSegment* segment)
{
  if (!sess->memout)
    return;

  GPAC_MemIoContext* ctx = gf_filter_get_rt_udta(sess->memout);
  if (!ctx) {
    GST_ELEMENT_ERROR(sess->element,
                      LIBRARY,
                      FAILED,
                      (NULL),
                      ("Failed to get runtime user data"));
    return;
  }

  // Get the segment offset
  guint64 offset = segment->base + segment->start + segment->offset;
  if (offset == GST_CLOCK_TIME_NONE)
    return;
  if (ctx->global_offset == GST_CLOCK_TIME_NONE || !ctx->is_continuous) {
    ctx->global_offset = MIN(offset, ctx->global_offset);
  } else if (ctx->global_offset > offset) {
    GST_ELEMENT_WARNING(
      sess->element,
      STREAM,
      FAILED,
      (NULL),
      ("Cannot set a global offset smaller than the current one"));
  }
}

//////////////////////////////////////////////////////////////////////////
// #MARK: Default Callbacks
//////////////////////////////////////////////////////////////////////////
static GF_Err
gpac_default_memin_process_cb(GF_Filter* filter)
{
  GPAC_MemIoContext* ctx = (GPAC_MemIoContext*)gf_filter_get_rt_udta(filter);

  // Flush the queue
  GF_FilterPacket* packet = NULL;
  while ((packet = g_queue_pop_head(ctx->queue)))
    gf_filter_pck_send(packet);

  // All packets are sent, check if the EOS is set
  if (ctx->eos) {
    // Set the EOS on all output PIDs
    for (guint i = 0; i < gf_filter_get_opid_count(filter); i++)
      gf_filter_pid_set_eos(gf_filter_get_opid(filter, i));
    return GF_EOS;
  }

  return GF_OK;
}

static Bool
gpac_default_memin_process_event_cb(GF_Filter* filter,
                                    const GF_FilterEvent* evt)
{
  if (evt->base.type == GF_FEVT_TRANSPORT_HINTS) {
    GF_FilterPid* pid = evt->base.on_pid;
    GF_Fraction intra_period = evt->transport_hints.seg_duration;
    GpacPadPrivate* priv = gf_filter_pid_get_udta(pid);
    GstElement* element = gst_pad_get_parent_element(priv->self);

    // Set the IDR period
    priv->idr_period =
      gf_timestamp_rescale(intra_period.num, intra_period.den, GST_SECOND);

    // Determine the next IDR frame
    if (priv->idr_last != GST_CLOCK_TIME_NONE) {
      priv->idr_next = priv->idr_last + priv->idr_period;

      // Request a new IDR, immediately
      GstEvent* gst_event =
        gst_video_event_new_upstream_force_key_unit(priv->idr_next, TRUE, 1);
      if (!gst_pad_push_event(priv->self, gst_event)) {
        GST_ELEMENT_WARNING(element,
                            STREAM,
                            FAILED,
                            (NULL),
                            ("Failed to push the force key unit event"));
      }
    }

    return GF_TRUE;
  }
  return GF_FALSE;
}

static GF_Err
gpac_default_memout_initialize_cb(GF_Filter* filter)
{
  GPAC_MemOutPrivateContext* ctx =
    (GPAC_MemOutPrivateContext*)gf_filter_get_udta(filter);

  // ext only used if not an alias, otherwise figure out from dst
  const char* ext = gf_filter_is_alias(filter) ? NULL : ctx->ext;
  if (!ext && ctx->dst) {
    ext = gf_file_ext_start(ctx->dst);
    if (ext)
      ext++;
  }

  GF_LOG(GF_LOG_INFO,
         GF_LOG_CORE,
         ("memout initialize ext %s dst %s is_alias %d\n",
          ext ? ext : "none",
          ctx->dst ? ctx->dst : "none",
          gf_filter_is_alias(filter)));
  if (!ext)
    return GF_OK;

  ctx->caps[0].code = GF_PROP_PID_STREAM_TYPE;
  ctx->caps[0].val = PROP_UINT(GF_STREAM_FILE);
  ctx->caps[0].flags = GF_CAPS_INPUT;

  ctx->caps[1].code = GF_PROP_PID_FILE_EXT;
  ctx->caps[1].val = PROP_STRING(ext);
  ctx->caps[1].flags = GF_CAPS_INPUT;
  gf_filter_override_caps(filter, ctx->caps, 2);

  return GF_OK;
}

static GF_Err
gpac_default_memout_process_cb(GF_Filter* filter)
{
  GPAC_MemIoContext* ctx = (GPAC_MemIoContext*)gf_filter_get_rt_udta(filter);

  for (u32 i = 0; i < gf_filter_get_ipid_count(filter); i++) {
    GF_Err e = GF_OK;
    GF_FilterPid* ipid = gf_filter_get_ipid(filter, i);
    GPAC_MemOutPIDContext* pctx =
      (GPAC_MemOutPIDContext*)gf_filter_pid_get_udta(ipid);

    // Get the packet
    GF_FilterPacket* pck = gf_filter_pid_get_packet(ipid);

    // If we have a post-process context, process the packet
    if (pctx && pctx->entry)
      e = pctx->entry->post_process(filter, ipid, pck);

    if (pck)
      gf_filter_pid_drop_packet(ipid);
    if (e != GF_OK)
      return e;
  }

  return GF_OK;
}

static Bool
gpac_default_memout_process_event_cb(GF_Filter* filter,
                                     const GF_FilterEvent* evt)
{
  GPAC_MemOutPIDContext* pctx =
    (GPAC_MemOutPIDContext*)gf_filter_pid_get_udta(evt->base.on_pid);

  // If we have a post-process context, process the event
  if (pctx && pctx->entry)
    return pctx->entry->process_event(filter, evt);
  return GF_FALSE; // No post-process context, do not process the event
}

static Bool
gpac_default_memout_use_alias_cb(GF_Filter* filter,
                                 const char* url,
                                 const char* mime)
{
  return GF_TRUE; // Always allow alias usage
}

static GF_FilterProbeScore
gpac_default_memout_probe_url_cb(const char* url, const char* mime)
{
  return GF_FPROBE_SUPPORTED; // Support everything
}

static GF_Err
gpac_default_memout_configure_pid_cb(GF_Filter* filter,
                                     GF_FilterPid* pid,
                                     Bool is_remove)
{
  GPAC_MemIoContext* ctx = (GPAC_MemIoContext*)gf_filter_get_rt_udta(filter);
  GPAC_MemOutPIDContext* pctx = gf_filter_pid_get_udta(pid);
  GPAC_MemOutPIDFlags udta = gf_filter_pid_get_udta_flags(pid);

  if (is_remove) {
    if (pctx) {
      // Free the post-process context if it exists
      if (pctx->entry)
        pctx->entry->ctx_free(pctx->private_ctx);
      g_free(pctx);
    }
    return GF_OK;
  }

  if (!udta) {
    GF_FilterEvent evt;
    gf_filter_pid_init_play_event(pid, &evt, 0, 1, "MemOut");
    gf_filter_pid_send_event(pid, &evt);
    gf_filter_pid_set_udta_flags(pid, GPAC_MEMOUT_PID_FLAG_INITIALIZED);
  } else {
    // If the PID is already initialized, we can skip the configuration
    if (udta & GPAC_MEMOUT_PID_FLAG_INITIALIZED) {
      GST_DEBUG_OBJECT(ctx->sess->element,
                       "PID %s is already initialized, proceeding with "
                       "configure_pid callback only",
                       gf_filter_pid_get_name(pid));
      return pctx->entry->configure_pid(filter, pid);
    }
  }

  // Allocate the post-process context
  pctx = g_new0(GPAC_MemOutPIDContext, 1);
  gf_filter_pid_set_udta(pid, pctx);

  // Check if there is a "dasher" upstream filter
  gboolean has_dasher = FALSE;
  GF_Filter* uf = gf_filter_pid_get_source_filter(pid);
  while (uf) {
    if (g_strcmp0(gf_filter_get_name(uf), "dasher") == 0) {
      has_dasher = TRUE;
      break;
    }
    GF_FilterPid* upid = gf_filter_get_ipid(uf, 0);
    if (!upid)
      break; // No upstream PID, stop checking
    uf = gf_filter_pid_get_source_filter(upid);
  }

  // Decide which post-process context to use
  if (has_dasher) {
    // If the upstream chain has dasher, we use the DASH post-process context,
    // regardless of whether it's connected to dasher directly or mp4mx
    pctx->entry = gpac_filter_get_post_process_registry_entry("dasher");
  } else {
    // Otherwise, use the post-process context based on connected filter name
    const gchar* source_name = gf_filter_pid_get_filter_name(pid);
    pctx->entry = gpac_filter_get_post_process_registry_entry(source_name);
  }

  // Create a new post-process context
  pctx->entry->ctx_init(&pctx->private_ctx);

  // Configure the PID with the post-process context
  return pctx->entry->configure_pid(filter, pid);
}
