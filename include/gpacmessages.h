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

#include <glib.h>
#include <gpac/tools.h>

#define GPAC_LOCAL_ERROR GF_Err gpac_error = GF_OK;
#define GPAC_ERROR_START(expr)                                     \
  const gchar* gpac_msg =                                          \
    g_strconcat(expr, ": ", gf_error_to_string(gpac_error), NULL);
#define GPAC_ERROR_END g_free((gpointer)gpac_msg);

#define gpac_return_if_fail(expr)                                  \
  G_STMT_START                                                     \
  {                                                                \
    GPAC_LOCAL_ERROR                                               \
    gpac_error = (expr);                                           \
    if (G_LIKELY(gpac_error == GF_OK)) {                           \
    } else {                                                       \
      GPAC_ERROR_START(#expr)                                      \
      g_return_if_fail_warning(G_LOG_DOMAIN, G_STRFUNC, gpac_msg); \
      GPAC_ERROR_END                                               \
      return (gpac_error);                                         \
    }                                                              \
  }                                                                \
  G_STMT_END

#define gpac_return_val_if_fail(expr, val)                         \
  G_STMT_START                                                     \
  {                                                                \
    GPAC_LOCAL_ERROR                                               \
    gpac_error = (expr);                                           \
    if (G_LIKELY(gpac_error == GF_OK)) {                           \
    } else {                                                       \
      GPAC_ERROR_START(#expr)                                      \
      g_return_if_fail_warning(G_LOG_DOMAIN, G_STRFUNC, gpac_msg); \
      GPAC_ERROR_END                                               \
      return (val);                                                \
    }                                                              \
  }                                                                \
  G_STMT_END
