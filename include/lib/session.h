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

#include "elements/common.h"

typedef struct
{
  GstElement* element;
  GF_FilterSession* session;
  GF_Filter* memin;
  GF_Filter* memout;

  // overrides
  const gchar* destination;

  /*< internal >*/
  gboolean had_data_flow;
  GstGpacParams* params;
} GPAC_SessionContext;

/*! initializes a gpac filter session
    \param[in] ctx the session context to initialize
    \param[in] element the element to initialize the session with
    \param[in] params single element parameters, can be NULL if the element
                      is not a single filter element
    \return TRUE if the session was initialized successfully, FALSE otherwise
*/
gboolean
gpac_session_init(GPAC_SessionContext* ctx,
                  GstElement* element,
                  GstGpacParams* params);

/*! closes a gpac filter session
    \param[in] ctx the session context to close
    \param[in] print_stats whether to print the session stats
    \return TRUE if the session was closed successfully, FALSE otherwise
*/
gboolean
gpac_session_close(GPAC_SessionContext* ctx, gboolean print_stats);

/*! aborts a gpac filter session
    \param[in] ctx the session context to abort
*/
void
gpac_session_abort(GPAC_SessionContext* ctx);

/*! runs a gpac filter session
    \param[in] ctx the session context to run
    \param[in] flush whether to flush the session
    \return GF_OK if the session was run successfully, an error code otherwise
*/
GF_Err
gpac_session_run(GPAC_SessionContext* ctx, gboolean flush);

/*! opens a gpac filter session
    \param[in] ctx the session context to open
    \param[in] graph the graph to open
    \return GF_OK if the session was opened successfully, an error code
*/
GF_Err
gpac_session_open(GPAC_SessionContext* ctx, gchar* graph);

/*! loads a gpac filter from a gpac filter session
    \param[in] ctx the session context to load the filter from
    \param[in] filter_name the name of the filter to load
    \return the loaded filter, or NULL if the filter could not be loaded
*/
GF_Filter*
gpac_session_load_filter(GPAC_SessionContext* ctx, const gchar* filter_name);

/*! checks if a gpac filter session has output
    \param[in] ctx the session context to check
    \return TRUE if the session has output, FALSE otherwise
*/
gboolean
gpac_session_has_output(GPAC_SessionContext* ctx);
