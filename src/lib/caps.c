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

#include "lib/caps.h"

GstGpacFormatProp gst_gpac_sink_formats = {
  .video_caps = GST_STATIC_CAPS(AV1_CAPS "; " H264_CAPS "; " H265_CAPS),
  .audio_caps = GST_STATIC_CAPS(AAC_CAPS "; " EAC3_CAPS),
  .subtitle_caps = GST_STATIC_CAPS(TEXT_UTF8),
  .caption_caps = GST_STATIC_CAPS(CEA708_CAPS),
};

GstPadTemplate* sink_templates[4] = { NULL };

GstPadTemplate*
gst_gpac_get_sink_template(GstGpacSinkTemplateType type)
{
  return sink_templates[type];
}

void
gpac_install_sink_pad_templates(GstElementClass* klass)
{
  // Video pad template
  if (sink_templates[TEMPLATE_VIDEO] == NULL) {
    sink_templates[TEMPLATE_VIDEO] = gst_pad_template_new(
      "video_%u",
      GST_PAD_SINK,
      GST_PAD_REQUEST,
      gst_static_caps_get(&gst_gpac_sink_formats.video_caps));
  }
  gst_element_class_add_pad_template(klass, sink_templates[TEMPLATE_VIDEO]);

  // Audio pad template
  if (sink_templates[TEMPLATE_AUDIO] == NULL) {
    sink_templates[TEMPLATE_AUDIO] = gst_pad_template_new(
      "audio_%u",
      GST_PAD_SINK,
      GST_PAD_REQUEST,
      gst_static_caps_get(&gst_gpac_sink_formats.audio_caps));
  }
  gst_element_class_add_pad_template(klass, sink_templates[TEMPLATE_AUDIO]);

  // Subtitle pad template
  if (sink_templates[TEMPLATE_SUBTITLE] == NULL) {
    sink_templates[TEMPLATE_SUBTITLE] = gst_pad_template_new(
      "subtitle_%u",
      GST_PAD_SINK,
      GST_PAD_REQUEST,
      gst_static_caps_get(&gst_gpac_sink_formats.subtitle_caps));
  }
  gst_element_class_add_pad_template(klass, sink_templates[TEMPLATE_SUBTITLE]);

  // Caption pad template
  if (sink_templates[TEMPLATE_CAPTION] == NULL) {
    sink_templates[TEMPLATE_CAPTION] = gst_pad_template_new(
      "caption_%u",
      GST_PAD_SINK,
      GST_PAD_REQUEST,
      gst_static_caps_get(&gst_gpac_sink_formats.caption_caps));
  }
  gst_element_class_add_pad_template(klass, sink_templates[TEMPLATE_CAPTION]);
}

GstStaticPadTemplate gst_gpac_src_template =
  GST_STATIC_PAD_TEMPLATE("src",
                          GST_PAD_SRC,
                          GST_PAD_ALWAYS,
                          GST_STATIC_CAPS(QT_CAPS));

void
gpac_install_src_pad_templates(GstElementClass* klass)
{
  gst_element_class_add_static_pad_template(klass, &gst_gpac_src_template);
}

GF_FilterCapability*
gpac_gstcaps_to_gfcaps(GstCaps* caps, guint* nb_caps)
{
  if (!caps)
    return NULL;

  // Get the structure from the caps
  if (gst_caps_get_size(caps) == 0) {
    GST_ERROR("No structure in caps");
    return NULL;
  } else if (gst_caps_get_size(caps) > 1)
    GST_WARNING("Multiple structures in caps, will only use the first one");
  GstStructure* structure = gst_caps_get_structure(caps, 0);

  /**
   * Currently, we only support file output stream. Therefore, we only need to
   * set MIME type and stream type.
   */

  // Allocate the capabilities
  *nb_caps = 2;
  GF_FilterCapability* gf_caps = g_new0(GF_FilterCapability, *nb_caps);
  if (!gf_caps)
    return NULL;

  // Set the stream type
  gf_caps[0].code = GF_PROP_PID_STREAM_TYPE;
  gf_caps[0].val.type = GF_PROP_UINT;
  gf_caps[0].val.value.uint = GF_STREAM_FILE;
  gf_caps[0].flags = GF_CAPS_INPUT;

  // Set the mime type
  gf_caps[1].code = GF_PROP_PID_MIME;
  gf_caps[1].val.type = GF_PROP_STRING;
  gf_caps[1].val.value.string = g_strdup(gst_structure_get_name(structure));
  gf_caps[1].flags = GF_CAPS_INPUT;

  return gf_caps;
}
