/*
 * Copyright (C) 2000-2012 the xine project
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
 *
 * Compact Disc Digital Audio (CDDA) Input Plugin
 *   by Mike Melanson (melanson@pcisys.net)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/types.h>
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#ifdef HAVE_DIRENT_H
#include <dirent.h>
#endif
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#ifdef HAVE_ALLOCA_H
#  include <alloca.h>
#endif

#ifdef HAVE_SYS_IOCTL_H
#  include <sys/ioctl.h>
#else
/* for WIN32 */
#  include <windef.h>
#  include <winioctl.h>
#endif

#include <netdb.h>

#include <signal.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <basedir.h>

#ifdef HAVE_FFMPEG_AVUTIL_H
#  include <base64.h>
#  include <sha1.h>
#else
#  include <libavutil/base64.h>
#  ifdef HAVE_LIBAVUTIL_SHA_H
#    include <libavutil/sha.h>
#  else
#    include <libavutil/sha1.h>
#  endif
#endif

#define LOG_MODULE "input_cdda"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/input_plugin.h>
#include "media_helper.h"

#if defined(__sun)
#define	DEFAULT_CDDA_DEVICE	"/vol/dev/aliases/cdrom0"
#elif defined(WIN32)
#define DEFAULT_CDDA_DEVICE "d:\\"
#elif defined(__OpenBSD__)
#define	DEFAULT_CDDA_DEVICE	"/dev/rcd0c"
#else
#define	DEFAULT_CDDA_DEVICE	"/dev/cdrom"
#endif

#define CDDB_SERVER             "freedb.freedb.org"
#define CDDB_PORT               8880
#define CDDB_PROTOCOL           6
#define CDDB_TIMEOUT            5000

/* CD-relevant defines and data structures */
#define CD_SECONDS_PER_MINUTE   60
#define CD_FRAMES_PER_SECOND    75
#define CD_RAW_FRAME_SIZE       2352
#define CD_LEADOUT_TRACK        0xAA
#define CD_BLOCK_OFFSET         150

#if !defined(HAVE_LIBAVUTIL_SHA_H)
/* old libavutil/sha1.h was found... */
#define AVSHA AVSHA1
#  define av_sha_init(c,b) 	av_sha1_init(c)
#  define av_sha_update		av_sha1_update
#  define av_sha_final		av_sha1_final
#  define av_sha_size		av_sha1_size
#endif

typedef struct _cdrom_toc_entry {
  int   track_mode;
  int   first_frame;
  int   first_frame_minute;
  int   first_frame_second;
  int   first_frame_frame;
  int   total_frames;
} cdrom_toc_entry;

typedef struct _cdrom_toc {
  int   first_track;
  int   last_track;
  int   total_tracks;
  int   ignore_last_track;

  cdrom_toc_entry *toc_entries;
  cdrom_toc_entry leadout_track;  /* need to know where last track ends */
} cdrom_toc;

/**************************************************************************
 * xine interface functions
 *************************************************************************/

#define MAX_TRACKS     99
#define CACHED_FRAMES  100

typedef struct {
  int                  start;
  char                *title;
} trackinfo_t;

typedef struct {
  input_plugin_t       input_plugin;
  input_class_t       *class;
  xine_stream_t       *stream;

  struct  {
    int                enabled;
    char              *server;
    int                port;

    char              *cdiscid;
    char              *disc_title;
    char              *disc_year;
    char              *disc_artist;
    char              *disc_category;

    int                fd;
    uint32_t           disc_id;

    int                disc_length;
    trackinfo_t       *track;
    int                num_tracks;
    int                have_cddb_info;
  } cddb;

  int                  fd;
  int                  net_fd;
  int                  track;
  char                *mrl;
  int                  first_frame;
  int                  current_frame;
  int                  last_frame;

  char                *cdda_device;

  unsigned char        cache[CACHED_FRAMES][CD_RAW_FRAME_SIZE];
  int                  cache_first;
  int                  cache_last;

#ifdef WIN32
    HANDLE h_device_handle;                         /* vcd device descriptor */
  long  hASPI;
  short i_sid;
  long  (*lpSendCommand)( void* );
#endif

} cdda_input_plugin_t;

typedef struct {

  input_class_t        input_class;

  xine_t              *xine;
  config_values_t     *config;

  char                *cdda_device;
  int                  cddb_error;

  cdda_input_plugin_t *ip;

  int                  show_hidden_files;
  char                *origin_path;

  int                  mrls_allocated_entries;
  xine_mrl_t         **mrls;

  char               **autoplaylist;

} cdda_input_class_t;


#ifdef WIN32

/* size of a CD sector */
#define CD_SECTOR_SIZE 2048

/* Win32 DeviceIoControl specifics */
typedef struct _TRACK_DATA {
    UCHAR Reserved;
    UCHAR Control : 4;
    UCHAR Adr : 4;
    UCHAR TrackNumber;
    UCHAR Reserved1;
    UCHAR Address[4];
} TRACK_DATA, *PTRACK_DATA;
typedef struct _CDROM_TOC {
    UCHAR Length[2];
    UCHAR FirstTrack;
    UCHAR LastTrack;
    TRACK_DATA TrackData[MAX_TRACKS+1];
} CDROM_TOC, *PCDROM_TOC;
typedef enum _TRACK_MODE_TYPE {
    YellowMode2,
    XAForm2,
    CDDA
} TRACK_MODE_TYPE, *PTRACK_MODE_TYPE;
typedef struct __RAW_READ_INFO {
    LARGE_INTEGER DiskOffset;
    ULONG SectorCount;
    TRACK_MODE_TYPE TrackMode;
} RAW_READ_INFO, *PRAW_READ_INFO;

#ifndef IOCTL_CDROM_BASE
#    define IOCTL_CDROM_BASE FILE_DEVICE_CD_ROM
#endif
#ifndef IOCTL_CDROM_READ_TOC
#    define IOCTL_CDROM_READ_TOC CTL_CODE(IOCTL_CDROM_BASE, 0x0000, \
                                          METHOD_BUFFERED, FILE_READ_ACCESS)
#endif
#ifndef IOCTL_CDROM_RAW_READ
#define IOCTL_CDROM_RAW_READ CTL_CODE(IOCTL_CDROM_BASE, 0x000F, \
                                      METHOD_OUT_DIRECT, FILE_READ_ACCESS)
#endif

/* Win32 aspi specific */
#define WIN_NT               ( GetVersion() < 0x80000000 )
#define ASPI_HAID           0
#define ASPI_TARGET         0
#define DTYPE_CDROM         0x05

#define SENSE_LEN           0x0E
#define SC_GET_DEV_TYPE     0x01
#define SC_EXEC_SCSI_CMD    0x02
#define SC_GET_DISK_INFO    0x06
#define SS_COMP             0x01
#define SS_PENDING          0x00
#define SS_NO_ADAPTERS      0xE8
#define SRB_DIR_IN          0x08
#define SRB_DIR_OUT         0x10
#define SRB_EVENT_NOTIFY    0x40

#define READ_CD 0xbe
#define SECTOR_TYPE_MODE2 0x14
#define READ_CD_USERDATA_MODE2 0x10

#define READ_TOC 0x43
#define READ_TOC_FORMAT_TOC 0x0

#pragma pack(1)

struct SRB_GetDiskInfo
{
    unsigned char   SRB_Cmd;
    unsigned char   SRB_Status;
    unsigned char   SRB_HaId;
    unsigned char   SRB_Flags;
    unsigned long   SRB_Hdr_Rsvd;
    unsigned char   SRB_Target;
    unsigned char   SRB_Lun;
    unsigned char   SRB_DriveFlags;
    unsigned char   SRB_Int13HDriveInfo;
    unsigned char   SRB_Heads;
    unsigned char   SRB_Sectors;
    unsigned char   SRB_Rsvd1[22];
};

struct SRB_GDEVBlock
{
    unsigned char SRB_Cmd;
    unsigned char SRB_Status;
    unsigned char SRB_HaId;
    unsigned char SRB_Flags;
    unsigned long SRB_Hdr_Rsvd;
    unsigned char SRB_Target;
    unsigned char SRB_Lun;
    unsigned char SRB_DeviceType;
    unsigned char SRB_Rsvd1;
};

struct SRB_ExecSCSICmd
{
    unsigned char   SRB_Cmd;
    unsigned char   SRB_Status;
    unsigned char   SRB_HaId;
    unsigned char   SRB_Flags;
    unsigned long   SRB_Hdr_Rsvd;
    unsigned char   SRB_Target;
    unsigned char   SRB_Lun;
    unsigned short  SRB_Rsvd1;
    unsigned long   SRB_BufLen;
    unsigned char   *SRB_BufPointer;
    unsigned char   SRB_SenseLen;
    unsigned char   SRB_CDBLen;
    unsigned char   SRB_HaStat;
    unsigned char   SRB_TargStat;
    unsigned long   *SRB_PostProc;
    unsigned char   SRB_Rsvd2[20];
    unsigned char   CDBByte[16];
    unsigned char   SenseArea[SENSE_LEN+2];
};

#pragma pack()

#endif /* WIN32 */

#ifdef LOG
static void print_cdrom_toc(cdrom_toc *toc) {

  int i;
  int time1;
  int time2;
  int timediff;

  printf("\ntoc:\n");
  printf("\tfirst track  = %d\n", toc->first_track);
  printf("\tlast track   = %d\n", toc->last_track);
  printf("\ttotal tracks = %d\n", toc->total_tracks);
  printf("\ntoc entries:\n");


  printf("leadout track: Control: %d MSF: %02d:%02d:%04d, first frame = %d\n",
	 toc->leadout_track.track_mode,
	 toc->leadout_track.first_frame_minute,
	 toc->leadout_track.first_frame_second,
	 toc->leadout_track.first_frame_frame,
	 toc->leadout_track.first_frame);

  /* fetch each toc entry */
  if (toc->first_track > 0) {
    for (i = toc->first_track; i <= toc->last_track; i++) {
      printf("\ttrack mode = %d", toc->toc_entries[i-1].track_mode);
      printf("\ttrack %d, audio, MSF: %02d:%02d:%02d, first frame = %d\n",
	     i,
	     toc->toc_entries[i-1].first_frame_minute,
	     toc->toc_entries[i-1].first_frame_second,
	     toc->toc_entries[i-1].first_frame_frame,
	     toc->toc_entries[i-1].first_frame);

      time1 = ((toc->toc_entries[i-1].first_frame_minute * 60) +
	       toc->toc_entries[i-1].first_frame_second);

      if (i == toc->last_track) {
	time2 = ((toc->leadout_track.first_frame_minute * 60) +
		 toc->leadout_track.first_frame_second);
      }
      else {
	time2 = ((toc->toc_entries[i].first_frame_minute * 60) +
		 toc->toc_entries[i].first_frame_second);
      }

      timediff = time2 - time1;

      printf("\t time: %02d:%02d\n", timediff/60, timediff%60);
    }
  }
}
#endif

static cdrom_toc * init_cdrom_toc(void) {

  cdrom_toc *toc;

  toc = calloc(1, sizeof (cdrom_toc));
  toc->first_track = toc->last_track = toc->total_tracks = 0;
  toc->toc_entries = NULL;

  return toc;
}

