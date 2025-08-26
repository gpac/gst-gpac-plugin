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

/* static info related to various format */

// clang-format off
#define INTERNAL_CAPS "application/x-gpac"

#define COMMON_VIDEO_CAPS \
  "width = (int) [ 16, MAX ], " \
  "height = (int) [ 16, MAX ]"

#define AV1_CAPS \
  "video/x-av1, " \
  "stream-format = (string) obu-stream, " \
  "alignment = (string) tu, " \
  COMMON_VIDEO_CAPS

#define H264_CAPS \
  "video/x-h264, " \
  "stream-format = (string) { avc, byte-stream }, " \
  "alignment = (string) au, " \
  COMMON_VIDEO_CAPS

#define H265_CAPS \
  "video/x-h265, " \
  "stream-format = (string) { hev1, byte-stream }, " \
  "alignment = (string) au, " \
  COMMON_VIDEO_CAPS

#define COMMON_AUDIO_CAPS(c, r) \
  "channels = (int) [ 1, " G_STRINGIFY (c) " ], " \
  "rate = (int) [ 1, " G_STRINGIFY (r) " ]"

#define AAC_CAPS \
  "audio/mpeg, " \
  "mpegversion = (int) 4, " \
  "stream-format = (string) raw, " \
  COMMON_AUDIO_CAPS (8, MAX)

#define EAC3_CAPS \
  "audio/x-eac3, " \
  "alignment = (string) iec61937, " \
  "channels = (int) [ 1, 6 ], " \
  "rate = (int) [ 8000, 48000 ]"

#define TEXT_UTF8 \
  "text/x-raw, " \
  "format=(string)utf8"

#define CEA708_CAPS \
  "closedcaption/x-cea-708, format=(string)cdp"

#define QT_CAPS "video/quicktime"

#define QT_CMAF_CAPS \
  QT_CAPS ", " \
  "variant=(string)cmaf"

#define MPEG_TS_CAPS \
  "video/mpegts, " \
  "systemstream = (boolean) true"
// clang-format on

typedef struct
{
  GstStaticCaps video_caps;
  GstStaticCaps audio_caps;
  GstStaticCaps subtitle_caps;
  GstStaticCaps caption_caps;
} GstGpacFormatProp;

typedef enum
{
  GPAC_TEMPLATE_VIDEO,
  GPAC_TEMPLATE_AUDIO,
  GPAC_TEMPLATE_SUBTITLE,
  GPAC_TEMPLATE_CAPTION,
} GstGpacSinkTemplateType;

/*! Get the sink pad template for the given type
    \param[in] type the type of the sink pad template
    \return the GstPadTemplate
*/
GstPadTemplate*
gst_gpac_get_sink_template(GstGpacSinkTemplateType type);

/*! Install the sink pad templates for the given element class
    \param[in] klass the element class to install the pad templates
*/
void
gpac_install_sink_pad_templates(GstElementClass* klass);

/*! Install the source pad templates for the given element class
    \param[in] klass the element class to install the pad templates
*/
void
gpac_install_src_pad_templates(GstElementClass* klass);

/*! Convert a GstCaps to a GF_FilterCapability array
    \param[in] caps the GstCaps to convert
    \param[out] nb_caps the number of capabilities in the returned array
    \return the array of GF_FilterCapability
*/
GF_FilterCapability*
gpac_gstcaps_to_gfcaps(GstCaps* caps, guint* nb_caps);
