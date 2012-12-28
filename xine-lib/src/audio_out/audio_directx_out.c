/*
 * Copyright (C) 2001-2003 the xine project
 *
 * This file is part of xine, a unix video player.
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
 * audio_directx_out.c, direct sound audio output plugin for xine
 * by Matthew Grooms <elon@altavista.com>
 */

/*
 * TODO:
 *   - stop looping on stop (requires AO_CTRL_PLAY_STOP?)
 *   - posibility of bad state when buffer overrun
 */

typedef unsigned char boolean;

#include <windows.h>
#include <dsound.h>

#define LOG_MODULE "audio_directx_out"
#define LOG_VERBOSE
/*
#define LOG
*/

#include <xine/audio_out.h>
#include <xine/xine_internal.h>


#define MAX_CHANNELS	          6
#define MAX_BITS		  16
#define MAX_SAMPLE_RATE		  44100
#define SOUND_BUFFER_DIV	  32
#define SOUND_BUFFER_MAX	  MAX_CHANNELS * (MAX_BITS / 8) * (((MAX_SAMPLE_RATE / SOUND_BUFFER_DIV) + 1) & ~1)

#define DSBUFF_INIT		  0
#define DSBUFF_LEFT		  1
#define DSBUFF_RIGHT	          2

#define AO_DIRECTX_IFACE_VERSION  9

/*****************************************************************************
 * DirectDraw GUIDs.
 * Defining them here allows us to get rid of the dxguid library during
 * the linking stage.
 *****************************************************************************/
#if 1
static const GUID xine_IID_IDirectSoundNotify = {
	0xB0210783,0x89CD,0x11D0,{0xAF,0x08,0x00,0xA0,0xC9,0x25,0xCD,0x16}
};
#ifdef IID_IDirectSoundNotify
#  undef IID_IDirectSoundNotify
#endif
#define IID_IDirectSoundNotify xine_IID_IDirectSoundNotify
#endif


/* -----------------------------------------
 *
 * ao_directx driver struct
 *
 * ----------------------------------------- */

typedef struct {
  ao_driver_t		ao_driver;
  int			capabilities;

  xine_t               *xine;

  /* directx objects */
  LPDIRECTSOUND         dsobj;
  LPDIRECTSOUNDBUFFER   dsbuffer;
  DSBCAPS		dsbcaps;
  LPDIRECTSOUNDNOTIFY   notify;
  DSBPOSITIONNOTIFY	notify_events[ 2 ];

  /* buffer vars */
  long                  buffer_size;
  int                   write_status;
  unsigned long         write_pos;

  uint8_t               prebuff[ SOUND_BUFFER_MAX ];
  uint32_t              prebuff_size;

  /* current buffer properties */
  int		        bits;
  int		        rate;
  int		        chnn;
  int		        frsz;

  /* current mixer settings */
  int                   mute;
  int		        volume;
} ao_directx_t;

typedef struct {
  audio_driver_class_t  driver_class;
  config_values_t      *config;
  xine_t               *xine;
} audiox_class_t;

/* -------------------------------------------
 *
 * BEGIN : Direct Sound and win32 handlers
 *         for xine audio output plugins.
 *
 * ------------------------------------------- */

void       Error( HWND hwnd, LPSTR szfmt, ... );
boolean    CreateDirectSound( ao_directx_t * ao_directx );
void       DestroyDirectSound( ao_directx_t * ao_directx );
boolean    CreateSoundBuffer( ao_directx_t * ao_directx );
void       DestroySoundBuffer( ao_directx_t * ao_directx );
uint32_t   FillSoundBuffer( ao_directx_t * ao_directx, int code, unsigned char * samples );

/* Display formatted error message in
 * popup message box. */

void Error( HWND hwnd, LPSTR szfmt, ... )
{
  char tempbuff[ 256 ];
  *tempbuff = 0;
  wvsprintf(	&tempbuff[ strlen( tempbuff ) ], szfmt, ( char * )( &szfmt + 1 ) );
  MessageBox( hwnd, tempbuff, "Error", MB_ICONERROR | MB_OK | MB_APPLMODAL | MB_SYSTEMMODAL );
}

/* Create our direct sound object and
 * set the cooperative level. */

