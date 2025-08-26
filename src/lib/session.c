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

#include "lib/session.h"
#include "lib/memio.h"
#include <gpac/list.h>

#define SEP_LINK 5
#define SEP_FRAG 2

GF_Err
process_link_directive(char* link,
                       GF_Filter* filter,
                       GF_List* loaded_filters,
                       char* ext_link)
{
  char* link_prev_filter_ext = NULL;
  GF_Filter* link_from;
  Bool reverse_order = GF_FALSE;
  s32 link_filter_idx = -1;

  if (!filter) {
    u32 idx = 0, count = gf_list_count(loaded_filters);
    if (!ext_link || !count)
      return GF_BAD_PARAM;
    ext_link[0] = 0;
    if (link[1] == GF_FS_DEFAULT_SEPS[SEP_LINK])
      idx = (u32)strtoul(link + 2, NULL, 10);
    else {
      idx = (u32)strtoul(link + 1, NULL, 10);
      if (count - 1 < idx)
        return GF_BAD_PARAM;
      idx = count - 1 - idx;
    }
    ext_link[0] = GF_FS_DEFAULT_SEPS[SEP_LINK];
    filter = gf_list_get(loaded_filters, idx);
    link = ext_link;
  }

  char* ext = strchr(link, GF_FS_DEFAULT_SEPS[SEP_FRAG]);
  if (ext) {
    ext[0] = 0;
    link_prev_filter_ext = ext + 1;
  }
  if (strlen(link) > 1) {
    if (link[1] == GF_FS_DEFAULT_SEPS[SEP_LINK]) {
      reverse_order = GF_TRUE;
      link++;
    }
    link_filter_idx = 0;
    if (strlen(link) > 1) {
      link_filter_idx = (s32)strtol(link + 1, NULL, 10);
      if (link_filter_idx < 0)
        return GF_BAD_PARAM;
    }
  } else
    link_filter_idx = 0;
  if (ext)
    ext[0] = GF_FS_DEFAULT_SEPS[SEP_FRAG];

  if (reverse_order)
    link_from = gf_list_get(loaded_filters, (u32)link_filter_idx);
  else
    link_from = gf_list_get(
      loaded_filters, gf_list_count(loaded_filters) - 1 - (u32)link_filter_idx);

  if (!link_from)
    return GF_BAD_PARAM;
  gf_filter_set_source(filter, link_from, link_prev_filter_ext);
  return GF_OK;
}

gboolean
gpac_session_init(GPAC_SessionContext* ctx,
                  GstElement* element,
                  GstGpacParams* params)
{
  ctx->session = gf_fs_new_defaults(GF_FS_FLAG_NON_BLOCKING);
  ctx->element = element;
  ctx->params = params;
  return ctx->session != NULL;
}

gboolean
gpac_session_close(GPAC_SessionContext* ctx, gboolean print_stats)
{
  if (ctx->session) {
    if (ctx->had_data_flow) {
      // Run the filter chain until the end
      gpac_session_run(ctx, TRUE);
    }

    // Stop the session
    gf_fs_stop(ctx->session);

    // Print session stats
    if (print_stats) {
      gf_log_set_tools_levels("app@info", 1);
      gf_fs_print_connections(ctx->session);
      gf_fs_print_stats(ctx->session);
      gf_log_set_tools_levels("app@warning", 1);
    }

    gpac_memio_free(ctx);

    // Reset the session context
    gf_fs_del(ctx->session);
    ctx->session = NULL;
    ctx->memin = NULL;
  }
  return TRUE;
}

GF_Err
gpac_session_run(GPAC_SessionContext* ctx, gboolean flush)
{
  if (!ctx->session)
    return GF_BAD_PARAM;
  gf_filter_post_process_task(ctx->memin);

  GF_Err e = GF_OK;
  guint32 steps = 100;
  do {
    e = gf_fs_run(ctx->session);
  } while (!gf_fs_is_last_task(ctx->session) &&
           (flush || (e == GF_OK && steps--)));

  // Check errors
  e = gf_fs_get_last_connect_error(ctx->session);
  if (e != GF_OK) {
    GST_ELEMENT_ERROR(ctx->element,
                      LIBRARY,
                      FAILED,
                      ("Failed to connect filters: %s", gf_error_to_string(e)),
                      (NULL));
    return e;
  }
  e = gf_fs_get_last_process_error(ctx->session);
  if (e != GF_OK) {
    GST_ELEMENT_ERROR(ctx->element,
                      LIBRARY,
                      FAILED,
                      ("Failed to process filters: %s", gf_error_to_string(e)),
                      (NULL));
    return e;
  }

  // Mark that we had data flow
  if (!flush)
    ctx->had_data_flow = TRUE;

  return GF_OK;
}

