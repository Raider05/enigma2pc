/*
 * Copyright (C) 2007 the xine project
 *
 * This file is part of xine, a free video player.
 *
 * xine is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * xine is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <xine/xine_internal.h>
#include <xine/spu.h>

#define BLACK_OPACITY   67
#define COLOUR_OPACITY 100

static void no_op (void *user_data, xine_cfg_entry_t *entry)
{
}

void _x_spu_misc_init (xine_t *this)
{
  this->config->register_range (this->config, "subtitles.bitmap.black_opacity",
                                BLACK_OPACITY, 0, 100,
                                _("opacity for the black parts of bitmapped subtitles"),
                                NULL,
                                10, no_op, NULL);
  this->config->register_range (this->config, "subtitles.bitmap.colour_opacity",
                                COLOUR_OPACITY, 0, 100,
                                _("opacity for the colour parts of bitmapped subtitles"),
                                NULL,
                                10, no_op, NULL);
}

void _x_spu_get_opacity (xine_t *this, xine_spu_opacity_t *opacity)
{
  cfg_entry_t *entry;

  entry = this->config->lookup_entry (this->config, "subtitles.bitmap.black_opacity");
  opacity->black = entry ? entry->num_value : BLACK_OPACITY;
  entry = this->config->lookup_entry (this->config, "subtitles.bitmap.colour_opacity");
  opacity->colour = entry ? entry->num_value : COLOUR_OPACITY;
}

int _x_spu_calculate_opacity (const clut_t *clut, uint8_t trans, const xine_spu_opacity_t *opacity)
{
  int value = (clut->y == 0 || (clut->y == 16 && clut->cb == 128 && clut->cr == 128))
	      ? opacity->black
	      : opacity->colour;
  return value * (255 - trans) / 100;
}
