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
#include "elements/gstgpactf.h"
#include "lib/memio.h"
#include <gpac/internal/isomedia_dev.h>

GST_DEBUG_CATEGORY_STATIC(gpac_mp4mx);
#define GST_CAT_DEFAULT gpac_mp4mx

#define GET_TYPE(type) mp4mx_ctx->contents[type]

static guint32 INIT_BOXES[] = { GF_ISOM_BOX_TYPE_FTYP,
                                GF_ISOM_BOX_TYPE_FREE,
                                GF_ISOM_BOX_TYPE_MOOV,
                                0 };
static guint32 HEADER_BOXES[] = { GF_ISOM_BOX_TYPE_STYP,
                                  GF_ISOM_BOX_TYPE_SIDX,
                                  GF_ISOM_BOX_TYPE_MOOF,
                                  0 };
static guint32 DATA_BOXES[] = { GF_ISOM_BOX_TYPE_MDAT, 0 };

typedef enum _BufferType
{
  INIT = 0,
  HEADER,
  DATA,

  LAST
} BufferType;

struct BoxMapping
{
  BufferType type;
  guint32* boxes;
} box_mapping[LAST] = {
  { INIT, INIT_BOXES },
  { HEADER, HEADER_BOXES },
  { DATA, DATA_BOXES },
};

typedef struct
{
  GstBuffer* buffer;
  gboolean is_complete;
} BufferContents;

typedef struct
{
  guint32 box_type;
  guint32 box_size;
  GstBuffer* buffer;
  gboolean parsed;
} BoxInfo;

typedef struct
{
  guint32 track_id;
  guint32 timescale;
  gboolean defaults_present;
  guint32 default_sample_duration;
  guint32 default_sample_size;
  guint32 default_sample_flags;
} TrackInfo;

typedef struct
{
  gboolean is_sync;
  gssize size;
  guint64 pts;
  guint64 dts;
  guint64 duration;
} SampleInfo;

typedef struct
{
  // Output queue for complete fragments
  GQueue* output_queue;

  // State
  BufferType current_type;
  guint32 segment_count;

  // Box parser state
  GQueue* box_queue;

  // Buffer contents for the init, header, and data
  BufferContents* contents[3];

  // Input context
  guint64 duration;
  guint64 mp4mx_ts;
  GHashTable* tracks;
  GArray* next_samples;
} Mp4mxCtx;

void
mp4mx_ctx_init(void** process_ctx)
{
  *process_ctx = g_new0(Mp4mxCtx, 1);
  Mp4mxCtx* ctx = (Mp4mxCtx*)*process_ctx;

  GST_DEBUG_CATEGORY_INIT(
    gpac_mp4mx, "gpacmp4mxpp", 0, "GPAC mp4mx post-processor");

  // Initialize the context
  ctx->output_queue = g_queue_new();
  ctx->current_type = INIT;
  ctx->segment_count = 0;
  ctx->box_queue = g_queue_new();

  // Allocate tracks and next samples
  ctx->tracks =
    g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
  ctx->next_samples = g_array_new(FALSE, TRUE, sizeof(SampleInfo));

  // Initialize the buffer contents
  for (guint i = 0; i < LAST; i++) {
    ctx->contents[i] = g_new0(BufferContents, 1);
    ctx->contents[i]->is_complete = FALSE;
  }
}

void
mp4mx_ctx_free(void* process_ctx)
{
  Mp4mxCtx* ctx = (Mp4mxCtx*)process_ctx;

  // Free the output queue
  while (!g_queue_is_empty(ctx->output_queue))
    gst_buffer_unref((GstBuffer*)g_queue_pop_head(ctx->output_queue));
  g_queue_free(ctx->output_queue);

  // Free the box queue
  while (!g_queue_is_empty(ctx->box_queue)) {
    BoxInfo* buf = g_queue_pop_head(ctx->box_queue);
    if (buf->buffer)
      gst_buffer_unref(buf->buffer);
  }
  g_queue_free(ctx->box_queue);

  // Free the buffer contents
  for (guint i = 0; i < LAST; i++) {
    if (ctx->contents[i]->buffer)
      gst_buffer_unref(ctx->contents[i]->buffer);
    g_free(ctx->contents[i]);
  }

  // Free the tracks and next samples
  g_hash_table_destroy(ctx->tracks);
  g_array_free(ctx->next_samples, TRUE);

  // Free the context
  g_free(ctx);
}

