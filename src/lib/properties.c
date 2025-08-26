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

#include "lib/properties.h"
#include "gpacmessages.h"
#include <gpac/filters.h>

typedef struct
{
  const gchar* name;
  gboolean is_simple; // if true, treat as "-arg" instead of "--arg=..."
} GPAC_Arg;
#define GPAC_DEF_ARG(name, is_simple) { name, is_simple }

// Define the override properties
static GF_GPACArg override_properties[] = {
  { 0 },
};

// Define the visible properties with their default values
static GPAC_Arg visible_properties[] = {
  GPAC_DEF_ARG("for-test", TRUE),
  GPAC_DEF_ARG("old-arch", TRUE),
  GPAC_DEF_ARG("strict-error", TRUE),
  GPAC_DEF_ARG("broken-cert", TRUE),
  { 0 },
};

void
gpac_get_property_attributes(GParamSpec* pspec,
                             gboolean* is_simple,
                             gboolean* is_visible)
{
  *is_visible = FALSE;
  *is_simple = FALSE;
  for (u32 i = 0; visible_properties[i].name; i++) {
    if (!g_strcmp0(g_param_spec_get_name(pspec), visible_properties[i].name)) {
      *is_simple = visible_properties[i].is_simple;
      *is_visible = TRUE;
      break;
    }
  }
}

void
gpac_install_local_properties(GObjectClass* gobject_class,
                              GPAC_PropertyId first_prop,
                              ...)
{
  // Initialize the variable argument list
  va_list args;
  GPAC_PropertyId prop = first_prop;
  va_start(args, first_prop);

  while (prop != GPAC_PROP_0) {
    switch (prop) {
      case GPAC_PROP_GRAPH:
        g_object_class_install_property(
          gobject_class,
          prop,
          g_param_spec_string("graph",
                              "Graph",
                              "Filter graph to use in gpac session",
                              NULL,
                              G_PARAM_READWRITE));
        break;

      case GPAC_PROP_PRINT_STATS:
        g_object_class_install_property(
          gobject_class,
          prop,
          g_param_spec_boolean("print-stats",
                               "Print Stats",
                               "Print filter session stats after execution",
                               FALSE,
                               G_PARAM_READWRITE));
        break;

      case GPAC_PROP_SYNC:
        g_object_class_install_property(
          gobject_class,
          prop,
          g_param_spec_boolean(
            "sync",
            "Sync",
            "Enable synchronization on the internal fakesink",
            FALSE,
            G_PARAM_READWRITE));
        break;

      case GPAC_PROP_DESTINATION:
        g_object_class_install_property(
          gobject_class,
          prop,
          g_param_spec_string("destination",
                              "Destination",
                              "Destination to use for the filter session. Adds "
                              "a new sink to the graph",
                              NULL,
                              G_PARAM_READWRITE));
        break;

      case GPAC_PROP_SEGDUR:
        g_object_class_install_property(
          gobject_class,
          prop,
          g_param_spec_float(
            "segdur",
            "Segment Duration",
            "Duration of each segment in seconds. This option is handled from "
            "the GStreamer side and not relayed to gpac",
            0,
            G_MAXFLOAT,
            0,
            G_PARAM_READWRITE));
        break;

      default:
        break;
    }

    // Get the next property
    prop = va_arg(args, GPAC_PropertyId);
  }

  // End the variable argument list
  va_end(args);
}

