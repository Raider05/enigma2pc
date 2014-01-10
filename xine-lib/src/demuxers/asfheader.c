#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_ICONV
#include <iconv.h>
#endif

#include <string.h>
#include <stdio.h>
#include <stdlib.h>


#define LOG_MODULE "asfheader"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xineutils.h>
#include "bswap.h"
#include "asfheader.h"

#ifndef HAVE_ICONV

/* dummy conversion perserving ASCII only */

#define iconv_open(TO, FROM) 0
#define iconv(CD, INBUF, INLEFT, OUTBUF, OUTLEFT) iconv_internal(INBUF, INLEFT, OUTBUF, OUTLEFT)
#define iconv_close(CD)
#ifdef ICONV_CONST
#  undef ICONV_CONST
#endif
#define ICONV_CONST const

typedef int iconv_t;

size_t iconv_internal(const char **inbuf, size_t *inbytesleft, char **outbuf, size_t *outbytesleft) {
  size_t i, n;
  const char *ins;
  char *outs;

  n = *inbytesleft / 2 > *outbytesleft ? *outbytesleft : *inbytesleft / 2;
  for (i = n, ins = *inbuf, outs = *outbuf; i > 0; i--) {
    outs[0] = ((ins[0] & 0x80) || ins[1]) ? '?' : ins[0];
    ins += 2;
    outs++;
  }
  *inbuf = ins;
  *outbuf = outs;
  (*inbytesleft) -= (2 * n);
  (*outbytesleft) -= n;

  return 0;
}
#endif


typedef struct asf_header_internal_s asf_header_internal_t;
struct asf_header_internal_s {
  asf_header_t            pub;

  /* private part */
  int                     number_count;
  uint16_t                numbers[ASF_MAX_NUM_STREAMS];
  uint8_t                *bitrate_pointers[ASF_MAX_NUM_STREAMS];
};


typedef struct asf_reader_s asf_reader_t;
struct asf_reader_s {
  uint8_t *buffer;
  size_t   pos;
  size_t   size;
};


static void asf_reader_init(asf_reader_t *reader, uint8_t *buffer, int size) {
  reader->buffer = buffer;
  reader->pos = 0;
  reader->size = size;
}

#if 0
static int asf_reader_get_8(asf_reader_t *reader, uint8_t *value) {
  if ((reader->size - reader->pos) < 1)
    return 0;
  *value = *(reader->buffer + reader->pos);
  reader->pos += 1;
  return 1;
}
#endif

static int asf_reader_get_16(asf_reader_t *reader, uint16_t *value) {
  if ((reader->size - reader->pos) < 2)
    return 0;
  *value = _X_LE_16(reader->buffer + reader->pos);
  reader->pos += 2;
  return 1;
}

static int asf_reader_get_32(asf_reader_t *reader, uint32_t *value) {
  if ((reader->size - reader->pos) < 4)
    return 0;
  *value = _X_LE_32(reader->buffer + reader->pos);
  reader->pos += 4;
  return 1;
}

static int asf_reader_get_64(asf_reader_t *reader, uint64_t *value) {
  if ((reader->size - reader->pos) < 8)
    return 0;
  *value = _X_LE_64(reader->buffer + reader->pos);
  reader->pos += 8;
  return 1;
}

static int asf_reader_get_guid(asf_reader_t *reader, GUID *value) {
  if ((reader->size - reader->pos) < 16)
    return 0;

  asf_get_guid(reader->buffer + reader->pos, value);
  reader->pos += 16;
  return 1;
}

static uint8_t *asf_reader_get_bytes(asf_reader_t *reader, size_t size) {
  uint8_t *buffer;

  if ((reader->size - reader->pos) < size)
    return NULL;
  if (! (buffer = malloc(size)) )
    return NULL;
  memcpy(buffer, reader->buffer + reader->pos, size);
  reader->pos += size;
  return buffer;
}

