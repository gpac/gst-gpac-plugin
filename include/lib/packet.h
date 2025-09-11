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

#include "lib/pid.h"
#include "lib/time.h"

#define GPAC_PCK_PROP_IMPL_ARGS_NO_ELEMENT                      \
  GpacPadPrivate *priv, GF_FilterPacket *pck, GF_FilterPid *pid
#define GPAC_PCK_PROP_IMPL_ARGS                         \
  GstBuffer *buffer, GPAC_PCK_PROP_IMPL_ARGS_NO_ELEMENT

/*! creates a new packet from the given buffer
    \param[in] buffer the buffer to create the packet from
    \param[in] priv the private data of the pad
    \param[in] pid the pid to create the packet for
    \return the new packet
*/
GF_FilterPacket*
gpac_pck_new_from_buffer(GstBuffer* buffer,
                         GpacPadPrivate* priv,
                         GF_FilterPid* pid);