static void free_cdrom_toc(cdrom_toc *toc) {

  if ( toc ) {
    free(toc->toc_entries);
    free(toc);
  }
}

#if defined (__linux__)

#include <linux/cdrom.h>

static int read_cdrom_toc(int fd, cdrom_toc *toc) {

  struct cdrom_tochdr tochdr;
  struct cdrom_tocentry tocentry;
  struct cdrom_multisession ms;
  int i;

  /* fetch the table of contents */
  if (ioctl(fd, CDROMREADTOCHDR, &tochdr) == -1) {
    perror("CDROMREADTOCHDR");
    return -1;
  }

  ms.addr_format = CDROM_LBA;
  if (ioctl(fd, CDROMMULTISESSION, &ms) == -1) {
    perror("CDROMMULTISESSION");
    return -1;
  }

  toc->first_track = tochdr.cdth_trk0;
  toc->last_track = tochdr.cdth_trk1;
  if (ms.xa_flag) {
    toc->ignore_last_track = 1;
  } else {
    toc->ignore_last_track = 0;
  }
  toc->total_tracks = toc->last_track - toc->first_track + 1;

  /* allocate space for the toc entries */
  toc->toc_entries = calloc(toc->total_tracks, sizeof(cdrom_toc_entry));
  if (!toc->toc_entries) {
    perror("calloc");
    return -1;
  }

  /* fetch each toc entry */
  for (i = toc->first_track; i <= toc->last_track; i++) {

    memset(&tocentry, 0, sizeof(tocentry));

    tocentry.cdte_track = i;
    tocentry.cdte_format = CDROM_MSF;
    if (ioctl(fd, CDROMREADTOCENTRY, &tocentry) == -1) {
      perror("CDROMREADTOCENTRY");
      return -1;
    }

    toc->toc_entries[i-1].track_mode = (tocentry.cdte_ctrl & 0x04) ? 1 : 0;
    toc->toc_entries[i-1].first_frame_minute = tocentry.cdte_addr.msf.minute;
    toc->toc_entries[i-1].first_frame_second = tocentry.cdte_addr.msf.second;
    toc->toc_entries[i-1].first_frame_frame = tocentry.cdte_addr.msf.frame;
    toc->toc_entries[i-1].first_frame =
      (tocentry.cdte_addr.msf.minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
      (tocentry.cdte_addr.msf.second * CD_FRAMES_PER_SECOND) +
       tocentry.cdte_addr.msf.frame;
  }

  /* fetch the leadout as well */
  memset(&tocentry, 0, sizeof(tocentry));

  tocentry.cdte_track = CD_LEADOUT_TRACK;
  tocentry.cdte_format = CDROM_MSF;
  if (ioctl(fd, CDROMREADTOCENTRY, &tocentry) == -1) {
    perror("CDROMREADTOCENTRY");
    return -1;
  }

#define XA_INTERVAL ((60 + 90 + 2) * CD_FRAMES)

  toc->leadout_track.track_mode = (tocentry.cdte_ctrl & 0x04) ? 1 : 0;
  toc->leadout_track.first_frame_minute = tocentry.cdte_addr.msf.minute;
  toc->leadout_track.first_frame_second = tocentry.cdte_addr.msf.second;
  toc->leadout_track.first_frame_frame = tocentry.cdte_addr.msf.frame;
  if (!ms.xa_flag) {
    toc->leadout_track.first_frame =
      (tocentry.cdte_addr.msf.minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
      (tocentry.cdte_addr.msf.second * CD_FRAMES_PER_SECOND) +
       tocentry.cdte_addr.msf.frame;
  } else {
    toc->leadout_track.first_frame = ms.addr.lba - XA_INTERVAL + 150;
  }

  return 0;
}

static int read_cdrom_frames(cdda_input_plugin_t *this_gen, int frame, int num_frames,
  unsigned char *data) {

  int fd = this_gen->fd;
  struct cdrom_msf msf;

  while( num_frames ) {
    /* read from starting frame... */
    msf.cdmsf_min0 = frame / CD_SECONDS_PER_MINUTE / CD_FRAMES_PER_SECOND;
    msf.cdmsf_sec0 = (frame / CD_FRAMES_PER_SECOND) % CD_SECONDS_PER_MINUTE;
    msf.cdmsf_frame0 = frame % CD_FRAMES_PER_SECOND;

    /* read until ending track (starting frame + 1)... */
    msf.cdmsf_min1 = (frame + 1) / CD_SECONDS_PER_MINUTE / CD_FRAMES_PER_SECOND;
    msf.cdmsf_sec1 = ((frame + 1) / CD_FRAMES_PER_SECOND) % CD_SECONDS_PER_MINUTE;
    msf.cdmsf_frame1 = (frame + 1) % CD_FRAMES_PER_SECOND;

    /* MSF structure is the input to the ioctl */
    memcpy(data, &msf, sizeof(msf));

    /* read a frame */
    if(ioctl(fd, CDROMREADRAW, data, data) < 0) {
      perror("CDROMREADRAW");
      return -1;
    }

    data += CD_RAW_FRAME_SIZE;
    frame++;
    num_frames--;
  }
  return 0;
}

#elif defined(__sun)

#include <sys/cdio.h>

static int read_cdrom_toc(int fd, cdrom_toc *toc) {

  struct cdrom_tochdr tochdr;
  struct cdrom_tocentry tocentry;
  int i;

  /* fetch the table of contents */
  if (ioctl(fd, CDROMREADTOCHDR, &tochdr) == -1) {
    perror("CDROMREADTOCHDR");
    return -1;
  }

  toc->first_track = tochdr.cdth_trk0;
  toc->last_track = tochdr.cdth_trk1;
  toc->total_tracks = toc->last_track - toc->first_track + 1;

  /* allocate space for the toc entries */
  toc->toc_entries = calloc(toc->total_tracks, sizeof(cdrom_toc_entry));
  if (!toc->toc_entries) {
    perror("calloc");
    return -1;
  }

  /* fetch each toc entry */
  for (i = toc->first_track; i <= toc->last_track; i++) {

    memset(&tocentry, 0, sizeof(tocentry));

    tocentry.cdte_track = i;
    tocentry.cdte_format = CDROM_MSF;
    if (ioctl(fd, CDROMREADTOCENTRY, &tocentry) == -1) {
      perror("CDROMREADTOCENTRY");
      return -1;
    }

    toc->toc_entries[i-1].track_mode = (tocentry.cdte_ctrl & 0x04) ? 1 : 0;
    toc->toc_entries[i-1].first_frame_minute = tocentry.cdte_addr.msf.minute;
    toc->toc_entries[i-1].first_frame_second = tocentry.cdte_addr.msf.second;
    toc->toc_entries[i-1].first_frame_frame = tocentry.cdte_addr.msf.frame;
    toc->toc_entries[i-1].first_frame =
      (tocentry.cdte_addr.msf.minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
      (tocentry.cdte_addr.msf.second * CD_FRAMES_PER_SECOND) +
       tocentry.cdte_addr.msf.frame;
  }

  if (tocentry.cdte_ctrl & CDROM_DATA_TRACK) {
    toc->ignore_last_track = 1;
  }
  else {
    toc->ignore_last_track = 0;
  }

  /* fetch the leadout as well */
  memset(&tocentry, 0, sizeof(tocentry));

  tocentry.cdte_track = CD_LEADOUT_TRACK;
  tocentry.cdte_format = CDROM_MSF;
  if (ioctl(fd, CDROMREADTOCENTRY, &tocentry) == -1) {
    perror("CDROMREADTOCENTRY");
    return -1;
  }

  toc->leadout_track.track_mode = (tocentry.cdte_ctrl & 0x04) ? 1 : 0;
  toc->leadout_track.first_frame_minute = tocentry.cdte_addr.msf.minute;
  toc->leadout_track.first_frame_second = tocentry.cdte_addr.msf.second;
  toc->leadout_track.first_frame_frame = tocentry.cdte_addr.msf.frame;
  toc->leadout_track.first_frame =
    (tocentry.cdte_addr.msf.minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
    (tocentry.cdte_addr.msf.second * CD_FRAMES_PER_SECOND) +
     tocentry.cdte_addr.msf.frame;

  return 0;
}

static int read_cdrom_frames(cdda_input_plugin_t *this_gen, int frame, int num_frames,
  unsigned char *data) {

  int fd = this_gen->fd;
  struct cdrom_cdda cdda;

  while( num_frames ) {
    cdda.cdda_addr = frame - 2 * CD_FRAMES_PER_SECOND;
    cdda.cdda_length = 1;
    cdda.cdda_data = data;
    cdda.cdda_subcode = CDROM_DA_NO_SUBCODE;

    /* read a frame */
    if(ioctl(fd, CDROMCDDA, &cdda) < 0) {
      perror("CDROMCDDA");
      return -1;
    }

    data += CD_RAW_FRAME_SIZE;
    frame++;
    num_frames--;
  }
  return 0;
}

#elif defined(__FreeBSD_kernel__) || defined(__NetBSD__) || defined(__OpenBSD__)

#include <sys/cdio.h>

#ifdef HAVE_SYS_SCSIIO_H
#include <sys/scsiio.h>
#endif

static int read_cdrom_toc(int fd, cdrom_toc *toc) {

  struct ioc_toc_header tochdr;
#if defined(__FreeBSD_kernel__)
  struct ioc_read_toc_single_entry tocentry;
#elif defined(__NetBSD__) || defined(__OpenBSD__)
  struct ioc_read_toc_entry tocentry;
  struct cd_toc_entry data;
#endif
  int i;

  /* fetch the table of contents */
  if (ioctl(fd, CDIOREADTOCHEADER, &tochdr) == -1) {
    perror("CDIOREADTOCHEADER");
    return -1;
  }

  toc->first_track = tochdr.starting_track;
  toc->last_track = tochdr.ending_track;
  toc->total_tracks = toc->last_track - toc->first_track + 1;

  /* allocate space for the toc entries */
  toc->toc_entries = calloc(toc->total_tracks, sizeof(cdrom_toc_entry));
  if (!toc->toc_entries) {
    perror("calloc");
    return -1;
  }

  /* fetch each toc entry */
  for (i = toc->first_track; i <= toc->last_track; i++) {

    memset(&tocentry, 0, sizeof(tocentry));

#if defined(__FreeBSD_kernel__)
    tocentry.track = i;
    tocentry.address_format = CD_MSF_FORMAT;
    if (ioctl(fd, CDIOREADTOCENTRY, &tocentry) == -1) {
      perror("CDIOREADTOCENTRY");
      return -1;
    }
#elif defined(__NetBSD__) || defined(__OpenBSD__)
    memset(&data, 0, sizeof(data));
    tocentry.data_len = sizeof(data);
    tocentry.data = &data;
    tocentry.starting_track = i;
    tocentry.address_format = CD_MSF_FORMAT;
    if (ioctl(fd, CDIOREADTOCENTRYS, &tocentry) == -1) {
      perror("CDIOREADTOCENTRYS");
      return -1;
    }
#endif

#if defined(__FreeBSD_kernel__)
    toc->toc_entries[i-1].track_mode = (tocentry.entry.control & 0x04) ? 1 : 0;
    toc->toc_entries[i-1].first_frame_minute = tocentry.entry.addr.msf.minute;
    toc->toc_entries[i-1].first_frame_second = tocentry.entry.addr.msf.second;
    toc->toc_entries[i-1].first_frame_frame = tocentry.entry.addr.msf.frame;
    toc->toc_entries[i-1].first_frame =
      (tocentry.entry.addr.msf.minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
      (tocentry.entry.addr.msf.second * CD_FRAMES_PER_SECOND) +
       tocentry.entry.addr.msf.frame;
#elif defined(__NetBSD__) || defined(__OpenBSD__)
    toc->toc_entries[i-1].track_mode = (tocentry.data->control & 0x04) ? 1 : 0;
    toc->toc_entries[i-1].first_frame_minute = tocentry.data->addr.msf.minute;
    toc->toc_entries[i-1].first_frame_second = tocentry.data->addr.msf.second;
    toc->toc_entries[i-1].first_frame_frame = tocentry.data->addr.msf.frame;
    toc->toc_entries[i-1].first_frame =
      (tocentry.data->addr.msf.minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
      (tocentry.data->addr.msf.second * CD_FRAMES_PER_SECOND) +
       tocentry.data->addr.msf.frame - CD_BLOCK_OFFSET;
#endif
  }

  /* fetch the leadout as well */
  memset(&tocentry, 0, sizeof(tocentry));

#if defined(__FreeBSD_kernel__)
  tocentry.track = CD_LEADOUT_TRACK;
  tocentry.address_format = CD_MSF_FORMAT;
  if (ioctl(fd, CDIOREADTOCENTRY, &tocentry) == -1) {
    perror("CDIOREADTOCENTRY");
    return -1;
  }
#elif defined(__NetBSD__) || defined(__OpenBSD__)
  memset(&data, 0, sizeof(data));
  tocentry.data_len = sizeof(data);
  tocentry.data = &data;
  tocentry.starting_track = CD_LEADOUT_TRACK;
  tocentry.address_format = CD_MSF_FORMAT;
  if (ioctl(fd, CDIOREADTOCENTRYS, &tocentry) == -1) {
    perror("CDIOREADTOCENTRYS");
    return -1;
  }
#endif

#if defined(__FreeBSD_kernel__)
  toc->leadout_track.track_mode = (tocentry.entry.control & 0x04) ? 1 : 0;
  toc->leadout_track.first_frame_minute = tocentry.entry.addr.msf.minute;
  toc->leadout_track.first_frame_second = tocentry.entry.addr.msf.second;
  toc->leadout_track.first_frame_frame = tocentry.entry.addr.msf.frame;
  toc->leadout_track.first_frame =
    (tocentry.entry.addr.msf.minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
    (tocentry.entry.addr.msf.second * CD_FRAMES_PER_SECOND) +
     tocentry.entry.addr.msf.frame;
#elif defined(__NetBSD__) || defined(__OpenBSD__)
  toc->leadout_track.track_mode = (tocentry.data->control & 0x04) ? 1 : 0;
  toc->leadout_track.first_frame_minute = tocentry.data->addr.msf.minute;
  toc->leadout_track.first_frame_second = tocentry.data->addr.msf.second;
  toc->leadout_track.first_frame_frame = tocentry.data->addr.msf.frame;
  toc->leadout_track.first_frame =
    (tocentry.data->addr.msf.minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
    (tocentry.data->addr.msf.second * CD_FRAMES_PER_SECOND) +
     tocentry.data->addr.msf.frame - CD_BLOCK_OFFSET;
#endif

  return 0;
}

static int read_cdrom_frames(cdda_input_plugin_t *this_gen, int frame, int num_frames,
  unsigned char *data) {

  int fd = this_gen->fd;

  while( num_frames ) {
#if defined(__FreeBSD_kernel__)
#if __FreeBSD_kernel_version < 501106
    struct ioc_read_audio cdda;

    cdda.address_format = CD_MSF_FORMAT;
    cdda.address.msf.minute = frame / CD_SECONDS_PER_MINUTE / CD_FRAMES_PER_SECOND;
    cdda.address.msf.second = (frame / CD_FRAMES_PER_SECOND) % CD_SECONDS_PER_MINUTE;
    cdda.address.msf.frame = frame % CD_FRAMES_PER_SECOND;
    cdda.nframes = 1;
    cdda.buffer = data;
    /* read a frame */
    if(ioctl(fd, CDIOCREADAUDIO, &cdda) < 0) {
#else
    if (pread(fd, data, CD_RAW_FRAME_SIZE, frame * CD_RAW_FRAME_SIZE) != CD_RAW_FRAME_SIZE) {
#endif
      perror("CDIOCREADAUDIO");
      return -1;
    }
#elif defined(__NetBSD__) || defined(__OpenBSD__)
    scsireq_t req;
    int nblocks = 1;

    memset(&req, 0, sizeof(req));
    req.cmd[0] = 0xbe;
    req.cmd[1] = 0;
    req.cmd[2] = (frame >> 24) & 0xff;
    req.cmd[3] = (frame >> 16) & 0xff;
    req.cmd[4] = (frame >> 8) & 0xff;
    req.cmd[5] = (frame >> 0) & 0xff;
    req.cmd[6] = (nblocks >> 16) & 0xff;
    req.cmd[7] = (nblocks >> 8) & 0xff;
    req.cmd[8] = (nblocks >> 0) & 0xff;
    req.cmd[9] = 0x78;
    req.cmdlen = 10;

    req.datalen = nblocks * CD_RAW_FRAME_SIZE;
    req.databuf = data;
    req.timeout = 10000;
    req.flags = SCCMD_READ;

    if(ioctl(fd, SCIOCCOMMAND, &req) < 0) {
      perror("SCIOCCOMMAND");
      return -1;
    }
#endif

    data += CD_RAW_FRAME_SIZE;
    frame++;
    num_frames--;
  }
  return 0;
}

#elif defined(WIN32)

static int read_cdrom_toc(cdda_input_plugin_t *this_gen, cdrom_toc *toc) {

  if( this_gen->hASPI )
  {
    /* This is for ASPI which obviously isn't supported! */
    lprintf("Windows ASPI support is not complete yet!\n");
    return -1;

  }
  else
    {
      DWORD dwBytesReturned;
      CDROM_TOC cdrom_toc;
      int i;

      if( DeviceIoControl( this_gen->h_device_handle,
			   IOCTL_CDROM_READ_TOC,
			   NULL, 0, &cdrom_toc, sizeof(CDROM_TOC),
			   &dwBytesReturned, NULL ) == 0 )
	{
#ifdef LOG
	  DWORD dw;
	  printf( "input_cdda: could not read TOCHDR\n" );
	  dw = GetLastError();
	  printf("GetLastError returned %u\n", dw);
#endif
	  return -1;
	}

      toc->first_track = cdrom_toc.FirstTrack;
      toc->last_track = cdrom_toc.LastTrack;
      toc->total_tracks = toc->last_track - toc->first_track + 1;


      /* allocate space for the toc entries */
      toc->toc_entries = calloc(toc->total_tracks, sizeof(cdrom_toc_entry));
      if (!toc->toc_entries) {
          perror("calloc");
          return -1;
      }


      /* fetch each toc entry */
      for (i = toc->first_track; i <= toc->last_track; i++) {

          toc->toc_entries[i-1].track_mode = (cdrom_toc.TrackData[i-1].Control & 0x04) ? 1 : 0;
          toc->toc_entries[i-1].first_frame_minute = cdrom_toc.TrackData[i-1].Address[1];
          toc->toc_entries[i-1].first_frame_second = cdrom_toc.TrackData[i-1].Address[2];
          toc->toc_entries[i-1].first_frame_frame = cdrom_toc.TrackData[i-1].Address[3];

          toc->toc_entries[i-1].first_frame =
              (toc->toc_entries[i-1].first_frame_minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
              (toc->toc_entries[i-1].first_frame_second * CD_FRAMES_PER_SECOND) +
              toc->toc_entries[i-1].first_frame_frame;
      }

	  /* Grab the leadout track too! (I think that this is correct?) */
	  i = toc->total_tracks;
      toc->leadout_track.track_mode = (cdrom_toc.TrackData[i].Control & 0x04) ? 1 : 0;
      toc->leadout_track.first_frame_minute = cdrom_toc.TrackData[i].Address[1];
      toc->leadout_track.first_frame_second = cdrom_toc.TrackData[i].Address[2];
      toc->leadout_track.first_frame_frame = cdrom_toc.TrackData[i].Address[3];
      toc->leadout_track.first_frame =
        (toc->leadout_track.first_frame_minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
        (toc->leadout_track.first_frame_second * CD_FRAMES_PER_SECOND) +
         toc->leadout_track.first_frame_frame;
  }

  return 0;
}


static int read_cdrom_frames(cdda_input_plugin_t *this_gen, int frame, int num_frames,
  unsigned char *data) {

  DWORD dwBytesReturned;
  RAW_READ_INFO raw_read_info;

  if( this_gen->hASPI )
  {
	  /* This is for ASPI which obviously isn't supported! */
    lprintf("Windows ASPI support is not complete yet!\n");
    return -1;

  }
  else
    {
      memset(data, 0, CD_RAW_FRAME_SIZE * num_frames);

      while( num_frames ) {

#ifdef LOG
	/*printf("\t Raw read frame %d\n", frame);*/
#endif
	raw_read_info.DiskOffset.QuadPart = frame * CD_SECTOR_SIZE;
	raw_read_info.SectorCount = 1;
	raw_read_info.TrackMode = CDDA;

	/* read a frame */
	if( DeviceIoControl( this_gen->h_device_handle,
			     IOCTL_CDROM_RAW_READ,
			     &raw_read_info, sizeof(RAW_READ_INFO), data,
			     CD_RAW_FRAME_SIZE,
			     &dwBytesReturned, NULL ) == 0 )
	  {
#ifdef LOG
	    DWORD dw;
	    printf( "input_cdda: could not read frame\n" );
	    dw = GetLastError();
	    printf("GetLastError returned %u\n", dw);
#endif
	    return -1;
	  }

	data += CD_RAW_FRAME_SIZE;
	frame++;
	num_frames--;
      }
    }
  return 0;
}

#else



static int read_cdrom_toc(int fd, cdrom_toc *toc) {
  /* read_cdrom_toc is not supported on other platforms */
  return -1;
}


static int read_cdrom_frames(cdda_input_plugin_t *this_gen, int frame, int num_frames,
  unsigned char *data) {
  return -1;
}

#endif


/**************************************************************************
 * network support functions. plays audio cd over the network.
 * see xine-lib/misc/cdda_server.c for the server application
 *************************************************************************/

#define _BUFSIZ 300


#ifndef WIN32
static int parse_url (char *urlbuf, char** host, int *port) {
  char   *start = NULL;
  char   *portcolon = NULL;

  if (host != NULL)
    *host = NULL;

  if (port != NULL)
    *port = 0;

  start = strstr(urlbuf, "://");
  if (start != NULL)
    start += 3;
  else
    start = urlbuf;

  while( *start == '/' )
    start++;

  portcolon = strchr(start, ':');

  if (host != NULL)
    *host = start;

  if (portcolon != NULL)
  {
    *portcolon = '\0';

    if (port != NULL)
        *port = atoi(portcolon + 1);
  }

  return 0;
}
#endif

static int XINE_FORMAT_PRINTF(4, 5)
network_command( xine_stream_t *stream, int socket, void *data_buf, const char *msg, ...)
{
  char     buf[_BUFSIZ];
  va_list  args;
  int      ret, n;

  va_start(args, msg);
  vsnprintf(buf, _BUFSIZ - 1, msg, args);
  va_end(args);

  /* Each line sent is '\n' terminated */
  if( buf[strlen(buf) - 1] != '\n' )
    strcat(buf, "\n");

  if( _x_io_tcp_write(stream, socket, buf, strlen(buf)) < (int)strlen(buf) )
  {
    if (stream)
      xprintf(stream->xine, XINE_VERBOSITY_DEBUG, "input_cdda: error writing to socket.\n");
    return -1;
  }

  if (_x_io_tcp_read_line(stream, socket, buf, _BUFSIZ) <= 0)
  {
    if (stream)
      xprintf(stream->xine, XINE_VERBOSITY_DEBUG, "input_cdda: error reading from socket.\n");
    return -1;
  }

  sscanf(buf, "%d %d", &ret, &n );

  if( n ) {
    if( !data_buf ) {
      if (stream)
        xprintf(stream->xine, XINE_VERBOSITY_DEBUG,
                "input_cdda: protocol error, data returned but no buffer provided.\n");
      return -1;
    }
      if ( _x_io_tcp_read(stream, socket, data_buf, n) < n )
      return -1;
  } else if ( data_buf ) {

    strcpy( data_buf, buf );
  }

  return ret;
}


#ifndef WIN32
static int network_connect(xine_stream_t *stream, const char *got_url )
{
  char *host;
  int port;
  int fd;

  char *url = strdup(got_url);
  parse_url(url, &host, &port);

  if( !host || !strlen(host) || !port )
  {
    free(url);
    return -1;
  }

  fd = _x_io_tcp_connect(stream, host, port);
  lprintf("TTTcosket=%d\n", fd);
  free(url);

  if( fd != -1 ) {
    if( network_command(stream, fd, NULL, "cdda_open") < 0 ) {
      xprintf(stream->xine, XINE_VERBOSITY_DEBUG, "input_cdda: error opening remote drive.\n");
      close(fd);
      return -1;
    }
  }
  return fd;
}

static int network_read_cdrom_toc(xine_stream_t *stream, int fd, cdrom_toc *toc) {

  char buf[_BUFSIZ];
  int i;

  /* fetch the table of contents */
  if( network_command(stream, fd, buf, "cdda_tochdr" ) == -1) {
    if (stream)
      xprintf(stream->xine, XINE_VERBOSITY_DEBUG, "input_cdda: network CDROMREADTOCHDR error.\n");
    return -1;
  }

  sscanf(buf,"%*s %*s %d %d", &toc->first_track, &toc->last_track);
  toc->total_tracks = toc->last_track - toc->first_track + 1;

  /* allocate space for the toc entries */
  toc->toc_entries = calloc(toc->total_tracks, sizeof(cdrom_toc_entry));
  if (!toc->toc_entries) {
    perror("calloc");
    return -1;
  }

  /* fetch each toc entry */
  for (i = toc->first_track; i <= toc->last_track; i++) {

    /* fetch the table of contents */
    if( network_command( stream, fd, buf, "cdda_tocentry %d", i ) == -1) {
      if (stream)
        xprintf(stream->xine, XINE_VERBOSITY_DEBUG, "input_cdda: network CDROMREADTOCENTRY error.\n");
      return -1;
    }

    sscanf(buf,"%*s %*s %d %d %d %d", &toc->toc_entries[i-1].track_mode,
                                      &toc->toc_entries[i-1].first_frame_minute,
                                      &toc->toc_entries[i-1].first_frame_second,
                                      &toc->toc_entries[i-1].first_frame_frame);

    toc->toc_entries[i-1].first_frame =
      (toc->toc_entries[i-1].first_frame_minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
      (toc->toc_entries[i-1].first_frame_second * CD_FRAMES_PER_SECOND) +
       toc->toc_entries[i-1].first_frame_frame;
  }

  /* fetch the leadout as well */
  if( network_command( stream, fd, buf, "cdda_tocentry %d", CD_LEADOUT_TRACK ) == -1) {
    if (stream)
      xprintf(stream->xine, XINE_VERBOSITY_DEBUG, "input_cdda: network CDROMREADTOCENTRY error.\n");
    return -1;
  }

  sscanf(buf,"%*s %*s %d %d %d %d", &toc->leadout_track.track_mode,
                                    &toc->leadout_track.first_frame_minute,
                                    &toc->leadout_track.first_frame_second,
                                    &toc->leadout_track.first_frame_frame);
  toc->leadout_track.first_frame =
    (toc->leadout_track.first_frame_minute * CD_SECONDS_PER_MINUTE * CD_FRAMES_PER_SECOND) +
    (toc->leadout_track.first_frame_second * CD_FRAMES_PER_SECOND) +
     toc->leadout_track.first_frame_frame;

  return 0;
}
#endif /* WIN32 */


static int network_read_cdrom_frames(xine_stream_t *stream, int fd, int first_frame, int num_frames,
  unsigned char data[CD_RAW_FRAME_SIZE]) {

  return network_command( stream, fd, data, "cdda_read %d %d", first_frame, num_frames );
}



/*
 * **************** CDDB *********************
 */
/*
 * Config callbacks
 */
static void cdda_device_cb(void *data, xine_cfg_entry_t *cfg) {
  cdda_input_class_t *class = (cdda_input_class_t *) data;

  class->cdda_device = cfg->str_value;
}
static void enable_cddb_changed_cb(void *data, xine_cfg_entry_t *cfg) {
  cdda_input_class_t *class = (cdda_input_class_t *) data;

  if(class->ip) {
    cdda_input_plugin_t *this = class->ip;

    if (this->cddb.enabled != cfg->num_value)
      class->cddb_error = 0;
    this->cddb.enabled = cfg->num_value;
  }
}
static void server_changed_cb(void *data, xine_cfg_entry_t *cfg) {
  cdda_input_class_t *class = (cdda_input_class_t *) data;

  if(class->ip) {
    cdda_input_plugin_t *this = class->ip;

    if (!this->cddb.server || (strcmp(this->cddb.server, cfg->str_value) != 0))
      class->cddb_error = 0;
    this->cddb.server = cfg->str_value;
  }
}
static void port_changed_cb(void *data, xine_cfg_entry_t *cfg) {
  cdda_input_class_t *class = (cdda_input_class_t *) data;

  if(class->ip) {
    cdda_input_plugin_t *this = class->ip;

    if (this->cddb.port != cfg->num_value)
      class->cddb_error = 0;
    this->cddb.port = cfg->num_value;
  }
}
#ifdef CDROM_SELECT_SPEED
static void speed_changed_cb(void *data, xine_cfg_entry_t *cfg) {
  cdda_input_class_t *class = (cdda_input_class_t *) data;

  if (class->ip) {
    cdda_input_plugin_t *this = class->ip;
    if (this->fd != -1)
      if (ioctl(this->fd, CDROM_SELECT_SPEED, cfg->num_value) != 0)
        xprintf(class->xine, XINE_VERBOSITY_DEBUG,
          "input_cdda: setting drive speed to %d failed\n", cfg->num_value);
  }
}
#endif

/*
 * Return 1 if CD has been changed, 0 of not, -1 on error.
 */
static int _cdda_is_cd_changed(cdda_input_plugin_t *this) {
#ifdef CDROM_MEDIA_CHANGED
  int err, cd_changed=0;

  if(this == NULL || this->fd < 0)
    return -1;

  if((err = ioctl(this->fd, CDROM_MEDIA_CHANGED, cd_changed)) < 0) {
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	    "input_cdda: ioctl(CDROM_MEDIA_CHANGED) failed: %s.\n", strerror(errno));
    return -1;
  }

  switch(err) {
  case 1:
    return 1;
    break;

  default:
    return 0;
    break;
  }

  return -1;
#else
  /*
   * At least on solaris, CDROM_MEDIA_CHANGED does not exist. Just return an
   * error for now
   */
  return -1;
#endif
}

/*
 * create a directory, in safe mode
 */
static void _cdda_mkdir_safe(xine_t *xine, char *path) {

  if(path == NULL)
    return;

#ifndef WIN32
  {
    struct stat  pstat;

    if((stat(path, &pstat)) < 0) {
      /* file or directory no exist, create it */
      if(mkdir(path, 0755) < 0) {
	xprintf(xine, XINE_VERBOSITY_DEBUG,
		"input_cdda: mkdir(%s) failed: %s.\n", path, strerror(errno));
	return;
      }
    }
    else {
      /* Check of found file is a directory file */
      if(!S_ISDIR(pstat.st_mode)) {
	xprintf(xine, XINE_VERBOSITY_DEBUG, "input_cdda: %s is not a directory.\n", path);
      }
    }
  }
#else
  {
    HANDLE          hList;
    TCHAR           szDir[MAX_PATH+3];
    WIN32_FIND_DATA FileData;

    // Get the proper directory path
    sprintf(szDir, "%s\\*", path);

    // Get the first file
    hList = FindFirstFile(szDir, &FileData);
    if (hList == INVALID_HANDLE_VALUE)
      {
	if(mkdir(path, 0) != 0) {
	  xprintf(xine, XINE_VERBOSITY_DEBUG, "input_cdda: mkdir(%s) failed.\n", path);
	  return;
	}
      }

    FindClose(hList);
  }
#endif /* WIN32 */
}

/*
 * Make recursive directory creation (given an absolute pathname)
 */
static void _cdda_mkdir_recursive_safe (xine_t *xine, char *path)
{
  if (!path)
    return;

  char buf[strlen (path) + 1];
  strcpy (buf, path);
  char *p = strchr (buf, '/') ? : buf;

  do
  {
    while (*p++ == '/') /**/;
    p = strchr (p, '/');
    if (p)
      *p = 0;
    _cdda_mkdir_safe (xine, buf);
    if (p)
      *p = '/';
  } while (p);
}

/*
 * Read from socket, fill char *s, return size length.
 */
static int _cdda_cddb_socket_read(cdda_input_plugin_t *this, char *str, int size) {
  int ret;
  ret = _x_io_tcp_read_line(this->stream, this->cddb.fd, str, size);

  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, "<<< %s\n", str);

  return ret;
}

/*
 * Send a command to socket
 */
static int _cdda_cddb_send_command(cdda_input_plugin_t *this, char *cmd) {

  if((this == NULL) || (this->cddb.fd < 0) || (cmd == NULL))
    return -1;

  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG, ">>> %s\n", cmd);

  return (int)_x_io_tcp_write(this->stream, this->cddb.fd, cmd, strlen(cmd));
}

/*
 * Handle return code od a command result.
 */
static int _cdda_cddb_handle_code(char *buf) {
  int  rcode, fdig, sdig, /*tdig,*/ err;

  err = -999;

  if (sscanf(buf, "%d", &rcode) == 1) {

    fdig = rcode / 100;
    sdig = (rcode - (fdig * 100)) / 10;
    /*tdig = (rcode - (fdig * 100) - (sdig * 10));*/

    /*
    printf(" %d--\n", fdig);
    printf(" -%d-\n", sdig);
    printf(" --%d\n", tdig);
    */

    err = rcode;

    switch (fdig) {
    case 1:
      /* printf("Informative message\n"); */
      break;
    case 2:
      /* printf("Command OK\n"); */
      break;
    case 3:
      /* printf("Command OK so far, continue\n"); */
      break;
    case 4:
      /* printf("Command OK, but cannot be performed for some specified reasons\n"); */
      err = 0 - rcode;
      break;
    case 5:
      /* printf("Command unimplemented, incorrect, or program error\n"); */
      err = 0 - rcode;
      break;
    default:
      /* printf("Unhandled case %d\n", fdig); */
      err = 0 - rcode;
      break;
    }

    switch (sdig) {
    case 0:
      /* printf("Ready for further commands\n"); */
      break;
    case 1:
      /* printf("More server-to-client output follows (until terminating marker)\n"); */
      break;
    case 2:
      /* printf("More client-to-server input follows (until terminating marker)\n"); */
      break;
    case 3:
      /* printf("Connection will close\n"); */
      err = 0 - rcode;
      break;
    default:
      /* printf("Unhandled case %d\n", sdig); */
      err = 0 - rcode;
      break;
    }
  }

  return err;
}

static inline char *_cdda_append (/*const*/ char *first, const char *second)
{
  if (!first)
    return strdup (second);

  char *result = (char *) realloc (first, strlen (first) + strlen (second) + 1);
  strcat (result, second);
  return result;
}

static void _cdda_parse_cddb_info (cdda_input_plugin_t *this, char *buffer, char **dtitle)
{
  /* buffer should be no more than 2048 bytes... */
  char buf[2048];
  int track_no;

  if (sscanf (buffer, "DTITLE=%s", &buf[0]) == 1) {
    char *pt = strchr (buffer, '=');
    if (pt) {
      ++pt;

      *dtitle = _cdda_append (*dtitle, pt);
      pt = strdup (*dtitle);

      char *title = strstr (pt, " / ");
      if (title)
      {
	*title = 0;
	title += 3;
	free (this->cddb.disc_artist);
	this->cddb.disc_artist = strdup (pt);
      }
      else
	title = pt;

      free (this->cddb.disc_title);
      this->cddb.disc_title = strdup (title);

      free (pt);
    }
  }
  else if (sscanf (buffer, "DYEAR=%s", &buf[0]) == 1) {
    char *pt = strchr (buffer, '=');
    if (pt && strlen (pt) == 5)
      this->cddb.disc_year = strdup (pt + 1);
  }
  else if(sscanf(buffer, "DGENRE=%s", &buf[0]) == 1) {
    char *pt = strchr(buffer, '=');
    if (pt)
      this->cddb.disc_category = strdup (pt + 1);
  }
  else if (sscanf (buffer, "TTITLE%d=%s", &track_no, &buf[0]) == 2) {
    char *pt = strchr(buffer, '=');
    this->cddb.track[track_no].title = _cdda_append (this->cddb.track[track_no].title, pt + 1);
  }
  else if (!strncmp (buffer, "EXTD=", 5))
  {
    if (!this->cddb.disc_year)
    {
      int nyear;
      char *y = strstr (buffer, "YEAR:");
      if (y && sscanf (y + 5, "%4d", &nyear) == 1)
	this->cddb.disc_year = _x_asprintf ("%d", nyear);
    }
  }
}

/*
 * Try to load cached cddb infos
 */
static int _cdda_load_cached_cddb_infos(cdda_input_plugin_t *this) {
  DIR  *dir;

  const char *const xdg_cache_home = xdgCacheHome(&this->stream->xine->basedir_handle);

  if(this == NULL)
    return 0;

  const size_t cdir_size = strlen(xdg_cache_home) + sizeof("/"PACKAGE"/cddb") + 10 + 1;
  char *const cdir = alloca(cdir_size);
  sprintf(cdir, "%s/" PACKAGE "/cddb", xdg_cache_home);

  if((dir = opendir(cdir)) != NULL) {
    struct dirent *pdir;

    while((pdir = readdir(dir)) != NULL) {
      char discid[9];

      snprintf(discid, sizeof(discid), "%08" PRIx32, this->cddb.disc_id);

      if(!strcasecmp(pdir->d_name, discid)) {
	FILE *fd;

	snprintf(cdir + cdir_size - 12, 10, "/%s", discid);
	if((fd = fopen(cdir, "r")) == NULL) {
	  xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
		  "input_cdda: fopen(%s) failed: %s.\n", cdir, strerror(errno));
	  closedir(dir);
	  return 0;
	}
	else {
	  char buffer[2048], *ln;
	  char *dtitle = NULL;

	  while ((ln = fgets(buffer, sizeof (buffer) - 1, fd)) != NULL) {

	    int length = strlen (buffer);
	    if (length && buffer[length - 1] == '\n')
	      buffer[length - 1] = '\0';

	    _cdda_parse_cddb_info (this, buffer, &dtitle);
	  }
	  fclose(fd);
	  free(dtitle);
	}

	closedir(dir);
	return 1;
      }
    }
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
      "input_cdda: cached entry for disc ID %08" PRIx32 " not found.\n", this->cddb.disc_id);
    closedir(dir);
  }

  return 0;
}

/*
 * Save cddb grabbed infos.
 */
static void _cdda_save_cached_cddb_infos(cdda_input_plugin_t *this, char *filecontent) {
  FILE  *fd;
  char *cfile;

  const char *const xdg_cache_home = xdgCacheHome(&this->stream->xine->basedir_handle);

  if((this == NULL) || (filecontent == NULL))
    return;

  /* the filename is always 8 characters */
  cfile = alloca(strlen(xdg_cache_home) + sizeof("/"PACKAGE"/cddb") + 9);
  strcpy(cfile, xdg_cache_home);
  strcat(cfile, "/"PACKAGE"/cddb");

  /* Ensure the cache directory exists */
  _cdda_mkdir_recursive_safe(this->stream->xine, cfile);

  sprintf(cfile, "%s/%08" PRIx32, cfile, this->cddb.disc_id);

  if((fd = fopen(cfile, "w")) == NULL) {
    xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	    "input_cdda: fopen(%s) failed: %s.\n", cfile, strerror(errno));
    return;
  }
  else {
    fprintf(fd, "%s", filecontent);
    fclose(fd);
  }

}

/*
 * Open a socket.
 */
static int _cdda_cddb_socket_open(cdda_input_plugin_t *this) {
  int sock;

#ifdef LOG
  printf("Conecting...");
  fflush(stdout);
#endif
  sock = _x_io_tcp_connect(this->stream, this->cddb.server, this->cddb.port);
  if (sock == -1 || _x_io_tcp_connect_finish(this->stream, sock, CDDB_TIMEOUT) != XIO_READY) {
    xine_log(this->stream->xine, XINE_LOG_MSG, _("%s: can't connect to %s:%d\n"), LOG_MODULE, this->cddb.server, this->cddb.port);
    lprintf("failed\n");
    return -1;
  }
  lprintf("done, sock = %d\n", sock);

  return sock;
}

/*
 * Close the socket
 */
static void _cdda_cddb_socket_close(cdda_input_plugin_t *this) {

  if((this == NULL) || (this->cddb.fd < 0))
    return;

  close(this->cddb.fd);
  this->cddb.fd = -1;
}

/*
 * Try to talk with CDDB server (to retrieve disc/tracks titles).
 */
static int _cdda_cddb_retrieve(cdda_input_plugin_t *this) {
  cdda_input_class_t *this_class = (cdda_input_class_t *)this->class;
  char buffer[2048], buffercache[32768], *m, *p;
  char *dtitle = NULL;
  int err, i;

  if(this == NULL) {
    return 0;
  }

  if(_cdda_load_cached_cddb_infos(this)) {
    this->cddb.have_cddb_info = 1;
    return 1;
  }
  if(!this->cddb.enabled || this_class->cddb_error) {
    this->cddb.have_cddb_info = 0;
    return 0;
  }
  else {
    this_class->cddb_error = 1;
    this->cddb.fd = _cdda_cddb_socket_open(this);
    if(this->cddb.fd >= 0) {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
	      _("input_cdda: successfully connected to cddb server '%s:%d'.\n"),
	      this->cddb.server, this->cddb.port);
    }
    else {
      xprintf(this->stream->xine, XINE_VERBOSITY_LOG,
	      _("input_cdda: failed to connect to cddb server '%s:%d' (%s).\n"),
	      this->cddb.server, this->cddb.port, strerror(errno));
      this->cddb.have_cddb_info = 0;
      return 0;
    }

    /* Read welcome message */
    memset(&buffer, 0, sizeof(buffer));
    err = _cdda_cddb_socket_read(this, buffer, sizeof(buffer) - 1);
    if (err < 0 || (err = _cdda_cddb_handle_code(buffer)) < 0) {
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "input_cdda: error while reading cddb welcome message.\n");
      _cdda_cddb_socket_close(this);
      return 0;
    }

    /* Send hello command */
    /* We don't send current user/host name to prevent spam.
     * Software that sends this is considered spyware
     * that most people don't like.
     */
    memset(&buffer, 0, sizeof(buffer));
    sprintf(buffer, "cddb hello unknown localhost xine %s\n", VERSION);
    if ((err = _cdda_cddb_send_command(this, buffer)) <= 0) {
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "input_cdda: error while sending cddb hello command.\n");
      _cdda_cddb_socket_close(this);
      return 0;
    }

    memset(&buffer, 0, sizeof(buffer));
    err = _cdda_cddb_socket_read(this, buffer, sizeof(buffer) - 1);
    if (err < 0 || (err = _cdda_cddb_handle_code(buffer)) < 0) {
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "input_cdda: cddb hello command returned error code '%03d'.\n", err);
      _cdda_cddb_socket_close(this);
      return 0;
    }
    /* Send server protocol number */
    /* For UTF-8 support - use protocol 6 */

    memset(&buffer, 0, sizeof(buffer));
    sprintf(buffer, "proto %d\n",CDDB_PROTOCOL);
    if ((err = _cdda_cddb_send_command(this, buffer)) <= 0) {
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "input_cdda: error while sending cddb protocol command.\n");
      _cdda_cddb_socket_close(this);
      return 0;
    }

    memset(&buffer, 0, sizeof(buffer));
    err = _cdda_cddb_socket_read(this, buffer, sizeof(buffer) - 1);
    if (err < 0 || (err = _cdda_cddb_handle_code(buffer)) < 0) {
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "input_cdda: cddb protocol command returned error code '%03d'.\n", err);
      _cdda_cddb_socket_close(this);
      return 0;
    }

    /* Send query command */
    memset(&buffer, 0, sizeof(buffer));
    size_t size = sprintf(buffer, "cddb query %08" PRIx32 " %d ", this->cddb.disc_id, this->cddb.num_tracks);
    for (i = 0; i < this->cddb.num_tracks; i++) {
      size += snprintf(buffer + size, sizeof(buffer) - size, "%d ", this->cddb.track[i].start);
    }
    snprintf(buffer + strlen(buffer), sizeof(buffer) - size, "%d\n", this->cddb.disc_length);
    if ((err = _cdda_cddb_send_command(this, buffer)) <= 0) {
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "input_cdda: error while sending cddb query command.\n");
      _cdda_cddb_socket_close(this);
      return 0;
    }

    memset(&buffer, 0, sizeof(buffer));
    err = _cdda_cddb_socket_read(this, buffer, sizeof(buffer) - 1);
    if (err < 0 || (((err = _cdda_cddb_handle_code(buffer)) != 200) && (err != 210) && (err != 211))) {
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "input_cdda: cddb query command returned error code '%03d'.\n", err);
      _cdda_cddb_socket_close(this);
      return 0;
    }

    if (err == 200) {
      p = buffer;
      i = 0;
      while ((i <= 2) && ((m = xine_strsep(&p, " ")) != NULL)) {
        if (i == 1) {
          this->cddb.disc_category = strdup(m);
        }
        else if(i == 2) {
          this->cddb.cdiscid = strdup(m);
        }
        i++;
      }
    }

    if ((err == 210) || (err == 211)) {
      memset(&buffer, 0, sizeof(buffer));
      err = _cdda_cddb_socket_read(this, buffer, sizeof(buffer) - 1);
      if (err < 0) {
        xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
                "input_cdda: cddb query command returned error code '%03d'.\n", err);
        _cdda_cddb_socket_close(this);
        return 0;
      }
      p = buffer;
      i = 0;
      while ((i <= 1) && ((m = xine_strsep(&p, " ")) != NULL)) {
        if (i == 0) {
          this->cddb.disc_category = strdup(m);
        }
        else if(i == 1) {
          this->cddb.cdiscid = strdup(m);
        }
        i++;
      }
      while (strcmp(buffer, ".")) {
        memset(&buffer, 0, sizeof(buffer));
        err = _cdda_cddb_socket_read(this, buffer, sizeof(buffer) - 1);
        if (err < 0) {
          xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
                  "input_cdda: cddb query command returned error code '%03d'.\n", err);
          _cdda_cddb_socket_close(this);
          return 0;
        }
      }
    }
    /* Send read command */
    memset(&buffer, 0, sizeof(buffer));
    snprintf(buffer, sizeof(buffer), "cddb read %s %s\n", this->cddb.disc_category, this->cddb.cdiscid);
    if ((err = _cdda_cddb_send_command(this, buffer)) <= 0) {
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "input_cdda: error while sending cddb read command.\n");
      _cdda_cddb_socket_close(this);
      return 0;
    }

    memset(&buffer, 0, sizeof(buffer));
    err = _cdda_cddb_socket_read(this, buffer, sizeof(buffer) - 1);
    if (err < 0 || (err = _cdda_cddb_handle_code(buffer)) != 210) {
      xprintf(this->stream->xine, XINE_VERBOSITY_DEBUG,
	      "input_cdda: cddb read command returned error code '%03d'.\n", err);
      _cdda_cddb_socket_close(this);
      return 0;
    }

    this->cddb.have_cddb_info = 1;
    memset(&buffercache, 0, sizeof(buffercache));

    while (strcmp(buffer, ".")) {
      size_t bufsize = strlen(buffercache);

      memset(&buffer, 0, sizeof(buffer));
      _cdda_cddb_socket_read(this, buffer, sizeof(buffer) - 1);
      snprintf(buffercache + bufsize, sizeof(buffercache) - bufsize, "%s\n", buffer);

      _cdda_parse_cddb_info (this, buffer, &dtitle);
    }
    free(dtitle);

    /* Save cddb info and close socket */
    _cdda_save_cached_cddb_infos(this, buffercache);
    _cdda_cddb_socket_close(this);
  }

  /* success */
  this_class->cddb_error = 0;
  return 1;
}