/* get an utf8 string */
static char *asf_reader_get_string(asf_reader_t *reader, size_t size, iconv_t cd) {
  char *inbuf, *outbuf;
  size_t inbytesleft, outbytesleft;
  char scratch[2048];

  if ((size == 0) ||((reader->size - reader->pos) < size))
    return NULL;

  inbuf = (char *)reader->buffer + reader->pos;
  inbytesleft = size;
  outbuf = scratch;
  outbytesleft = sizeof(scratch);
  reader->pos += size;
  if (iconv (cd, (ICONV_CONST char **)&inbuf, &inbytesleft, &outbuf, &outbytesleft) != (size_t)-1) {
    return strdup(scratch);
  } else {
    lprintf("iconv error\n");
    return NULL;
  }
}

static int asf_reader_skip(asf_reader_t *reader, size_t size) {
  if ((reader->size - reader->pos) < size) {
    reader->pos = reader->size;
    return 0;
  }
  reader->pos += size;
  return size;
}

static uint8_t *asf_reader_get_buffer(asf_reader_t *reader) {
  return (reader->buffer + reader->pos);
}

static int asf_reader_eos(asf_reader_t *reader) {
  if (reader->pos < reader->size)
    return 0;
  else
    return 1;
}

static size_t asf_reader_get_size(asf_reader_t *reader) {
  return reader->size - reader->pos;
}

void asf_get_guid(uint8_t *buffer, GUID *value) {
  int i;

  value->Data1 = _X_LE_32(buffer);
  value->Data2 = _X_LE_16(buffer + 4);
  value->Data3 = _X_LE_16(buffer + 6);
  for(i = 0; i < 8; i++) {
    value->Data4[i] = *(buffer + i + 8);
  }
}

