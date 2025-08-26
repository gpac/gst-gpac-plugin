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
#include "elements/gstgpacsink.h"

GST_DEBUG_CATEGORY_STATIC(gst_gpac_tf_debug);
#define GST_CAT_DEFAULT gst_gpac_tf_debug

// #MARK: Pad Class
G_DEFINE_TYPE(GstGpacTransformPad, gst_gpac_tf_pad, GST_TYPE_AGGREGATOR_PAD);

enum
{
  GPAC_PROP_PAD_0,
  GPAC_PROP_PAD_PID,
};

// #MARK: [P] Properties
static void
gst_gpac_tf_pad_set_property(GObject* object,
                             guint prop_id,
                             const GValue* value,
                             GParamSpec* pspec)
{
  GstGpacTransformPad* pad = GST_GPAC_TF_PAD(object);
  g_return_if_fail(GST_IS_GPAC_TF_PAD(object));

  switch (prop_id) {
    case GPAC_PROP_PAD_PID:
      pad->pid = g_value_get_pointer(value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
}

static void
gst_gpac_tf_pad_get_property(GObject* object,
                             guint prop_id,
                             GValue* value,
                             GParamSpec* pspec)
{
  GstGpacTransformPad* pad = GST_GPAC_TF_PAD(object);
  g_return_if_fail(GST_IS_GPAC_TF_PAD(object));

  switch (prop_id) {
    case GPAC_PROP_PAD_PID:
      g_value_set_pointer(value, pad->pid);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
}

// #MARK: [P] Initialization
static void
gst_gpac_tf_pad_init(GstGpacTransformPad* pad)
{
  GpacPadPrivate* priv = g_new0(GpacPadPrivate, 1);
  priv->self = GST_PAD(pad);
  priv->idr_period = GST_CLOCK_TIME_NONE;
  priv->idr_last = GST_CLOCK_TIME_NONE;
  priv->idr_next = GST_CLOCK_TIME_NONE;
  gst_pad_set_element_private(GST_PAD(pad), priv);

  pad->pid = NULL;
};

static void
gst_gpac_tf_pad_finalize(GObject* object)
{
  GstGpacTransformPad* pad = GST_GPAC_TF_PAD(object);
  GpacPadPrivate* priv = gst_pad_get_element_private(GST_PAD(pad));

  if (priv) {
    if (priv->caps)
      gst_caps_unref(priv->caps);
    if (priv->segment)
      gst_segment_free(priv->segment);
    if (priv->tags)
      gst_tag_list_unref(priv->tags);
    g_free(priv);
    gst_pad_set_element_private(GST_PAD(pad), NULL);
  }

  G_OBJECT_CLASS(gst_gpac_tf_pad_parent_class)->finalize(object);
}

static void
gst_gpac_tf_pad_class_init(GstGpacTransformPadClass* klass)
{
  GObjectClass* gobject_class = (GObjectClass*)klass;

  gobject_class->finalize = GST_DEBUG_FUNCPTR(gst_gpac_tf_pad_finalize);
  gobject_class->set_property = GST_DEBUG_FUNCPTR(gst_gpac_tf_pad_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR(gst_gpac_tf_pad_get_property);

  g_object_class_install_property(
    gobject_class,
    GPAC_PROP_PAD_PID,
    g_param_spec_pointer("pid",
                         "GPAC's Filter PID",
                         "The PID of the filter in the GPAC session",
                         G_PARAM_READWRITE));
};

// #MARK: Element Class
#define gst_gpac_tf_parent_class parent_class
G_DEFINE_TYPE(GstGpacTransform, gst_gpac_tf, GST_TYPE_AGGREGATOR);

// #MARK: Properties
static void
gst_gpac_tf_set_property(GObject* object,
                         guint prop_id,
                         const GValue* value,
                         GParamSpec* pspec)
{
  GstGpacTransform* gpac_tf = GST_GPAC_TF(object);
  g_return_if_fail(GST_IS_GPAC_TF(object));
  if (gpac_set_property(GPAC_PROP_CTX(GPAC_CTX), prop_id, value, pspec))
    return;

  // Handle the element properties
  if (IS_ELEMENT_PROPERTY(prop_id)) {
    switch (prop_id) {
      case GPAC_PROP_SEGDUR:
        gpac_tf->global_idr_period =
          ((guint64)g_value_get_float(value)) * GST_SECOND;
        GST_DEBUG_OBJECT(object,
                         "Set global IDR period to %" GST_TIME_FORMAT,
                         GST_TIME_ARGS(gpac_tf->global_idr_period));
        break;

      default:
        break;
    }
    return;
  }

  G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
}

static void
gst_gpac_tf_get_property(GObject* object,
                         guint prop_id,
                         GValue* value,
                         GParamSpec* pspec)
{
  GstGpacTransform* gpac_tf = GST_GPAC_TF(object);
  g_return_if_fail(GST_IS_GPAC_TF(object));
  if (gpac_get_property(GPAC_PROP_CTX(GPAC_CTX), prop_id, value, pspec))
    return;

  // Handle the element properties
  if (IS_ELEMENT_PROPERTY(prop_id)) {
    switch (prop_id) {
      case GPAC_PROP_SEGDUR:
        g_value_set_float(value,
                          ((float)gpac_tf->global_idr_period) / GST_SECOND);
        break;

      default:
        break;
    }
    return;
  }

  G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
}

// #MARK: Helper Functions
static gboolean
gpac_prepare_pids(GstElement* element)
{
  GstGpacTransform* gpac_tf = GST_GPAC_TF(element);
  GstIterator* pad_iter;
  GValue item = G_VALUE_INIT;
  gboolean done = FALSE;
  gboolean ret = FALSE;

  // Track configuration status
  GHashTable* pids_before_reset =
    g_hash_table_new(g_direct_hash, g_direct_equal);
  GHashTable* pids_seen = g_hash_table_new(g_direct_hash, g_direct_equal);

  // Iterate over the pads
  pad_iter = gst_element_iterate_sink_pads(element);
  while (!done) {
    switch (gst_iterator_next(pad_iter, &item)) {
      case GST_ITERATOR_OK: {
        GstPad* pad = g_value_get_object(&item);
        GstAggregatorPad* agg_pad = GST_AGGREGATOR_PAD(pad);
        GpacPadPrivate* priv = gst_pad_get_element_private(pad);

        // Get the PID
        GF_FilterPid* pid = NULL;
        g_object_get(agg_pad, "pid", &pid, NULL);

        // Create the PID if necessary
        if (pid == NULL) {
          pid = gpac_pid_new(GPAC_SESS_CTX(GPAC_CTX));
          if (G_UNLIKELY(pid == NULL)) {
            GST_ELEMENT_ERROR(
              element, STREAM, FAILED, (NULL), ("Failed to create PID"));
            goto fail;
          }
          g_object_set(agg_pad, "pid", pid, NULL);

          // Share the pad private data
          gf_filter_pid_set_udta(pid, priv);
        }

        if (priv->flags) {
          if (G_UNLIKELY(!gpac_pid_reconfigure(element, priv, pid))) {
            GST_ELEMENT_ERROR(
              element, STREAM, FAILED, (NULL), ("Failed to reconfigure PID"));
            goto fail;
          }
          priv->flags = 0;
        }

        // Track the PID
        g_hash_table_insert(pids_seen, pid, GINT_TO_POINTER(1));

        g_value_reset(&item);
        break;
      }
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync(pad_iter);

        // Add seen PIDs to the before reset list
        g_hash_table_foreach(
          pids_seen, (GHFunc)g_hash_table_insert, pids_before_reset);
        g_hash_table_remove_all(pids_seen);

        break;
      case GST_ITERATOR_ERROR:
        GST_ELEMENT_ERROR(
          element, STREAM, FAILED, (NULL), ("Failed to iterate over pads"));
        goto fail;
      case GST_ITERATOR_DONE:
        done = TRUE;
        ret = TRUE;
        break;
    }
  }

  // Check for PIDs that are no longer needed
  GHashTableIter iter;
  gpointer key, value;
  g_hash_table_iter_init(&iter, pids_before_reset);
  while (g_hash_table_iter_next(&iter, &key, &value)) {
    if (!g_hash_table_contains(pids_seen, key)) {
      GF_FilterPid* pid = key;
      gpac_pid_del(pid);
    }
  }

fail:
  // Clean up
  g_value_unset(&item);
  gst_iterator_free(pad_iter);
  g_hash_table_destroy(pids_before_reset);
  g_hash_table_destroy(pids_seen);
  return ret;
}

// #MARK: Aggregator
GstFlowReturn
gst_gpac_tf_consume(GstAggregator* agg, Bool is_eos)
{
  GstFlowReturn flow_ret = GST_FLOW_OK;
  GstGpacTransform* gpac_tf = GST_GPAC_TF(agg);
  GstSegment* segment = &GST_AGGREGATOR_PAD(agg->srcpad)->segment;

  GST_DEBUG_OBJECT(agg, "Consuming output...");

  void* output;
  GPAC_FilterPPRet ret;
  while ((ret = gpac_memio_consume(GPAC_SESS_CTX(GPAC_CTX), &output))) {
    if (ret & GPAC_FILTER_PP_RET_ERROR) {
      // An error occurred, stop processing
      goto error;
    }

    if (ret == GPAC_FILTER_PP_RET_EMPTY) {
      // No data available
      GST_DEBUG_OBJECT(agg, "No more data available, exiting");
      return is_eos ? GST_FLOW_EOS : GST_FLOW_OK;
    }

    gboolean had_signal = (ret & GPAC_FILTER_PP_RET_SIGNAL) != 0;
    if (ret > GPAC_MAY_HAVE_BUFFER) {
      if (output) {
        // Notify the selected sample
        gst_aggregator_selected_samples(agg,
                                        GST_BUFFER_PTS(output),
                                        GST_BUFFER_DTS(output),
                                        GST_BUFFER_DURATION(output),
                                        NULL);
      }

      if (HAS_FLAG(ret, GPAC_FILTER_PP_RET_BUFFER)) {
        // Send the buffer
        GST_DEBUG_OBJECT(agg, "Sending buffer");
        flow_ret = gst_aggregator_finish_buffer(agg, GST_BUFFER(output));
        GST_DEBUG_OBJECT(agg, "Buffer sent!");
      } else if (HAS_FLAG(ret, GPAC_FILTER_PP_RET_BUFFER_LIST)) {
        // Send the buffer list
        GST_DEBUG_OBJECT(agg, "Sending buffer list");
        GstBufferList* buffer_list = GST_BUFFER_LIST(output);

        // Show in debug the buffer list pts and dts (running time) and flags
        for (guint i = 0; i < gst_buffer_list_length(buffer_list); i++) {
          GstBuffer* buffer = gst_buffer_list_get(buffer_list, i);
          // Calculate pts and dts in running time
          guint64 pts = gst_segment_to_running_time(
            segment, GST_FORMAT_TIME, GST_BUFFER_PTS(buffer));
          guint64 dts = gst_segment_to_running_time(
            segment, GST_FORMAT_TIME, GST_BUFFER_DTS(buffer));
          guint64 duration = gst_segment_to_running_time(
            segment, GST_FORMAT_TIME, GST_BUFFER_DURATION(buffer));
          GST_DEBUG_OBJECT(
            agg,
            "Buffer %d: PTS: %" GST_TIME_FORMAT ", DTS: %" GST_TIME_FORMAT
            ", Duration: %" GST_TIME_FORMAT ", IsHeader: %s",
            i,
            GST_TIME_ARGS(pts),
            GST_TIME_ARGS(dts),
            GST_TIME_ARGS(duration),
            GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_HEADER) ? "Yes"
                                                                   : "No");
        }

        flow_ret = gst_aggregator_finish_buffer_list(agg, buffer_list);
        GST_DEBUG_OBJECT(agg, "Buffer list sent!");
      } else if (HAS_FLAG(ret, GPAC_FILTER_PP_RET_NULL)) {
        // If we had signals, consume all of them first
        if (had_signal)
          continue;

        if (is_eos)
          return GST_FLOW_EOS;

        GST_DEBUG_OBJECT(agg,
                         "Sending sync buffer, possibly connected to fakesink");

        // We send only one buffer regardless of potential pending buffers
        GstBuffer* buffer = gpac_tf->sync_buffer;
        if (!buffer)
          buffer = gst_buffer_new();

        // Send the sync buffer
        flow_ret = gst_aggregator_finish_buffer(agg, buffer);

        // Buffer is transferred to the aggregator, so we set it to NULL
        if (gpac_tf->sync_buffer)
          gpac_tf->sync_buffer = NULL;

        return flow_ret;
      } else {
        GST_ELEMENT_WARNING(
          agg, STREAM, FAILED, (NULL), ("Unknown return value: %d", ret));
        g_warn_if_reached();
      }

      if (flow_ret != GST_FLOW_OK) {
        GST_ELEMENT_ERROR(agg,
                          STREAM,
                          FAILED,
                          (NULL),
                          ("Failed to finish buffer, ret: %d", flow_ret));
        return flow_ret;
      }
    }
  }

error:
  GST_ELEMENT_ERROR(agg, STREAM, FAILED, (NULL), ("Failed to consume output"));
  return GST_FLOW_ERROR;
}

static gboolean
gst_gpac_tf_sink_event(GstAggregator* agg,
                       GstAggregatorPad* pad,
                       GstEvent* event)
{
  GstGpacTransform* gpac_tf = GST_GPAC_TF(GST_ELEMENT(agg));
  GpacPadPrivate* priv = gst_pad_get_element_private(GST_PAD(pad));

  switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_CAPS: {
      if (priv->caps)
        gst_caps_unref(priv->caps);

      GstCaps* caps;
      gst_event_parse_caps(event, &caps);
      priv->caps = gst_caps_ref(caps);
      priv->flags |= GPAC_PAD_CAPS_SET;
      break;
    }

    case GST_EVENT_SEGMENT: {
      if (priv->segment)
        gst_segment_free(priv->segment);

      const GstSegment* segment;
      gst_event_parse_segment(event, &segment);
      priv->segment = gst_segment_copy(segment);
      priv->flags |= GPAC_PAD_SEGMENT_SET;
      priv->dts_offset_set = FALSE;

      gboolean is_video_pad = gst_pad_get_pad_template(GST_PAD(pad)) ==
                              gst_gpac_get_sink_template(GPAC_TEMPLATE_VIDEO);
      gboolean is_only_pad = g_list_length(GST_ELEMENT(agg)->sinkpads) == 1;

      // Update the segment and global offset only if video pad or the only pad
      if (is_video_pad || is_only_pad) {
        gst_aggregator_update_segment(agg, gst_segment_copy(segment));
        gpac_memio_set_global_offset(GPAC_SESS_CTX(GPAC_CTX), segment);
      }

      // Check if playback rate is equal to 1.0
      if (segment->rate != 1.0)
        GST_FIXME_OBJECT(agg,
                         "Handling of non-1.0 playback rate is not "
                         "implemented yet");

      break;
    }

    case GST_EVENT_TAG: {
      if (priv->tags)
        gst_tag_list_unref(priv->tags);

      GstTagList* tags;
      gst_event_parse_tag(event, &tags);
      priv->tags = gst_tag_list_ref(tags);
      priv->flags |= GPAC_PAD_TAGS_SET;
      break;
    }

    case GST_EVENT_EOS: {
      // Set this pad as EOS
      priv->eos = TRUE;

      // Are all pads EOS?
      gboolean all_eos = TRUE;
      GList* sinkpads = GST_ELEMENT(agg)->sinkpads;
      for (GList* l = sinkpads; l; l = l->next) {
        GstAggregatorPad* agg_pad = GST_AGGREGATOR_PAD(l->data);
        GpacPadPrivate* priv = gst_pad_get_element_private(GST_PAD(agg_pad));
        if (!priv->eos) {
          all_eos = FALSE;
          break;
        }
      }

      if (!all_eos) {
        GST_DEBUG_OBJECT(agg, "Not all pads are EOS, not sending EOS to GPAC");
        break;
      }

      // If all pads are EOS, send EOS to the source
      GST_DEBUG_OBJECT(agg, "All pads are EOS, sending EOS to GPAC");
      gpac_memio_set_eos(GPAC_SESS_CTX(GPAC_CTX), TRUE);
      gpac_session_run(GPAC_SESS_CTX(GPAC_CTX), TRUE);
      gst_gpac_tf_consume(agg, GST_EVENT_TYPE(event) == GST_EVENT_EOS);
      break;
    }

    case GST_EVENT_FLUSH_START:
      gpac_session_run(GPAC_SESS_CTX(GPAC_CTX), TRUE);
      gst_gpac_tf_consume(agg, GST_EVENT_TYPE(event) == GST_EVENT_EOS);
      break;

    default:
      break;
  }

  return GST_AGGREGATOR_CLASS(parent_class)->sink_event(agg, pad, event);
}

gboolean
gst_gpac_tf_negotiated_src_caps(GstAggregator* agg, GstCaps* caps)
{
  GstGpacTransform* gpac_tf = GST_GPAC_TF(GST_ELEMENT(agg));
  GstElement* element = GST_ELEMENT(agg);
  GObjectClass* klass = G_OBJECT_CLASS(G_TYPE_INSTANCE_GET_CLASS(
    G_OBJECT(element), GST_TYPE_GPAC_TF, GstGpacTransformClass));
  GstGpacParams* params = GST_GPAC_GET_PARAMS(klass);

  // Check if this is element is inside our sink bin
  GstObject* sink_bin = gst_element_get_parent(element);
  if (GST_IS_GPAC_SINK(sink_bin)) {
    // We might already have a destination set
    if ((params->is_single && params->info->destination) ||
        GPAC_PROP_CTX(GPAC_CTX)->destination) {
      return TRUE;
    }
  }

  return gpac_memio_set_gst_caps(GPAC_SESS_CTX(GPAC_CTX), caps);
}

void
gst_gpac_request_idr(GstAggregator* agg, GstPad* pad, GstBuffer* buffer)
{
  GstGpacTransform* gpac_tf = GST_GPAC_TF(GST_ELEMENT(agg));
  GpacPadPrivate* priv = gst_pad_get_element_private(pad);
  GstEvent* gst_event;

  // Skip if this is not a key frame
  if (GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT))
    return;

  // Skip if we don't have a valid PTS
  if (!GST_BUFFER_PTS_IS_VALID(buffer))
    return;

  // Decide on which IDR period to use
  guint64 idr_period = GST_CLOCK_TIME_NONE;
  if (priv->idr_period != GST_CLOCK_TIME_NONE) {
    idr_period = priv->idr_period;
    // Preserve the IDR period sent by gpac
    gpac_tf->gpac_idr_period = idr_period;
  }

  // Use the global IDR period if available
  if (gpac_tf->global_idr_period)
    idr_period = gpac_tf->global_idr_period;

  // Skip if we don't have an IDR period
  if (idr_period == GST_CLOCK_TIME_NONE)
    return;

  priv->idr_last = gst_segment_to_running_time(
    priv->segment, GST_FORMAT_TIME, GST_BUFFER_PTS(buffer));

  GST_DEBUG_OBJECT(agg,
                   "Key frame received at %" GST_TIME_FORMAT,
                   GST_TIME_ARGS(priv->idr_last));

  // If this is the first IDR, request it immediately
  if (priv->idr_next == GST_CLOCK_TIME_NONE) {
    priv->idr_next = priv->idr_last + idr_period;
    goto request;
  }

  // If this IDR arrived before the next scheduled IDR, ignore
  if (priv->idr_last < priv->idr_next) {
    guint64 diff = priv->idr_next - priv->idr_last;
    GST_DEBUG_OBJECT(agg,
                     "IDR arrived %" GST_TIME_FORMAT
                     " before the next IDR on pad %s",
                     GST_TIME_ARGS(diff),
                     GST_PAD_NAME(pad));
    return;
  }

  // Check if this IDR was on time
  guint64 diff = priv->idr_last - priv->idr_next;
  if (diff)
    GST_ELEMENT_WARNING(agg,
                        STREAM,
                        FAILED,
                        ("IDR was late by %" GST_TIME_FORMAT
                         " on pad %s, reconsider encoding options",
                         GST_TIME_ARGS(diff),
                         GST_PAD_NAME(pad)),
                        (NULL));

  // Schedule the next IDR at the desired time, regardless of whether current
  // one was late or not
  priv->idr_next += idr_period;

request:
  // Send the next IDR request
  gst_event =
    gst_video_event_new_upstream_force_key_unit(priv->idr_next, TRUE, 1);
  GST_DEBUG_OBJECT(
    agg, "Requesting IDR at %" GST_TIME_FORMAT, GST_TIME_ARGS(priv->idr_next));
  if (!gst_pad_push_event(pad, gst_event))
    GST_ELEMENT_WARNING(
      agg, STREAM, FAILED, (NULL), ("Failed to push the force key unit event"));
}

static GstFlowReturn
gst_gpac_tf_aggregate(GstAggregator* agg, gboolean timeout)
{
  GstGpacTransform* gpac_tf = GST_GPAC_TF(GST_ELEMENT(agg));
  GstIterator* pad_iter;
  GValue item = G_VALUE_INIT;
  gboolean done = FALSE;
  gboolean has_buffers = TRUE;

  // Check and create PIDs if necessary
  if (!gpac_prepare_pids(GST_ELEMENT(agg))) {
    GST_ELEMENT_ERROR(agg, STREAM, FAILED, (NULL), ("Failed to prepare PIDs"));
    return GST_FLOW_ERROR;
  }

  GST_DEBUG_OBJECT(agg, "Aggregating buffers");

  // Create the temporary queue
  GQueue* queue = g_queue_new();

  // Keep consuming buffers until all pads are drained
  while (has_buffers) {
    has_buffers = FALSE;
    done = FALSE;

    // Iterate over the pads
    pad_iter = gst_element_iterate_sink_pads(GST_ELEMENT(agg));
    while (!done) {
      switch (gst_iterator_next(pad_iter, &item)) {
        case GST_ITERATOR_OK: {
          GF_FilterPid* pid = NULL;
          GstPad* pad = g_value_get_object(&item);
          GpacPadPrivate* priv = gst_pad_get_element_private(pad);
          GstBuffer* buffer =
            gst_aggregator_pad_pop_buffer(GST_AGGREGATOR_PAD(pad));

          // Continue if no buffer is available
          if (!buffer) {
            GST_DEBUG_OBJECT(
              agg, "No buffer available on pad %s", GST_PAD_NAME(pad));
            goto next;
          }

          // We found at least one buffer, continue the outer loop
          has_buffers = TRUE;

          // Skip droppable/gap buffers
          if (GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_GAP)) {
            GST_DEBUG_OBJECT(
              agg, "Gap buffer received on pad %s", GST_PAD_NAME(pad));
            goto next;
          }

          // Send the key frame request
          // Only send IDR request for video pads
          if (gst_pad_get_pad_template(pad) ==
              gst_gpac_get_sink_template(GPAC_TEMPLATE_VIDEO)) {
            gst_gpac_request_idr(agg, pad, buffer);
          }

          // Get the PID
          g_object_get(GST_AGGREGATOR_PAD(pad), "pid", &pid, NULL);
          g_assert(pid);

          // Create the packet
          GF_FilterPacket* packet = gpac_pck_new_from_buffer(buffer, priv, pid);
          if (!packet) {
            GST_ELEMENT_ERROR(agg,
                              STREAM,
                              FAILED,
                              (NULL),
                              ("Failed to create packet from buffer"));
            goto next;
          }

          // Enqueue the packet
          g_queue_push_tail(queue, packet);

          // Select the highest PTS for sync buffer
          gboolean is_video_pad =
            gst_pad_get_pad_template(GST_PAD(pad)) ==
            gst_gpac_get_sink_template(GPAC_TEMPLATE_VIDEO);
          gboolean is_only_pad = g_list_length(GST_ELEMENT(agg)->sinkpads) == 1;
          if (is_video_pad || is_only_pad) {
            if (gpac_tf->sync_buffer) {
              guint64 current_pts = GST_BUFFER_PTS(buffer);
              guint64 sync_pts = GST_BUFFER_PTS(gpac_tf->sync_buffer);
              if (current_pts > sync_pts) {
                gst_buffer_replace(&gpac_tf->sync_buffer, buffer);
              }
            } else {
              // If no sync buffer exists, create one
              gpac_tf->sync_buffer = gst_buffer_ref(buffer);
            }
          }

        next:
          if (buffer)
            gst_buffer_unref(buffer);
          g_value_reset(&item);
          break;
        }
        case GST_ITERATOR_RESYNC:
          gst_iterator_resync(pad_iter);
          GST_ELEMENT_WARNING(agg,
                              STREAM,
                              FAILED,
                              (NULL),
                              ("Data structure changed during pad iteration, "
                               "discarding all packets"));
          g_queue_clear_full(queue, (GDestroyNotify)gf_filter_pck_unref);
          break;
        case GST_ITERATOR_ERROR:
        case GST_ITERATOR_DONE:
          done = TRUE;
          break;
      }
    }
  }

  // Clean up
  g_value_unset(&item);
  gst_iterator_free(pad_iter);

  // Check if we have any packets to send
  if (g_queue_is_empty(queue)) {
    GST_DEBUG_OBJECT(agg, "No packets to send, returning EOS");
    g_queue_free(queue);
    return GST_FLOW_EOS;
  }

  // Merge the queues
  while (!g_queue_is_empty(queue)) {
    gpointer pck = g_queue_pop_head(queue);
    g_queue_push_tail(gpac_tf->queue, pck);
  }
  g_queue_free(queue);

  // Run the filter session
  if (gpac_session_run(GPAC_SESS_CTX(GPAC_CTX), FALSE) != GF_OK) {
    GST_ELEMENT_ERROR(
      agg, STREAM, FAILED, (NULL), ("Failed to run the GPAC session"));
    return GST_FLOW_ERROR;
  }

  // Consume the output
  return gst_gpac_tf_consume(agg, FALSE);
}