/*
 * Compute cddb disc compliant id
 */
static unsigned int _cdda_cddb_sum(int n) {
  unsigned int ret = 0;

  while(n > 0) {
    ret += (n % 10);
    n /= 10;
  }
  return ret;
}
static uint32_t _cdda_calc_cddb_id(cdda_input_plugin_t *this) {
  int i, tsum = 0;

  if(this == NULL || (this->cddb.num_tracks <= 0))
    return 0;

  for(i = 0; i < this->cddb.num_tracks; i++)
    tsum += _cdda_cddb_sum((this->cddb.track[i].start / CD_FRAMES_PER_SECOND));

  return ((tsum % 0xff) << 24
	  | (this->cddb.disc_length - (this->cddb.track[0].start / CD_FRAMES_PER_SECOND)) << 8
	  | this->cddb.num_tracks);
}

/*
 * Compute Musicbrainz CDIndex ID
 */
static void _cdda_cdindex(cdda_input_plugin_t *this, cdrom_toc *toc) {
  char temp[10];
  struct AVSHA *sha_ctx = malloc(av_sha_size);
  unsigned char digest[20];
  /* We're going to encode 20 bytes in base64, which will become
   * 6 * 32 / 8 = 24 bytes.
   * libavutil's base64 encoding functions, though, wants the size to
   * be at least len * 4 / 3 + 12, so let's use 39.
   */
  char base64[39];
  int i;

  av_sha_init(sha_ctx, 160);

  sprintf(temp, "%02X", toc->first_track);
  av_sha_update(sha_ctx, (unsigned char*) temp, strlen(temp));

  sprintf(temp, "%02X", toc->last_track - toc->ignore_last_track);
  av_sha_update(sha_ctx, (unsigned char*) temp, strlen(temp));

  sprintf (temp, "%08X", toc->leadout_track.first_frame);// + 150);
  av_sha_update(sha_ctx, (unsigned char*) temp, strlen(temp));

  for (i = toc->first_track; i <= toc->last_track - toc->ignore_last_track; i++) {
    sprintf(temp, "%08X", toc->toc_entries[i - 1].first_frame);
    av_sha_update(sha_ctx, (unsigned char*) temp, strlen(temp));
  }

  for (i = toc->last_track - toc->ignore_last_track + 1; i < 100; i++) {
    av_sha_update(sha_ctx, (unsigned char*) temp, strlen(temp));
  }

  av_sha_final(sha_ctx, digest);
  free(sha_ctx);

  av_base64_encode(base64, 39, digest, 20);

  _x_meta_info_set_utf8(this->stream, XINE_META_INFO_CDINDEX_DISCID, base64);
}

