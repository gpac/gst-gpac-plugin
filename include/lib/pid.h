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

#include <gpac/filters.h>
#include <gst/gst.h>

#include "lib/session.h"
#include "lib/time.h"

/**
 * GpacPadFlags: Flags that indicate which properties of the pad are set.
 */
typedef enum
{
  GPAC_PAD_CAPS_SET = 1 << 0,
  GPAC_PAD_TAGS_SET = 1 << 1,
  GPAC_PAD_SEGMENT_SET = 1 << 2
} GpacPadFlags;

/**
 * GpacPadPrivate: Holds the latest information about the pad.
 */
typedef struct
{
  GstPad* self;
  guint id; // Monotonic ID of the pad, used for ID property in gpac

  // Information from the pad
  gboolean eos;
  GstCaps* caps;
  GstTagList* tags;
  GstSegment* segment;

  // Flags that indicate which properties are set
  GpacPadFlags flags;

  // State for the buffers
  gint64 dts_offset;
  gboolean dts_offset_set;
  gboolean last_frame_was_keyframe;

  // State for the encoder
  guint64 idr_period;
  guint64 idr_last;
  guint64 idr_next;

  // Capabilities required for the PID
  GList* gpac_caps;
  gboolean caps_changed;
} GpacPadPrivate;

#define GPAC_PID_PROP_IMPL_ARGS_NO_ELEMENT \
  GpacPadPrivate *priv, GF_FilterPid *pid
#define GPAC_PID_PROP_IMPL_ARGS                           \
  GstElement *element, GPAC_PID_PROP_IMPL_ARGS_NO_ELEMENT

/*! reconfigures a pid based on the given element and pad private data
    \param[in] element the element that the pad belongs to
    \param[in] priv the private data of the pad
    \param[in] pid the pid to reconfigure for the pad
    \return TRUE if the pad was reconfigured successfully, FALSE otherwise
*/
gboolean gpac_pid_reconfigure(GPAC_PID_PROP_IMPL_ARGS);

/*! creates a new filter pid
    \param[in] sess the session context
    \return the new pid
*/
GF_FilterPid*
gpac_pid_new(GPAC_SessionContext* sess);

/*! deletes a filter pid
    \param[in] pid the pid to delete
*/
void
gpac_pid_del(GF_FilterPid* pid);
