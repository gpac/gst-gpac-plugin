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

static GF_Err
gpac_default_memin_process_cb(GF_Filter* filter);

static GF_Err
gpac_default_memout_process_cb(GF_Filter* filter);

static GF_Err
gpac_default_memout_configure_pid_cb(GF_Filter* filter,
                                     GF_FilterPid* PID,
                                     Bool is_remove);

static const GF_FilterCapability DefaultMemOutCaps[] = {
  CAP_UINT(GF_CAPS_INPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_FILE),
  CAP_STRING(GF_CAPS_INPUT, GF_PROP_PID_FILE_EXT, "*"),
};

GF_FilterRegister MemOutRegister = {
  .name = "memout",
  // Set to (u32) -1 once we add support for multiple "src" pads
  .max_extra_pids = 0,
  .priority = -1,
  .flags = GF_FS_REG_FORCE_REMUX,
  .caps = DefaultMemOutCaps,
  .nb_caps = G_N_ELEMENTS(DefaultMemOutCaps),
  .process = gpac_default_memout_process_cb,
  .configure_pid = gpac_default_memout_configure_pid_cb,
};

GF_Err
gpac_memio_new(GPAC_SessionContext* sess, GPAC_MemIoDirection dir)
{
  GF_Err e = GF_OK;
  GF_Filter* memio = NULL;

  if (dir == GPAC_MEMIO_DIR_IN) {
    memio = sess->memin = gf_fs_new_filter(sess->session, "memin", 0, &e);
    g_return_val_if_fail(sess->memin, e);
    gf_filter_set_process_ckb(memio, gpac_default_memin_process_cb);
  } else {
    gf_fs_add_filter_register(sess->session, &MemOutRegister);
    memio = sess->memout = gf_fs_load_filter(sess->session, "memout", &e);
    g_return_val_if_fail(sess->memout, e);
  }

  // Set the runtime user data
  GPAC_MemIoContext* rt_udta = g_new0(GPAC_MemIoContext, 1);
  g_return_val_if_fail(rt_udta, GF_OUT_OF_MEM);
  gpac_return_if_fail(gf_filter_set_rt_udta(memio, rt_udta));
  rt_udta->dir = dir;

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
  g_return_if_fail(rt_udta);
  rt_udta->queue = queue;
}

void
gpac_memio_set_eos(GPAC_SessionContext* sess, gboolean eos)
{
  if (!sess->memin)
    return;

  GPAC_MemIoContext* rt_udta = gf_filter_get_rt_udta(sess->memin);
  g_return_if_fail(rt_udta);
  rt_udta->eos = eos;
}

gboolean
gpac_memio_set_caps(GPAC_SessionContext* sess, GstCaps* caps)
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
    GST_ERROR("Failed to convert the caps to GF_FilterCapability");
    return FALSE;
  }

  // Set the capabilities
  if (gf_filter_override_caps(sess->memout, gf_caps, new_nb_caps) != GF_OK) {
    GST_ERROR(
      "Failed to set the caps on the memory output filter, reverting to "
      "the previous caps");
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

  GPAC_MemIoContext* ctx = gf_filter_get_rt_udta(sess->memout);
  g_return_val_if_fail(ctx, GPAC_FILTER_PP_RET_ERROR);

  // Check if we have a input PID yet
  if (!ctx->ipid)
    return GPAC_FILTER_PP_RET_EMPTY;

  // Get the post-process context
  const gchar* source_name = gf_filter_pid_get_filter_name(ctx->ipid);
  post_process_registry_entry* pp_entry =
    gpac_filter_get_post_process_registry_entry(source_name);

  // Consume the packet
  return pp_entry->consume(sess->memout, outptr);
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

static GF_Err
gpac_default_memout_process_cb(GF_Filter* filter)
{
  GPAC_MemIoContext* ctx = (GPAC_MemIoContext*)gf_filter_get_rt_udta(filter);

  // Get the packet
  GF_FilterPacket* pck = gf_filter_pid_get_packet(ctx->ipid);
  if (!pck)
    return GF_OK;

  // Get the post-process context
  const gchar* source_name = gf_filter_pid_get_filter_name(ctx->ipid);
  post_process_registry_entry* pp_entry =
    gpac_filter_get_post_process_registry_entry(source_name);

  // Post-process the packet
  GF_Err e = pp_entry->post_process(filter, pck);
  gf_filter_pid_drop_packet(ctx->ipid);
  return e;
}

static GF_Err
gpac_default_memout_configure_pid_cb(GF_Filter* filter,
                                     GF_FilterPid* PID,
                                     Bool is_remove)
{
  GPAC_MemIoContext* ctx = (GPAC_MemIoContext*)gf_filter_get_rt_udta(filter);

  // Free the previous post-process context
  if (ctx->ipid) {
    const gchar* source_name = gf_filter_pid_get_filter_name(ctx->ipid);
    post_process_registry_entry* pp_entry =
      gpac_filter_get_post_process_registry_entry(source_name);
    pp_entry->ctx_free(ctx->process_ctx);
  }

  // Set the new PID
  ctx->ipid = PID;

  // Create a new post-process context
  const gchar* source_name = gf_filter_pid_get_filter_name(ctx->ipid);
  post_process_registry_entry* pp_entry =
    gpac_filter_get_post_process_registry_entry(source_name);
  pp_entry->ctx_init(&ctx->process_ctx);

  return GF_OK;
}