boolean CreateDirectSound( ao_directx_t * ao_directx )
{
  DSCAPS        dscaps;
  HWND          hxinewnd;

  lprintf("CreateDirectSound(%08x) Enter\n", (unsigned long)ao_directx);

  /* create direct sound object */

  if( DirectSoundCreate( 0, &ao_directx->dsobj, 0 ) != DS_OK )
    {
      Error( 0, "DirectSoundCreate : Unable to create direct sound object" );
      lprintf("CreateDirectSound() Exit! Returning False\n");
      return FALSE;
    }


  /* try to get our current xine window */

  hxinewnd = FindWindow( "xinectrlwindow", "xine" );
  if( !hxinewnd )
    hxinewnd = GetDesktopWindow();

  /* set direct sound cooperative level */

  if( IDirectSound_SetCooperativeLevel( ao_directx->dsobj, hxinewnd, DSSCL_EXCLUSIVE ) != DS_OK )
    {
      Error( 0, "IDirectSound_SetCooperativeLevel : could not set direct sound cooperative level" );
      lprintf("CreateDirectSound() Exit! Returning False\n");
      return FALSE;
    }

  /* get the direct sound device caps */

  memset( &dscaps, 0, sizeof( dscaps ) );
  dscaps.dwSize = sizeof( dscaps );
  if( IDirectSound_GetCaps( ao_directx->dsobj, &dscaps ) != DS_OK )
    {
      Error( 0, "IDirectSound_GetCaps : Unable to get direct sound device capabilities" );
      lprintf("CreateDirectSound() Exit! Returning False\n");
      return FALSE;
    }

  lprintf("CreateDirectSound() Exit! Returning True\n");
  return TRUE;
}

/* Destroy all direct sound allocated
 * resources. */

void DestroyDirectSound( ao_directx_t * ao_directx )
{

  lprintf("DestroyDirectSound(%08x) Enter\n", (unsigned long)ao_directx);

  if( ao_directx->dsobj )
    {
      lprintf("IDirectSound_Release()\n");

      IDirectSound_Release( ao_directx->dsobj );
      ao_directx->dsobj = 0;
    }

  lprintf("DestroyDirectSound() Exit\n");
}

/* Used to create directx sound buffer,
 * notification events, and initialize
 * buffer to null sample data. */