GF_Err
mp4mx_configure_pid(GF_Filter* filter, GF_FilterPid* pid)
{
  GPAC_MemOutPIDContext* pctx =
    (GPAC_MemOutPIDContext*)gf_filter_pid_get_udta(pid);
  Mp4mxCtx* mp4mx_ctx = (Mp4mxCtx*)pctx->private_ctx;

  // Get the timescale from the PID
  mp4mx_ctx->mp4mx_ts = GST_SECOND;
  const GF_PropertyValue* p =
    gf_filter_pid_get_property(pid, GF_PROP_PID_TIMESCALE);
  if (p)
    mp4mx_ctx->mp4mx_ts = p->value.uint;

  return GF_OK;
}

Bool
mp4mx_process_event(GF_Filter* filter, const GF_FilterEvent* evt)
{
  return GF_FALSE; // No event processing
}

GstMemory*
mp4mx_create_memory(const u8* data, guint32 size, GF_FilterPacket* pck)
{
  gf_filter_pck_ref(&pck);
  return gst_memory_new_wrapped(GST_MEMORY_FLAG_READONLY,
                                (gpointer)data,
                                size,
                                0,
                                size,
                                pck,
                                (GDestroyNotify)gf_filter_pck_unref);
}

gboolean
mp4mx_is_box_complete(BoxInfo* box)
{
  return box && box->buffer &&
         gst_buffer_get_size(box->buffer) == box->box_size;
}

GF_Err
mp4mx_parse_moov(GF_Filter* filter, GF_FilterPid* pid, GstBuffer* buffer)
{
  GPAC_MemIoContext* ctx = (GPAC_MemIoContext*)gf_filter_get_rt_udta(filter);
  GPAC_MemOutPIDContext* pctx =
    (GPAC_MemOutPIDContext*)gf_filter_pid_get_udta(pid);
  Mp4mxCtx* mp4mx_ctx = (Mp4mxCtx*)pctx->private_ctx;

  // Map the buffer
  g_auto(GstBufferMapInfo) map = GST_MAP_INFO_INIT;
  if (G_UNLIKELY(!gst_buffer_map(buffer, &map, GST_MAP_READ))) {
    GST_ELEMENT_ERROR(ctx->sess->element,
                      STREAM,
                      FAILED,
                      (NULL),
                      ("Failed to map moov buffer"));
    return GF_CORRUPTED_DATA;
  }

  // Parse the box
  GF_BitStream* bs = gf_bs_new(map.data, map.size, GF_BITSTREAM_READ);
  GF_Box* moov = NULL;
  GF_Err err = gf_isom_box_parse(&moov, bs);
  if (err != GF_OK) {
    GST_ELEMENT_ERROR(
      ctx->sess->element, STREAM, FAILED, (NULL), ("Failed to parse moov box"));
    return GF_CORRUPTED_DATA;
  }

  // Check if we have duration in the mvhd box
  GF_MovieHeaderBox* mvhd = (GF_MovieHeaderBox*)gf_isom_box_find_child(
    moov->child_boxes, GF_ISOM_BOX_TYPE_MVHD);
  if (mvhd) {
    mp4mx_ctx->duration =
      gf_timestamp_rescale(mvhd->duration, mvhd->timeScale, GST_SECOND);
  }

  // Go through all tracks
  guint32 count = gf_list_count(moov->child_boxes);
  for (guint32 i = 0; i < count; i++) {
    GF_Box* box = (GF_Box*)gf_list_get(moov->child_boxes, i);
    if (box->type != GF_ISOM_BOX_TYPE_TRAK)
      continue;

    // Create new tracks
    TrackInfo* track = g_new0(TrackInfo, 1);

    // Get the track id
    GF_TrackHeaderBox* tkhd = (GF_TrackHeaderBox*)gf_isom_box_find_child(
      box->child_boxes, GF_ISOM_BOX_TYPE_TKHD);
    track->track_id = tkhd->trackID;

    // Get the timescale
    GF_Box* mdia =
      gf_isom_box_find_child(box->child_boxes, GF_ISOM_BOX_TYPE_MDIA);
    GF_MediaHeaderBox* mdhd = (GF_MediaHeaderBox*)gf_isom_box_find_child(
      mdia->child_boxes, GF_ISOM_BOX_TYPE_MDHD);
    track->timescale = mdhd->timeScale;

    GST_DEBUG_OBJECT(ctx->sess->element,
                     "Found track %d with timescale %d",
                     track->track_id,
                     track->timescale);

    // Find the mvex for this track
    for (guint32 j = 0; j < gf_list_count(moov->child_boxes); j++) {
      GF_Box* mvex = (GF_Box*)gf_list_get(moov->child_boxes, j);
      if (mvex->type != GF_ISOM_BOX_TYPE_MVEX)
        continue;

      // Find the track fragment box
      for (guint32 k = 0; k < gf_list_count(mvex->child_boxes); k++) {
        GF_TrackExtendsBox* trex =
          (GF_TrackExtendsBox*)gf_list_get(mvex->child_boxes, k);
        if (trex->type != GF_ISOM_BOX_TYPE_TREX)
          continue;

        // Check if this is the right track
        if (trex->trackID != track->track_id)
          continue;

        // Set the defaults
        track->defaults_present = TRUE;
        track->default_sample_duration = trex->def_sample_duration;
        track->default_sample_size = trex->def_sample_size;
        track->default_sample_flags = trex->def_sample_flags;

        GST_DEBUG_OBJECT(ctx->sess->element,
                         "Found defaults for track %d: duration: %d, size: %d, "
                         "flags: %d",
                         track->track_id,
                         track->default_sample_duration,
                         track->default_sample_size,
                         track->default_sample_flags);
      }
    }

    // Add the track to the hash table
    g_hash_table_insert(
      mp4mx_ctx->tracks, GUINT_TO_POINTER(track->track_id), track);
  }

  // Free the box
  gf_isom_box_del(moov);
  gf_bs_del(bs);

  return GF_OK;
}