/*
 * return cbbd disc id.
 */
static uint32_t _cdda_get_cddb_id(cdda_input_plugin_t *this) {

  if(this == NULL || (this->cddb.num_tracks <= 0))
    return 0;

  return _cdda_calc_cddb_id(this);
}

/*
 * Free allocated memory for CDDB informations
 */
static void _cdda_free_cddb_info(cdda_input_plugin_t *this) {

  if(this->cddb.track) {
    int t;

    for(t = 0; t < this->cddb.num_tracks; t++) {
      free(this->cddb.track[t].title);
    }

    free(this->cddb.track);
    free(this->cddb.cdiscid);
    free(this->cddb.disc_title);
    free(this->cddb.disc_artist);
    free(this->cddb.disc_category);
    free(this->cddb.disc_year);

  }
}
/*
 * ********** END OF CDDB ***************
 */

static int cdda_open(cdda_input_plugin_t *this_gen,
					 const char *cdda_device, cdrom_toc *toc, int *fdd) {
#ifndef WIN32
  int fd = -1;

  if ( !cdda_device ) return -1;

  *fdd = -1;

  if (this_gen)
    this_gen->fd = -1;

  /* We use O_NONBLOCK for when /proc/sys/dev/cdrom/check_media is at 1 on
   * Linux systems */
  fd = xine_open_cloexec(cdda_device, O_RDONLY | O_NONBLOCK);
  if (fd == -1) {
    return -1;
  }

  if (this_gen)
    this_gen->fd = fd;

#ifdef CDROM_SELECT_SPEED
  if (this_gen->stream) {
    int speed;
    speed = this_gen->stream->xine->config->lookup_entry(this_gen->stream->xine->config,
      "media.audio_cd.drive_slowdown")->num_value;
    if (speed && ioctl(fd, CDROM_SELECT_SPEED, speed) != 0)
      xprintf(this_gen->stream->xine, XINE_VERBOSITY_DEBUG,
        "input_cdda: setting drive speed to %d failed\n", speed);
  }
#endif

  *fdd = fd;
  return 0;

#else /* WIN32 */
  if ( !cdda_device ) return -1;

  *fdd = -1;

  if (this_gen) {
    this_gen->fd = -1;
    this_gen->h_device_handle = NULL;
    this_gen->i_sid = 0;
    this_gen->hASPI = 0;
    this_gen->lpSendCommand = 0;
  }
  else
      return -1;

  /* We are going to assume that we are opening a
   * device and not a file!
   */
  if( WIN_NT )
    {
      char psz_win32_drive[7];

      lprintf( "using winNT/2K/XP ioctl layer" );

      sprintf( psz_win32_drive, "\\\\.\\%c:", cdda_device[0] );

      this_gen->h_device_handle = CreateFile( psz_win32_drive, GENERIC_READ,
					      FILE_SHARE_READ | FILE_SHARE_WRITE,
					      NULL, OPEN_EXISTING,
					      FILE_FLAG_NO_BUFFERING |
					      FILE_FLAG_RANDOM_ACCESS, NULL );
      return (this_gen->h_device_handle == NULL) ? -1 : 0;
    }
  else
    {
      HMODULE hASPI = NULL;
      long (*lpGetSupport)( void ) = NULL;
      long (*lpSendCommand)( void* ) = NULL;
      DWORD dwSupportInfo;
      int i, j, i_hostadapters;
      char c_drive = cdda_device[0];

      hASPI = LoadLibrary( "wnaspi32.dll" );
      if( hASPI != NULL )
	{
	  lpGetSupport = GetProcAddress( hASPI,
						   "GetASPI32SupportInfo" );
	  lpSendCommand = GetProcAddress( hASPI,
						    "SendASPI32Command" );
	}

      if( hASPI == NULL || lpGetSupport == NULL || lpSendCommand == NULL )
	{
	  lprintf( "unable to load aspi or get aspi function pointers" );

	  if( hASPI ) FreeLibrary( hASPI );
	  return -1;
	}

      /* ASPI support seems to be there */

      dwSupportInfo = lpGetSupport();

      if( HIBYTE( LOWORD ( dwSupportInfo ) ) == SS_NO_ADAPTERS )
	{
	  lprintf( "no host adapters found (aspi)" );
	  FreeLibrary( hASPI );
	  return -1;
	}

      if( HIBYTE( LOWORD ( dwSupportInfo ) ) != SS_COMP )
	{
	  lprintf( "unable to initalize aspi layer" );

	  FreeLibrary( hASPI );
	  return -1;
	}

      i_hostadapters = LOBYTE( LOWORD( dwSupportInfo ) );
      if( i_hostadapters == 0 )
	{
	  FreeLibrary( hASPI );
	  return -1;
	}

      c_drive = c_drive > 'Z' ? c_drive - 'a' : c_drive - 'A';

      for( i = 0; i < i_hostadapters; i++ )
	{
          for( j = 0; j < 15; j++ )
	    {
              struct SRB_GetDiskInfo srbDiskInfo;

              srbDiskInfo.SRB_Cmd         = SC_GET_DISK_INFO;
              srbDiskInfo.SRB_HaId        = i;
              srbDiskInfo.SRB_Flags       = 0;
              srbDiskInfo.SRB_Hdr_Rsvd    = 0;
              srbDiskInfo.SRB_Target      = j;
              srbDiskInfo.SRB_Lun         = 0;

              lpSendCommand( (void*) &srbDiskInfo );

              if( (srbDiskInfo.SRB_Status == SS_COMP) &&
                  (srbDiskInfo.SRB_Int13HDriveInfo == c_drive) )
		{
                  /* Make sure this is a cdrom device */
                  struct SRB_GDEVBlock   srbGDEVBlock;

                  memset( &srbGDEVBlock, 0, sizeof(struct SRB_GDEVBlock) );
                  srbGDEVBlock.SRB_Cmd    = SC_GET_DEV_TYPE;
                  srbGDEVBlock.SRB_HaId   = i;
                  srbGDEVBlock.SRB_Target = j;

                  lpSendCommand( (void*) &srbGDEVBlock );

                  if( ( srbGDEVBlock.SRB_Status == SS_COMP ) &&
                      ( srbGDEVBlock.SRB_DeviceType == DTYPE_CDROM ) )
		    {
                      this_gen->i_sid = MAKEWORD( i, j );
                      this_gen->hASPI = (long)hASPI;
                      this_gen->lpSendCommand = lpSendCommand;

                      lprintf( "using aspi layer" );

                      return 0;
		    }
                  else
		    {
		      FreeLibrary( hASPI );
		      lprintf( "%s: is not a cdrom drive", cdda_device[0] );
		      return -1;
		    }
		}
	    }
	}

      FreeLibrary( hASPI );

      lprintf( "unable to get haid and target (aspi)" );
    }

#endif /* WIN32 */

    return -1;
}

