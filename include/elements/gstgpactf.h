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

#include "elements/common.h"
#include "gpacmessages.h"

#include "lib/caps.h"
#include "lib/main.h"
#include "lib/memio.h"
#include "lib/packet.h"
#include "lib/pid.h"
#include "lib/properties.h"
#include "lib/session.h"
#include "lib/signals.h"

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
  guint32 video_pad_count;
  guint32 audio_pad_count;
  guint32 subtitle_pad_count;
  guint32 caption_pad_count;

  /* Input Queue */
  GQueue* queue;

  /* Sacrificial Buffer (for syncing) */
  GstBuffer* sync_buffer;
};

/**
 * Accessors
 */
// NOLINTBEGIN
#define GPAC_CTX &gpac_tf->gpac_ctx
#define GPAC_PROP_CTX(ctx) (ctx.prop)
#define GPAC_SESS_CTX(ctx) (ctx.sess)
// NOLINTEND

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

static GstStaticPadTemplate internal_pad_template =
  GST_STATIC_PAD_TEMPLATE("src",
                          GST_PAD_SRC,
                          GST_PAD_ALWAYS,
                          GST_STATIC_CAPS(INTERNAL_CAPS));

G_END_DECLS