GF_Err
mp4mx_parse_moof(GF_Filter* filter, GF_FilterPid* pid, GstBuffer* buffer)
{
  GPAC_MemIoContext* ctx = (GPAC_MemIoContext*)gf_filter_get_rt_udta(filter);
  GPAC_MemOutPIDContext* pctx =
    (GPAC_MemOutPIDContext*)gf_filter_pid_get_udta(pid);
  Mp4mxCtx* mp4mx_ctx = (Mp4mxCtx*)pctx->private_ctx;

  // Map the buffer
  g_auto(GstBufferMapInfo) map = GST_MAP_INFO_INIT;
  if (G_UNLIKELY(!gst_buffer_map(buffer, &map, GST_MAP_READ))) {
    GST_ELEMENT_ERROR(ctx->sess->element,
                      STREAM,
                      FAILED,
                      (NULL),
                      ("Failed to map moof buffer"));
    return GF_CORRUPTED_DATA;
  }

  // Parse the box
  GF_BitStream* bs = gf_bs_new(map.data, map.size, GF_BITSTREAM_READ);
  GF_Box* moof = NULL;
  GF_Err err = gf_isom_box_parse(&moof, bs);
  if (err != GF_OK) {
    GST_ELEMENT_ERROR(
      ctx->sess->element, STREAM, FAILED, (NULL), ("Failed to parse moof box"));
    return GF_CORRUPTED_DATA;
  }

  // Check if there are multiple tracks
  guint32 count = 0;
  for (guint32 i = 0; i < gf_list_count(moof->child_boxes); i++) {
    GF_Box* box = (GF_Box*)gf_list_get(moof->child_boxes, i);
    if (box->type == GF_ISOM_BOX_TYPE_TRAF)
      count++;
  }
  if (count > 1) {
    GST_FIXME_OBJECT(ctx->sess->element,
                     "Multiple tracks in moof not supported yet");
    return GF_NOT_SUPPORTED;
  }

  // Find the required boxes
  GF_Box* traf =
    gf_isom_box_find_child(moof->child_boxes, GF_ISOM_BOX_TYPE_TRAF);
  if (!traf) {
    GST_DEBUG_OBJECT(ctx->sess->element, "No traf box found");
    g_array_set_size(mp4mx_ctx->next_samples, 0);
    goto empty_moof;
  }

  GF_TrackFragmentHeaderBox* tfhd =
    (GF_TrackFragmentHeaderBox*)gf_isom_box_find_child(traf->child_boxes,
                                                       GF_ISOM_BOX_TYPE_TFHD);
  GF_TFBaseMediaDecodeTimeBox* tfdt =
    (GF_TFBaseMediaDecodeTimeBox*)gf_isom_box_find_child(traf->child_boxes,
                                                         GF_ISOM_BOX_TYPE_TFDT);
  GF_TrackFragmentRunBox* trun =
    (GF_TrackFragmentRunBox*)gf_isom_box_find_child(traf->child_boxes,
                                                    GF_ISOM_BOX_TYPE_TRUN);

  // Get the track info
  TrackInfo* track =
    g_hash_table_lookup(mp4mx_ctx->tracks, GUINT_TO_POINTER(tfhd->trackID));
  if (!track) {
    GST_ERROR_OBJECT(ctx->sess->element, "Track %d not found", tfhd->trackID);
    return GF_BAD_PARAM;
  }

  // Declare the defaults
  guint32 default_sample_duration = track->default_sample_duration;
  guint32 default_sample_size = track->default_sample_size;
  guint32 default_sample_flags = track->default_sample_flags;

  // Look at the track fragment header box
  gboolean default_sample_duration_present = (tfhd->flags & 0x8) == 0x8;
  gboolean default_sample_size_present = (tfhd->flags & 0x10) == 0x10;
  gboolean default_sample_flags_present = (tfhd->flags & 0x20) == 0x20;

  if (default_sample_duration_present)
    default_sample_duration = tfhd->def_sample_duration;
  if (default_sample_size_present)
    default_sample_size = tfhd->def_sample_size;
  if (default_sample_flags_present)
    default_sample_flags = tfhd->def_sample_flags;

  // Resize the next samples array
  g_array_set_size(mp4mx_ctx->next_samples, trun->sample_count);

  // Look at all samples
  for (guint32 i = 0; i < trun->sample_count; i++) {
    GF_TrunEntry* entry = &trun->samples[i];
    SampleInfo* sample = &g_array_index(mp4mx_ctx->next_samples, SampleInfo, i);

    // Check if this is a sync sample
    gboolean first_sample_flags_present = (trun->flags & 0x4) == 0x4;
    gboolean sample_flags_present = (trun->flags & 0x400) == 0x400;

    u32 flags = 0;
    if (i == 0 && first_sample_flags_present) {
      flags = trun->first_sample_flags;
    } else if (sample_flags_present) {
      flags = entry->flags;
    } else if (default_sample_flags_present || track->defaults_present) {
      flags = default_sample_flags;
    } else {
      GST_ERROR_OBJECT(ctx->sess->element, "No sample flags found");
      return GF_CORRUPTED_DATA;
    }

    sample->is_sync = GF_ISOM_GET_FRAG_SYNC(flags);

    // Retrieve the sample size and offset
    gboolean sample_size_present = (trun->flags & 0x200) == 0x200;

    if (sample_size_present) {
      sample->size = entry->size;
    } else if (default_sample_size_present || track->defaults_present) {
      sample->size = default_sample_size;
    } else {
      GST_ERROR_OBJECT(ctx->sess->element, "No sample size found");
      return GF_CORRUPTED_DATA;
    }

    // Retrieve the sample duration
    gboolean sample_duration_present = (trun->flags & 0x100) == 0x100;

    guint64 duration = 0;
    if (sample_duration_present) {
      duration = entry->Duration;
    } else if (default_sample_duration_present || track->defaults_present) {
      duration = default_sample_duration;
    } else {
      GST_ERROR_OBJECT(ctx->sess->element, "No sample duration found");
      return GF_CORRUPTED_DATA;
    }

    sample->duration =
      gf_timestamp_rescale(duration, track->timescale, GST_SECOND);

    // Retrieve the sample DTS
    guint64 dts = tfdt->baseMediaDecodeTime + i;
    sample->dts = gf_timestamp_rescale(dts, track->timescale, GST_SECOND);
    sample->dts += ctx->global_offset;

    // Retrieve the sample PTS
    guint64 pts = dts + entry->CTS_Offset;
    sample->pts = gf_timestamp_rescale(pts, track->timescale, GST_SECOND);
    sample->pts += ctx->global_offset;

    GST_TRACE_OBJECT(ctx->sess->element,
                     "Sample %d [%s]: size: %" G_GSSIZE_FORMAT ", "
                     "duration: %" GST_TIME_FORMAT ", "
                     "DTS: %" GST_TIME_FORMAT ", "
                     "PTS: %" GST_TIME_FORMAT,
                     i,
                     sample->is_sync ? "S" : "NS",
                     sample->size,
                     GST_TIME_ARGS(sample->duration),
                     GST_TIME_ARGS(sample->dts),
                     GST_TIME_ARGS(sample->pts));
  }

empty_moof:
  // Free the box
  gf_isom_box_del(moof);
  gf_bs_del(bs);

  return GF_OK;
}