int asf_find_object_id (GUID *g) {
  int i;

  for (i = 1; i < GUID_END; i++) {
    if (!memcmp(g, &guids[i].guid, sizeof(GUID))) {
      lprintf ("asf_find_object_id: %s\n", guids[i].name);
      return i;
    }
  }
  lprintf ("asf_find_object_id: unknown GUID: 0x%04X, 0x%02X, 0x%02X, {0x%01X, 0x%01X, 0x%01X, 0x%01X, 0x%01X, 0x%01X, 0x%01X, 0x%01X}\n",
    g->Data1, g->Data2, g->Data3, g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
  return GUID_ERROR;
}

/* Manage id mapping */
static int asf_header_get_stream_id(asf_header_t *header_pub, uint16_t stream_number) {
  asf_header_internal_t *header = (asf_header_internal_t *)header_pub;
  int i;
  
  /* linear search */
  for (i = 0; i < header->number_count; i++) {
    if (stream_number == header->numbers[i]) {
      lprintf("asf_header_get_stream_id: id found: %d\n", i);
      return i;
    }
  }

  /* not found */
  if (header->number_count >= ASF_MAX_NUM_STREAMS)
    return -1;

  header->numbers[header->number_count] = stream_number;
  header->number_count++;
  return header->number_count - 1;
}

static int asf_header_parse_file_properties(asf_header_t *header, uint8_t *buffer, int buffer_len) {
  asf_reader_t reader;
  asf_file_t *asf_file;
  uint32_t flags = 0;

  if (buffer_len < 80) {
    lprintf("invalid asf file properties object\n");
    return 0;
  }

  if (! (asf_file = malloc(sizeof(asf_file_t))) ) {
    lprintf("cannot allocate asf_file_struct\n");
    return 0;
  }

  asf_reader_init(&reader, buffer, buffer_len);
 
  asf_reader_get_guid(&reader, &asf_file->file_id);
  asf_reader_get_64(&reader, &asf_file->file_size);

  /* creation date */
  asf_reader_skip(&reader, 8);
  asf_reader_get_64(&reader, &asf_file->data_packet_count);
  asf_reader_get_64(&reader, &asf_file->play_duration);
  asf_reader_get_64(&reader, &asf_file->send_duration);
  asf_reader_get_64(&reader, &asf_file->preroll);
  
  asf_reader_get_32(&reader, &flags);
  asf_reader_get_32(&reader, &asf_file->packet_size);

  /* duplicated packet size */
  asf_reader_skip(&reader, 4);
  asf_reader_get_32(&reader, &asf_file->max_bitrate);
  
  asf_file->broadcast_flag = flags & 0x1;
  asf_file->seekable_flag = flags & 0x2;

  header->file = asf_file;

  lprintf("File properties\n");
  lprintf("  file_id:                           %04X\n", asf_file->file_id.Data1);
  lprintf("  file_size:                         %"PRIu64"\n", asf_file->file_size);
  lprintf("  data_packet_count:                 %"PRIu64"\n", asf_file->data_packet_count);
  lprintf("  play_duration:                     %"PRIu64"\n", asf_file->play_duration);
  lprintf("  send_duration:                     %"PRIu64"\n", asf_file->send_duration);
  lprintf("  preroll:                           %"PRIu64"\n", asf_file->preroll);
  lprintf("  broadcast_flag:                    %d\n", asf_file->broadcast_flag);
  lprintf("  seekable_flag:                     %d\n", asf_file->seekable_flag);
  lprintf("  packet_size:                       %"PRIu32"\n", asf_file->packet_size);
  lprintf("  max_bitrate:                       %"PRIu32"\n", asf_file->max_bitrate);
  return 1;
}

static int asf_header_parse_stream_properties(asf_header_t *header, uint8_t *buffer, int buffer_len) {
  asf_reader_t reader;
  uint16_t flags = 0;
  uint32_t junk;
  GUID guid;
  asf_stream_t *asf_stream = NULL;
  int stream_id;

  if (buffer_len < 54)
    goto exit_error;

  if (! (asf_stream = malloc(sizeof(asf_stream_t))) )
    goto exit_error;

  asf_stream->private_data = NULL;
  asf_stream->error_correction_data = NULL;

  asf_reader_init(&reader, buffer, buffer_len);

  asf_reader_get_guid(&reader, &guid);
  asf_stream->stream_type = asf_find_object_id(&guid);
  asf_reader_get_guid(&reader, &guid);
  asf_stream->error_correction_type = asf_find_object_id(&guid);

  asf_reader_get_64(&reader, &asf_stream->time_offset);
  asf_reader_get_32(&reader, &asf_stream->private_data_length);
  asf_reader_get_32(&reader, &asf_stream->error_correction_data_length);

  asf_reader_get_16(&reader, &flags);
  asf_stream->stream_number = flags & 0x7F;
  asf_stream->encrypted_flag = flags >> 15;

  asf_reader_get_32(&reader, &junk);

  asf_stream->private_data = asf_reader_get_bytes(&reader, asf_stream->private_data_length);
  if (!asf_stream->private_data)
    goto exit_error;

  asf_stream->error_correction_data = asf_reader_get_bytes(&reader, asf_stream->error_correction_data_length);
  if (!asf_stream->error_correction_data)
    goto exit_error;

  lprintf("Stream_properties\n");
  lprintf("  stream_number:                     %d\n", asf_stream->stream_number);
  lprintf("  stream_type:                       %s\n", guids[asf_stream->stream_type].name);
  lprintf("  error_correction_type:             %s\n", guids[asf_stream->error_correction_type].name);
  lprintf("  time_offset:                       %"PRIu64"\n", asf_stream->time_offset);
  lprintf("  private_data_length:               %"PRIu32"\n", asf_stream->private_data_length);
  lprintf("  error_correction_data_length:      %"PRIu32"\n", asf_stream->error_correction_data_length);
  lprintf("  encrypted_flag:                    %d\n", asf_stream->encrypted_flag);

  stream_id = asf_header_get_stream_id(header, asf_stream->stream_number);
  if (stream_id >= 0) {
    header->streams[stream_id] = asf_stream;
    header->stream_count++;
  }
  return 1;

exit_error:
  if (asf_stream) {
    if (asf_stream->private_data)
      free(asf_stream->private_data);
    if (asf_stream->error_correction_data)
      free(asf_stream->error_correction_data);
    free(asf_stream);
  }
  return 0;
}

static int asf_header_parse_stream_extended_properties(asf_header_t *header, uint8_t *buffer, int buffer_len) {
  asf_reader_t reader;
  uint32_t flags = 0;
  uint16_t stream_number = 0;
  int i;
  int stream_id;
  asf_stream_extension_t *asf_stream_extension;

  if (buffer_len < 64)
    return 0;

  if (! (asf_stream_extension = malloc(sizeof(asf_stream_extension_t))) )
    return 0;

  asf_reader_init(&reader, buffer, buffer_len);

  asf_reader_get_64(&reader, &asf_stream_extension->start_time);
  asf_reader_get_64(&reader, &asf_stream_extension->end_time);

  asf_reader_get_32(&reader, &asf_stream_extension->data_bitrate);
  asf_reader_get_32(&reader, &asf_stream_extension->buffer_size);
  asf_reader_get_32(&reader, &asf_stream_extension->initial_buffer_fullness);
  asf_reader_get_32(&reader, &asf_stream_extension->alternate_data_bitrate);
  asf_reader_get_32(&reader, &asf_stream_extension->alternate_buffer_size);
  asf_reader_get_32(&reader, &asf_stream_extension->alternate_initial_buffer_fullness);
  asf_reader_get_32(&reader, &asf_stream_extension->max_object_size);

  /* 4 flags */
  asf_reader_get_32(&reader, &flags);
  asf_stream_extension->reliable_flag = flags  & 1;
  asf_stream_extension->seekable_flag = (flags >> 1) & 1;
  asf_stream_extension->no_cleanpoints_flag = (flags >> 2) & 1;
  asf_stream_extension->resend_live_cleanpoints_flag = (flags >> 3) & 1;

  asf_reader_get_16(&reader, &stream_number);

  asf_reader_get_16(&reader, &asf_stream_extension->language_id);
  asf_reader_get_64(&reader, &asf_stream_extension->average_time_per_frame);

  asf_reader_get_16(&reader, &asf_stream_extension->stream_name_count);
  asf_reader_get_16(&reader, &asf_stream_extension->payload_extension_system_count);

  /* get stream names */
  if (asf_stream_extension->stream_name_count) {
    asf_stream_extension->stream_names = malloc (asf_stream_extension->stream_name_count * sizeof(void*));
    for (i = 0; i < asf_stream_extension->stream_name_count; i++) {
      uint16_t lang_index, length = 0;
      asf_reader_get_16(&reader, &lang_index);
      asf_reader_get_16(&reader, &length);
      asf_stream_extension->stream_names[i] = (char*)asf_reader_get_bytes(&reader, length); /* store them */
    }
  }

  /* skip payload extensions */
  if (asf_stream_extension->payload_extension_system_count) {
    for (i = 0; i < asf_stream_extension->payload_extension_system_count; i++) {
      GUID guid;
      uint16_t data_size;
      uint32_t length = 0;
      asf_reader_get_guid(&reader, &guid);
      asf_reader_get_16(&reader, &data_size);
      asf_reader_get_32(&reader, &length);
      asf_reader_skip(&reader, length);
    }
  }

  stream_id = asf_header_get_stream_id(header, stream_number);
  if (stream_id >= 0) {
    header->stream_extensions[stream_id] = asf_stream_extension;
  }

  /* embeded stream properties */
  if (asf_reader_get_size(&reader) >= 24) {
    GUID guid;
    uint64_t object_length = 0;

    asf_reader_get_guid(&reader, &guid);
    asf_reader_get_64(&reader, &object_length);

    /* check length validity */
    if (asf_reader_get_size(&reader) == (object_length - 24)) {
      int object_id = asf_find_object_id(&guid);
      switch (object_id) {
        case GUID_ASF_STREAM_PROPERTIES:
          asf_header_parse_stream_properties(header, asf_reader_get_buffer(&reader), object_length - 24);
          break;
        default:
          lprintf ("unexpected object\n");
          break;
      }
    } else {
      lprintf ("invalid object length\n");
    }
  }

  lprintf("Stream extension properties\n");
  lprintf("  stream_number:                     %"PRIu16"\n", stream_number);
  lprintf("  start_time:                        %"PRIu64"\n", asf_stream_extension->start_time);
  lprintf("  end_time:                          %"PRIu64"\n", asf_stream_extension->end_time);
  lprintf("  data_bitrate:                      %"PRIu32"\n", asf_stream_extension->data_bitrate);
  lprintf("  buffer_size:                       %"PRIu32"\n", asf_stream_extension->buffer_size);
  lprintf("  initial_buffer_fullness:           %"PRIu32"\n", asf_stream_extension->initial_buffer_fullness);
  lprintf("  alternate_data_bitrate:            %"PRIu32"\n", asf_stream_extension->alternate_data_bitrate);
  lprintf("  alternate_buffer_size:             %"PRIu32"\n", asf_stream_extension->alternate_buffer_size);
  lprintf("  alternate_initial_buffer_fullness: %"PRIu32"\n", asf_stream_extension->alternate_initial_buffer_fullness);
  lprintf("  max_object_size:                   %"PRIu32"\n", asf_stream_extension->max_object_size);
  lprintf("  language_id:                       %"PRIu16"\n", asf_stream_extension->language_id);
  lprintf("  average_time_per_frame:            %"PRIu64"\n", asf_stream_extension->average_time_per_frame);
  lprintf("  stream_name_count:                 %"PRIu16"\n", asf_stream_extension->stream_name_count);
  lprintf("  payload_extension_system_count:    %"PRIu16"\n", asf_stream_extension->payload_extension_system_count);
  lprintf("  reliable_flag:                     %d\n", asf_stream_extension->reliable_flag);
  lprintf("  seekable_flag:                     %d\n", asf_stream_extension->seekable_flag);
  lprintf("  no_cleanpoints_flag:               %d\n", asf_stream_extension->no_cleanpoints_flag);
  lprintf("  resend_live_cleanpoints_flag:      %d\n", asf_stream_extension->resend_live_cleanpoints_flag);

  return 1;
}

static int asf_header_parse_stream_bitrate_properties(asf_header_t *header_pub, uint8_t *buffer, int buffer_len) {
  asf_header_internal_t *header = (asf_header_internal_t *)header_pub;
  asf_reader_t reader;
  uint16_t bitrate_count = 0;
  int i;
  int stream_id;

  if (buffer_len < 2)
    return 0;

  asf_reader_init(&reader, buffer, buffer_len);
  asf_reader_get_16(&reader, &bitrate_count);

  if (buffer_len < (2 + 6 * bitrate_count))
    return 0;

  lprintf ("  bitrate count: %d\n", bitrate_count);

  for(i = 0; i < bitrate_count; i++) {
    uint16_t flags = 0;
    uint32_t bitrate = 0;
    int stream_number;
    uint8_t *bitrate_pointer;

    asf_reader_get_16(&reader, &flags);
    stream_number = flags & 0x7f;

    bitrate_pointer = asf_reader_get_buffer(&reader);
    asf_reader_get_32(&reader, &bitrate);
    lprintf ("  stream num %d, bitrate %"PRIu32"\n", stream_number, bitrate);

    stream_id = asf_header_get_stream_id(&header->pub, stream_number);
    if (stream_id >= 0) {
      header->pub.bitrates[stream_id] = bitrate;
      header->bitrate_pointers[stream_id] = bitrate_pointer;
    }
  }
  return 1;
}

static int asf_header_parse_metadata(asf_header_t *header_pub, uint8_t *buffer, int buffer_len)
{
  asf_header_internal_t *header = (asf_header_internal_t *)header_pub;
  asf_reader_t reader;
  uint16_t i, records_count = 0;
  iconv_t iconv_cd;

  if (buffer_len < 2)
    return 0;

  if ((iconv_cd = iconv_open ("UTF-8", "UCS-2LE")) == (iconv_t)-1)
    return 0;

  asf_reader_init(&reader, buffer, buffer_len);
  asf_reader_get_16(&reader, &records_count);

  for (i = 0; i < records_count; i++)
  {
    uint16_t index, stream = 0, name_len = 0, data_type;
    uint32_t data_len = 0;
    int stream_id;

    asf_reader_get_16 (&reader, &index); 
    asf_reader_get_16 (&reader, &stream); 
    stream &= 0x7f;
    asf_reader_get_16 (&reader, &name_len); 
    asf_reader_get_16 (&reader, &data_type); 
    asf_reader_get_32 (&reader, &data_len); 

    stream_id = asf_header_get_stream_id (&header->pub, stream);

    if (data_len >= 4)
    {
      char *name = asf_reader_get_string (&reader, name_len, iconv_cd);
      if (name && !strcmp (name, "AspectRatioX"))
      {
        asf_reader_get_32 (&reader, &header->pub.aspect_ratios[stream_id].x);
        data_len -= 4;
      }
      else if (name && !strcmp (name, "AspectRatioY"))
      {
        asf_reader_get_32 (&reader, &header->pub.aspect_ratios[stream_id].y);
        data_len -= 4;
      }
      free (name);
      asf_reader_skip (&reader, data_len);
    }
    else
      asf_reader_skip (&reader, data_len + name_len);
  }

  iconv_close (iconv_cd);
  return 1;
}

static int asf_header_parse_header_extension(asf_header_t *header, uint8_t *buffer, int buffer_len) {
  asf_reader_t reader;

  GUID junk1;
  uint16_t junk2;
  uint32_t data_length;

  if (buffer_len < 22)
    return 0;

  asf_reader_init(&reader, buffer, buffer_len);

  asf_reader_get_guid(&reader, &junk1);
  asf_reader_get_16(&reader, &junk2);
  asf_reader_get_32(&reader, &data_length);

  lprintf("parse_asf_header_extension: length: %"PRIu32"\n", data_length);

  while (!asf_reader_eos(&reader)) {

    GUID guid;
    int object_id;
    uint64_t object_length = 0, object_data_length;

    if (asf_reader_get_size(&reader) < 24) {
      printf("invalid buffer size\n");
      return 0;
    }

    asf_reader_get_guid(&reader, &guid);
    asf_reader_get_64(&reader, &object_length);

    object_data_length = object_length - 24;
    object_id = asf_find_object_id(&guid);
    switch (object_id) {
      case GUID_EXTENDED_STREAM_PROPERTIES:
        asf_header_parse_stream_extended_properties(header, asf_reader_get_buffer(&reader), object_data_length);
        break;
      case GUID_METADATA:
        asf_header_parse_metadata(header, asf_reader_get_buffer(&reader), object_data_length);
        break;
      case GUID_ADVANCED_MUTUAL_EXCLUSION:
      case GUID_GROUP_MUTUAL_EXCLUSION:
      case GUID_STREAM_PRIORITIZATION:
      case GUID_BANDWIDTH_SHARING:
      case GUID_LANGUAGE_LIST:
      case GUID_METADATA_LIBRARY:
      case GUID_INDEX_PARAMETERS:
      case GUID_MEDIA_OBJECT_INDEX_PARAMETERS:
      case GUID_TIMECODE_INDEX_PARAMETERS:
      case GUID_ADVANCED_CONTENT_ENCRYPTION:
      case GUID_COMPATIBILITY:
	    case GUID_ASF_PADDING:
        break;
      default:
        lprintf ("unexpected object\n");
        break;
    }
    asf_reader_skip(&reader, object_data_length);
  }
  return 1;
}

static int asf_header_parse_content_description(asf_header_t *header_pub, uint8_t *buffer, int buffer_len) {
  asf_header_internal_t *header = (asf_header_internal_t *)header_pub;
  asf_reader_t reader;
  asf_content_t *content;
  uint16_t title_length = 0, author_length = 0, copyright_length = 0, description_length = 0, rating_length = 0;
  iconv_t iconv_cd;

  if (buffer_len < 10)
    return 0;

  content = calloc(1, sizeof(asf_content_t));
  if (!content)
    return 0;

  if ( (iconv_cd = iconv_open("UTF-8", "UCS-2LE")) == (iconv_t)-1 )
    return 0;

  asf_reader_init(&reader, buffer, buffer_len);
  asf_reader_get_16(&reader, &title_length);
  asf_reader_get_16(&reader, &author_length);
  asf_reader_get_16(&reader, &copyright_length);
  asf_reader_get_16(&reader, &description_length);
  asf_reader_get_16(&reader, &rating_length);

  content->title = asf_reader_get_string(&reader, title_length, iconv_cd);
  content->author = asf_reader_get_string(&reader, author_length, iconv_cd);
  content->copyright = asf_reader_get_string(&reader, copyright_length, iconv_cd);
  content->description = asf_reader_get_string(&reader, description_length, iconv_cd);
  content->rating = asf_reader_get_string(&reader, rating_length, iconv_cd);

  lprintf("title: %d chars: \"%s\"\n", title_length, content->title);
  lprintf("author: %d chars: \"%s\"\n", author_length, content->author);
  lprintf("copyright: %d chars: \"%s\"\n", copyright_length, content->copyright);
  lprintf("description: %d chars: \"%s\"\n", description_length, content->description);
  lprintf("rating: %d chars: \"%s\"\n", rating_length, content->rating);

  header->pub.content = content;

  iconv_close(iconv_cd);
  return 1;
}


asf_header_t *asf_header_new (uint8_t *buffer, int buffer_len) {

  asf_header_internal_t *asf_header;
  asf_reader_t reader;
  uint32_t object_count;
  uint16_t junk;

  asf_header = calloc(1, sizeof(asf_header_internal_t));
  if (!asf_header)
    return NULL;

  lprintf("parsing_asf_header\n");
  if (buffer_len < 6) {
    printf("invalid buffer size\n");
    return NULL;
  }

  if (! (asf_header = calloc(1, sizeof(asf_header_internal_t))) )
    return NULL;

  asf_reader_init(&reader, buffer, buffer_len);
  asf_reader_get_32(&reader, &object_count);
  asf_reader_get_16(&reader, &junk);

  while (!asf_reader_eos(&reader)) {

    GUID guid;
    int object_id;
    uint64_t object_length = 0, object_data_length;

    if (asf_reader_get_size(&reader) < 24) {
      printf("invalid buffer size\n");
      goto exit_error;
    }

    asf_reader_get_guid(&reader, &guid);
    asf_reader_get_64(&reader, &object_length);

    object_data_length = object_length - 24;

    object_id = asf_find_object_id(&guid);
    switch (object_id) {
    
      case GUID_ASF_FILE_PROPERTIES:
        asf_header_parse_file_properties(&asf_header->pub, asf_reader_get_buffer(&reader), object_data_length);
        break;

      case GUID_ASF_STREAM_PROPERTIES:
        asf_header_parse_stream_properties(&asf_header->pub, asf_reader_get_buffer(&reader), object_data_length);
        break;

      case GUID_ASF_STREAM_BITRATE_PROPERTIES:
        asf_header_parse_stream_bitrate_properties(&asf_header->pub, asf_reader_get_buffer(&reader), object_data_length);
        break;

	    case GUID_ASF_HEADER_EXTENSION:
        asf_header_parse_header_extension(&asf_header->pub, asf_reader_get_buffer(&reader), object_data_length);
        break;

	    case GUID_ASF_CONTENT_DESCRIPTION:
        asf_header_parse_content_description(&asf_header->pub, asf_reader_get_buffer(&reader), object_data_length);
        break;

	    case GUID_ASF_CODEC_LIST:
	    case GUID_ASF_SCRIPT_COMMAND:
	    case GUID_ASF_MARKER:
	    case GUID_ASF_BITRATE_MUTUAL_EXCLUSION:
	    case GUID_ASF_ERROR_CORRECTION:
	    case GUID_ASF_EXTENDED_CONTENT_DESCRIPTION:
	    case GUID_ASF_EXTENDED_CONTENT_ENCRYPTION:
	    case GUID_ASF_PADDING:
        break;
    
      default:
        lprintf ("unexpected object\n");
        break;
    }
    asf_reader_skip(&reader, object_data_length);
  }

  /* basic checks */
  if (!asf_header->pub.file) {
    lprintf("no file object present\n");
    goto exit_error;
  }
  if (!asf_header->pub.content) {
    lprintf("no content object present\n");
    asf_header->pub.content = calloc(1, sizeof(asf_content_t));
    if (!asf_header->pub.content)
      goto exit_error;
  }

  return &asf_header->pub;

exit_error:
  asf_header_delete(&asf_header->pub);
  return NULL;
}


static void asf_header_delete_file_properties(asf_file_t *asf_file) {
  free(asf_file);
}

static void asf_header_delete_content(asf_content_t *asf_content) {
  if (asf_content->title)
    free(asf_content->title);
  if (asf_content->author)
    free(asf_content->author);
  if (asf_content->copyright)
    free(asf_content->copyright);
  if (asf_content->description)
    free(asf_content->description);
  if (asf_content->rating)
    free(asf_content->rating);
  free(asf_content);
}

static void asf_header_delete_stream_properties(asf_stream_t *asf_stream) {
  if (asf_stream->private_data)
    free(asf_stream->private_data);
  if (asf_stream->error_correction_data)
    free(asf_stream->error_correction_data);
  free(asf_stream);
}

static void asf_header_delete_stream_extended_properties(asf_stream_extension_t *asf_stream_extension) {
  int i;

  if (asf_stream_extension->stream_name_count > 0) {
    for (i = 0; i < asf_stream_extension->stream_name_count; i++) {
      free(asf_stream_extension->stream_names[i]);
    }
    free(asf_stream_extension->stream_names);
  }
  free(asf_stream_extension);
}

void asf_header_delete (asf_header_t *header_pub) {
  asf_header_internal_t *header = (asf_header_internal_t *)header_pub;
  int i;

  if (header->pub.file)
    asf_header_delete_file_properties(header->pub.file);

  if (header->pub.content)
    asf_header_delete_content(header->pub.content);

  for (i = 0; i < ASF_MAX_NUM_STREAMS; i++) {
    if (header->pub.streams[i])
      asf_header_delete_stream_properties(header->pub.streams[i]);
    if (header->pub.stream_extensions[i])
      asf_header_delete_stream_extended_properties(header->pub.stream_extensions[i]);
  }
  
  free(header);
}

/* Given a bandwidth, select the best stream */
static int asf_header_choose_stream (asf_header_internal_t *header, int stream_type,
                                     uint32_t bandwidth) {
  int i;
  int max_lt, min_gt;

  max_lt = min_gt = -1;
  for (i = 0; i < header->pub.stream_count; i++) {
    if (header->pub.streams[i]->stream_type == stream_type) {
      if (header->pub.bitrates[i] <= bandwidth) {
        if ((max_lt == -1) || (header->pub.bitrates[i] > header->pub.bitrates[max_lt]))
          max_lt = i;
      } else {
        if ((min_gt == -1) || (header->pub.bitrates[i] < header->pub.bitrates[min_gt]))
          min_gt = i;
      }
    }
  }

  return (max_lt != -1) ? max_lt : min_gt;
}

void asf_header_choose_streams (asf_header_t *header_pub, uint32_t bandwidth,
                                int *video_id, int *audio_id) {
  asf_header_internal_t *header = (asf_header_internal_t *)header_pub;
  uint32_t bandwidth_left;

  *video_id = *audio_id = -1;
  bandwidth_left = bandwidth;

  lprintf("%d streams, bandwidth %"PRIu32"\n", header->pub.stream_count, bandwidth_left);

  /* choose a video stream adapted to the user bandwidth */
  *video_id = asf_header_choose_stream (header, GUID_ASF_VIDEO_MEDIA, bandwidth_left);
  if (*video_id != -1) {
    if (header->pub.bitrates[*video_id] < bandwidth_left) {
      bandwidth_left -= header->pub.bitrates[*video_id];
    } else {
      bandwidth_left = 0;
    }
    lprintf("selected video stream %d, bandwidth left: %"PRIu32"\n",
      header->pub.streams[*video_id]->stream_number, bandwidth_left);
  } else {
    lprintf("no video stream\n");
  }

  /* choose a audio stream adapted to the user bandwidth */
  *audio_id = asf_header_choose_stream (header, GUID_ASF_AUDIO_MEDIA, bandwidth_left);
  if (*audio_id != -1) {
    if (header->pub.bitrates[*audio_id] < bandwidth_left) {
      bandwidth_left -= header->pub.bitrates[*audio_id];
    } else {
      bandwidth_left = 0;
    }
    lprintf("selected audio stream %d, bandwidth left: %"PRIu32"\n",
      header->pub.streams[*audio_id]->stream_number, bandwidth_left);
  } else {
    lprintf("no audio stream\n");
  }
}

void asf_header_disable_streams (asf_header_t *header_pub, int video_id, int audio_id) {
  asf_header_internal_t *header = (asf_header_internal_t *)header_pub;
  int i;

  for (i = 0; i < header->pub.stream_count; i++) {
    int stream_type = header->pub.streams[i]->stream_type;

    if (((stream_type == GUID_ASF_VIDEO_MEDIA) && (i != video_id)) ||
      ((stream_type == GUID_ASF_AUDIO_MEDIA) && (i != audio_id))) {
      uint8_t *bitrate_pointer = header->bitrate_pointers[i];
      /* disable  the stream */
      lprintf("stream %d disabled\n", header->pub.streams[i]->stream_number);
      *bitrate_pointer++ = 0;
      *bitrate_pointer++ = 0;
      *bitrate_pointer++ = 0;
      *bitrate_pointer = 0;
    }
  }
}