static int cdda_close(cdda_input_plugin_t *this_gen) {

  if (!this_gen)
      return 0;

  if( this_gen->fd != -1 ) {
#ifdef CDROM_SELECT_SPEED
    if (this_gen->stream) {
      int speed;
      speed = this_gen->stream->xine->config->lookup_entry(this_gen->stream->xine->config,
        "media.audio_cd.drive_slowdown")->num_value;
      if (speed && ioctl(this_gen->fd, CDROM_SELECT_SPEED, 0) != 0)
        xprintf(this_gen->stream->xine, XINE_VERBOSITY_DEBUG,
          "input_cdda: setting drive speed to normal failed\n");
    }
#endif
    close(this_gen->fd);
  }
  this_gen->fd = -1;

  if (this_gen->net_fd != -1)
    close(this_gen->net_fd);
  this_gen->net_fd = -1;

#ifdef WIN32
  if( this_gen->h_device_handle )
     CloseHandle( this_gen->h_device_handle );
  this_gen->h_device_handle = NULL;
  if( this_gen->hASPI )
      FreeLibrary( (HMODULE)this_gen->hASPI );
  this_gen->hASPI = (long)NULL;
#endif /* WIN32 */

  return 0;
}


static uint32_t cdda_plugin_get_capabilities (input_plugin_t *this_gen) {

  return INPUT_CAP_SEEKABLE;
}


