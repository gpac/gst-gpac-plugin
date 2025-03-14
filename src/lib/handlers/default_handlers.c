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

#define DEFAULT_HANDLER_SIGNATURE(prop_nickname)                           \
  gboolean prop_nickname##_default_handler(GPAC_PROP_IMPL_ARGS_NO_ELEMENT)

// Optional if not explicitly marked as mandatory
#define DEFAULT_HANDLER(prop_nickname)                                     \
  gboolean prop_nickname##_default_handler(GPAC_PROP_IMPL_ARGS_NO_ELEMENT) \
  {                                                                        \
    return TRUE;                                                           \
  }

#define DEFAULT_HANDLER_MANDATORY(prop_nickname, prop_4cc)                 \
  gboolean prop_nickname##_default_handler(GPAC_PROP_IMPL_ARGS_NO_ELEMENT) \
  {                                                                        \
    SKIP_IF_SET(prop_4cc)                                                  \
    GST_WARNING("Could not determine the value for %s", #prop_nickname);   \
    return FALSE;                                                          \
  }

//
// Default Default handlers
//
DEFAULT_HANDLER_MANDATORY(stream_type, GF_PROP_PID_STREAM_TYPE)
DEFAULT_HANDLER_MANDATORY(mime, GF_PROP_PID_MIME)
DEFAULT_HANDLER_MANDATORY(codec_id, GF_PROP_PID_CODECID)
DEFAULT_HANDLER_MANDATORY(unframed, GF_PROP_PID_UNFRAMED)

// Following are optional
DEFAULT_HANDLER(width)
DEFAULT_HANDLER(height)
DEFAULT_HANDLER(bitrate)
DEFAULT_HANDLER(max_bitrate)
DEFAULT_HANDLER(decoder_config)
DEFAULT_HANDLER(duration)
DEFAULT_HANDLER(fps)
DEFAULT_HANDLER(timescale)
DEFAULT_HANDLER(sample_rate)
DEFAULT_HANDLER(num_channels)
DEFAULT_HANDLER(language)
DEFAULT_HANDLER(dbsize)

//
// Default handlers
//
DEFAULT_HANDLER_SIGNATURE(id)
{
  // Check if we have already set the id
  SKIP_IF_SET(GF_PROP_PID_ID);

  // Set a new monotonic id
  static guint id = 1;
  SET_PROP(GF_PROP_PID_ID, PROP_UINT(id++));
  return TRUE;
}