boolean CreateSoundBuffer( ao_directx_t * ao_directx )
{
  DSBUFFERDESC	dsbdesc;
  PCMWAVEFORMAT	pcmwf;

  lprintf("CreateSoundBuffer(%08x) Enter\n", (unsigned long)ao_directx);

  /* calculate buffer and frame size */

  ao_directx->frsz        = ( ao_directx->bits / 8 ) * ao_directx->chnn;
  /* buffer size, must be even and aligned to frame size */
  ao_directx->buffer_size = (ao_directx->frsz * ((ao_directx->rate / SOUND_BUFFER_DIV + 1) & ~1));

  /* release any existing sound buffer
   * related resources */

  DestroySoundBuffer( ao_directx );

  /* create a secondary sound buffer */

  memset( &pcmwf, 0, sizeof( PCMWAVEFORMAT ) );
  pcmwf.wBitsPerSample     = ( unsigned short ) ao_directx->bits;
  pcmwf.wf.wFormatTag      = WAVE_FORMAT_PCM;
  pcmwf.wf.nChannels       = ao_directx->chnn;
  pcmwf.wf.nSamplesPerSec  = ao_directx->rate;
  pcmwf.wf.nBlockAlign     = ao_directx->frsz;
  pcmwf.wf.nAvgBytesPerSec = ao_directx->rate * ao_directx->frsz;

  memset( &dsbdesc, 0, sizeof( DSBUFFERDESC ) );
  dsbdesc.dwSize        = sizeof( DSBUFFERDESC );
  dsbdesc.dwFlags       = (DSBCAPS_CTRLVOLUME | DSBCAPS_GLOBALFOCUS |
			   DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_CTRLPOSITIONNOTIFY);
  dsbdesc.dwBufferBytes = ao_directx->buffer_size;
  dsbdesc.lpwfxFormat   = ( LPWAVEFORMATEX ) &pcmwf;

  if( IDirectSound_CreateSoundBuffer( ao_directx->dsobj, &dsbdesc,
				      &ao_directx->dsbuffer, 0 ) != DS_OK )
    {
      Error( 0, "IDirectSound_CreateSoundBuffer : Unable to create secondary sound buffer" );
      return FALSE;
    }

  /* get the buffer capabilities */

  memset( &ao_directx->dsbcaps, 0, sizeof( DSBCAPS ) );
  ao_directx->dsbcaps.dwSize = sizeof( DSBCAPS );

  if( IDirectSound_GetCaps( ao_directx->dsbuffer, &ao_directx->dsbcaps ) != DS_OK )
    {
      Error( 0, "IDirectSound_GetCaps : Unable to get secondary sound buffer capabilities" );
      return FALSE;
    }

  /* create left side notification ( non-signaled ) */

  ao_directx->notify_events[ 0 ].hEventNotify = CreateEvent( NULL, FALSE, FALSE, NULL );

  /* create right side notification ( signaled ) */

  ao_directx->notify_events[ 1 ].hEventNotify = CreateEvent( NULL, FALSE, FALSE, NULL );

  if( !ao_directx->notify_events[ 0 ].hEventNotify || !ao_directx->notify_events[ 1 ].hEventNotify )
    {
      Error( 0, "CreateEvent : Unable to create sound notification events" );
      return FALSE;
    }

  /* get the direct sound notification interface */

  if( IDirectSoundBuffer_QueryInterface( ao_directx->dsbuffer,
					 &IID_IDirectSoundNotify,
					 (LPVOID *)&ao_directx->notify ) != DS_OK )
    {
      Error( 0, "IDirectSoundBuffer_QueryInterface : Unable to get notification interface" );
      return FALSE;
    }

  /* set notification events */

  ao_directx->notify_events[ 0 ].dwOffset = 0;
  ao_directx->notify_events[ 1 ].dwOffset = ao_directx->buffer_size / 2;

  if( IDirectSoundNotify_SetNotificationPositions(  ao_directx->notify, 2,
						    ao_directx->notify_events ) != DS_OK )
    {
      Error( 0, "IDirectSoundNotify_SetNotificationPositions : Unable to set notification positions" );
      return FALSE;
    }

  /* DEBUG : set sound buffer volume */

  if( IDirectSoundBuffer_SetVolume( ao_directx->dsbuffer, DSBVOLUME_MAX ) != DS_OK )
    {
      Error( 0, "IDirectSoundBuffer_SetVolume : Unable to set sound buffer volume" );
      return FALSE;
    }

  /* initialize our sound buffer */

  IDirectSoundBuffer_SetVolume( ao_directx->dsbuffer, DSBVOLUME_MIN );
  FillSoundBuffer( ao_directx, DSBUFF_INIT, 0 );

  return TRUE;

  lprintf("CreateSoundBuffer() Exit\n");
}

/* Destroy all direct sound buffer allocated
 * resources. */

void DestroySoundBuffer( ao_directx_t * ao_directx )
{
  lprintf("DestroySoundBuffer(%08x) Enter\n", (unsigned long)ao_directx);

  /* stop our buffer and zero it out */

  if( ao_directx->dsbuffer )
    {
      IDirectSoundBuffer_SetVolume( ao_directx->dsbuffer, DSBVOLUME_MIN );
      IDirectSoundBuffer_Stop( ao_directx->dsbuffer );
      FillSoundBuffer( ao_directx, DSBUFF_INIT, 0 );
    }

  /* release our notification events */

  if( ao_directx->notify_events[ 0 ].hEventNotify )
    {
      CloseHandle( ao_directx->notify_events[ 0 ].hEventNotify );
      ao_directx->notify_events[ 0 ].hEventNotify = 0;
    }

  if( ao_directx->notify_events[ 1 ].hEventNotify )
    {
      CloseHandle( ao_directx->notify_events[ 1 ].hEventNotify );
      ao_directx->notify_events[ 1 ].hEventNotify = 0;
    }

  /* release our buffer notification interface */

  if( ao_directx->notify )
    {
      IDirectSoundNotify_Release( ao_directx->notify );
      ao_directx->notify = 0;
    }

  /* release our direct sound buffer */

  if( ao_directx->dsbuffer )
    {
      IDirectSoundBuffer_Release( ao_directx->dsbuffer );
      ao_directx->dsbuffer = 0;
    }

  lprintf("DestroySoundBuffer() Exit\n");
}