gboolean
mp4mx_parse_boxes(GF_Filter* filter, GF_FilterPid* pid, GF_FilterPacket* pck)
{
  GPAC_MemIoContext* ctx = (GPAC_MemIoContext*)gf_filter_get_rt_udta(filter);
  GPAC_MemOutPIDContext* pctx =
    (GPAC_MemOutPIDContext*)gf_filter_pid_get_udta(pid);
  Mp4mxCtx* mp4mx_ctx = (Mp4mxCtx*)pctx->private_ctx;

  // Get the data
  u32 size;
  const u8* data = gf_filter_pck_get_data(pck, &size);
  GST_DEBUG_OBJECT(
    ctx->sess->element, "Processing data of size %" G_GUINT32_FORMAT, size);

  guint32 offset = 0;
  while (offset < size) {
    BoxInfo* box = g_queue_peek_tail(mp4mx_ctx->box_queue);
    if (!box || mp4mx_is_box_complete(box)) {
      box = g_new0(BoxInfo, 1);
      g_queue_push_tail(mp4mx_ctx->box_queue, box);
    }

    // If we have leftover data, append it to the current buffer
    if (box->buffer && gst_buffer_get_size(box->buffer) != box->box_size) {
      guint32 leftover =
        MIN(box->box_size - gst_buffer_get_size(box->buffer), size - offset);
      if (gst_buffer_get_size(box->buffer) > 0)
        GST_DEBUG_OBJECT(ctx->sess->element,
                         "Incomplete box %s, appending %" G_GUINT32_FORMAT
                         " bytes",
                         gf_4cc_to_str(box->box_type),
                         leftover);
      GstMemory* mem = mp4mx_create_memory(data + offset, leftover, pck);

      // Append the memory to the buffer
      gst_buffer_append_memory(box->buffer, mem);

      // Update the offset
      offset += leftover;
      GST_DEBUG_OBJECT(ctx->sess->element,
                       "Wrote %" G_GSIZE_FORMAT " bytes of %" G_GUINT32_FORMAT
                       " for box %s",
                       gst_buffer_get_size(box->buffer),
                       box->box_size,
                       gf_4cc_to_str(box->box_type));
      continue;
    }

    // Parse the box header
    box->box_size =
      GUINT32_FROM_LE((data[offset] << 24) | (data[offset + 1] << 16) |
                      (data[offset + 2] << 8) | data[offset + 3]);
    box->box_type =
      GUINT32_FROM_LE((data[offset + 4] << 24) | (data[offset + 5] << 16) |
                      (data[offset + 6] << 8) | data[offset + 7]);
    GST_DEBUG_OBJECT(ctx->sess->element,
                     "Saw box %s with size %" G_GUINT32_FORMAT,
                     gf_4cc_to_str(box->box_type),
                     box->box_size);

    // Create a new buffer
    box->buffer = gst_buffer_new();

    // Preserve the timing information
    GST_BUFFER_PTS(box->buffer) = gf_filter_pck_get_cts(pck);
    GST_BUFFER_DTS(box->buffer) = gf_filter_pck_get_dts(pck);
    GST_BUFFER_DURATION(box->buffer) = gf_filter_pck_get_duration(pck);
  }

  // Check if process can continue
  BoxInfo* box = g_queue_peek_head(mp4mx_ctx->box_queue);
  if (!mp4mx_is_box_complete(box))
    return FALSE;

  // Check if we need any further parsing
  for (guint i = 0; i < g_queue_get_length(mp4mx_ctx->box_queue); i++) {
    BoxInfo* box = g_queue_peek_nth(mp4mx_ctx->box_queue, i);
    if (!mp4mx_is_box_complete(box) || box->parsed)
      continue;

    switch (box->box_type) {
      case GF_ISOM_BOX_TYPE_MOOV:
        gpac_return_val_if_fail(mp4mx_parse_moov(filter, pid, box->buffer),
                                FALSE);
        break;

      case GF_ISOM_BOX_TYPE_MOOF:
        gpac_return_val_if_fail(mp4mx_parse_moof(filter, pid, box->buffer),
                                FALSE);
        break;

      default:
        break;
    }

    box->parsed = TRUE;
  }

  return TRUE;
}