void
gpac_install_filter_properties(GObjectClass* gobject_class,
                               GList* blacklist,
                               const gchar* filter_name)
{
  GF_FilterSession* session = gf_fs_new_defaults(0U);
  guint num_filters = gf_fs_filters_registers_count(session);

  // Get the list seperator
  GF_Err e;
  GF_Filter* dummy = gf_fs_new_filter(session, "dummy", 0, &e);
  char sep_list = (char)gf_filter_get_sep(dummy, GF_FS_SEP_LIST);

  const GF_FilterRegister* filter;
  for (guint i = 0; i < num_filters; i++) {
    filter = gf_fs_get_filter_register(session, i);
    if (!g_strcmp0(filter->name, filter_name))
      break;
    filter = NULL;
  }
  gf_fs_del(session);
  g_assert(filter);

  guint option_idx = 0;
  while (filter->args && filter->args[option_idx].arg_name) {
    // Skip hidden options
    if (filter->args[option_idx].flags & GF_ARG_HINT_HIDE)
      goto skip;

    // Check if the option name is valid
    if (!g_param_spec_is_valid_name(filter->args[option_idx].arg_name))
      goto skip;

    // Check if the option is already registered
    if (g_object_class_find_property(gobject_class,
                                     filter->args[option_idx].arg_name))
      goto skip;

    // Check if the option is blacklisted
    GList* item;
    for (item = blacklist; item; item = item->next)
      if (!g_strcmp0(filter->args[option_idx].arg_name, item->data))
        goto skip;

#define SPEC_INSTALL(type, ...)                            \
  g_object_class_install_property(                         \
    gobject_class,                                         \
    GPAC_PROP_FILTER_OFFSET + option_idx,                  \
    g_param_spec_##type(filter->args[option_idx].arg_name, \
                        filter->args[option_idx].arg_name, \
                        filter->args[option_idx].arg_desc, \
                        __VA_ARGS__,                       \
                        G_PARAM_WRITABLE));

    // Special case for enum values
    if (filter->args[option_idx].min_max_enum &&
        strstr(filter->args[option_idx].min_max_enum, "|")) {
      SPEC_INSTALL(string, NULL);
      goto skip;
    }

    // Parse the default value
    const GF_PropertyValue p =
      gf_props_parse_value(filter->args[option_idx].arg_type,
                           filter->args[option_idx].arg_name,
                           filter->args[option_idx].arg_default_val,
                           filter->args[option_idx].min_max_enum,
                           sep_list);

    // Register the option
    switch (filter->args[option_idx].arg_type) {
      case GF_PROP_SINT:
        SPEC_INSTALL(int, G_MININT, G_MAXINT, p.value.sint);
        break;
      case GF_PROP_UINT:
        SPEC_INSTALL(uint, 0, G_MAXUINT, (guint)p.value.uint);
        break;
      case GF_PROP_LSINT:
        SPEC_INSTALL(int64, G_MININT64, G_MAXINT64, (gint64)p.value.longsint);
        break;
      case GF_PROP_LUINT:
        SPEC_INSTALL(uint64, 0, G_MAXUINT64, (guint64)p.value.longuint);
        break;
      case GF_PROP_FLOAT:
      case GF_PROP_FRACTION: {
        gfloat fval = 0.0F;
        if (p.value.lfrac.den != 0)
          fval = (gfloat)p.value.frac.num / (gfloat)p.value.frac.den;
        SPEC_INSTALL(float, -G_MAXFLOAT, G_MAXFLOAT, fval);
        break;
      }
      case GF_PROP_DOUBLE:
      case GF_PROP_FRACTION64: {
        gdouble dval = 0.0;
        if (p.value.lfrac.den != 0)
          dval = (gdouble)p.value.lfrac.num / (gdouble)p.value.lfrac.den;
        SPEC_INSTALL(double, -G_MAXDOUBLE, G_MAXDOUBLE, dval);
        break;
      }
      case GF_PROP_BOOL:
        SPEC_INSTALL(boolean, p.value.boolean);
        break;
      case GF_PROP_STRING:
        SPEC_INSTALL(string, p.value.string);
        break;
      default:
        SPEC_INSTALL(string, NULL);
        break;
    }

#undef SPEC_INSTALL

  skip:
    option_idx++;
  }
}

void
gpac_install_global_properties(GObjectClass* gobject_class)
{
  // Install the visible global properties
  const GF_GPACArg* args = gf_sys_get_options();
  for (u32 i = GPAC_PROP_GLOBAL_OFFSET;
       visible_properties[i - GPAC_PROP_GLOBAL_OFFSET].name;
       i++) {
    // Find the property in the global properties
    for (u32 j = 0; args[j].name; j++) {
      const GF_GPACArg* arg = &args[j];
      const GPAC_Arg* prop = &visible_properties[i - GPAC_PROP_GLOBAL_OFFSET];
      if (!g_strcmp0(prop->name, arg->name)) {
        g_object_class_install_property(
          gobject_class,
          i,
          prop->is_simple ? g_param_spec_boolean(arg->name,
                                                 arg->name,
                                                 arg->description,
                                                 FALSE,
                                                 G_PARAM_READWRITE)
                          : g_param_spec_string(arg->name,
                                                arg->name,
                                                arg->description,
                                                NULL,
                                                G_PARAM_READWRITE));
        break;
      }
    }
  }
}