/* Used to fill our looping sound buffer
 * with data. */

uint32_t FillSoundBuffer( ao_directx_t * ao_directx, int code, unsigned char * samples )
{
  uint8_t *     buff_pointer;   /* pointer inside circular buffer */
  DWORD         buff_length;    /* bytes locked by pointer */
  uint32_t      half_size;      /* half our sound buffer size */
  uint32_t      result;         /* error result */

#ifdef LOG
  if ((void*)samples != (void*)0)
    printf("audio_directx_out: FillSoundBuffer(%08x, %d, Null) Enter\n", (unsigned long)ao_directx, code);
  else
    printf("audio_directx_out: FillSoundBuffer(%08x, %d, Null) Enter\n", (unsigned long)ao_directx, code);
#endif

  half_size = ao_directx->buffer_size / 2;

  if( code == DSBUFF_INIT )
    {
      lprintf("FillSoundBuffer: DSBUFF_INIT\n");

      /* set our new status code */

      ao_directx->write_status = DSBUFF_RIGHT;

      /* lock our sound buffer for write access */

      result = IDirectSoundBuffer_Lock( ao_directx->dsbuffer,
					0, 0,
					(LPVOID *)&buff_pointer, &buff_length,
					NULL, 0, DSBLOCK_ENTIREBUFFER );
      if( result  != DS_OK )
	{
	  Error( 0, "IDirectSoundBuffer_Lock : could not lock sound buffer" );
	  return 0;
	}

      /* clear our entire sound buffer */

      memset( buff_pointer, 0, buff_length );

      /* unlock our sound buffer */

      if( IDirectSoundBuffer_Unlock( ao_directx->dsbuffer,
				     buff_pointer, buff_length,
				     0, 0 ) != DS_OK )
	{
	  Error( 0, "IDirectSoundBuffer_Unlock : could not unlock sound buffer" );
	  return 0;
	}

      /* start the buffer playing */

      if( IDirectSoundBuffer_Play( ao_directx->dsbuffer, 0, 0, DSBPLAY_LOOPING ) != DS_OK )
	{
	  Error( 0, "IDirectSoundBuffer_Play : could not play sound buffer" );
	  return 0 ;
	}
      else
	IDirectSoundBuffer_SetVolume( ao_directx->dsbuffer, ao_directx->volume );
    }
  else if( code == DSBUFF_LEFT )
    {
      lprintf("FillSoundBuffer: DSBUFF_LEFT\n");
      /* set our new status code */

      ao_directx->write_status = DSBUFF_RIGHT;

      /* lock our sound buffer for write access */

      result = IDirectSoundBuffer_Lock( ao_directx->dsbuffer,
					0, half_size,
					(LPVOID *)&buff_pointer, &buff_length,
					0, 0, 0 );
      if( result  != DS_OK )
	{
	  Error( 0, "IDirectSoundBuffer_Lock : could not lock sound buffer" );
	  return 0;
	}

      /* write data to our sound buffer */

      memcpy( buff_pointer, samples, buff_length );

      /* unlock our sound buffer */

      if( IDirectSoundBuffer_Unlock( ao_directx->dsbuffer,
				     buff_pointer, buff_length,
				     0, 0 ) != DS_OK )
	{
	  Error( 0, "IDirectSoundBuffer_Unlock : could not unlock sound buffer" );
	  return 0;
	}

    }
  else if( code == DSBUFF_RIGHT )
    {
      lprintf("FillSoundBuffer: DSBUFF_RIGHT\n");
      /* set our new status code */

      ao_directx->write_status = DSBUFF_LEFT;

      /* lock our sound buffer for write access */

      result = IDirectSoundBuffer_Lock( ao_directx->dsbuffer,
					half_size, half_size,
					(LPVOID *)&buff_pointer, &buff_length,
					0, 0, 0 );
      if( result  != DS_OK )
	{
	  Error( 0, "IDirectSoundBuffer_Lock : could not lock sound buffer" );
	  return 0;
	}

      /* write data to our sound buffer */

      memcpy( buff_pointer, samples, buff_length );

      /* unlock our sound buffer */

      if( IDirectSoundBuffer_Unlock( ao_directx->dsbuffer,
				     buff_pointer, buff_length,
				     0, 0 ) != DS_OK )
	{
	  Error( 0, "IDirectSoundBuffer_Unlock : could not unlock sound buffer" );
	  return 0;
	}
    }

  lprintf("FillSoundBuffer() Exit\n");

  return buff_length;
}

