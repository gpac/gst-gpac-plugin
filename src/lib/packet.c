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

#include "lib/packet.h"
#include "conversion/packet/registry.h"
#include "utils.h"

static void
gpac_pck_destructor(GF_Filter* filter, GF_FilterPid* PID, GF_FilterPacket* pck)
{
  const GF_PropertyValue* prop =
    gf_filter_pck_get_property(pck, GF_PROP_PCK_UDTA);
  if (prop) {
    GstBuffer* buffer = prop->value.ptr;
    gst_buffer_unref(buffer);
  }
}

guint64
gpac_pck_get_stream_time(GstClockTime time,
                         GpacPadPrivate* priv,
                         gboolean is_dts)
{
  GstElement* element = gst_pad_get_parent_element(priv->self);
  if (!GST_CLOCK_TIME_IS_VALID(time))
    goto fail;

  guint64 unsigned_time;
  int ret = gst_segment_to_stream_time_full(
    priv->segment, GST_FORMAT_TIME, time, &unsigned_time);
  if (ret == 0)
    goto fail;

  // It's possible that DTS is negative, so we need to offset it with the
  // DTS value
  gboolean is_negative = ret < 0;
  if (is_dts && is_negative && !priv->dts_offset_set) {
    priv->dts_offset = (gint64)unsigned_time;
    priv->dts_offset_set = TRUE;
  }

  // Offset the time with the initial DTS offset
  if (is_dts) {
    if (is_negative) {
      unsigned_time = priv->dts_offset - unsigned_time;
    } else {
      unsigned_time = priv->dts_offset + unsigned_time;
    }
  } else if (is_negative) {
    GST_ELEMENT_WARNING(element,
                        STREAM,
                        FAILED,
                        ("PTS %" GST_TIME_FORMAT
                         " is not valid, likely not related to current segment",
                         GST_TIME_ARGS(time)),
                        (NULL));
  }

  return unsigned_time;

fail:
  GST_ELEMENT_ERROR(element,
                    STREAM,
                    FAILED,
                    ("Failed to convert time %" GST_TIME_FORMAT
                     " to stream time",
                     GST_TIME_ARGS(time)),
                    (NULL));
  return 0;
}

void
gpac_configure_video(GstBuffer* buffer,
                     GpacPadPrivate* priv,
                     GF_FilterPacket* packet)
{
  // Decide on sample dependency flags
  gboolean is_delta =
    GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  gboolean is_droppable =
    GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DROPPABLE);
  gboolean is_discontinuity =
    GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DISCONT);

  // Set the SAP type for video streams
  gf_filter_pck_set_sap(packet,
                        is_delta ? GF_FILTER_SAP_NONE : GF_FILTER_SAP_1);

  // Note that acceptance tests doesn't like SAP, is_leading, sample_depends_on
  u32 flags = 0;

  // is_leading
  if (is_delta)
    flags |= (priv->last_frame_was_keyframe ? 3 : 2) << 6;

  // sample_depends_on
  if (is_discontinuity)
    flags |= 2 << 4;
  else
    flags |= ((is_delta ? 1 : 2) << 4);

  flags |= ((is_droppable ? 2 : 1) << 2); // sample_is_depended_on
  flags |= (2 << 0);                      // sample_has_redundancy

  gf_filter_pck_set_dependency_flags(packet, flags);
  priv->last_frame_was_keyframe = !is_delta;
}

void
gpac_pck_prop_configure(GPAC_PCK_PROP_IMPL_ARGS)
{
  // Check arguments
  g_return_if_fail(buffer != NULL);
  g_return_if_fail(priv != NULL);
  g_return_if_fail(pck != NULL);

  // Go through the property registry
  for (u32 i = 0; i < gpac_pck_get_num_supported_props(); i++) {
    prop_registry_entry* entry = &prop_registry[i];

    // Run the handler for the property
    entry->handler(buffer, priv, pck);
  }
}

GF_FilterPacket*
gpac_pck_new_from_buffer(GstBuffer* buffer,
                         GpacPadPrivate* priv,
                         GF_FilterPid* pid)
{
  const GF_PropertyValue* p;
  GstElement* element = gst_pad_get_parent_element(priv->self);

  // Map the buffer
  g_auto(GstBufferMapInfo) map = GST_MAP_INFO_INIT;
  if (G_UNLIKELY(!gst_buffer_map(buffer, &map, GST_MAP_READ))) {
    GST_ELEMENT_ERROR(
      element, STREAM, FAILED, (NULL), ("Failed to map buffer"));
    return NULL;
  }

  // Create a new shared packet
  GF_FilterPacket* packet =
    gf_filter_pck_new_shared(pid, map.data, map.size, gpac_pck_destructor);

  // Ref the buffer so that we can free it later
  GstBuffer* ref = gst_buffer_ref(buffer);
  GF_Err err =
    gf_filter_pck_set_property(packet, GF_PROP_PCK_UDTA, &PROP_POINTER(ref));
  if (G_UNLIKELY(err != GF_OK)) {
    GST_ELEMENT_ERROR(element,
                      STREAM,
                      FAILED,
                      (NULL),
                      ("Failed to save the buffer ref to the packet"));
    gst_buffer_unref(ref);
    gf_filter_pck_unref(packet);
    return NULL;
  }

  // Get the fps from the PID
  GF_Fraction fps = { 30, 1 };
  p = gf_filter_pid_get_property(pid, GF_PROP_PID_FPS);
  if (p)
    fps = p->value.frac;

  // Get the timescale from the PID
  guint64 timescale = GST_SECOND;
  p = gf_filter_pid_get_property(pid, GF_PROP_PID_TIMESCALE);
  if (p)
    timescale = p->value.uint;

  // Set the DTS to DTS or PTS, whichever is valid
  if (GST_BUFFER_DTS_IS_VALID(buffer) || GST_BUFFER_PTS_IS_VALID(buffer)) {
    guint64 dts =
      gpac_pck_get_stream_time(GST_BUFFER_DTS_OR_PTS(buffer), priv, TRUE);
    dts = gpac_time_rescale_with_fps(dts, fps, timescale);
    gf_filter_pck_set_dts(packet, dts);
  }

  // Set the CTS to PTS if it's valid
  if (GST_BUFFER_PTS_IS_VALID(buffer)) {
    guint64 cts = gpac_pck_get_stream_time(GST_BUFFER_PTS(buffer), priv, FALSE);
    cts = gpac_time_rescale_with_fps(cts, fps, timescale);
    gf_filter_pck_set_cts(packet, cts);
  }

  // Set the duration
  if (GST_BUFFER_DURATION_IS_VALID(buffer)) {
    guint64 duration = GST_BUFFER_DURATION(buffer);
    duration = gpac_time_rescale_with_fps(duration, fps, timescale);
    gf_filter_pck_set_duration(packet, duration);
  }

  // Set packet framing
  gf_filter_pck_set_framing(packet, GF_TRUE, GF_TRUE);

  // Set the default SAP type
  gf_filter_pck_set_sap(packet, GF_FILTER_SAP_1);

  // Check if this is a video
  const GF_PropertyValue* p_typ =
    gf_filter_pid_get_property(pid, GF_PROP_PID_STREAM_TYPE);
  gboolean is_video = p_typ && p_typ->value.uint == GF_STREAM_VISUAL;

  // For video streams, we need further configuration
  if (is_video) {
    gpac_configure_video(buffer, priv, packet);
  }

  // Configure the packet properties
  gpac_pck_prop_configure(buffer, priv, packet);

  return packet;
}