static off_t cdda_plugin_read (input_plugin_t *this_gen, void *buf, off_t len) {

  cdda_input_plugin_t *this = (cdda_input_plugin_t *) this_gen;
  int err = 0;

  /* only allow reading in block-sized chunks */

  if (len != CD_RAW_FRAME_SIZE)
    return 0;

  if (this->current_frame > this->last_frame)
    return 0;

  /* populate frame cache */
  if( this->cache_first == -1 ||
      this->current_frame < this->cache_first ||
      this->current_frame > this->cache_last ) {

    this->cache_first = this->current_frame;
    this->cache_last = this->current_frame + CACHED_FRAMES - 1;
    if( this->cache_last > this->last_frame )
      this->cache_last = this->last_frame;

#ifndef WIN32
    if ( this->fd != -1 )
#else
	if ( this->h_device_handle )
#endif /* WIN32 */

      err = read_cdrom_frames(this, this->cache_first,
                             this->cache_last - this->cache_first + 1,
                             this->cache[0]);
    else if ( this->net_fd != -1 )
      err = network_read_cdrom_frames(this->stream, this->net_fd, this->cache_first,
                                      this->cache_last - this->cache_first + 1,
                                      this->cache[0]);
  }

  if( err < 0 )
    return 0;

  memcpy(buf, this->cache[this->current_frame-this->cache_first], CD_RAW_FRAME_SIZE);
  this->current_frame++;

  return CD_RAW_FRAME_SIZE;
}

