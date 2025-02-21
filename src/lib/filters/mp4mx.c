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

#include "elements/gstgpactf.h"
#include "lib/filters/filters.h"
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
} BoxInfo;

typedef struct
{
  // Output queue for complete GOPs
  GQueue* output_queue;

  // State
  BufferType current_type;
  guint32 segment_count;

  // Box parser state
  GQueue* box_queue;

  // Buffer contents for the init, header, and data
  BufferContents* contents[3];

  // Input context
  guint32 timescale;
} Mp4mxCtx;

void
mp4mx_ctx_init(void** process_ctx)
{
  *process_ctx = g_new0(Mp4mxCtx, 1);
  Mp4mxCtx* ctx = (Mp4mxCtx*)*process_ctx;

  GST_DEBUG_CATEGORY_INIT(
    gpac_mp4mx, "gpacmp4mx", 0, "GPAC mp4mx post-processor");

  // Initialize the context
  ctx->output_queue = g_queue_new();
  ctx->current_type = INIT;
  ctx->segment_count = 0;
  ctx->box_queue = g_queue_new();

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

  // Free the context
  g_free(ctx);
}

GF_Err
mp4mx_configure_pid(GF_Filter* filter, GF_FilterPid* pid)
{
  GPAC_MemIoContext* ctx = (GPAC_MemIoContext*)gf_filter_get_rt_udta(filter);
  Mp4mxCtx* mp4mx_ctx = (Mp4mxCtx*)ctx->process_ctx;

  // Get the timescale from the PID
  mp4mx_ctx->timescale = GST_SECOND;
  const GF_PropertyValue* p =
    gf_filter_pid_get_property(pid, GF_PROP_PID_TIMESCALE);
  if (p)
    mp4mx_ctx->timescale = p->value.uint;

  return GF_OK;
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

gboolean
mp4mx_parse_boxes(GF_Filter* filter, GF_FilterPacket* pck)
{
  GPAC_MemIoContext* ctx = (GPAC_MemIoContext*)gf_filter_get_rt_udta(filter);
  Mp4mxCtx* mp4mx_ctx = (Mp4mxCtx*)ctx->process_ctx;

  // Declare variables
  gboolean need_more_data = FALSE;

  // Get the data
  u32 size;
  const u8* data = gf_filter_pck_get_data(pck, &size);
  GST_DEBUG("Processing data of size %" G_GUINT32_FORMAT, size);

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
        GST_DEBUG("Incomplete box %s, appending %" G_GUINT32_FORMAT " bytes",
                  gf_4cc_to_str(box->box_type),
                  leftover);
      GstMemory* mem = mp4mx_create_memory(data + offset, leftover, pck);

      // Append the memory to the buffer
      gst_buffer_append_memory(box->buffer, mem);

      // Update the offset
      offset += leftover;
      GST_DEBUG("Wrote %" G_GSIZE_FORMAT " bytes of %" G_GUINT32_FORMAT
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
    GST_DEBUG("Saw box %s with size %" G_GUINT32_FORMAT,
              gf_4cc_to_str(box->box_type),
              box->box_size);

    // Create a new buffer
    box->buffer = gst_buffer_new();

    // Calculate the PTS
    if (gf_filter_pck_get_cts(pck) != GF_FILTER_NO_TS) {
      guint64 pts = gf_timestamp_rescale(
        gf_filter_pck_get_cts(pck), mp4mx_ctx->timescale, GST_SECOND);
      pts += ctx->global_offset;
      GST_BUFFER_PTS(box->buffer) = pts;
    }

    // Calculate the DTS
    if (gf_filter_pck_get_dts(pck) != GF_FILTER_NO_TS) {
      guint64 dts = gf_timestamp_rescale(
        gf_filter_pck_get_dts(pck), mp4mx_ctx->timescale, GST_SECOND);
      dts += ctx->global_offset;
      GST_BUFFER_DTS(box->buffer) = dts;
    }

    // Calculate the duration
    guint64 duration = gf_timestamp_rescale(
      gf_filter_pck_get_duration(pck), mp4mx_ctx->timescale, GST_SECOND);
    GST_BUFFER_DURATION(box->buffer) = duration;
  }

  // Check if process can continue
  BoxInfo* box = g_queue_peek_head(mp4mx_ctx->box_queue);
  return mp4mx_is_box_complete(box);
}

GstBufferList*
mp4mx_create_buffer_list(GF_Filter* filter)
{
  GPAC_MemIoContext* ctx = (GPAC_MemIoContext*)gf_filter_get_rt_udta(filter);
  GstGpacTransform* gpac_tf = GST_GPAC_TF(GST_ELEMENT(ctx->sess->element));
  Mp4mxCtx* mp4mx_ctx = (Mp4mxCtx*)ctx->process_ctx;

  GstBufferList* buffer_list = gst_buffer_list_new();
  GST_DEBUG("GOP complete");

  // Set the flags
  for (guint type = 0; type < LAST; type++) {
    switch (type) {
      case INIT:
        if (!GET_TYPE(INIT)->is_complete)
          break;
        GST_BUFFER_FLAG_SET(GET_TYPE(type)->buffer, GST_BUFFER_FLAG_HEADER);

        if (mp4mx_ctx->segment_count == 0) {
          GST_BUFFER_FLAG_SET(GET_TYPE(type)->buffer, GST_BUFFER_FLAG_DISCONT);
          ctx->is_continuous = TRUE;
        }
        break;
      case HEADER:
        GST_BUFFER_FLAG_SET(GET_TYPE(type)->buffer, GST_BUFFER_FLAG_HEADER);

        // Prefer global IDR period if set
        guint64 idr_period = gpac_tf->global_idr_period != 0
                               ? gpac_tf->global_idr_period
                               : gpac_tf->gpac_idr_period;

        // Check if mdat starts with a sync sample
        if (idr_period && GST_BUFFER_PTS(GET_TYPE(type)->buffer) % idr_period) {
          GST_BUFFER_FLAG_SET(GET_TYPE(type)->buffer,
                              GST_BUFFER_FLAG_DELTA_UNIT);
        }
        break;
      case DATA:
        GST_BUFFER_FLAG_SET(GET_TYPE(type)->buffer, GST_BUFFER_FLAG_MARKER);
        GST_BUFFER_FLAG_SET(GET_TYPE(type)->buffer, GST_BUFFER_FLAG_DELTA_UNIT);
        break;

      default:
        break;
    }
  }

  // Init only if it's present
  if (GET_TYPE(INIT)->is_complete) {
    GST_DEBUG("Adding init buffer");
    gst_buffer_list_add(buffer_list, GET_TYPE(INIT)->buffer);

    // Init won't have timings set, set it using header
    GST_BUFFER_PTS(GET_TYPE(INIT)->buffer) =
      GST_BUFFER_PTS(GET_TYPE(HEADER)->buffer);
    GST_BUFFER_DTS(GET_TYPE(INIT)->buffer) =
      GST_BUFFER_DTS(GET_TYPE(HEADER)->buffer) -
      GST_BUFFER_DURATION(GET_TYPE(HEADER)->buffer);
    GST_BUFFER_DURATION(GET_TYPE(INIT)->buffer) = GST_CLOCK_TIME_NONE;
  }

  // Copy the mdat header
  GstMemory* mdat_hdr =
    gst_memory_share(gst_buffer_peek_memory(GET_TYPE(DATA)->buffer, 0), 0, 8);

  // Append the memory to the header
  gst_buffer_append_memory(GET_TYPE(HEADER)->buffer, mdat_hdr);

  // Resize the data buffer
  gst_buffer_resize(GET_TYPE(DATA)->buffer, 8, -1);

  // Add the header and data buffers
  gst_buffer_list_add(buffer_list, GET_TYPE(HEADER)->buffer);
  gst_buffer_list_add(buffer_list, GET_TYPE(DATA)->buffer);

  // Reset the buffer contents
  for (guint i = 0; i < LAST; i++) {
    GET_TYPE(i)->buffer = NULL;
    GET_TYPE(i)->is_complete = FALSE;
  }

  return buffer_list;
}

GF_Err
mp4mx_post_process(GF_Filter* filter, GF_FilterPacket* pck)
{
  GPAC_MemIoContext* ctx = (GPAC_MemIoContext*)gf_filter_get_rt_udta(filter);
  Mp4mxCtx* mp4mx_ctx = (Mp4mxCtx*)ctx->process_ctx;
  if (!pck)
    return GF_OK;

  // Parse the boxes
  if (!mp4mx_parse_boxes(filter, pck))
    return GF_OK;

  // Iterate over the boxes
  while (!g_queue_is_empty(mp4mx_ctx->box_queue)) {
    BoxInfo* box = g_queue_peek_head(mp4mx_ctx->box_queue);
    if (!mp4mx_is_box_complete(box))
      break;

    GST_DEBUG("Current type: %d", mp4mx_ctx->current_type);
    guint32 last_type;
    for (last_type = mp4mx_ctx->current_type; last_type < LAST; last_type++) {
      // Check if the current box is related to current type
      gboolean found = FALSE;
      for (guint i = 0; box_mapping[last_type].boxes[i]; i++) {
        if (box_mapping[last_type].boxes[i] == box->box_type) {
          found = TRUE;
          break;
        }
      }

      // Data only has one box type
      if (found && last_type == DATA)
        GET_TYPE(last_type)->is_complete = TRUE;

      if (found) {
        GST_DEBUG("Found box type %s for type %d",
                  gf_4cc_to_str(box->box_type),
                  last_type);
        break;
      }

      // If not found, it's likely the last type was completed
      GET_TYPE(last_type)->is_complete = TRUE;
    }

    // Check if the box is unknown
    if (last_type == LAST) {
      GST_ERROR("mp4mx post-processor encountered unknown box type %s",
                gf_4cc_to_str(box->box_type));
      g_assert_not_reached();
    }

    // Set the current type
    mp4mx_ctx->current_type = last_type;

    // Create the master buffer
    GstBuffer** master_buffer = &GET_TYPE(last_type)->buffer;
    if (!(*master_buffer))
      *master_buffer = box->buffer;
    else
      *master_buffer = gst_buffer_append(*master_buffer, box->buffer);

    GST_DEBUG("New buffer [type: %d, size: %" G_GUINT32_FORMAT
              "]: %p (PTS: %" G_GUINT64_FORMAT ", DTS: %" G_GUINT64_FORMAT
              ", duration: %" G_GUINT64_FORMAT ")",
              last_type,
              box->box_size,
              *master_buffer,
              GST_BUFFER_PTS(*master_buffer),
              GST_BUFFER_DTS(*master_buffer),
              GST_BUFFER_DURATION(*master_buffer));

    // Pop the box
    g_free(g_queue_pop_head(mp4mx_ctx->box_queue));
  }

  // Check if the GOP is complete
  if (!GET_TYPE(HEADER)->is_complete || !GET_TYPE(DATA)->is_complete)
    return GF_OK;

  // Create and enqueue the buffer list
  GstBufferList* buffer_list = mp4mx_create_buffer_list(filter);
  g_queue_push_tail(mp4mx_ctx->output_queue, buffer_list);

  // Increment the segment count
  mp4mx_ctx->segment_count++;
  GST_DEBUG("Enqueued GOP #%" G_GUINT32_FORMAT, mp4mx_ctx->segment_count);

  // Reset the current type
  mp4mx_ctx->current_type = HEADER;

  return GF_OK;
}

GPAC_FilterPPRet
mp4mx_consume(GF_Filter* filter, void** outptr)
{
  GPAC_MemIoContext* ctx = (GPAC_MemIoContext*)gf_filter_get_rt_udta(filter);
  Mp4mxCtx* mp4mx_ctx = (Mp4mxCtx*)ctx->process_ctx;
  *outptr = NULL;

  // Check if the queue is empty
  if (g_queue_is_empty(mp4mx_ctx->output_queue))
    return GPAC_FILTER_PP_RET_EMPTY;

  // Assign the output
  *outptr = g_queue_pop_head(mp4mx_ctx->output_queue);
  return GPAC_FILTER_PP_RET_BUFFER_LIST;
}