GstBufferList*
mp4mx_create_buffer_list(GF_Filter* filter, GF_FilterPid* pid)
{
  GPAC_MemIoContext* ctx = (GPAC_MemIoContext*)gf_filter_get_rt_udta(filter);
  GstGpacTransform* gpac_tf = GST_GPAC_TF(GST_ELEMENT(ctx->sess->element));
  GPAC_MemOutPIDContext* pctx =
    (GPAC_MemOutPIDContext*)gf_filter_pid_get_udta(pid);
  Mp4mxCtx* mp4mx_ctx = (Mp4mxCtx*)pctx->private_ctx;

  // Check if the init and header buffers are present
  gboolean init_present = GET_TYPE(INIT)->is_complete && GET_TYPE(INIT)->buffer;
  gboolean header_present =
    GET_TYPE(HEADER)->is_complete && GET_TYPE(HEADER)->buffer;

  // Declare variables
  GstMemory* mdat_hdr = NULL;
  gboolean segment_boundary = FALSE;

  // Create a new buffer list
  GstBufferList* buffer_list = gst_buffer_list_new();
  GST_DEBUG_OBJECT(ctx->sess->element, "Fragment completed");

  //
  // Split the DATA buffer into samples, if we have the sample information
  //

  // Copy the data as is if we don't have sample information
  gboolean has_sample_info = mp4mx_ctx->next_samples->len > 0;
  if (!has_sample_info) {
    GST_DEBUG_OBJECT(
      ctx->sess->element,
      "No sample information found, appending data buffer as is");

    // Set the flags
    GST_BUFFER_FLAG_SET(GET_TYPE(DATA)->buffer, GST_BUFFER_FLAG_MARKER);
    GST_BUFFER_FLAG_SET(GET_TYPE(DATA)->buffer, GST_BUFFER_FLAG_DELTA_UNIT);

    // size of the data buffer
    guint64 size = gst_buffer_get_size(GET_TYPE(DATA)->buffer);

    // We have to rely on mp4mx timing information

    // PTS
    guint64 pts = gf_timestamp_rescale(
      GST_BUFFER_PTS(GET_TYPE(DATA)->buffer), mp4mx_ctx->mp4mx_ts, GST_SECOND);
    pts += ctx->global_offset;
    GST_BUFFER_PTS(GET_TYPE(DATA)->buffer) = pts;

    // DTS
    guint64 dts = gf_timestamp_rescale(
      GST_BUFFER_DTS(GET_TYPE(DATA)->buffer), mp4mx_ctx->mp4mx_ts, GST_SECOND);
    dts += ctx->global_offset;
    GST_BUFFER_DTS(GET_TYPE(DATA)->buffer) = dts;

    // Duration
    // For duration, we have to use mvhd since we don't parse the moov for
    // sample informations
    GST_BUFFER_DURATION(GET_TYPE(DATA)->buffer) = mp4mx_ctx->duration;

    gst_buffer_list_add(buffer_list, GET_TYPE(DATA)->buffer);
    goto headers;
  }

  // Move the mdat header out of the data buffer
  mdat_hdr =
    gst_memory_share(gst_buffer_peek_memory(GET_TYPE(DATA)->buffer, 0), 0, 8);
  gst_buffer_resize(GET_TYPE(DATA)->buffer, 8, -1);

  // Go through all samples
  gssize data_offset = 0;
  for (guint s = 0; s < mp4mx_ctx->next_samples->len; s++) {
    SampleInfo* sample = &g_array_index(mp4mx_ctx->next_samples, SampleInfo, s);

    // Create a new buffer
    GstBuffer* sample_buffer = gst_buffer_new();

    // Slice the buffer
    gssize avail_sample_size = sample->size;
    gssize offset = 0;
    guint n_blocks = gst_buffer_n_memory(GET_TYPE(DATA)->buffer);
    for (guint b = 0; b < n_blocks && avail_sample_size; b++) {
      GstMemory* mem = gst_buffer_peek_memory(GET_TYPE(DATA)->buffer, b);
      gssize size = (gssize)gst_memory_get_sizes(mem, NULL, NULL);

      // Skip until we reach the data offset
      if (offset + size <= data_offset)
        goto skip;

      // Check the offset within the block
      gssize block_offset = 0;
      if (offset < data_offset) {
        block_offset = data_offset - offset;
        offset += block_offset;
        size -= block_offset;
      }

      // Check if the block is enough
      if (size <= avail_sample_size) {
        gst_buffer_append_memory(sample_buffer,
                                 gst_memory_share(mem, block_offset, size));
        avail_sample_size -= size;
      } else {
        gst_buffer_append_memory(
          sample_buffer,
          gst_memory_share(mem, block_offset, avail_sample_size));
        avail_sample_size = 0;
      }

    skip:
      // Update the local offset
      offset += size;
    }

    // Set the marker flag if it's the last sample
    if (s == mp4mx_ctx->next_samples->len - 1)
      GST_BUFFER_FLAG_SET(sample_buffer, GST_BUFFER_FLAG_MARKER);

    // Set the delta unit flag. These buffers are always delta because they
    // follow a moof
    GST_BUFFER_FLAG_SET(sample_buffer, GST_BUFFER_FLAG_DELTA_UNIT);
    if (sample->is_sync)
      segment_boundary = TRUE;

    // Set the PTS, DTS, and duration
    GST_BUFFER_PTS(sample_buffer) = sample->pts;
    GST_BUFFER_DTS(sample_buffer) = sample->dts;
    GST_BUFFER_DURATION(sample_buffer) = sample->duration;

    // Append the sample buffer
    gst_buffer_list_add(buffer_list, sample_buffer);

    GST_TRACE_OBJECT(
      ctx->sess->element,
      "Added sample %d to the buffer list: size: %" G_GSSIZE_FORMAT ", "
      "duration: %" GST_TIME_FORMAT ", "
      "DTS: %" GST_TIME_FORMAT ", "
      "PTS: %" GST_TIME_FORMAT,
      s,
      sample->size,
      GST_TIME_ARGS(sample->duration),
      GST_TIME_ARGS(sample->dts),
      GST_TIME_ARGS(sample->pts));

    // Update the data offset
    data_offset += sample->size;
  }

  // Unref the data buffer
  gst_buffer_unref(GET_TYPE(DATA)->buffer);

headers:
  // Add the init buffer if it's present
  if (init_present) {
    GST_DEBUG_OBJECT(ctx->sess->element, "Adding init buffer to the beginning");

    // Set the flags
    GST_BUFFER_FLAG_SET(GET_TYPE(INIT)->buffer, GST_BUFFER_FLAG_HEADER);
    if (mp4mx_ctx->segment_count == 0) {
      GST_BUFFER_FLAG_SET(GET_TYPE(INIT)->buffer, GST_BUFFER_FLAG_DISCONT);
      ctx->is_continuous = TRUE;
    }

    // Set the timing information
    guint64 pts = G_MAXUINT64;
    guint64 dts = G_MAXUINT64;
    if (has_sample_info) {
      // Set PTS and DTS to the minimum of the samples
      for (guint s = 0; s < mp4mx_ctx->next_samples->len; s++) {
        SampleInfo* sample =
          &g_array_index(mp4mx_ctx->next_samples, SampleInfo, s);

        dts = MIN(dts, sample->dts);
        if (sample->pts < pts)
          pts = sample->pts;
        else {
          // PTS can only decrease if there are B-frames, so we break
          break;
        }
      }
    } else {
      // Set the PTS and DTS to the minimum of the data
      pts = GST_BUFFER_PTS(GET_TYPE(DATA)->buffer);
      dts = GST_BUFFER_DTS(GET_TYPE(DATA)->buffer);
    }

    GST_BUFFER_PTS(GET_TYPE(INIT)->buffer) = pts;
    GST_BUFFER_DTS(GET_TYPE(INIT)->buffer) = dts;
    GST_BUFFER_DURATION(GET_TYPE(INIT)->buffer) = GST_CLOCK_TIME_NONE;

    gst_buffer_list_insert(buffer_list, 0, GET_TYPE(INIT)->buffer);
  }

  // Add the header buffer if it's present
  if (header_present) {
    GST_DEBUG_OBJECT(ctx->sess->element, "Adding header buffer after init");

    // Set the flags
    GST_BUFFER_FLAG_SET(GET_TYPE(HEADER)->buffer, GST_BUFFER_FLAG_HEADER);

    // Set the delta unit based on the first sample
    if (!segment_boundary)
      GST_BUFFER_FLAG_SET(GET_TYPE(HEADER)->buffer, GST_BUFFER_FLAG_DELTA_UNIT);

    // Set the timing information
    // Set the timing information
    guint64 pts = G_MAXUINT64;
    guint64 dts = G_MAXUINT64;
    guint64 duration = 0;
    if (has_sample_info) {
      // Set PTS and DTS to the minimum of the samples
      for (guint s = 0; s < mp4mx_ctx->next_samples->len; s++) {
        SampleInfo* sample =
          &g_array_index(mp4mx_ctx->next_samples, SampleInfo, s);

        pts = MIN(pts, sample->pts);
        dts = MIN(dts, sample->dts);
        duration += sample->duration;
      }
    } else {
      // Set the PTS and DTS to the minimum of the data
      pts = GST_BUFFER_PTS(GET_TYPE(DATA)->buffer);
      dts = GST_BUFFER_DTS(GET_TYPE(DATA)->buffer);
      duration = GST_BUFFER_DURATION(GET_TYPE(DATA)->buffer);
    }

    GST_BUFFER_PTS(GET_TYPE(HEADER)->buffer) = pts;
    GST_BUFFER_DTS(GET_TYPE(HEADER)->buffer) = dts;
    GST_BUFFER_DURATION(GET_TYPE(HEADER)->buffer) = duration;

    // Append the mdat header
    if (mdat_hdr)
      gst_buffer_append_memory(GET_TYPE(HEADER)->buffer, mdat_hdr);

    // Insert the header buffer
    gst_buffer_list_insert(
      buffer_list, init_present ? 1 : 0, GET_TYPE(HEADER)->buffer);
  }

  // Reset the buffer contents
  for (guint i = 0; i < LAST; i++) {
    GET_TYPE(i)->buffer = NULL;
    GET_TYPE(i)->is_complete = FALSE;
  }

  return buffer_list;
}

