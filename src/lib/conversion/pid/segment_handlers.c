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
#include "lib/pid.h"

#define SEGMENT_HANDLER_SIGNATURE(prop_nickname)                    \
  gboolean prop_nickname##_segment_handler(GPAC_PID_PROP_IMPL_ARGS)

#define DEFAULT_HANDLER(prop_nickname)                              \
  gboolean prop_nickname##_segment_handler(GPAC_PID_PROP_IMPL_ARGS) \
  {                                                                 \
    return FALSE;                                                   \
  }

//
// Default Segment handlers
//

//
// Segment handlers
//
SEGMENT_HANDLER_SIGNATURE(duration)
{
  const GF_PropertyValue* p;

  // Check if either start and stop is valid
  gboolean range_valid = GST_CLOCK_TIME_IS_VALID(priv->segment->start) &&
                         GST_CLOCK_TIME_IS_VALID(priv->segment->stop);
  gboolean duration_valid = GST_CLOCK_TIME_IS_VALID(priv->segment->duration);

  // Duration cannot be calculated from the segment
  if (!range_valid && !duration_valid)
    return FALSE;

  // Get the duration
  guint64 duration;
  if (duration_valid)
    duration = priv->segment->duration;
  else // if (range_valid)
    duration = priv->segment->stop - priv->segment->start;

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

  // Construct the duration
  GF_Fraction64 duration_fr = {
    .num = (s64)gpac_time_rescale_with_fps(duration, fps, timescale),
    .den = timescale,
  };

  // Set the duration
  SET_PROP(GF_PROP_PID_DURATION, PROP_FRAC64(duration_fr));
  return TRUE;
}
