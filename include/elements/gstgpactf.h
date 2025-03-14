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

#pragma once

#include "gpacmessages.h"

#include "lib/caps.h"
#include "lib/filters/filters.h"
#include "lib/main.h"
#include "lib/memio.h"
#include "lib/packet.h"
#include "lib/pid.h"
#include "lib/properties.h"
#include "lib/session.h"

#include <gst/base/gstaggregator.h>
#include <gst/gst.h>
#include <gst/video/video-event.h>

G_BEGIN_DECLS

#define GST_TYPE_GPAC_TF (gst_gpac_tf_get_type())
G_DECLARE_FINAL_TYPE(GstGpacTransform, gst_gpac_tf, GST, GPAC_TF, GstAggregator)

/**
 * GstGpacTransform: Opaque data structure.
 */
struct _GstGpacTransform
{
  GstAggregator parent;

  /* GPAC Context */
  GPAC_Context gpac_ctx;

  /* Element specific options */
  guint64 global_idr_period;
  guint64 gpac_idr_period;

  /* General Pad Information */
  guint32 container_pad_count;
  guint32 video_pad_count;
  guint32 audio_pad_count;
  guint32 subtitle_pad_count;
  guint32 caption_pad_count;

  /* Input Queue */
  GQueue* queue;
};

/**
 * Accessors
 */
#define GPAC_CTX &gpac_tf->gpac_ctx
#define GPAC_PROP_CTX(ctx) (ctx.prop)
#define GPAC_SESS_CTX(ctx) (ctx.sess)

#define GST_TYPE_GPAC_TF_PAD (gst_gpac_tf_pad_get_type())
G_DECLARE_FINAL_TYPE(GstGpacTransformPad,
                     gst_gpac_tf_pad,
                     GST,
                     GPAC_TF_PAD,
                     GstAggregatorPad)

/**
 * GstGpacTransformPad: Opaque data structure.
 */
struct _GstGpacTransformPad
{
  GstAggregatorPad parent;

  /* GPAC PID */
  GF_FilterPid* pid;
};

GST_ELEMENT_REGISTER_DECLARE(gpac_tf);

/**
 * Subelements
 */
#define GPAC_TF_SUBELEMENT_COMMON(name, caps)                  \
  .filter_name = name,                                         \
  .src_template = GST_STATIC_PAD_TEMPLATE(                     \
    "src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(caps))

#define GPAC_TF_SUBELEMENT(name, caps, opts) \
  { .alias_name = name,                      \
    GPAC_TF_SUBELEMENT_COMMON(name, caps),   \
    .default_options = opts }
#define GPAC_TF_SUBELEMENT_AS(alias, name, caps, opts) \
  { .alias_name = alias,                               \
    GPAC_TF_SUBELEMENT_COMMON(name, caps),             \
    .default_options = opts }

typedef struct
{
  const gchar* name;
  const gchar* value;
  // If not forced, user can override this option
  gboolean forced;
} filter_option;

typedef struct
{
  const gchar* alias_name;
  const gchar* filter_name;
  // The pad template to constrain the element with
  GstStaticPadTemplate src_template;
  // The default options to apply on the filter
  filter_option* default_options;
} subelement_info;

#define GPAC_TF_FILTER_OPTION(name, value, forced) { name, value, forced }
#define GPAC_TF_FILTER_OPTION_ARRAY(...) \
  (filter_option[])                      \
  {                                      \
    __VA_ARGS__,                         \
    {                                    \
      NULL, NULL, FALSE                  \
    }                                    \
  }

static subelement_info subelements[] = {
  GPAC_TF_SUBELEMENT_AS(
    "cmafmux",
    "mp4mx",
    QT_CMAF_CAPS,
    GPAC_TF_FILTER_OPTION_ARRAY(GPAC_TF_FILTER_OPTION("cmaf", "cmf2", TRUE),
                                GPAC_TF_FILTER_OPTION("store", "frag", FALSE))),
  GPAC_TF_SUBELEMENT("mp4mx", QT_CAPS, NULL)
};

/**
 * Custom options to expose for the GPAC filters
 */
#define GPAC_TF_FILTER_OPTIONS(name, ...)                           \
  { .filter_name = name, .options = (guint32[]){ __VA_ARGS__, 0 } }

typedef struct
{
  const gchar* filter_name;
  guint32* options;
} filter_option_overrides;

static filter_option_overrides filter_options[] = {
  GPAC_TF_FILTER_OPTIONS("mp4mx", GPAC_PROP_SEGDUR),
};

/**
 * Quark
 */
#define GST_GPAC_TF_PARAMS_QDATA g_quark_from_static_string("gpac-tf-params")
#define GST_GPAC_TF_GET_PARAMS(klass)                                    \
  g_type_get_qdata(G_OBJECT_CLASS_TYPE(klass), GST_GPAC_TF_PARAMS_QDATA)
typedef struct _GstGpacTransformParams
{
  gboolean is_single;
  subelement_info* info;
} GstGpacTransformParams;

G_END_DECLS