BufferType
mp4mx_get_buffer_type(GF_FilterPid* pid, guint32 box_type)
{
  GPAC_MemOutPIDContext* ctx =
    (GPAC_MemOutPIDContext*)gf_filter_pid_get_udta(pid);
  Mp4mxCtx* mp4mx_ctx = (Mp4mxCtx*)ctx->private_ctx;

  // Check if the box type is related to the current type
  for (guint i = mp4mx_ctx->current_type; i < LAST; i++) {
    for (guint j = 0; box_mapping[i].boxes[j]; j++) {
      if (box_mapping[i].boxes[j] == box_type)
        return box_mapping[i].type;
    }
  }

  return LAST;
}

GF_Err
mp4mx_post_process(GF_Filter* filter, GF_FilterPid* pid, GF_FilterPacket* pck)
{
  GPAC_MemIoContext* ctx = (GPAC_MemIoContext*)gf_filter_get_rt_udta(filter);
  GPAC_MemOutPIDContext* pctx =
    (GPAC_MemOutPIDContext*)gf_filter_pid_get_udta(pid);
  Mp4mxCtx* mp4mx_ctx = (Mp4mxCtx*)pctx->private_ctx;
  if (!pck)
    return GF_OK;

  // Parse the boxes
  if (!mp4mx_parse_boxes(filter, pid, pck))
    return GF_OK;

  // Iterate over the boxes
  while (!g_queue_is_empty(mp4mx_ctx->box_queue)) {
    BoxInfo* box = g_queue_peek_head(mp4mx_ctx->box_queue);
    if (!mp4mx_is_box_complete(box))
      break;

    GST_DEBUG_OBJECT(
      ctx->sess->element, "Current type: %d", mp4mx_ctx->current_type);

    // Get the buffer type
    BufferType type = mp4mx_get_buffer_type(pid, box->box_type);
    if (type == LAST) {
      GST_WARNING_OBJECT(ctx->sess->element,
                         "Box %s is not related to any current or future "
                         "buffer types",
                         gf_4cc_to_str(box->box_type));
      goto skip;
    }

    GST_DEBUG_OBJECT(ctx->sess->element,
                     "Found box type %s for type %d",
                     gf_4cc_to_str(box->box_type),
                     type);

    // Mark all previous types as complete
    for (guint j = mp4mx_ctx->current_type; j < type; j++)
      GET_TYPE(j)->is_complete = TRUE;

    // Mark the current type as complete if it's DATA
    if (type == DATA)
      GET_TYPE(type)->is_complete = TRUE;

    // Set the current type
    mp4mx_ctx->current_type = type;

    // Create the master buffer
    GstBuffer** master_buffer = &GET_TYPE(type)->buffer;
    if (!(*master_buffer))
      *master_buffer = box->buffer;
    else
      *master_buffer = gst_buffer_append(*master_buffer, box->buffer);

    GST_DEBUG_OBJECT(ctx->sess->element,
                     "New buffer [type: %d, size: %" G_GUINT32_FORMAT
                     "]: %p (PTS: %" G_GUINT64_FORMAT
                     ", DTS: %" G_GUINT64_FORMAT
                     ", duration: %" G_GUINT64_FORMAT ")",
                     type,
                     box->box_size,
                     *master_buffer,
                     GST_BUFFER_PTS(*master_buffer),
                     GST_BUFFER_DTS(*master_buffer),
                     GST_BUFFER_DURATION(*master_buffer));

  skip:
    // Pop the box
    g_free(g_queue_pop_head(mp4mx_ctx->box_queue));
  }

  // Check if the fragment is completed
  if (!GET_TYPE(HEADER)->is_complete || !GET_TYPE(DATA)->is_complete)
    return GF_OK;

  // Create and enqueue the buffer list
  GstBufferList* buffer_list = mp4mx_create_buffer_list(filter, pid);
  g_queue_push_tail(mp4mx_ctx->output_queue, buffer_list);

  // Increment the segment count
  mp4mx_ctx->segment_count++;
  GST_DEBUG_OBJECT(ctx->sess->element,
                   "Enqueued fragment #%" G_GUINT32_FORMAT,
                   mp4mx_ctx->segment_count);

  // Reset the current type
  mp4mx_ctx->current_type = INIT;

  return GF_OK;
}

GPAC_FilterPPRet
mp4mx_consume(GF_Filter* filter, GF_FilterPid* pid, void** outptr)
{
  GPAC_MemOutPIDContext* ctx =
    (GPAC_MemOutPIDContext*)gf_filter_pid_get_udta(pid);
  Mp4mxCtx* mp4mx_ctx = (Mp4mxCtx*)ctx->private_ctx;

  // Check if the queue is empty
  if (g_queue_is_empty(mp4mx_ctx->output_queue))
    return GPAC_FILTER_PP_RET_EMPTY;

  // Assign the output
  if (outptr) {
    *outptr = g_queue_pop_head(mp4mx_ctx->output_queue);
    return GPAC_FILTER_PP_RET_BUFFER_LIST;
  }
  return GPAC_FILTER_PP_RET_NULL;
}