/* -----------------------------------------
 *
 * BEGIN : Xine driver audio output plugin
 *         handlers.
 *
 * ----------------------------------------- */

static int ao_directx_control(ao_driver_t *this_gen, int cmd, ...) {
  switch (cmd)
    {

    case AO_CTRL_PLAY_PAUSE:
      break;

    case AO_CTRL_PLAY_RESUME:
      break;

    case AO_CTRL_FLUSH_BUFFERS:
      break;
    }

  return 0;
}


static int ao_directx_open( ao_driver_t * ao_driver, uint32_t bits, uint32_t rate, int mode )
{
  ao_directx_t  *ao_directx = ( ao_directx_t * ) ao_driver;

  lprintf("ao_directx_open(%08x, %d, %d, %d) Enter\n", (unsigned long)ao_directx, bits, rate, mode);

  /* store input rate and bits */

  ao_directx->bits = bits;
  ao_directx->rate = rate;

  /* store channel count */

  switch( mode )
    {
    case AO_CAP_MODE_MONO:
      ao_directx->chnn = 1;
      xprintf(ao_directx->xine, XINE_VERBOSITY_DEBUG, "ao_directx : opened in AO_CAP_MODE_MONO mode\n" );
      break;

    case AO_CAP_MODE_STEREO:
      ao_directx->chnn = 2;
      xprintf(ao_directx->xine, XINE_VERBOSITY_DEBUG, "ao_directx : opened in AO_CAP_MODE_STEREO mode\n" );
      break;

    case AO_CAP_MODE_4CHANNEL:
      ao_directx->chnn = 4;
      xprintf(ao_directx->xine, XINE_VERBOSITY_DEBUG, "ao_directx : opened in AO_CAP_MODE_4CHANNEL mode\n" );
      break;

    case AO_CAP_MODE_5CHANNEL:
      ao_directx->chnn = 5;
      xprintf(ao_directx->xine, XINE_VERBOSITY_DEBUG, "ao_directx : opened in AO_CAP_MODE_5CHANNEL mode\n" );
      break;

    case AO_CAP_MODE_5_1CHANNEL:
      ao_directx->chnn = 6;
      xprintf(ao_directx->xine, XINE_VERBOSITY_DEBUG, "ao_directx : opened in AO_CAP_MODE_5_1CHANNEL mode\n" );
      break;

    case AO_CAP_MODE_A52:
    case AO_CAP_MODE_AC5:
      return 0;
    }

  if (!CreateSoundBuffer( ao_directx )) return 0;

  lprintf("ao_directx_open() Exit! Returning ao_directx->rate=%d\n", ao_directx->rate);

  return ao_directx->rate;
}

static int ao_directx_num_channels( ao_driver_t * ao_driver )
{
  ao_directx_t  *ao_directx = ( ao_directx_t * ) ao_driver;
  return ao_directx->chnn;
}

static int ao_directx_bytes_per_frame( ao_driver_t * ao_driver )
{
  ao_directx_t  *ao_directx = ( ao_directx_t * ) ao_driver;
  return ao_directx->frsz;
}

static int ao_directx_get_gap_tolerance( ao_driver_t * ao_driver )
{
  return 5000;
}

static int ao_directx_delay( ao_driver_t * ao_driver )
{
  DWORD	         current_read;
  DWORD	         bytes_left;
  DWORD	         frames_left;
  ao_directx_t  *ao_directx = ( ao_directx_t * ) ao_driver;

  lprintf("ao_directx_delay(%08x) Enter\n", (unsigned long)ao_directx);

  IDirectSoundBuffer_GetCurrentPosition( ao_directx->dsbuffer, &current_read, 0 );

  if( ao_directx->write_pos > current_read )
    bytes_left = ( ao_directx->write_pos - current_read );
  else
    bytes_left = ( ao_directx->write_pos + ao_directx->buffer_size - current_read );

  frames_left = ( ao_directx->prebuff_size + bytes_left ) / ao_directx->frsz;

  lprintf("ao_directx_delay() Exit! Returning frames_left=%d\n", frames_left);

  return frames_left;
}