static buf_element_t *cdda_plugin_read_block (input_plugin_t *this_gen, fifo_buffer_t *fifo,
  off_t nlen) {

  buf_element_t *buf;

  buf = fifo->buffer_pool_alloc(fifo);
  buf->content = buf->mem;
  buf->type = BUF_DEMUX_BLOCK;

  buf->size = cdda_plugin_read(this_gen, buf->content, nlen);
  if (buf->size == 0) {
    buf->free_buffer(buf);
    buf = NULL;
  }

  return buf;
}

static off_t cdda_plugin_seek (input_plugin_t *this_gen, off_t offset, int origin) {
  cdda_input_plugin_t *this = (cdda_input_plugin_t *) this_gen;
  int seek_to_frame;

  /* compute the proposed frame and check if it is within bounds */
  if (origin == SEEK_SET)
    seek_to_frame = offset / CD_RAW_FRAME_SIZE + this->first_frame;
  else if (origin == SEEK_CUR)
    seek_to_frame = offset / CD_RAW_FRAME_SIZE + this->current_frame;
  else
    seek_to_frame = offset / CD_RAW_FRAME_SIZE + this->last_frame;

  if ((seek_to_frame >= this->first_frame) &&
      (seek_to_frame <= this->last_frame))
    this->current_frame = seek_to_frame;

  return (this->current_frame - this->first_frame) * CD_RAW_FRAME_SIZE;
}

static off_t cdda_plugin_get_current_pos (input_plugin_t *this_gen){
  cdda_input_plugin_t *this = (cdda_input_plugin_t *) this_gen;

  return (this->current_frame - this->first_frame) * CD_RAW_FRAME_SIZE;
}

static off_t cdda_plugin_get_length (input_plugin_t *this_gen) {
  cdda_input_plugin_t *this = (cdda_input_plugin_t *) this_gen;

  return (this->last_frame - this->first_frame + 1) * CD_RAW_FRAME_SIZE;
}

static uint32_t cdda_plugin_get_blocksize (input_plugin_t *this_gen) {

  return 0;
}

static const char* cdda_plugin_get_mrl (input_plugin_t *this_gen) {
  cdda_input_plugin_t *this = (cdda_input_plugin_t *) this_gen;

  return this->mrl;
}

static int cdda_plugin_get_optional_data (input_plugin_t *this_gen,
                                          void *data, int data_type) {
  return 0;
}

static void cdda_plugin_dispose (input_plugin_t *this_gen ) {
  cdda_input_plugin_t *this = (cdda_input_plugin_t *) this_gen;

  _cdda_free_cddb_info(this);

  cdda_close(this);

  free(this->mrl);

  free(this->cdda_device);
  if (this->class) {
    cdda_input_class_t *inp = (cdda_input_class_t *) this->class;
    inp->ip = NULL;
  }

  free(this);
}


static int cdda_plugin_open (input_plugin_t *this_gen ) {
  cdda_input_plugin_t *this = (cdda_input_plugin_t *) this_gen;
  cdda_input_class_t  *class = (cdda_input_class_t *) this_gen->input_class;
  cdrom_toc            *toc;
  int                  fd  = -1;
  char                *cdda_device;
  int                  err = -1;

  lprintf("cdda_plugin_open\n");

  /* get the CD TOC */
  toc = init_cdrom_toc();

  if( this->cdda_device )
    cdda_device = this->cdda_device;
  else
    cdda_device = class->cdda_device;

#ifndef WIN32
  if( strchr(cdda_device,':') ) {
    fd = network_connect(this->stream, cdda_device);
    if( fd != -1 ) {
      this->net_fd = fd;

      err = network_read_cdrom_toc(this->stream, this->net_fd, toc);
    }
  }
#endif

  if( this->net_fd == -1 ) {

    if (cdda_open(this, cdda_device, toc, &fd) == -1) {
      free_cdrom_toc(toc);
      return 0;
    }

#ifndef WIN32
    err = read_cdrom_toc(this->fd, toc);
#else
    err = read_cdrom_toc(this, toc);
#endif

#ifdef LOG
    print_cdrom_toc(toc);
#endif

  }


  if ( (err < 0) || (toc->first_track > (this->track + 1)) ||
      (toc->last_track < (this->track + 1))) {

	cdda_close(this);

    free_cdrom_toc(toc);
    return 0;
  }

  /* set up the frame boundaries for this particular track */
  this->first_frame = this->current_frame =
    toc->toc_entries[this->track].first_frame;
  if (this->track + 1 == toc->last_track)
    this->last_frame = toc->leadout_track.first_frame - 1;
  else
    this->last_frame = toc->toc_entries[this->track + 1].first_frame - 1;

  /* invalidate cache */
  this->cache_first = this->cache_last = -1;

  /* get the Musicbrainz CDIndex */
  _cdda_cdindex (this, toc);

  /*
   * CDDB
   */
  _cdda_free_cddb_info(this);

  this->cddb.num_tracks = toc->total_tracks;

  if(this->cddb.num_tracks) {
    int t;

    this->cddb.track = (trackinfo_t *) calloc(this->cddb.num_tracks, sizeof(trackinfo_t));

    for(t = 0; t < this->cddb.num_tracks; t++) {
      int length = (toc->toc_entries[t].first_frame_minute * CD_SECONDS_PER_MINUTE +
		    toc->toc_entries[t].first_frame_second);

      this->cddb.track[t].start = (length * CD_FRAMES_PER_SECOND +
				   toc->toc_entries[t].first_frame_frame);
      this->cddb.track[t].title = NULL;
    }

  }

  this->cddb.disc_length = (toc->leadout_track.first_frame_minute * CD_SECONDS_PER_MINUTE +
			    toc->leadout_track.first_frame_second);
  this->cddb.disc_id     = _cdda_get_cddb_id(this);

  if((this->cddb.have_cddb_info == 0) || (_cdda_is_cd_changed(this) == 1))
    _cdda_cddb_retrieve(this);

  if(this->cddb.disc_title) {
    lprintf("Disc Title: %s\n", this->cddb.disc_title);

    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_ALBUM, this->cddb.disc_title);
  }

  if(this->cddb.track[this->track].title) {
    /* Check for track 'titles' of the form <artist> / <title>. */
    char *pt;
    pt = strstr(this->cddb.track[this->track].title, " / ");
    if (pt != NULL) {
      char *track_artist;
      track_artist = strdup(this->cddb.track[this->track].title);
      track_artist[pt - this->cddb.track[this->track].title] = 0;
      lprintf("Track %d Artist: %s\n", this->track+1, track_artist);

      _x_meta_info_set_utf8(this->stream, XINE_META_INFO_ARTIST, track_artist);
      free(track_artist);
      pt += 3;
    }
    else {
      if(this->cddb.disc_artist) {
	lprintf("Disc Artist: %s\n", this->cddb.disc_artist);

	_x_meta_info_set_utf8(this->stream, XINE_META_INFO_ARTIST, this->cddb.disc_artist);
      }

      pt = this->cddb.track[this->track].title;
    }
    lprintf("Track %d Title: %s\n", this->track+1, pt);

    char tracknum[4];
    snprintf(tracknum, 4, "%d", this->track+1);
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_TRACK_NUMBER, tracknum);
    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_TITLE, pt);
  }

  if(this->cddb.disc_category) {
    lprintf("Disc Category: %s\n", this->cddb.disc_category);

    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_GENRE, this->cddb.disc_category);
  }

  if(this->cddb.disc_year) {
    lprintf("Disc Year: %s\n", this->cddb.disc_year);

    _x_meta_info_set_utf8(this->stream, XINE_META_INFO_YEAR, this->cddb.disc_year);
  }

  free_cdrom_toc(toc);

  return 1;
}

