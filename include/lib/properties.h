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

typedef struct
{
  gchar* graph;
  gboolean print_stats;
  gboolean sync;
  gchar* destination;
  GList* properties;
  GList* blacklist;

  /*< private >*/
  gchar** props_as_argv;
} GPAC_PropertyContext;

// Property Registry
typedef enum
{
  GPAC_PROP_0,

  // Top-level properties
  GPAC_PROP_GRAPH,
  GPAC_PROP_PRINT_STATS,
  GPAC_PROP_SYNC,
  GPAC_PROP_DESTINATION,

  // Element-specific properties
  GPAC_PROP_ELEMENT_OFFSET,
  GPAC_PROP_SEGDUR,

  // Offset for the filter and global properties
  GPAC_PROP_FILTER_OFFSET,
  GPAC_PROP_GLOBAL_OFFSET = (1 << 8), // Way higher than any filter property
} GPAC_PropertyId;

#define IS_TOP_LEVEL_PROPERTY(prop)                           \
  ((prop) > GPAC_PROP_0 && (prop) < GPAC_PROP_ELEMENT_OFFSET)
#define IS_ELEMENT_PROPERTY(prop)                                         \
  ((prop) > GPAC_PROP_ELEMENT_OFFSET && (prop) < GPAC_PROP_FILTER_OFFSET)
#define IS_FILTER_PROPERTY(prop)                                          \
  ((prop) >= GPAC_PROP_FILTER_OFFSET && (prop) < GPAC_PROP_GLOBAL_OFFSET)
#define IS_GLOBAL_PROPERTY(prop) ((prop) >= GPAC_PROP_GLOBAL_OFFSET)

/*! installs the local (element-specific) properties as properties of a GObject
   class
    \param[in] gobject_class the GObject class to install the properties to
    \param[in] first_prop the first property id to install
    \param[in] ... the rest of the property ids to install
*/
void
gpac_install_local_properties(GObjectClass* gobject_class,
                              GPAC_PropertyId first_prop,
                              ...);

/*! installs the filter properties as properties of a GObject class
    \param[in] gobject_class the GObject class to install the properties to
    \param[in] blacklist the list of properties to blacklist
    \param[in] filter_name the name of the filter
*/
void
gpac_install_filter_properties(GObjectClass* gobject_class,
                               GList* blacklist,
                               const gchar* filter_name);

/*! installs the global (gpac-specific) properties as properties of a GObject
    class
    \param[in] gobject_class the GObject class to install the properties to
*/
void
gpac_install_global_properties(GObjectClass* gobject_class);

/*! sets a property of a GObject based on the property id
    \param[in] object the GObject to set the property of
    \param[in] property_id the id of the property to set
    \param[in] value the value to set the property to
    \param[in] pspec the property specification
    \return TRUE if the property was set successfully, FALSE otherwise
*/
gboolean
gpac_set_property(GPAC_PropertyContext* ctx,
                  guint property_id,
                  const GValue* value,
                  GParamSpec* pspec);

/*! gets a property of a GObject based on the property id
    \param[in] object the GObject to get the property of
    \param[in] property_id the id of the property to get
    \param[in] value the value to get the property to
    \param[in] pspec the property specification
    \return TRUE if the property was retrieved successfully, FALSE otherwise
*/
gboolean
gpac_get_property(GPAC_PropertyContext* ctx,
                  guint property_id,
                  GValue* value,
                  GParamSpec* pspec);

/*! applies the properties to the gpac context
    \param[in] ctx the gpac context to apply the properties to
    \return TRUE if the properties were applied successfully, FALSE otherwise
*/
gboolean
gpac_apply_properties(GPAC_PropertyContext* ctx);