static int ao_directx_write( ao_driver_t * ao_driver, int16_t * frame_buffer, uint32_t num_frames )
{
  ao_directx_t  *ao_directx = ( ao_directx_t * ) ao_driver;
  uint32_t	 frame_bytes;   /* how many bytes to lock */
  uint32_t	 wrote;	        /* number of bytes written */
  uint32_t	 half_size;     /* half our sound buffer size */

  lprintf("ao_directx_write(%08x, %08x, %d) Enter\n",
	  (unsigned long)ao_directx, (unsigned long)frame_buffer, num_frames);

  /* zero write counter */

  wrote = 0;

  /* calculate how many bytes in frame_buffer */

  frame_bytes = num_frames * ao_directx->frsz;

  /* calculate half our buffer size */

  half_size = ao_directx->buffer_size / 2;

  /* fill audio prebuff */

  memcpy( ao_directx->prebuff + ao_directx->prebuff_size, frame_buffer, frame_bytes );
  ao_directx->prebuff_size = ao_directx->prebuff_size + frame_bytes;

  /* check to see if we have enough in prebuff to
   * fill half of our sound buffer */
  while( ao_directx->prebuff_size >= half_size )
    {
      /* write to our sound buffer */

      if( ao_directx->write_status == DSBUFF_LEFT )
	{
	  /* wait for our read pointer to reach the right half
	   * of our sound buffer, we only want to write to the
	   * left side */

	  WaitForSingleObject( ao_directx->notify_events[ 1 ].hEventNotify, INFINITE );

	  /* fill left half of our buffer */

	  wrote = FillSoundBuffer( ao_directx, DSBUFF_LEFT, ao_directx->prebuff );
	}
      else if( ao_directx->write_status == DSBUFF_RIGHT )
	{
	  /* wait for our read pointer to reach the left half,
	   * of our sound buffer, we only want to write to the
	   * right side */

	  WaitForSingleObject( ao_directx->notify_events[ 0 ].hEventNotify, INFINITE );

	  /* fill right half of our buffer */

	  wrote = FillSoundBuffer( ao_directx, DSBUFF_RIGHT, ao_directx->prebuff );
	}

      /* calc bytes written and store position for next write */

      ao_directx->write_pos = ( ao_directx->write_pos + wrote ) % ao_directx->buffer_size;

      /* copy remaining contents of prebuff and recalc size */

      memcpy( ao_directx->prebuff, ao_directx->prebuff + wrote, ao_directx->prebuff_size - wrote );
      ao_directx->prebuff_size = ao_directx->prebuff_size - wrote;
    }

  lprintf("ao_directx_write() Exit! Returning num_frmaes=%d\n", num_frames);

  return num_frames;
}

static void ao_directx_close( ao_driver_t * ao_driver )
{
  ao_directx_t  *ao_directx = ( ao_directx_t * ) ao_driver;

  lprintf("ao_directx_close(%08x) Enter\n", (unsigned long)ao_directx);

  /* release any existing sound buffer
   * related resources */

  DestroySoundBuffer( ao_directx );

  lprintf("ao_directx_close() Exit!\n");
}

static uint32_t ao_directx_get_capabilities( ao_driver_t * ao_driver )
{
  return AO_CAP_MODE_STEREO | AO_CAP_MIXER_VOL | AO_CAP_PCM_VOL | AO_CAP_MUTE_VOL;
}

static void ao_directx_exit( ao_driver_t * ao_driver )
{
  ao_directx_t  *ao_directx = ( ao_directx_t * ) ao_driver;

  lprintf("ao_directx_exit(%08x) Enter\n", (unsigned long)ao_directx);

  /* release any existing sound buffer
   * related resources */

  DestroySoundBuffer( ao_directx );

  /* release any existing direct sound
   * related resources */

  DestroyDirectSound( ao_directx );

  /* free our driver */

  free( ao_directx );

  lprintf("ao_directx_exit() Exit!\n");
}

static int ao_directx_get_property( ao_driver_t * ao_driver, int property )
{
  return 0;
}

