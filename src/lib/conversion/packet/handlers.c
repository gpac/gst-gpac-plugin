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

#include <gpac/id3.h>
#include <gst/gst.h>

gboolean
id3_handler(GPAC_PCK_PROP_IMPL_ARGS)
{
  const GstMetaInfo* info = gst_meta_get_info("Id3Meta");
  if (!info)
    return FALSE;

  GstMeta* meta = gst_buffer_get_meta(buffer, info->api);
  if (!meta)
    return FALSE;

  GstStructure* s = ((GstCustomMeta*)meta)->structure;
  const GValue* tags_val = gst_structure_get_value(s, "tags");

  if (!tags_val || !GST_VALUE_HOLDS_LIST(tags_val))
    return FALSE;

  guint n = gst_value_list_get_size(tags_val);
  if (n == 0)
    return FALSE;

  // Create new list to store the tags
  GF_List* tag_list = gf_list_new();
  guint64 pts = gf_filter_pck_get_cts(pck);
  guint32 timescale = gf_filter_pid_get_timescale(pid);

  for (guint i = 0; i < n; ++i) {
    const GValue* tag_val = gst_value_list_get_value(tags_val, i);
    const GstStructure* tag_struct = g_value_get_boxed(tag_val);

    const GValue* data_val = gst_structure_get_value(tag_struct, "data");
    if (data_val && G_VALUE_HOLDS(data_val, GST_TYPE_BUFFER)) {
      GstBuffer* tag_buf = g_value_get_boxed(data_val);
      g_auto(GstBufferMapInfo) map = GST_MAP_INFO_INIT;
      if (gst_buffer_map(tag_buf, &map, GST_MAP_READ)) {
        // Create a new ID3 tag
        GF_ID3_TAG* tag;
        GF_SAFEALLOC(tag, GF_ID3_TAG);
        gf_id3_tag_new(tag, timescale, pts, map.data, map.size);

        // Add the tag to the list
        gf_list_add(tag_list, tag);
      }
    }
  }

  // Write the tags to a bitstream
  GF_BitStream* bs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
  gf_id3_list_to_bitstream(tag_list, bs);

  // Free the tag list
  GF_ID3_TAG* tag;
  while ((tag = (GF_ID3_TAG*)gf_list_pop_back(tag_list)))
    gf_id3_tag_free(tag);
  gf_list_del(tag_list);

  // Export the bitrstream
  u8* data;
  u32 size;
  gf_bs_get_content(bs, &data, &size);

  // Set the ID3 tags as a property on the packet
  gf_filter_pck_set_property_str(pck, "id3", &PROP_DATA(data, size));

  // Free the data
  gf_free(data);
  gf_bs_del(bs);

  return TRUE;
}
