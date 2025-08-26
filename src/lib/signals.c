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

#include "lib/signals.h"
#include "elements/common.h"

typedef struct
{
  const gchar* preset_name;
  union
  {
    struct
    {
      guint32 from_id;
      guint32 to_id;
    };
    guint32* ids;
  };
} signal_info;

#define GPAC_SIGNAL_ID_ARRAY(...) (guint32[]){ __VA_ARGS__, 0 }
#define GPAC_SIGNAL_PRESET_RANGE(name, from, to)              \
  { .preset_name = (name), .from_id = (from), .to_id = (to) }
#define GPAC_SIGNAL_PRESET(name, ...)                                 \
  { .preset_name = (name), .ids = GPAC_SIGNAL_ID_ARRAY(__VA_ARGS__) }

static signal_info signal_presets[] = {
  GPAC_SIGNAL_PRESET_RANGE("dasher_all",
                           GPAC_SIGNAL_DASHER_MANIFEST,
                           GPAC_SIGNAL_DASHER_DELETE_SEGMENT),
};

void
register_signal(GObjectClass* klass, GPAC_SignalId id)
{
  g_assert(klass != NULL);
  g_assert(id < GPAC_SIGNAL_LAST);
  GstGpacParams* params = GST_GPAC_GET_PARAMS(klass);
  guint* registered_signals = params->registered_signals;

  if (registered_signals[id])
    return; // Already registered

  switch (id) {
    case GPAC_SIGNAL_DASHER_MANIFEST:
    case GPAC_SIGNAL_DASHER_MANIFEST_VARIANT:
    case GPAC_SIGNAL_DASHER_SEGMENT_INIT:
    case GPAC_SIGNAL_DASHER_SEGMENT:
      registered_signals[id] = g_signal_new(
        gpac_signal_names[id - 1], // Adjusted index for 0-based array
        G_TYPE_FROM_CLASS(klass),
        G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE,
        0,
        NULL,
        NULL,
        NULL,
        G_TYPE_OUTPUT_STREAM,
        1,
        G_TYPE_STRING);
      break;

    case GPAC_SIGNAL_DASHER_DELETE_SEGMENT:
      registered_signals[id] = g_signal_new(
        gpac_signal_names[id - 1], // Adjusted index for 0-based array
        G_TYPE_FROM_CLASS(klass),
        G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE,
        0,
        NULL,
        NULL,
        NULL,
        G_TYPE_BOOLEAN, // Whether the segment was deleted
        1,
        G_TYPE_STRING);
      break;

    default:
      break;
  };
}

void
gpac_install_signals_by_presets(GObjectClass* gobject_class,
                                const gchar* presets)
{
  g_assert(gobject_class != NULL);
  g_assert(presets != NULL);

  gchar** preset_list = g_strsplit(presets, ",", -1);
  for (gint i = 0; preset_list[i] != NULL; i++) {
    const gchar* preset_name = g_strstrip(preset_list[i]);
    for (guint j = 0; j < G_N_ELEMENTS(signal_presets); j++) {
      if (g_strcmp0(signal_presets[j].preset_name, preset_name) == 0) {
        guint32 from = signal_presets[j].from_id;
        guint32 to = signal_presets[j].to_id;
        if (!from)
          from = GPAC_SIGNAL_START;
        if (!to)
          to = GPAC_SIGNAL_END;

        for (guint32 id = from; id <= to && id < GPAC_SIGNAL_LAST; id++) {
          register_signal(gobject_class, id);
        }
        break;
      }
    }
  }
  g_strfreev(preset_list);
}

void
gpac_install_all_signals(GObjectClass* gobject_class)
{
  g_assert(gobject_class != NULL);

  for (guint32 id = GPAC_SIGNAL_START; id < GPAC_SIGNAL_LAST; id++) {
    register_signal(gobject_class, id);
  }
}

gboolean
gpac_signal_try_emit(GstElement* element,
                     GPAC_SignalId id,
                     const gchar* location,
                     GOutputStream** output_stream)
{
  g_assert(element != NULL);
  g_assert(id < GPAC_SIGNAL_LAST);

  GstObject* parent = GST_OBJECT(element);
  do {
    GObjectClass* klass = G_OBJECT_GET_CLASS(parent);
    GstGpacParams* params = GST_GPAC_GET_PARAMS(klass);
    guint* registered_signals = params->registered_signals;

    // We may not have registered this signal
    if (!registered_signals[id])
      continue;

    guint signal_id = registered_signals[id];
    if (signal_id) {
      if (output_stream) {
        *output_stream = NULL;
        g_signal_emit(parent, signal_id, 0, location, output_stream);
        return (*output_stream != NULL);
      }

      gboolean deleted = FALSE;
      g_signal_emit(parent, signal_id, 0, location, &deleted);
      return deleted;
    }
  } while ((parent = gst_element_get_parent(parent)));

  GST_DEBUG_OBJECT(element,
                   "Signal %s not registered for element %s",
                   gpac_signal_names[id - 1],
                   GST_OBJECT_NAME(element));

  return FALSE;
}