// #MARK: Pad Management
static GstAggregatorPad*
gst_gpac_tf_create_new_pad(GstAggregator* element,
                           GstPadTemplate* templ,
                           const gchar* pad_name,
                           const GstCaps* caps)
{
  GstElementClass* klass = GST_ELEMENT_GET_CLASS(element);
  GstGpacTransform* agg = GST_GPAC_TF(element);
  gchar* name;
  gint pad_id;

#define TEMPLATE_CHECK(prefix, count_field)                                 \
  if (templ == gst_element_class_get_pad_template(klass, prefix "_%u")) {   \
    if (pad_name != NULL && sscanf(pad_name, prefix "_%u", &pad_id) == 1) { \
      name = g_strdup(pad_name);                                            \
    } else {                                                                \
      name = g_strdup_printf(prefix "_%u", agg->count_field++);             \
    }                                                                       \
  } else

  // Check the pad template and decide the pad name
  TEMPLATE_CHECK("video", video_pad_count)
  TEMPLATE_CHECK("audio", audio_pad_count)
  TEMPLATE_CHECK("subtitle", subtitle_pad_count)
  TEMPLATE_CHECK("caption", caption_pad_count)
  {
    GST_ELEMENT_WARNING(
      agg, STREAM, FAILED, (NULL), ("This is not our template!"));
    return NULL;
  }

#undef TEMPLATE_CHECK

  GST_DEBUG_OBJECT(agg, "Creating new pad %s", name);

  // Create the pad
  GstGpacTransformPad* pad = g_object_new(GST_TYPE_GPAC_TF_PAD,
                                          "name",
                                          name,
                                          "direction",
                                          templ->direction,
                                          "template",
                                          templ,
                                          NULL);
  g_free(name);

  // Get the total number of pads
  guint pad_count = agg->audio_pad_count + agg->video_pad_count +
                    agg->subtitle_pad_count + agg->caption_pad_count;

  // Initialize the private data
  GpacPadPrivate* priv = gst_pad_get_element_private(GST_PAD(pad));
  priv->id = pad_count;
  if (caps) {
    priv->caps = gst_caps_copy(caps);
    priv->flags |= GPAC_PAD_CAPS_SET;
  }

  return GST_AGGREGATOR_PAD(pad);
}

