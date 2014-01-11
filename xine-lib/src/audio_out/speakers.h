/*
 * Copyright (C) 2013 the xine project
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 */

#define MONO            0
#define STEREO          1
#define HEADPHONES      2
#define SURROUND21      3
#define SURROUND3       4
#define SURROUND4       5
#define SURROUND41      6
#define SURROUND5       7
#define SURROUND51      8
#define SURROUND6       9
#define SURROUND61      10
#define SURROUND71      11
#define A52_PASSTHRU    12

#define AUDIO_DEVICE_SPEAKER_ARRANGEMENT_HELP                           \
    _("speaker arrangement"),                                           \
    _("Select how your speakers are arranged, "                         \
      "this determines which speakers xine uses for sound output. "     \
      "The individual values are:\n\n"                                  \
      "Mono 1.0: You have only one speaker.\n"                          \
      "Stereo 2.0: You have two speakers for left and right channel.\n" \
      "Headphones 2.0: You use headphones.\n"                           \
      "Stereo 2.1: You have two speakers for left and right channel, and one " \
      "subwoofer for the low frequencies.\n"                            \
      "Surround 3.0: You have three speakers for left, right and rear channel.\n" \
      "Surround 4.0: You have four speakers for front left and right and rear " \
      "left and right channels.\n"                                      \
      "Surround 4.1: You have four speakers for front left and right and rear " \
      "left and right channels, and one subwoofer for the low frequencies.\n" \
      "Surround 5.0: You have five speakers for front left, center and right and " \
      "rear left and right channels.\n"                                 \
      "Surround 5.1: You have five speakers for front left, center and right and " \
      "rear left and right channels, and one subwoofer for the low frequencies.\n" \
      "Surround 6.0: You have six speakers for front left, center and right and " \
      "rear left, center and right channels.\n"                         \
      "Surround 6.1: You have six speakers for front left, center and right and " \
      "rear left, center and right channels, and one subwoofer for the low frequencies.\n" \
      "Surround 7.1: You have seven speakers for front left, center and right, " \
      "left and right and rear left and right channels, and one subwoofer for the " \
      "low frequencies.\n"                                              \
      "Pass Through: Your sound system will receive undecoded digital sound from xine. " \
      "You need to connect a digital surround decoder capable of decoding the " \
      "formats you want to play to your sound card's digital output.")

#define AUDIO_DEVICE_SPEAKER_ARRANGEMENT_TYPES \
  static const char * const speaker_arrangement[] = { \
    "Mono 1.0", "Stereo 2.0", "Headphones 2.0", "Stereo 2.1", \
    "Surround 3.0", "Surround 4.0", "Surround 4.1", "Surround 5.0", \
    "Surround 5.1", "Surround 6.0", "Surround 6.1", "Surround 7.1", \
    "Pass Through", NULL};