static void free_autoplay_list(cdda_input_class_t *this)
{
  /* free old playlist */
  if (this->autoplaylist) {
    unsigned int i;
    for( i = 0; this->autoplaylist[i]; i++ ) {
      free( this->autoplaylist[i] );
      this->autoplaylist[i] = NULL;
    }

    free(this->autoplaylist);
    this->autoplaylist = NULL;
  }
}

static const char * const * cdda_class_get_autoplay_list (input_class_t *this_gen,
					    int *num_files) {

  cdda_input_class_t *this = (cdda_input_class_t *) this_gen;
  cdda_input_plugin_t *ip = this->ip;
  cdrom_toc *toc;
  int fd, i, err = -1;
  int num_tracks;

  lprintf("cdda_class_get_autoplay_list for >%s<\n", this->cdda_device);

  free_autoplay_list(this);

  /* get the CD TOC */
  toc = init_cdrom_toc();

  fd = -1;

  if (!ip) {
    /* we need an instance pointer to store all the details about the
     * device we are going to open; but it is possible that this function
     * gets called, before a plugin instance has been created;
     * let's create a dummy instance in such a condition */
    ip = calloc(1, sizeof(cdda_input_plugin_t));
    ip->stream = NULL;
    ip->fd = -1;
    ip->net_fd = -1;
  }

#ifndef WIN32
  if( strchr(this->cdda_device,':') ) {
    fd = network_connect(ip->stream, this->cdda_device);
    if( fd != -1 ) {
      err = network_read_cdrom_toc(ip->stream, fd, toc);
    }
  }
#endif

  if (fd == -1) {
    if (cdda_open(ip, this->cdda_device, toc, &fd) == -1) {
      lprintf("cdda_class_get_autoplay_list: opening >%s< failed %s\n",
              this->cdda_device, strerror(errno));
      if (ip != this->ip) free(ip);
      return NULL;
    }

#ifndef WIN32
    err = read_cdrom_toc(fd, toc);
#else
    err = read_cdrom_toc(ip, toc);
#endif /* WIN32 */
  }

#ifdef LOG
  print_cdrom_toc(toc);
#endif

  cdda_close(ip);

  if ( err < 0 ) {
    if (ip != this->ip) free(ip);
    return NULL;
  }

  num_tracks = toc->last_track - toc->first_track;
  if (toc->ignore_last_track)
    num_tracks--;
  if (num_tracks >= MAX_TRACKS-1)
    num_tracks = MAX_TRACKS - 2;

  this->autoplaylist = calloc(num_tracks + 2, sizeof(char *));
  for ( i = 0; i <= num_tracks; i++ )
    this->autoplaylist[i] = _x_asprintf("cdda:/%d",i+toc->first_track);

  *num_files = toc->last_track - toc->first_track + 1;

  free_cdrom_toc(toc);
  if (ip != this->ip) free(ip);
  return (const char * const *)this->autoplaylist;
}

static input_plugin_t *cdda_class_get_instance (input_class_t *cls_gen, xine_stream_t *stream,
                                    const char *mrl) {

  cdda_input_plugin_t *this;
  cdda_input_class_t  *class = (cdda_input_class_t *) cls_gen;
  int                  track;
  xine_cfg_entry_t     enable_entry, server_entry, port_entry;
  char                *cdda_device = NULL;
  int                  cddb_error = class->cddb_error;

  lprintf("cdda_class_get_instance: >%s<\n", mrl);

  /* fetch the CD track to play */
  if (!strncasecmp (mrl, "cdda:/", 6)) {

    const char *p, *slash = mrl + 6;
    while (*slash == '/')
      ++slash;
    p = --slash; /* point at a slash */
    while (*p >= '0' && *p <= '9')
      ++p;
    if (*p) {
      char *lastslash;
      cdda_device = strdup (slash);
      p = lastslash = strrchr (cdda_device, '/'); /* guaranteed to return non-NULL */
      while (*++p >= '0' && *p <= '9')
        /**/;
      if (!*p) {
        track = atoi (lastslash + 1);
        *lastslash = 0;
        if (lastslash == cdda_device) {
          free (cdda_device);
          cdda_device = NULL;
        }
      } else {
        track = -1;
      }
    } else {
      track = atoi (slash + 1);
    }
    if (track < 1)
      track = 1;
  } else
    return NULL;

  this = calloc(1, sizeof (cdda_input_plugin_t));

  class->ip = this;
  this->stream      = stream;
  this->mrl         = strdup(mrl);
  this->cdda_device = cdda_device;

  /* CD tracks start from 1; internal data structure indexes from 0 */
  this->track      = track - 1;
  this->cddb.track = NULL;
  this->fd         = -1;
  this->net_fd     = -1;
  this->class      = (input_class_t *) class;

  this->input_plugin.open               = cdda_plugin_open;
  this->input_plugin.get_capabilities   = cdda_plugin_get_capabilities;
  this->input_plugin.read               = cdda_plugin_read;
  this->input_plugin.read_block         = cdda_plugin_read_block;
  this->input_plugin.seek               = cdda_plugin_seek;
  this->input_plugin.get_current_pos    = cdda_plugin_get_current_pos;
  this->input_plugin.get_length         = cdda_plugin_get_length;
  this->input_plugin.get_blocksize      = cdda_plugin_get_blocksize;
  this->input_plugin.get_mrl            = cdda_plugin_get_mrl;
  this->input_plugin.get_optional_data  = cdda_plugin_get_optional_data;
  this->input_plugin.dispose            = cdda_plugin_dispose;
  this->input_plugin.input_class        = cls_gen;

  /*
   * Lookup config entries.
   */
  if(xine_config_lookup_entry(this->stream->xine, "media.audio_cd.use_cddb",
			      &enable_entry))
    enable_cddb_changed_cb(class, &enable_entry);

  if(xine_config_lookup_entry(this->stream->xine, "media.audio_cd.cddb_server",
			      &server_entry))
    server_changed_cb(class, &server_entry);

  if(xine_config_lookup_entry(this->stream->xine, "media.audio_cd.cddb_port",
			      &port_entry))
    port_changed_cb(class, &port_entry);

  class->cddb_error = cddb_error;

  return (input_plugin_t *)this;
}


static void cdda_class_dispose (input_class_t *this_gen) {
  cdda_input_class_t  *this = (cdda_input_class_t *) this_gen;
  config_values_t     *config = this->xine->config;

  config->unregister_callback(config, "media.audio_cd.device");
  config->unregister_callback(config, "media.audio_cd.use_cddb");
  config->unregister_callback(config, "media.audio_cd.cddb_server");
  config->unregister_callback(config, "media.audio_cd.cddb_port");
#ifdef CDROM_SELECT_SPEED
  config->unregister_callback(config, "media.audio_cd.drive_slowdown");
#endif

  free_autoplay_list(this);

  while (this->mrls_allocated_entries) {
    MRL_ZERO(this->mrls[this->mrls_allocated_entries - 1]);
    free(this->mrls[this->mrls_allocated_entries--]);
  }
  free (this->mrls);

  free (this);
}

static int cdda_class_eject_media (input_class_t *this_gen) {
  cdda_input_class_t  *this = (cdda_input_class_t *) this_gen;

  return media_eject_media (this->xine, this->cdda_device);
}


static void *init_plugin (xine_t *xine, void *data) {

  cdda_input_class_t  *this;
  config_values_t     *config;

  this = calloc(1, sizeof (cdda_input_class_t));

  this->xine   = xine;
  this->config = xine->config;
  config       = xine->config;

  this->input_class.get_instance       = cdda_class_get_instance;
  this->input_class.identifier         = "cdda";
  this->input_class.description        = N_("CD Digital Audio (aka. CDDA)");
  /* this->input_class.get_dir            = cdda_class_get_dir; */
  this->input_class.get_dir            = NULL;
  this->input_class.get_autoplay_list  = cdda_class_get_autoplay_list;
  this->input_class.dispose            = cdda_class_dispose;
  this->input_class.eject_media        = cdda_class_eject_media;

  this->mrls = NULL;
  this->mrls_allocated_entries = 0;
  this->ip = NULL;

  this->cdda_device = config->register_filename(config, "media.audio_cd.device",
					      DEFAULT_CDDA_DEVICE, XINE_CONFIG_STRING_IS_DEVICE_NAME,
					      _("device used for CD audio"),
					      _("The path to the device, usually a "
						"CD or DVD drive, which you intend to use "
						"for playing audio CDs."),
					      10, cdda_device_cb, (void *) this);

  config->register_bool(config, "media.audio_cd.use_cddb", 1,
			_("query CDDB"), _("Enables CDDB queries, which will give you "
			"convenient title and track names for your audio CDs.\n"
			"Keep in mind that, unless you use your own private CDDB, this information "
			"is retrieved from an internet server which might collect a profile "
			"of your listening habits."),
			10, enable_cddb_changed_cb, (void *) this);

  config->register_string(config, "media.audio_cd.cddb_server", CDDB_SERVER,
			  _("CDDB server name"), _("The CDDB server used to retrieve the "
			  "title and track information from.\nThis setting is security critical, "
			  "because the sever will receive information about your listening habits "
			  "and could answer the queries with malicious replies. Be sure to enter "
			  "a server you can trust."), XINE_CONFIG_SECURITY,
			  server_changed_cb, (void *) this);

  config->register_num(config, "media.audio_cd.cddb_port", CDDB_PORT,
		       _("CDDB server port"), _("The server port used to retrieve the "
		       "title and track information from."), XINE_CONFIG_SECURITY,
		       port_changed_cb, (void *) this);

#ifdef CDROM_SELECT_SPEED
  config->register_num(config, "media.audio_cd.drive_slowdown", 4,
		       _("slow down disc drive to this speed factor"),
		       _("Since some CD or DVD drives make some really "
			 "loud noises because of the fast disc rotation, "
			 "xine will try to slow them down. With standard "
			 "CD or DVD playback, the high datarates that "
			 "require the fast rotation are not needed, so "
			 "the slowdown should not affect playback performance.\n"
			 "A value of zero here will disable the slowdown."),
		       10, speed_changed_cb, (void *) this);
#endif

  this->cddb_error = 0;

  return this;
}

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_INPUT | PLUGIN_MUST_PRELOAD, 18, "CD", XINE_VERSION_CODE, NULL, init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};