// #MARK: Lifecycle
static void
gst_gpac_tf_reset(GstGpacTransform* tf)
{
  gboolean done = FALSE;
  GValue item = G_VALUE_INIT;
  GstIterator* pad_iter = gst_element_iterate_sink_pads(GST_ELEMENT(tf));
  while (!done) {
    switch (gst_iterator_next(pad_iter, &item)) {
      case GST_ITERATOR_OK: {
        GF_FilterPid* pid = NULL;
        GstPad* pad = g_value_get_object(&item);
        GpacPadPrivate* priv = gst_pad_get_element_private(pad);

        // Reset the PID
        g_object_set(GST_AGGREGATOR_PAD(pad), "pid", NULL, NULL);
        break;
      }
      case GST_ITERATOR_RESYNC:
      case GST_ITERATOR_ERROR:
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  g_value_unset(&item);
  gst_iterator_free(pad_iter);

  // Empty the queue
  if (tf->queue)
    g_queue_clear_full(tf->queue, (GDestroyNotify)gf_filter_pck_unref);
}

static gboolean
gst_gpac_tf_start(GstAggregator* aggregator)
{
  GstGpacTransform* gpac_tf = GST_GPAC_TF(aggregator);
  GstElement* element = GST_ELEMENT(aggregator);
  GObjectClass* klass = G_OBJECT_CLASS(G_TYPE_INSTANCE_GET_CLASS(
    G_OBJECT(element), GST_TYPE_GPAC_TF, GstGpacTransformClass));
  GstGpacParams* params = GST_GPAC_GET_PARAMS(klass);
  GstSegment segment;

  // Check if we have the graph property set
  if (!params->is_single && !GPAC_PROP_CTX(GPAC_CTX)->graph) {
    GST_ELEMENT_ERROR(
      element, STREAM, FAILED, (NULL), ("Graph property must be set"));
    return FALSE;
  }

  // Convert the properties to arguments
  if (!gpac_apply_properties(GPAC_PROP_CTX(GPAC_CTX))) {
    GST_ELEMENT_ERROR(
      element, LIBRARY, INIT, (NULL), ("Failed to apply properties"));
    return FALSE;
  }
  // Initialize the GPAC context
  if (!gpac_init(GPAC_CTX, element)) {
    GST_ELEMENT_ERROR(
      element, LIBRARY, INIT, (NULL), ("Failed to initialize GPAC context"));
    return FALSE;
  }

  // Create the session
  if (!gpac_session_init(GPAC_SESS_CTX(GPAC_CTX), element, params)) {
    GST_ELEMENT_ERROR(
      element, LIBRARY, INIT, (NULL), ("Failed to initialize GPAC session"));
    return FALSE;
  }

  // Create the memory input
  gpac_return_val_if_fail(
    gpac_memio_new(GPAC_SESS_CTX(GPAC_CTX), GPAC_MEMIO_DIR_IN), FALSE);
  gpac_memio_assign_queue(
    GPAC_SESS_CTX(GPAC_CTX), GPAC_MEMIO_DIR_IN, gpac_tf->queue);

  // Open the session
  gchar* graph = NULL;
  if (params->is_single) {
    if (params->info->default_options) {
      GList* props = GPAC_PROP_CTX(GPAC_CTX)->properties;
      GString* options = g_string_new(NULL);

      // Only override if not set already
      for (guint32 i = 0; params->info->default_options[i].name; i++) {
        gboolean found = FALSE;

        for (GList* l = props; l != NULL; l = l->next) {
          gchar* prop = (gchar*)l->data;

          g_autofree gchar* prefix =
            g_strdup_printf("--%s", params->info->default_options[i].name);
          if (g_str_has_prefix(prop, prefix)) {
            found = TRUE;
            break;
          }
        }

        if (!found) {
          const gchar* name = params->info->default_options[i].name;
          const gchar* value = params->info->default_options[i].value;
          g_string_append_printf(options, "%s=%s:", name, value);
        }
      }

      // Remove the trailing colon
      if (options->len > 0)
        g_string_truncate(options, options->len - 1);

      if (options->len > 0)
        graph =
          g_strdup_printf("%s:%s", params->info->filter_name, options->str);
      else
        graph = g_strdup(params->info->filter_name);
      g_string_free(options, TRUE);
    } else {
      graph = g_strdup(params->info->filter_name);
    }
  } else {
    graph = g_strdup(GPAC_PROP_CTX(GPAC_CTX)->graph);
  }

  // Set the destination override on session context
  GPAC_SESS_CTX(GPAC_CTX)->destination = GPAC_PROP_CTX(GPAC_CTX)->destination;

  gpac_return_val_if_fail(gpac_session_open(GPAC_SESS_CTX(GPAC_CTX), graph),
                          FALSE);
  g_free(graph);

  // Create the memory output
  gboolean is_inside_sink = GST_IS_GPAC_SINK(gst_element_get_parent(element));
  gboolean requires_memout =
    params->info && GPAC_SE_IS_REQUIRES_MEMOUT(params->info->flags);
  requires_memout = !is_inside_sink || (is_inside_sink && requires_memout) ||
                    GPAC_PROP_CTX(GPAC_CTX)->destination;

  if (requires_memout) {
    gpac_return_val_if_fail(
      gpac_memio_new(GPAC_SESS_CTX(GPAC_CTX), GPAC_MEMIO_DIR_OUT), FALSE);
  }

  // Check if the session has an output
  if (!gpac_session_has_output(GPAC_SESS_CTX(GPAC_CTX))) {
    GST_ELEMENT_ERROR(
      element, STREAM, FAILED, (NULL), ("Session has no output"));
    return FALSE;
  }

  // Initialize the PIDs for all pads
  if (!gpac_prepare_pids(element)) {
    GST_ELEMENT_ERROR(
      element, LIBRARY, FAILED, (NULL), ("Failed to prepare PIDs"));
    return FALSE;
  }
  GST_DEBUG_OBJECT(element, "GPAC session started");

  // Initialize the segment
  gst_segment_init(&segment, GST_FORMAT_TIME);
  gst_aggregator_update_segment(aggregator, &segment);
  return TRUE;
}

static gboolean
gst_gpac_tf_stop(GstAggregator* aggregator)
{
  GstElement* element = GST_ELEMENT(aggregator);
  GstGpacTransform* gpac_tf = GST_GPAC_TF(aggregator);
  GObjectClass* klass = G_OBJECT_CLASS(G_TYPE_INSTANCE_GET_CLASS(
    G_OBJECT(element), GST_TYPE_GPAC_TF, GstGpacTransformClass));
  GstGpacParams* params = GST_GPAC_GET_PARAMS(klass);

  // Reset the element
  gst_gpac_tf_reset(gpac_tf);

  // Close the session
  if (!gpac_session_close(GPAC_SESS_CTX(GPAC_CTX),
                          GPAC_PROP_CTX(GPAC_CTX)->print_stats)) {
    GST_ELEMENT_ERROR(
      element, LIBRARY, SHUTDOWN, (NULL), ("Failed to close GPAC session"));
    return FALSE;
  }

  // Destroy the GPAC context
  gpac_destroy(GPAC_CTX);
  GST_DEBUG_OBJECT(element, "GPAC session stopped");
  return TRUE;
}

static void
gst_gpac_tf_finalize(GObject* object)
{
  GstGpacTransform* gpac_tf = GST_GPAC_TF(object);
  GPAC_PropertyContext* ctx = GPAC_PROP_CTX(GPAC_CTX);

  // Free the properties
  g_list_free(ctx->properties);
  ctx->properties = NULL;

  if (ctx->props_as_argv) {
    for (u32 i = 0; ctx->props_as_argv[i]; i++)
      g_free(ctx->props_as_argv[i]);
    g_free((void*)ctx->props_as_argv);
  }

  // Free the queue
  if (gpac_tf->queue) {
    g_assert(g_queue_is_empty(gpac_tf->queue));
    g_queue_free(gpac_tf->queue);
    gpac_tf->queue = NULL;
  }

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

// #MARK: Initialization
static void
gst_gpac_tf_init(GstGpacTransform* tf)
{
  gst_gpac_tf_reset(tf);
  tf->queue = g_queue_new();
}

static void
gst_gpac_tf_class_init(GstGpacTransformClass* klass)
{
  // Initialize the class
  GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
  GstElementClass* gstelement_class = GST_ELEMENT_CLASS(klass);
  GstAggregatorClass* gstaggregator_class = GST_AGGREGATOR_CLASS(klass);
  parent_class = g_type_class_peek_parent(klass);

  // Install the pad templates
  gpac_install_sink_pad_templates(gstelement_class);

  // Set the pad management functions
  gstaggregator_class->create_new_pad =
    GST_DEBUG_FUNCPTR(gst_gpac_tf_create_new_pad);

  // Set the aggregator functions
  gstaggregator_class->sink_event = GST_DEBUG_FUNCPTR(gst_gpac_tf_sink_event);
  gstaggregator_class->aggregate = GST_DEBUG_FUNCPTR(gst_gpac_tf_aggregate);
  gstaggregator_class->negotiated_src_caps =
    GST_DEBUG_FUNCPTR(gst_gpac_tf_negotiated_src_caps);
  gstaggregator_class->start = GST_DEBUG_FUNCPTR(gst_gpac_tf_start);
  gstaggregator_class->stop = GST_DEBUG_FUNCPTR(gst_gpac_tf_stop);

  gst_type_mark_as_plugin_api(GST_TYPE_GPAC_TF_PAD, 0);
}

// #MARK: Registration
static void
gst_gpac_tf_subclass_init(GstGpacTransformClass* klass)
{
  GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
  GstElementClass* gstelement_class = GST_ELEMENT_CLASS(klass);
  GstGpacParams* params = GST_GPAC_GET_PARAMS(klass);

  // Set the finalizer
  gobject_class->finalize = GST_DEBUG_FUNCPTR(gst_gpac_tf_finalize);

  // Set the property handlers
  gobject_class->set_property = GST_DEBUG_FUNCPTR(gst_gpac_tf_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR(gst_gpac_tf_get_property);
  gpac_install_global_properties(gobject_class);
  gpac_install_local_properties(
    gobject_class, GPAC_PROP_PRINT_STATS, GPAC_PROP_0);

  // Add the subclass-specific properties and pad templates
  if (params->is_single) {
    if (params->is_inside_sink) {
      gst_element_class_add_static_pad_template(gstelement_class,
                                                &internal_pad_template);
    } else {
      gst_element_class_add_static_pad_template(gstelement_class,
                                                &params->info->src_template);
    }

    // Set property blacklist
    GList* blacklist = NULL;
    if (params->info->default_options) {
      filter_option* opt = params->info->default_options;
      for (u32 i = 0; opt[i].name; i++) {
        if (opt[i].forced)
          blacklist = g_list_append(blacklist, (gpointer)opt[i].name);
      }
    }

    // Install the filter properties
    gpac_install_filter_properties(
      gobject_class, blacklist, params->info->filter_name);
    g_list_free(blacklist);

    // Install the signals if not inside the sink bin
    // They would be installed by the sink bin itself
    if (!params->is_inside_sink && params->info->signal_presets) {
      gpac_install_signals_by_presets(gobject_class,
                                      params->info->signal_presets);
    }

    // Check if we have any filter options to expose
    for (u32 i = 0; i < G_N_ELEMENTS(filter_options); i++) {
      filter_option_overrides* opts = &filter_options[i];
      if (g_strcmp0(opts->filter_name, params->info->filter_name) == 0) {
        for (u32 j = 0; opts->options[j]; j++) {
          guint32 prop_id = opts->options[j];
          gpac_install_local_properties(gobject_class, prop_id, GPAC_PROP_0);
        }
        break;
      }
    }
  } else {
    gpac_install_src_pad_templates(gstelement_class);
    gpac_install_local_properties(
      gobject_class, GPAC_PROP_GRAPH, GPAC_PROP_DESTINATION, GPAC_PROP_0);

    if (!params->is_inside_sink)
      gpac_install_all_signals(gobject_class);

    // We don't know which filters will be used, so we expose all options
    for (u32 i = 0; i < G_N_ELEMENTS(filter_options); i++) {
      filter_option_overrides* opts = &filter_options[i];
      for (u32 j = 0; opts->options[j]; j++) {
        guint32 prop_id = opts->options[j];
        gpac_install_local_properties(gobject_class, prop_id, GPAC_PROP_0);
      }
    }
  }

  // Set the metadata
  const gchar* longname = "gpac transformer";
  if (params->is_single) {
    if (!g_strcmp0(params->info->filter_name, params->info->alias_name))
      longname =
        g_strdup_printf("gpac %s transformer", params->info->filter_name);
    else
      longname = g_strdup_printf("gpac %s (%s) transformer",
                                 params->info->alias_name,
                                 params->info->filter_name);
  }
  gst_element_class_set_static_metadata(
    gstelement_class,
    longname,
    "Aggregator/Transform",
    "Aggregates and transforms incoming data via GPAC",
    "Deniz Ugur <deniz.ugur@motionspell.com>");
}

gboolean
gst_gpac_tf_register(GstPlugin* plugin)
{
  GType type;
  GstGpacParams* params;
  GTypeInfo subclass_typeinfo = {
    sizeof(GstGpacTransformClass),
    NULL, // base_init
    NULL, // base_finalize
    (GClassInitFunc)gst_gpac_tf_subclass_init,
    NULL, // class_finalize
    NULL, // class_data
    sizeof(GstGpacTransform),
    0,
    NULL, // instance_init
  };

  GST_DEBUG_CATEGORY_INIT(gst_gpac_tf_debug, "gpactf", 0, "GPAC Transform");

  // Register the regular transform element
  GST_LOG("Registering regular gpac transform element");
  params = g_new0(GstGpacParams, 1);
  params->is_single = FALSE;
  params->is_inside_sink = FALSE;
  type = g_type_register_static(
    GST_TYPE_GPAC_TF, "GstGpacTransformRegular", &subclass_typeinfo, 0);
  g_type_set_qdata(type, GST_GPAC_PARAMS_QDATA, params);
  if (!gst_element_register(plugin, "gpactf", GST_RANK_PRIMARY, type)) {
    GST_ERROR_OBJECT(plugin,
                     "Failed to register regular gpac transform element");
    return FALSE;
  }

  // Register subelements
  for (u32 i = 0; i < G_N_ELEMENTS(subelements); i++) {
    subelement_info* info = &subelements[i];

    if (info->flags & GPAC_SE_SINK_ONLY) {
      GST_DEBUG_OBJECT(plugin,
                       "Subelement %s is a sink only element, skipping",
                       info->alias_name);
      continue;
    }

    // Register the sub transform element
    GST_LOG("Registering %s transform subelement", info->filter_name);
    params = g_new0(GstGpacParams, 1);
    params->is_single = TRUE;
    params->is_inside_sink = FALSE;
    params->info = info;
    const gchar* name = g_strdup_printf("gpac%s", info->alias_name);
    const gchar* type_name =
      g_strdup_printf("GstGpacTransform%c%s",
                      g_ascii_toupper(info->alias_name[0]),
                      info->alias_name + 1);

    type = g_type_register_static(
      GST_TYPE_GPAC_TF, type_name, &subclass_typeinfo, 0);
    g_type_set_qdata(type, GST_GPAC_PARAMS_QDATA, params);
    if (!gst_element_register(plugin, name, GST_RANK_SECONDARY, type)) {
      GST_ERROR_OBJECT(
        plugin, "Failed to register %s transform subelement", info->alias_name);
      return FALSE;
    }
  }

  return TRUE;
}

GST_ELEMENT_REGISTER_DEFINE_CUSTOM(gpac_tf, gst_gpac_tf_register);

// #MARK: Private registration
GType
gst_gpac_tf_register_custom(subelement_info* se_info,
                            gboolean is_inside_sink,
                            gboolean is_single)
{
  const gchar* type_name =
    g_strdup_printf("GstGpacTransformPrivate%c%s",
                    g_ascii_toupper(se_info->alias_name[0]),
                    se_info->alias_name + 1);

  // Check if the type is already registered
  if (g_type_from_name(type_name) != G_TYPE_INVALID) {
    GST_WARNING("Type %s is already registered, returning existing type",
                type_name);
    return g_type_from_name(type_name);
  }

  GType type;
  GTypeInfo type_info = {
    sizeof(GstGpacTransformClass),
    NULL, // base_init
    NULL, // base_finalize
    (GClassInitFunc)gst_gpac_tf_subclass_init,
    NULL, // class_finalize
    NULL, // class_data
    sizeof(GstGpacTransform),
    0,
    NULL,
  };

  GST_DEBUG_CATEGORY_INIT(gst_gpac_tf_debug, "gpactf", 0, "GPAC Transform");

  // Register the custom transform element
  GST_LOG("Creating a private gpac transform element with subelement info: %p",
          se_info);

  GstGpacParams* params = g_new0(GstGpacParams, 1);
  params->is_single = is_single;
  params->is_inside_sink = is_inside_sink;
  params->info = se_info;

  type = g_type_register_static(GST_TYPE_GPAC_TF, type_name, &type_info, 0);
  g_type_set_qdata(type, GST_GPAC_PARAMS_QDATA, params);
  return type;
}