gboolean
gpac_set_property(GPAC_PropertyContext* ctx,
                  guint property_id,
                  const GValue* value,
                  GParamSpec* pspec)
{
  if (g_param_value_defaults(pspec, value))
    return TRUE;

  if (IS_TOP_LEVEL_PROPERTY(property_id)) {
    switch (property_id) {
      case GPAC_PROP_GRAPH:
        g_free(ctx->graph);
        ctx->graph = g_value_dup_string(value);
        break;
      case GPAC_PROP_PRINT_STATS:
        ctx->print_stats = g_value_get_boolean(value);
        break;
      case GPAC_PROP_SYNC:
        ctx->sync = g_value_get_boolean(value);
        break;
      case GPAC_PROP_DESTINATION:
        g_free(ctx->destination);
        ctx->destination = g_value_dup_string(value);
        break;
      default:
        return FALSE;
    }
  } else if (IS_FILTER_PROPERTY(property_id)) {
    // Get the value as a string
    gchar* value_str;
    if (G_VALUE_HOLDS_STRING(value))
      value_str = g_value_dup_string(value);
    else if (G_VALUE_HOLDS_BOOLEAN(value))
      value_str = g_strdup(g_value_get_boolean(value) ? "true" : "false");
    else
      value_str = g_strdup_value_contents(value);

    // No value, return
    if (!value_str)
      return TRUE;

    // Convert snake case to kebab case
    // GLib uses kebab case for command line arguments, so we have to convert it
    // back to snake case
    gchar* param = g_strdup(g_param_spec_get_name(pspec));
    for (gchar* c = param; *c; c++)
      if (*c == '-')
        *c = '_';

    // Add the property to the list
    gchar* property = g_strdup_printf("--%s=%s", param, value_str);
    ctx->properties = g_list_append(ctx->properties, property);
    g_free(value_str);
    g_free(param);
    return TRUE;
  } else if (IS_GLOBAL_PROPERTY(property_id)) {
    // Check if the property is visible and if it is simple
    gboolean is_simple, is_visible;
    gpac_get_property_attributes(pspec, &is_simple, &is_visible);

    // If the property is not visible, return
    if (!is_visible)
      return FALSE;

    // Add the property to the list
    gchar* property;
    if (is_simple)
      property = g_strdup_printf("-%s", g_param_spec_get_name(pspec));
    else
      property = g_strdup_printf(
        "--%s=%s", g_param_spec_get_name(pspec), g_value_get_string(value));
    ctx->properties = g_list_append(ctx->properties, property);
  } else {
    // Unknown property or not handled here
    return FALSE;
  }
  return TRUE;
}

gboolean
gpac_get_property(GPAC_PropertyContext* ctx,
                  guint property_id,
                  GValue* value,
                  GParamSpec* pspec)
{
  if (IS_TOP_LEVEL_PROPERTY(property_id)) {
    switch (property_id) {
      case GPAC_PROP_GRAPH:
        g_value_set_string(value, ctx->graph);
        break;
      case GPAC_PROP_PRINT_STATS:
        g_value_set_boolean(value, ctx->print_stats);
        break;
      case GPAC_PROP_SYNC:
        g_value_set_boolean(value, ctx->sync);
        break;
      case GPAC_PROP_DESTINATION:
        g_value_set_string(value, ctx->destination);
        break;
      default:
        return FALSE;
    }
  } else if (IS_GLOBAL_PROPERTY(property_id)) {
    gboolean is_simple, is_visible;
    gpac_get_property_attributes(pspec, &is_simple, &is_visible);
    g_assert(is_visible);
    const gchar* prop_val =
      gf_sys_find_global_arg(g_param_spec_get_name(pspec));

    if (is_simple)
      g_value_set_boolean(value, prop_val != NULL);
    else
      g_value_set_string(value, prop_val);
  } else if (IS_ELEMENT_PROPERTY(property_id)) {
    // Handled in the element
    return FALSE;
  } else {
    // Unknown property or not handled here
    g_warn_if_reached();
  }
  return TRUE;
}

gboolean
gpac_apply_properties(GPAC_PropertyContext* ctx)
{
  // Add the default properties
  g_autolist(GList) override_list = NULL;
  for (u32 i = 0; override_properties[i].name; i++) {
    const GF_GPACArg* arg = &override_properties[i];
    gchar* property = g_strdup_printf("--%s=%s", arg->name, arg->val);
    override_list = g_list_append(override_list, property);
  }

  // Free the previous arguments
  if (ctx->props_as_argv) {
    for (u32 i = 0; ctx->props_as_argv[i]; i++)
      g_free(ctx->props_as_argv[i]);
    g_free((void*)ctx->props_as_argv);
    ctx->props_as_argv = NULL;
  }

  // Allocate the arguments array
  u32 num_properties =
    g_list_length(ctx->properties) + g_list_length(override_list);
  // +1 for the executable name
  num_properties++;
  // +1 for the NULL termination
  num_properties++;

  ctx->props_as_argv = (gchar**)g_malloc0_n(num_properties, sizeof(gchar*));

  // Add the executable name
  ctx->props_as_argv[0] = g_strdup("gst");

  void* item;
  for (u32 i = 0; i < g_list_length(ctx->properties); i++) {
    item = g_list_nth_data(ctx->properties, i);
    ctx->props_as_argv[i + 1] = (gchar*)g_strdup(item);
  }
  for (u32 i = 0; i < g_list_length(override_list); i++) {
    item = g_list_nth_data(override_list, i);
    ctx->props_as_argv[i + 1 + g_list_length(ctx->properties)] = (gchar*)item;
  }

  // Add the NULL termination
  ctx->props_as_argv[num_properties - 1] = NULL;

  // Set the gpac system arguments
  gpac_return_val_if_fail(
    gf_sys_set_args((s32)num_properties - 1, (const char**)ctx->props_as_argv),
    FALSE);

  return TRUE;
}