GF_Err
gpac_session_open(GPAC_SessionContext* ctx, gchar* graph)
{
  // Initialize the variables
  GF_Err e = GF_OK;

  // Check if the session is initialized
  if (G_UNLIKELY(!ctx->session)) {
    GST_ELEMENT_ERROR(
      ctx->element,
      STREAM,
      FAILED,
      (NULL),
      ("Failed to open gpac filter session, session not initialized"));
    return GF_BAD_PARAM;
  }

  if (!graph)
    return GF_OK;

  // Load the graph
  GF_List* links_directives = gf_list_new();
  GF_List* loaded_filters = gf_list_new();
  gchar** nodes = g_strsplit(graph, " ", -1);

  // We must have a memory input filter, add it so that it can be referenced
  if (!ctx->memin) {
    GST_ELEMENT_ERROR(ctx->element,
                      LIBRARY,
                      FAILED,
                      (NULL),
                      ("Memory input filter is missing"));
    return GF_BAD_PARAM;
  }
  gf_list_add(loaded_filters, ctx->memin);

  // Loop through the nodes
  for (guint i = 0; nodes[i]; i++) {
    GF_Filter* filter = NULL;
    gboolean f_loaded = FALSE;
    gchar* node = nodes[i];

    // Check if this is an input or output node
    if (!strcmp(node, "-i")) {
      filter = gf_fs_load_source(ctx->session, nodes[++i], NULL, NULL, &e);
      f_loaded = TRUE;
    } else if (!strcmp(node, "-o")) {
      filter = gf_fs_load_destination(ctx->session, nodes[++i], NULL, NULL, &e);
      f_loaded = TRUE;
    } else {
      if (node[0] == '-') {
        GST_ELEMENT_WARNING(
          ctx->element,
          STREAM,
          FAILED,
          (NULL),
          ("Cannot parse global option within graph: %s", node));
      }
    }

    if (!f_loaded) {
      if (node[0] == GF_FS_DEFAULT_SEPS[SEP_LINK]) {
        char* next_sep = NULL;
        if (node[1] == GF_FS_DEFAULT_SEPS[SEP_LINK])
          next_sep = strchr(node + 2, GF_FS_DEFAULT_SEPS[SEP_LINK]);
        else
          next_sep = strchr(node + 1, GF_FS_DEFAULT_SEPS[SEP_LINK]);
        if (next_sep) {
          if (process_link_directive(node, NULL, loaded_filters, next_sep)) {
            GST_ERROR("Failed to process link directive: %s", node);
            e = GF_BAD_PARAM;
            goto finish;
          }
          continue;
        }
        gf_list_add(links_directives, node);
        continue;
      }

      if (ctx->destination) {
        if (strstr(node, "dasher")) {
          // We need to append "mname" option to the filter name
          const gchar* dst = ctx->destination;
          const gchar* after_slash = strrchr(ctx->destination, '/');
          dst = after_slash ? after_slash + 1 : ctx->destination;
          gchar* new_node = g_strdup_printf("%s:gpac:mname=%s", node, dst);
          node = new_node;
        }
      }

      filter = gf_fs_load_filter(ctx->session, node, &e);
    }

    if (G_UNLIKELY(!filter)) {
      GST_ERROR(
        "Failed to load filter \"%s\": %s", node, gf_error_to_string(e));
      e = GF_BAD_PARAM;
      goto finish;
    }

    while (gf_list_count(links_directives)) {
      char* link = gf_list_pop_front(links_directives);
      if (process_link_directive(link, filter, loaded_filters, NULL)) {
        GST_ERROR("Failed to process link directive: %s", link);
        e = GF_BAD_PARAM;
        goto finish;
      }
    }
    gf_list_add(loaded_filters, filter);
  }

finish:
  gf_list_del(links_directives);
  gf_list_del(loaded_filters);
  g_strfreev(nodes);

  return e;
}

GF_Filter*
gpac_session_load_filter(GPAC_SessionContext* ctx, const gchar* filter_name)
{
  GF_Err e = GF_OK;
  GF_Filter* filter = gf_fs_load_filter(ctx->session, filter_name, &e);
  if (!filter) {
    GST_ELEMENT_ERROR(
      ctx->element,
      STREAM,
      FAILED,
      ("Failed to load filter \"%s\": %s", filter_name, gf_error_to_string(e)),
      (NULL));
  }
  return filter;
}

gboolean
gpac_session_has_output(GPAC_SessionContext* ctx)
{
  u32 count = gf_fs_get_filters_count(ctx->session);
  for (u32 i = 0; i < count; i++) {
    GF_Filter* filter = gf_fs_get_filter(ctx->session, i);
    if (gf_filter_is_sink(filter))
      return TRUE;
  }
  return FALSE;
}