static int ao_directx_set_property( ao_driver_t * ao_driver, int property, int value )
{
  ao_directx_t  *ao_directx = ( ao_directx_t * ) ao_driver;

  lprintf("ao_directx_set_property(%08x, %d, %d) Enter\n",
	  (unsigned long)ao_directx, property, value);

  switch( property )
    {
    case AO_PROP_PCM_VOL:
    case AO_PROP_MIXER_VOL:
      lprintf("ao_directx_set_property: AO_PROP_PCM_VOL|AO_PROP_MIXER_VOL\n");

      ao_directx->volume = value * ( DSBVOLUME_MIN / 100 / 3);

      if( !ao_directx->mute && ao_directx->dsbuffer )
	IDirectSoundBuffer_SetVolume( ao_directx->dsbuffer, ao_directx->volume );

      xprintf(ao_directx->xine, XINE_VERBOSITY_DEBUG,
	      "ao_directx : volume set to %d - directX volume = %d\n", value, ao_directx->volume);

      return value;

      break;

    case AO_PROP_MUTE_VOL:
      lprintf("ao_directx_set_property: AO_PROP_MUTE_VOL\n");

      ao_directx->mute = value;

      if( !ao_directx->mute && ao_directx->dsbuffer )
	IDirectSoundBuffer_SetVolume( ao_directx->dsbuffer, ao_directx->volume );

      if( ao_directx->mute && ao_directx->dsbuffer )
	IDirectSoundBuffer_SetVolume( ao_directx->dsbuffer, DSBVOLUME_MIN );

      xprintf(ao_directx->xine, XINE_VERBOSITY_DEBUG, "ao_directx : mute toggled" );

      return value;

      break;
    }

  lprintf("ao_directx_set_property() Exit! Returning ~value=%d\n", ~value);

  return ~value;
}

static ao_driver_t *open_plugin (audio_driver_class_t *class_gen, const void *data)
{
  audiox_class_t  *class = (audiox_class_t *) class_gen;
  ao_directx_t    *ao_directx;

  ao_directx = calloc(1, sizeof(ao_directx_t));
  if (!ao_directx)
    return NULL;

  lprintf("open_plugin(%08x, %08x) Enter\n", (unsigned long)class_gen, (unsigned long)data);
  lprintf("open_plugin: ao_directx=%08x\n", (unsigned long)ao_directx);

  ao_directx->xine                              = class->xine;

  ao_directx->ao_driver.get_capabilities        = ao_directx_get_capabilities;
  ao_directx->ao_driver.get_property	        = ao_directx_get_property;
  ao_directx->ao_driver.set_property            = ao_directx_set_property;
  ao_directx->ao_driver.open	                = ao_directx_open;
  ao_directx->ao_driver.num_channels            = ao_directx_num_channels;
  ao_directx->ao_driver.bytes_per_frame	        = ao_directx_bytes_per_frame;
  ao_directx->ao_driver.delay                   = ao_directx_delay;
  ao_directx->ao_driver.write                   = ao_directx_write;
  ao_directx->ao_driver.close                   = ao_directx_close;
  ao_directx->ao_driver.exit                    = ao_directx_exit;
  ao_directx->ao_driver.get_gap_tolerance       = ao_directx_get_gap_tolerance;
  ao_directx->ao_driver.control                 = ao_directx_control;

  CreateDirectSound( ao_directx );

  lprintf("open_plugin() Exit! Returning ao_directx=%08x\n", (unsigned long)ao_directx);

  return ( ao_driver_t * ) ao_directx;
}

static void *init_class (xine_t *xine, void *data) {
  audiox_class_t    *audiox;

  lprintf("init_class() Enter\n");

  /*
   * from this point on, nothing should go wrong anymore
   */
  audiox = calloc(1, sizeof (audiox_class_t));
  if (!audiox)
    return NULL;

  audiox->driver_class.open_plugin     = open_plugin;
  audiox->driver_class.identifier      = "DirectX";
  audiox->driver_class.description     = N_("xine audio output plugin for win32 using directx");
  audiox->driver_class.dispose         = default_audio_driver_class_dispose;

  audiox->xine                         = xine;
  audiox->config                       = xine->config;

  lprintf("init_class() Exit! Returning audiox=%08x\n", audiox);

  return audiox;
}

static const ao_info_t ao_info_directx = {
  1                    /* priority        */
};

/*
 * exported plugin catalog entry
 */
const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_AUDIO_OUT, AO_DIRECTX_IFACE_VERSION, "directx", XINE_VERSION_CODE, &ao_info_directx, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
