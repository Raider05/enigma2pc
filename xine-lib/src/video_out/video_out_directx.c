/*
 * Copyright (C) 2000-2005 the xine project
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
 * video_out_directx.c, direct draw video output plugin for xine
 * by Matthew Grooms <elon@altavista.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

typedef unsigned char boolean;

#include <windows.h>
#include <ddraw.h>

#include <pthread.h>

#define LOG_MODULE "video_out_directx"
#define LOG_VERBOSE
/*
#define LOG
*/

#include "xine.h"
#include <xine/video_out.h>
#include <xine/xine_internal.h>

#include <xine/xine_internal.h>
#include "yuv2rgb.h"

#define NEW_YUV 1

/* Set to 1 for RGB support */
#define RGB_SUPPORT          0

#define BORDER_SIZE	     8
#define IMGFMT_NATIVE	     4

/*****************************************************************************
 * DirectDraw GUIDs.
 * Defining them here allows us to get rid of the dxguid library during
 * the linking stage.
 *****************************************************************************/
#if 1
static const GUID xine_IID_IDirectDraw = {
	0x6C14DB80,0xA733,0x11CE,{0xA5,0x21,0x00,0x20,0xAF,0x0B,0xE5,0x60}
};
#ifdef IID_IDirectDraw
#  undef IID_IDirectDraw
#endif
#define IID_IDirectDraw xine_IID_IDirectDraw
#endif

#if 0
static const GUID IID_IDirectDraw2 = {
	0xB3A6F3E0,0x2B43,0x11CF,{0xA2,0xDE,0x00,0xAA,0x00,0xB9,0x33,0x56}
};
#ifdef IID_IDirectDraw2
#  undef IID_IDirectDraw2
#endif
#define IID_IDirectDraw2 xine_IID_IDirectDraw2
#endif

#if 0
static const GUID IID_IDirectDraw4 = {
	0x9C59509A,0x39BD,0x11D1,{0x8C,0x4A,0x00,0xC0,0x4F,0xD9,0x30,0xC5}
};
#ifdef IID_IDirectDraw4
#  undef IID_IDirectDraw4
#endif
#define IID_IDirectDraw4 xine_IID_IDirectDraw4
#endif

/* -----------------------------------------
 *
 *  vo_directx frame struct
 *
 * ----------------------------------------- */

typedef struct {
  vo_frame_t               vo_frame;
  uint8_t                 *buffer;
  int			   format;
  uint32_t			   width;
  uint32_t			   height;
  int		           size;
  double	           ratio;
} win32_frame_t;

/* -----------------------------------------
 *
 *  vo_directx driver struct
 *
 * ----------------------------------------- */

typedef enum {
  VO_DIRECTX_HWACCEL_FULL = 0,
  VO_DIRECTX_HWACCEL_SCALE = 1,
  VO_DIRECTX_HWACCEL_NONE = 2
} vo_directx_hwaccel_enum;

typedef struct {
  vo_driver_t		   vo_driver;
  win32_visual_t          *win32_visual;

  xine_t                  *xine;

  LPDIRECTDRAW		   ddobj;	    /* direct draw object */
  LPDIRECTDRAWSURFACE	   primary;	    /* primary dd surface */
  LPDIRECTDRAWSURFACE	   secondary;	    /* secondary dd surface  */
  LPDIRECTDRAWCLIPPER	   ddclipper;	    /* dd clipper object */
  uint8_t *		   contents;	    /* secondary contents */
  win32_frame_t           *current;         /* current frame */

  int			   req_format;	    /* requested frame format */
  int			   act_format;	    /* actual frame format */
  uint32_t			   width;	    /* frame with */
  uint32_t			   height;	    /* frame height */
  double		   ratio;	    /* frame ratio */
  vo_directx_hwaccel_enum  hwaccel;         /* requested level of HW acceleration */

  yuv2rgb_factory_t       *yuv2rgb_factory; /* used for format conversion */
  yuv2rgb_t               *yuv2rgb;         /* used for format conversion */
  int			   mode;	    /* rgb mode */
  int		           bytespp;	    /* rgb bits per pixel */
  DDPIXELFORMAT primary_pixel_format;
  DDSURFACEDESC	ddsd; /* set by Lock(), used during display_frame */
  alphablend_t             alphablend_extra_data;
} win32_driver_t;

typedef struct {
  video_driver_class_t     driver_class;
  config_values_t         *config;
  xine_t                  *xine;
} directx_class_t;

char *config_hwaccel_values[] = {"full", "scale", "none", NULL };

/* -----------------------------------------
 *
 *  BEGIN : Direct Draw and win32 handlers
 *          for xine video output plugins.
 *
 * ----------------------------------------- */

/* Display formatted error message in
 * popup message box.*/

static void Error( HWND hwnd, LPSTR szfmt, ... )
{
  char tempbuff[ 256 ];
  *tempbuff = 0;
  wvsprintf(	&tempbuff[ strlen( tempbuff ) ], szfmt, ( char * )( &szfmt + 1 ) );
  MessageBox( hwnd, tempbuff, "Error", MB_ICONERROR | MB_OK | MB_APPLMODAL | MB_SYSTEMMODAL );
}

/* Update our drivers current knowledge
 * of our windows video out posistion */

static void UpdateRect( win32_visual_t * win32_visual )
{
  if( win32_visual->FullScreen )
    {
      SetRect( &win32_visual->WndRect, 0, 0,
	       GetSystemMetrics( SM_CXSCREEN ),
	       GetSystemMetrics( SM_CYSCREEN ) );
    }
  else
    {
      GetClientRect( win32_visual->WndHnd, &win32_visual->WndRect );
      ClientToScreen( win32_visual->WndHnd, ( POINT * ) &win32_visual->WndRect );
      ClientToScreen( win32_visual->WndHnd, ( POINT * ) &win32_visual->WndRect + 1 );
    }
}

/* Create our direct draw object, primary
 * surface and clipper object.
 *
 * NOTE : The primary surface is more or
 * less a viewport into the parent desktop
 * window and will always have a pixel format
 * identical to the current display mode. */

static boolean CreatePrimary( win32_driver_t * win32_driver )
{
  LPDIRECTDRAW			ddobj;
  DDSURFACEDESC			ddsd;
  HRESULT					result;

  /* create direct draw object */

  result = DirectDrawCreate( 0, &ddobj, 0 );
  if( result != DD_OK )
    {
      Error( 0, "DirectDrawCreate : error %ld", result );
      xprintf(win32_driver->xine, XINE_VERBOSITY_DEBUG, "vo_out_directx : DirectDrawCreate : error %ld\n", result );
      return 0;
    }

  /* set cooperative level */

  result = IDirectDraw_SetCooperativeLevel( ddobj, win32_driver->win32_visual->WndHnd, DDSCL_NORMAL );
  if( result != DD_OK )
    {
      Error( 0, "SetCooperativeLevel : error 0x%lx", result );
      return 0;
    }

  /* try to get new interface */

  result = IDirectDraw_QueryInterface( ddobj, &IID_IDirectDraw, (LPVOID *) &win32_driver->ddobj );
  if( result != DD_OK )
    {
      Error( 0, "ddobj->QueryInterface : DirectX required" );
      return 0;
    }

  /* release our old interface */

  IDirectDraw_Release( ddobj );

  /* create primary_surface */

  memset( &ddsd, 0, sizeof( ddsd ) );
  ddsd.dwSize         = sizeof( ddsd );
  ddsd.dwFlags        = DDSD_CAPS;
  ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;

  result = IDirectDraw_CreateSurface( win32_driver->ddobj, &ddsd, &win32_driver->primary, 0 );
  if( result != DD_OK )
    {
      Error( 0, "CreateSurface ( primary ) : error 0x%lx", result );
      return 0;
    }

  /* create our clipper object */

  result = IDirectDraw_CreateClipper( win32_driver->ddobj, 0, &win32_driver->ddclipper, 0 );
  if( result != DD_OK )
    {
      Error( 0, "CreateClipper : error 0x%lx", result );
      return 0;
    }

  /* associate our clipper with our window */

  result = IDirectDrawClipper_SetHWnd( win32_driver->ddclipper, 0, win32_driver->win32_visual->WndHnd );
  if( result != DD_OK )
    {
      Error( 0, "ddclipper->SetHWnd : error 0x%lx", result );
      return 0;
    }

  /* associate our primary surface with our clipper */

  result = IDirectDrawSurface_SetClipper( win32_driver->primary, win32_driver->ddclipper );
  if( result != DD_OK )
    {
      Error( 0, "ddclipper->SetHWnd : error 0x%lx", result );
      return 0;
    }

  /* store our objects in our visual struct */

  UpdateRect( win32_driver->win32_visual );

  return 1;
}

/* Create our secondary ( off screen ) buffer.
 * The optimal secondary buffer is a h/w
 * overlay with the same pixel format as the
 * xine frame type. However, since this is
 * not always supported by the host h/w,
 * we will fall back to creating an rgb buffer
 * in video memory qith the same pixel format
 * as the primary surface. At least then we
 * can use h/w scaling if supported. */

static boolean CreateSecondary( win32_driver_t * win32_driver, int width, int height, int format )
{
  DDSURFACEDESC ddsd;
  HRESULT result;

  if( format == XINE_IMGFMT_YV12 )
    xprintf(win32_driver->xine, XINE_VERBOSITY_DEBUG, "vo_out_directx : switching to YV12 overlay type\n" );

  if( format == XINE_IMGFMT_YUY2 )
    xprintf(win32_driver->xine, XINE_VERBOSITY_DEBUG, "vo_out_directx : switching to YUY2 overlay type\n" );

#if RGB_SUPPORT
  if( format == IMGFMT_RGB )
    xprintf(win32_driver->xine, XINE_VERBOSITY_DEBUG, "vo_out_directx : switching to RGB overlay type\n" );
#endif

  if( !win32_driver->ddobj )
    return FALSE;

  /* store our reqested format,
   * width and height */

  win32_driver->req_format	= format;
  win32_driver->width			= width;
  win32_driver->height		= height;

  /* if we already have a secondary
   * surface then release it */

  if( win32_driver->secondary )
    IDirectDrawSurface_Release( win32_driver->secondary );

  memset( &ddsd, 0, sizeof( ddsd ) );
  ddsd.dwSize         = sizeof( ddsd );
  ddsd.dwWidth        = width;
  ddsd.dwHeight       = height;

  if (win32_driver->hwaccel <= VO_DIRECTX_HWACCEL_FULL) {

  if( format == XINE_IMGFMT_YV12 )
    {
      /* the requested format is XINE_IMGFMT_YV12 */

      ddsd.dwFlags                       = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
      ddsd.ddsCaps.dwCaps                = DDSCAPS_VIDEOMEMORY | DDSCAPS_OVERLAY;
      ddsd.ddpfPixelFormat.dwSize        = sizeof(DDPIXELFORMAT);
      ddsd.ddpfPixelFormat.dwFlags       = DDPF_FOURCC;
      ddsd.ddpfPixelFormat.dwYUVBitCount = 16;
      ddsd.ddpfPixelFormat.dwFourCC      = mmioFOURCC( 'Y', 'V', '1', '2' );

      lprintf("CreateSecondary() - act_format = (YV12) %d\n", XINE_IMGFMT_YV12);

      win32_driver->act_format = XINE_IMGFMT_YV12;
    }

  if( format == XINE_IMGFMT_YUY2 )
    {
      /* the requested format is XINE_IMGFMT_YUY2 */

      ddsd.dwFlags                       = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
      ddsd.ddsCaps.dwCaps                = DDSCAPS_VIDEOMEMORY | DDSCAPS_OVERLAY;
      ddsd.ddpfPixelFormat.dwSize        = sizeof(DDPIXELFORMAT);
      ddsd.ddpfPixelFormat.dwFlags       = DDPF_FOURCC;
      ddsd.ddpfPixelFormat.dwYUVBitCount = 16;
      ddsd.ddpfPixelFormat.dwFourCC      = mmioFOURCC( 'Y', 'U', 'Y', '2' );

      lprintf("CreateSecondary() - act_format = (YUY2) %d\n", XINE_IMGFMT_YUY2);

      win32_driver->act_format = XINE_IMGFMT_YUY2;
    }

#if RGB_SUPPORT
  if( format == IMGFMT_RGB )
    {
      /* the requested format is IMGFMT_RGB */

      ddsd.dwFlags                       = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
      ddsd.ddsCaps.dwCaps                = DDSCAPS_VIDEOMEMORY | DDSCAPS_OVERLAY;
      ddsd.ddpfPixelFormat.dwSize        = sizeof(DDPIXELFORMAT);
      ddsd.ddpfPixelFormat.dwFlags       = DDPF_RGB;
      ddsd.ddpfPixelFormat.dwYUVBitCount = 24;
      ddsd.ddpfPixelFormat.dwRBitMask    = 0xff0000;
      ddsd.ddpfPixelFormat.dwGBitMask    = 0x00ff00;
      ddsd.ddpfPixelFormat.dwBBitMask    = 0x0000ff;

      lprintf("CreateSecondary() - act_format = (RGB) %d\n", IMGFMT_RGB);

      win32_driver->act_format = IMGFMT_RGB;
    }
#endif /* RGB_SUPPORT */

  lprintf("CreateSecondary() - IDirectDraw_CreateSurface()\n");
  if( IDirectDraw_CreateSurface( win32_driver->ddobj, &ddsd, &win32_driver->secondary, 0 ) == DD_OK )
    return TRUE;

  }

  if (win32_driver->hwaccel <= VO_DIRECTX_HWACCEL_SCALE) {

  /*  Our fallback method is to create a back buffer
   *  with the same image format as the primary surface */

  lprintf("CreateSecondary() - Falling back to back buffer same as primary\n");
  lprintf("CreateSecondary() - act_format = (NATIVE) %d\n", IMGFMT_NATIVE);

  ddsd.dwFlags        = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
  ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
  ddsd.ddpfPixelFormat = win32_driver->primary_pixel_format;
  win32_driver->act_format = IMGFMT_NATIVE;

  if( IDirectDraw_CreateSurface( win32_driver->ddobj, &ddsd, &win32_driver->secondary, 0 ) == DD_OK )
    return TRUE;

  }

  /*  Our second fallback - all w/o HW acceleration */
  lprintf("CreateSecondary() - Falling back, disabling HW acceleration \n");
  ddsd.dwFlags        = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
  ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
  win32_driver->act_format = IMGFMT_NATIVE;
  if( (result = IDirectDraw_CreateSurface( win32_driver->ddobj, &ddsd, &win32_driver->secondary, 0 )) == DD_OK )
    return TRUE;

  /* This is bad. We cant even create a surface with
   * the same format as the primary surface. */
  Error( 0, "CreateSurface ( Secondary ) : unable to create a suitable rendering surface: 0x%08lX", result );
  return FALSE;
}

/* Destroy all direct draw driver allocated
 * resources. */

static void Destroy( win32_driver_t * win32_driver )
{
  if( win32_driver->ddclipper )
    IDirectDrawClipper_Release( win32_driver->ddclipper );

  if( win32_driver->primary )
    IDirectDrawSurface_Release( win32_driver->primary );

  if( win32_driver->secondary )
    IDirectDrawSurface_Release( win32_driver->secondary );

  if( win32_driver->ddobj )
    IDirectDraw_Release( win32_driver->ddobj );

  _x_alphablend_free(&win32_driver->alphablend_extra_data);

  free( win32_driver );
}

/* Check the current pixel format of the
 * display mode. This is neccesary in case
 * the h/w does not support an overlay for
 * the native frame format. */

static boolean CheckPixelFormat( win32_driver_t * win32_driver )
{
  DDPIXELFORMAT	ddpf;
  HRESULT			result;

  /* get the pixel format of our primary surface */

  memset( &ddpf, 0, sizeof( DDPIXELFORMAT ));
  ddpf.dwSize = sizeof( DDPIXELFORMAT );
  result = IDirectDrawSurface_GetPixelFormat( win32_driver->primary, &ddpf );
  if( result != DD_OK )
    {
      Error( 0, "IDirectDrawSurface_GetPixelFormat ( CheckPixelFormat ) : error 0x%lx", result );
      return 0;
    }

  /* store pixel format for CreateSecondary */

  win32_driver->primary_pixel_format = ddpf;

  /* TODO : support paletized video modes */

  if( ( ddpf.dwFlags & DDPF_PALETTEINDEXED1 ) ||
      ( ddpf.dwFlags & DDPF_PALETTEINDEXED2 ) ||
      ( ddpf.dwFlags & DDPF_PALETTEINDEXED4 ) ||
      ( ddpf.dwFlags & DDPF_PALETTEINDEXED8 ) ||
      ( ddpf.dwFlags & DDPF_PALETTEINDEXEDTO8 ) )
    return FALSE;

  /* store bytes per pixel */

  win32_driver->bytespp = ddpf.dwRGBBitCount / 8;

  /* find the rgb mode for software
   * colorspace conversion */

  if( ddpf.dwRGBBitCount == 32 )
    {
      if( ddpf.dwRBitMask == 0xff0000 )
	win32_driver->mode = MODE_32_RGB;
      else
	win32_driver->mode = MODE_32_BGR;
    }

  if( ddpf.dwRGBBitCount == 24 )
    {
      if( ddpf.dwRBitMask == 0xff0000 )
	win32_driver->mode = MODE_24_RGB;
      else
	win32_driver->mode = MODE_24_BGR;
    }

  if( ddpf.dwRGBBitCount == 16 )
    {
      if( ddpf.dwRBitMask == 0xf800 )
	win32_driver->mode = MODE_16_RGB;
      else
	win32_driver->mode = MODE_16_BGR;
    }

  if( ddpf.dwRGBBitCount == 15 )
    {
      if( ddpf.dwRBitMask == 0x7C00 )
	win32_driver->mode = MODE_15_RGB;
      else
	win32_driver->mode = MODE_15_BGR;
    }

	lprintf("win32 mode: %u\n", win32_driver->mode);
  return TRUE;
}

#if 0
/* Create a Direct draw surface from
 * a bitmap resource..
 *
 * NOTE : This is not really useful
 * anymore since the xine logo code is
 * being pushed to the backend. */


static LPDIRECTDRAWSURFACE CreateBMP( win32_driver_t * win32_driver, int resource )
{
  LPDIRECTDRAWSURFACE	bmp_surf;
  DDSURFACEDESC	bmp_ddsd;
  HBITMAP		bmp_hndl;
  BITMAP		bmp_head;
  HDC			hdc_dds;
  HDC			hdc_mem;

  /* load our bitmap from a resource */

  if( !( bmp_hndl = LoadBitmap( win32_driver->win32_visual->HInst, MAKEINTRESOURCE( resource ) ) ) )
    {
      Error( 0, "CreateBitmap : could not load bmp resource" );
      return 0;
    }

  /* create an off screen surface with
   * the same dimentions as our bitmap */

  GetObject( bmp_hndl, sizeof( bmp_head ), &bmp_head );

  memset( &bmp_ddsd, 0, sizeof( bmp_ddsd ) );
  bmp_ddsd.dwSize         = sizeof( bmp_ddsd );
  bmp_ddsd.dwFlags        = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
  bmp_ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
  bmp_ddsd.dwWidth        = bmp_head.bmWidth;
  bmp_ddsd.dwHeight       = bmp_head.bmHeight;

  if( IDirectDraw_CreateSurface( win32_driver->ddobj, &bmp_ddsd, &bmp_surf, 0 ) != DD_OK )
    {
      Error( 0, "CreateSurface ( bitmap ) : could not create dd surface" );
      return 0;
    }

  /* get a handle to our surface dc,
   * create a compat dc and load
   * our bitmap into the compat dc */

  IDirectDrawSurface_GetDC( bmp_surf, &hdc_dds );
  hdc_mem = CreateCompatibleDC( hdc_dds );
  SelectObject( hdc_mem, bmp_hndl );

  /* copy our bmp from the compat dc
   * into our dd surface */

  BitBlt( hdc_dds, 0, 0, bmp_head.bmWidth, bmp_head.bmHeight,
	  hdc_mem, 0, 0, SRCCOPY );

  /* clean up */

  DeleteDC( hdc_mem );
  DeleteObject( bmp_hndl );
  IDirectDrawSurface_ReleaseDC( bmp_surf, hdc_dds );

  return bmp_surf;
}
#endif

/* Merge overlay with the current primary
 * surface. This funtion is only used when
 * a h/w overlay of the current frame type
 * is supported. */

static boolean Overlay( LPDIRECTDRAWSURFACE src_surface, RECT * src_rect,
		 LPDIRECTDRAWSURFACE dst_surface, RECT * dst_rect,
		 COLORREF color_key )
{
  DWORD			dw_color_key;
  DDPIXELFORMAT		ddpf;
  DDOVERLAYFX		ddofx;
  int			flags;
  HRESULT		result;

  /* compute the colorkey pixel value from the RGB value we've got/
   * NOTE : based on videolan colorkey code */

  memset( &ddpf, 0, sizeof( DDPIXELFORMAT ));
  ddpf.dwSize = sizeof( DDPIXELFORMAT );
  result = IDirectDrawSurface_GetPixelFormat( dst_surface, &ddpf );
  if( result != DD_OK )
    {
      Error( 0, "IDirectDrawSurface_GetPixelFormat : could not get surface pixel format" );
      return FALSE;
    }

  dw_color_key = ( DWORD ) color_key;
  dw_color_key = ( DWORD ) ( ( ( dw_color_key * ddpf.dwRBitMask ) / 255 ) & ddpf.dwRBitMask );

  memset( &ddofx, 0, sizeof( DDOVERLAYFX ) );
  ddofx.dwSize = sizeof( DDOVERLAYFX );
  ddofx.dckDestColorkey.dwColorSpaceLowValue = dw_color_key;
  ddofx.dckDestColorkey.dwColorSpaceHighValue = dw_color_key;

  /* set our overlay flags */

  flags = DDOVER_SHOW | DDOVER_KEYDESTOVERRIDE;

  /* attempt to overlay the surface */

  result = IDirectDrawSurface_UpdateOverlay( src_surface, src_rect, dst_surface, dst_rect, flags, &ddofx );
  if( result != DD_OK )
    {
      if( result == DDERR_SURFACELOST )
	{
	  IDirectDrawSurface_Restore( src_surface );
	  IDirectDrawSurface_Restore( dst_surface );

	  IDirectDrawSurface_UpdateOverlay( src_surface, src_rect, dst_surface, dst_rect, flags, &ddofx );
	}
      else
	{
	  Error( 0, "IDirectDrawSurface_UpdateOverlay : error 0x%lx. You can try disable hardware acceleration (option video.directx.hwaccel).", result );
	  return FALSE;
	}
    }

  return TRUE;
}

/* Copy our off screen surface into our primary
 * surface. This funtion is only used when a
 * h/w overlay of the current frame format is
 * not supported. */

static boolean BltCopy( LPDIRECTDRAWSURFACE src_surface, RECT * src_rect,
		 LPDIRECTDRAWSURFACE dst_surface, RECT * dst_rect )
{
  DDSURFACEDESC	ddsd_target;
  HRESULT	result;

  memset( &ddsd_target, 0, sizeof( ddsd_target ) );
  ddsd_target.dwSize = sizeof( ddsd_target );

  /* attempt to blt the surface sontents */

  result = IDirectDrawSurface_Blt( dst_surface, dst_rect, src_surface, src_rect, DDBLT_WAIT, 0 );
  if( result != DD_OK )
    {
      if( result != DDERR_SURFACELOST )
	{
	  IDirectDrawSurface_Restore( src_surface );
	  IDirectDrawSurface_Restore( dst_surface );

	  IDirectDrawSurface_Blt( dst_surface, dst_rect, src_surface, src_rect, DDBLT_WAIT, 0 );
	}
      else
	{
	  Error( 0, "IDirectDrawSurface_Blt : error 0x%lx", result );
	  return FALSE;
	}
    }

  return TRUE;
}

/* Display our current frame. This function
 * corrects frame output ratio and clipps the
 * frame if nessesary. It will then handle
 * moving the image contents contained in our
 * secondary surface to our primary surface. */

static boolean DisplayFrame( win32_driver_t * win32_driver )
{
  int					view_width;
  int					view_height;
  int					scaled_width;
  int					scaled_height;
  int					screen_width;
  int					screen_height;
  RECT					clipped;
  RECT					centered;

  /* aspect ratio calculations */

  /* TODO : account for screen ratio as well */

  view_width	= win32_driver->win32_visual->WndRect.right - win32_driver->win32_visual->WndRect.left;
  view_height	= win32_driver->win32_visual->WndRect.bottom - win32_driver->win32_visual->WndRect.top;

  if( view_width / win32_driver->ratio < view_height )
    {
      scaled_width = view_width - BORDER_SIZE;
      scaled_height = view_width / win32_driver->ratio - BORDER_SIZE;
    }
  else
    {
      scaled_width = view_height * win32_driver->ratio - BORDER_SIZE;
      scaled_height = view_height - BORDER_SIZE;
    }

  /* center our overlay in our view frame */

  centered.left = ( view_width - scaled_width ) / 2 + win32_driver->win32_visual->WndRect.left;
  centered.right = centered.left + scaled_width;
  centered.top = ( view_height - scaled_height ) / 2 + win32_driver->win32_visual->WndRect.top;
  centered.bottom = centered.top + scaled_height;

  /* clip our overlay if it is off screen */

  screen_width	= GetSystemMetrics( SM_CXSCREEN );
  screen_height	= GetSystemMetrics( SM_CYSCREEN );

  if( centered.left < 0 )
    {
      double x_scale = ( double ) ( view_width + centered.left ) / ( double ) view_width;
      clipped.left   = win32_driver->width - ( int ) ( win32_driver->width * x_scale );
      centered.left  = 0;
    }
  else
    clipped.left = 0;

  if( centered.top < 0 )
    {
      double y_scale = ( double ) ( view_height + centered.top ) / ( double ) view_height;
      clipped.left   = win32_driver->height - ( int ) ( win32_driver->height * y_scale );
      centered.left  = 0;
    }
  else
    clipped.top = 0;

  if( centered.right > screen_width )
    {
      double x_scale = ( double ) ( view_width - ( centered.right - screen_width ) ) / ( double ) view_width;
      clipped.right  = ( int ) ( win32_driver->width * x_scale );
      centered.right = screen_width;
    }
  else
    clipped.right = win32_driver->width;

  if( centered.bottom > screen_height )
    {
      double y_scale  = ( double ) ( view_height - ( centered.bottom - screen_height ) ) / ( double ) view_height;
      clipped.bottom  = ( int ) ( win32_driver->height * y_scale );
      centered.bottom = screen_height;
    }
  else
    clipped.bottom = win32_driver->height;

  /* if surface is entirely off screen or the
   * width or height is 0 for the overlay or
   * the output view area, then return without
   * overlay update */

  if( ( centered.left > screen_width ) ||
      ( centered.top >  screen_height ) ||
      ( centered.right < 0 ) ||
      ( centered.bottom < 0 ) ||
      ( clipped.left >= clipped.right ) ||
      ( clipped.top >= clipped.bottom ) ||
      ( view_width <= 0 ) ||
      ( view_height <= 0 ) )

    return 1;

  /* we have a h/w supported overlay */

  if( ( win32_driver->act_format == XINE_IMGFMT_YV12 ) || ( win32_driver->act_format == XINE_IMGFMT_YUY2 ) )
    return Overlay( win32_driver->secondary, &clipped, win32_driver->primary, &centered, win32_driver->win32_visual->ColorKey );

  /* we do not have a h/w supported overlay */

  return BltCopy( win32_driver->secondary, &clipped, win32_driver->primary, &centered );
}

/* Lock our back buffer to update its contents. */

static void * Lock( win32_driver_t * win32_driver, void * surface )
{
  LPDIRECTDRAWSURFACE	lock_surface = ( LPDIRECTDRAWSURFACE ) surface;
  HRESULT		result;

  if( !surface )
    return 0;

  memset( &win32_driver->ddsd, 0, sizeof( win32_driver->ddsd ) );
  win32_driver->ddsd.dwSize = sizeof( win32_driver->ddsd );

  result = IDirectDrawSurface_Lock( lock_surface, 0, &win32_driver->ddsd, DDLOCK_WAIT | DDLOCK_NOSYSLOCK, 0 );
  if( result == DDERR_SURFACELOST )
    {
      IDirectDrawSurface_Restore( lock_surface );
      result = IDirectDrawSurface_Lock( lock_surface, 0, &win32_driver->ddsd, DDLOCK_WAIT | DDLOCK_NOSYSLOCK, 0 );

      if( result != DD_OK )
	return 0;

    }
  else if( result != DD_OK )
    {
      if( result == DDERR_GENERIC )
	{
	  Error( 0, "surface->Lock : error, DDERR_GENERIC" );
	  exit( 1 );
	}
    }

  return win32_driver->ddsd.lpSurface;
}

/* Unlock our back buffer to prepair for display. */

static void Unlock( void * surface )
{
  LPDIRECTDRAWSURFACE lock_surface = ( LPDIRECTDRAWSURFACE ) surface;

  if( !surface )
    return;

  IDirectDrawSurface_Unlock( lock_surface, 0 );
}

/* -----------------------------------------
 *
 * BEGIN : Xine driver video output plugin
 *         handlers.
 *
 * ----------------------------------------- */

static uint32_t win32_get_capabilities( vo_driver_t * vo_driver )
{
  uint32_t retVal;

  retVal = VO_CAP_YV12 | VO_CAP_YUY2;

#if RGB_SUPPORT
  retVal |= VO_CAP_RGB;
#endif /* RGB_SUPPORT */

  return retVal;
}

static void win32_frame_field( vo_frame_t * vo_frame, int which_field )
{
  /* I have no idea what this even
   * does, frame interlace stuff? */
}

static void win32_free_framedata(vo_frame_t* vo_frame)
{

  win32_frame_t * frame = ( win32_frame_t * ) vo_frame;

  if(frame->vo_frame.base[0]) {
    free(frame->vo_frame.base[0]);
    frame->vo_frame.base[0] = NULL;
  }

  if(frame->vo_frame.base[1]) {
    free(frame->vo_frame.base[1]);
    frame->vo_frame.base[1] = NULL;
  }

  if(frame->vo_frame.base[2]) {
    free(frame->vo_frame.base[2]);
    frame->vo_frame.base[2] = NULL;
  }
}

static void win32_frame_dispose( vo_frame_t * vo_frame )
{
  win32_frame_t * win32_frame = ( win32_frame_t * ) vo_frame;

  free(win32_frame->buffer);

  win32_free_framedata(vo_frame);

  free( win32_frame );
}

static vo_frame_t * win32_alloc_frame( vo_driver_t * vo_driver )
{
  win32_frame_t  *win32_frame;

  win32_frame = calloc(1, sizeof(win32_frame_t));
  if (!win32_frame)
    return NULL;

  pthread_mutex_init(&win32_frame->vo_frame.mutex, NULL);

  win32_frame->vo_frame.proc_slice = NULL;
  win32_frame->vo_frame.proc_frame = NULL;
  win32_frame->vo_frame.field      = win32_frame_field;
  win32_frame->vo_frame.dispose    = win32_frame_dispose;
  win32_frame->format              = -1;

  return ( vo_frame_t * ) win32_frame;
}


static void win32_update_frame_format( vo_driver_t * vo_driver, vo_frame_t * vo_frame, uint32_t width,
				       uint32_t height, double ratio, int format, int flags )
{
  win32_driver_t  *win32_driver = ( win32_driver_t * ) vo_driver;
  win32_frame_t   *win32_frame  = ( win32_frame_t * ) vo_frame;

  /*printf("vo_out_directx : win32_update_frame_format() - width = %d, height=%d, ratio_code=%d, format=%d, flags=%d\n", width, height, ratio_code, format, flags);*/


  if( ( win32_frame->format	!= format	) ||
      ( win32_frame->width	!= width	) ||
      ( win32_frame->height	!= height	) )
    {
      /* free our allocated memory */

      win32_free_framedata((vo_frame_t *)&win32_frame->vo_frame);

      /* create new render buffer */
      if( format == XINE_IMGFMT_YV12 )
	{
	  win32_frame->vo_frame.pitches[0] = 8*((width + 7) / 8);
	  win32_frame->vo_frame.pitches[1] = 8*((width + 15) / 16);
	  win32_frame->vo_frame.pitches[2] = 8*((width + 15) / 16);

	  win32_frame->vo_frame.base[0] = malloc(win32_frame->vo_frame.pitches[0] * height);
	  win32_frame->vo_frame.base[1] = malloc(win32_frame->vo_frame.pitches[1] * ((height+1)/2));
	  win32_frame->vo_frame.base[2] = malloc(win32_frame->vo_frame.pitches[2] * ((height+1)/2));

	  win32_frame->size = win32_frame->vo_frame.pitches[0] * height * 2;
	}
      else if( format == XINE_IMGFMT_YUY2 )
	{
	  win32_frame->vo_frame.pitches[0] = 8*((width + 3) / 4);

	  win32_frame->vo_frame.base[0] = malloc(win32_frame->vo_frame.pitches[0] * height * 2);
	  win32_frame->vo_frame.base[1] = NULL;
	  win32_frame->vo_frame.base[2] = NULL;

	  win32_frame->size = win32_frame->vo_frame.pitches[0] * height * 2;
	}
#if RGB_SUPPORT
      else if( format == IMGFMT_RGB )
	{
	  win32_frame->size   = width * height * 3;
	  win32_frame->buffer = malloc( win32_frame->size );
	  vo_frame->base[0]   = win32_frame->buffer;
	}
#endif
      else
	{
	  xprintf (win32_driver->xine, XINE_VERBOSITY_DEBUG,
		   "vo_out_directx : !!! unsupported image format %04x !!!\n", format );
	  exit (1);
	}

      win32_frame->format	= format;
      win32_frame->width	= width;
      win32_frame->height	= height;
      win32_frame->ratio	= ratio;
    }

}

static void win32_display_frame( vo_driver_t * vo_driver, vo_frame_t * vo_frame )
{
  win32_driver_t  *win32_driver = ( win32_driver_t * ) vo_driver;
  win32_frame_t   *win32_frame  = ( win32_frame_t * ) vo_frame;


  /* if the required width, height or format has changed
   * then recreate the secondary buffer */

  if( ( win32_driver->req_format	!= win32_frame->format	) ||
      ( win32_driver->width		!= win32_frame->width	) ||
      ( win32_driver->height		!= win32_frame->height	) )
    {
      CreateSecondary( win32_driver, win32_frame->width, win32_frame->height, win32_frame->format );
    }

  /* determine desired ratio */

  win32_driver->ratio = win32_frame->ratio;

  /* lock our surface to update its contents */

  win32_driver->contents = Lock( win32_driver, win32_driver->secondary );

  /* surface unavailable, skip frame render */

  if( !win32_driver->contents )
    {
      vo_frame->free( vo_frame );
      return;
    }

  /* if our actual frame format is the native screen
   * pixel format, we need to convert it */

  if( win32_driver->act_format == IMGFMT_NATIVE )
    {
      /* use the software color conversion functions
       * to rebuild the frame in our native screen
       * pixel format ... this is slow */

      if( win32_driver->req_format == XINE_IMGFMT_YV12 )
	{
	  /* convert from yv12 to native
	   * screen pixel format */

#if NEW_YUV
	  win32_driver->yuv2rgb->configure( win32_driver->yuv2rgb,
					    win32_driver->width, win32_driver->height,
					    win32_frame->vo_frame.pitches[0], win32_frame->vo_frame.pitches[1],
					    win32_driver->width, win32_driver->height,
					    win32_driver->width * win32_driver->bytespp);
#else
	  yuv2rgb_setup( win32_driver->yuv2rgb,
			 win32_driver->width, win32_driver->height,
			 win32_frame->vo_frame.pitches[0], win32_frame->vo_frame.pitches[1],
			 win32_driver->width, win32_driver->height,
			 win32_driver->width * win32_driver->bytespp );

#endif

	  win32_driver->yuv2rgb->yuv2rgb_fun( win32_driver->yuv2rgb,
					      win32_driver->contents,
					      win32_frame->vo_frame.base[0],
					      win32_frame->vo_frame.base[1],
					      win32_frame->vo_frame.base[2] );
	}

      if( win32_driver->req_format == XINE_IMGFMT_YUY2 )
	{
	  /* convert from yuy2 to native
	   * screen pixel format */
#if NEW_YUV
	  win32_driver->yuv2rgb->configure( win32_driver->yuv2rgb,
					    win32_driver->width, win32_driver->height,
					    win32_frame->vo_frame.pitches[0], win32_frame->vo_frame.pitches[0] / 2,
					    win32_driver->width, win32_driver->height,
					    win32_driver->width * win32_driver->bytespp );
#else

	  yuv2rgb_setup( win32_driver->yuv2rgb,
			 win32_driver->width, win32_driver->height,
			 win32_frame->vo_frame.pitches[0], win32_frame->vo_frame.pitches[0] / 2,
			 win32_driver->width, win32_driver->height,
			 win32_driver->width * win32_driver->bytespp );

#endif
	  win32_driver->yuv2rgb->yuy22rgb_fun( win32_driver->yuv2rgb,
					       win32_driver->contents,
					       win32_frame->vo_frame.base[0] );
	}

#if RGB_SUPPORT
      if( win32_driver->req_format == IMGFMT_RGB )
	{
	  /* convert from 24 bit rgb to native
	   * screen pixel format */

	  /* TODO : rgb2rgb conversion */
	}
#endif
    }
  else
    {
      /* the actual format is identical to our
       * stream format. we just need to copy it */

    int line;
    uint8_t * src;
    vo_frame_t * frame = vo_frame;
    uint8_t * dst = (uint8_t *)win32_driver->contents;

    switch(win32_frame->format)
	{
      case XINE_IMGFMT_YV12:
        src = frame->base[0];
        for (line = 0; line < frame->height ; line++){
          xine_fast_memcpy( dst, src, frame->width);
          src += vo_frame->pitches[0];
          dst += win32_driver->ddsd.lPitch;
        }

        src = frame->base[2];
        for (line = 0; line < frame->height/2 ; line++){
          xine_fast_memcpy( dst, src, frame->width/2);
          src += vo_frame->pitches[2];
          dst += win32_driver->ddsd.lPitch/2;
        }

        src = frame->base[1];
        for (line = 0; line < frame->height/2 ; line++){
          xine_fast_memcpy( dst, src, frame->width/2);
          src += vo_frame->pitches[1];
          dst += win32_driver->ddsd.lPitch/2;
        }
	  break;

	case XINE_IMGFMT_YUY2:
	default:
      src = frame->base[0];
      for (line = 0; line < frame->height ; line++){
	    xine_fast_memcpy( dst, src, frame->width*2);
	    src += vo_frame->pitches[0];
	    dst += win32_driver->ddsd.lPitch;
	  }
	  break;
	}
  }

  /* unlock the surface  */

  Unlock( win32_driver->secondary );

  /* scale, clip and display our frame */

  DisplayFrame( win32_driver );

  /* tag our frame as displayed */
  if((win32_driver->current != NULL) && ((vo_frame_t *)win32_driver->current != vo_frame)) {
    vo_frame->free(&win32_driver->current->vo_frame);
  }
  win32_driver->current = (win32_frame_t *)vo_frame;

}

static void win32_overlay_blend( vo_driver_t * vo_driver, vo_frame_t * vo_frame, vo_overlay_t * vo_overlay )
{
  win32_frame_t * win32_frame = ( win32_frame_t * ) vo_frame;
  win32_driver_t * win32_driver = ( win32_driver_t * ) vo_driver;

  win32_driver->alphablend_extra_data.offset_x = vo_frame->overlay_offset_x;
  win32_driver->alphablend_extra_data.offset_y = vo_frame->overlay_offset_y;

  /* temporary overlay support, somthing more appropriate
   * for win32 will be devised at a later date */

  if( vo_overlay->rle )
    {
      if( vo_frame->format == XINE_IMGFMT_YV12 )
	_x_blend_yuv( win32_frame->vo_frame.base, vo_overlay, win32_frame->width, win32_frame->height, win32_frame->vo_frame.pitches, &win32_driver->alphablend_extra_data );
      else
	_x_blend_yuy2( win32_frame->vo_frame.base[0], vo_overlay, win32_frame->width, win32_frame->height, win32_frame->vo_frame.pitches[0], &win32_driver->alphablend_extra_data );
    }
}

static int win32_get_property( vo_driver_t * vo_driver, int property )
{
  lprintf( "win32_get_property\n" );

  return 0;
}

static int win32_set_property( vo_driver_t * vo_driver, int property, int value )
{
  return value;
}

static void win32_get_property_min_max( vo_driver_t * vo_driver, int property, int * min, int * max )
{
  *min = 0;
  *max = 0;
}

static int win32_gui_data_exchange( vo_driver_t * vo_driver, int data_type, void * data )
{
  win32_driver_t  *win32_driver = ( win32_driver_t * ) vo_driver;

  switch( data_type )
    {

    case GUI_WIN32_MOVED_OR_RESIZED:
      UpdateRect( win32_driver->win32_visual );
      DisplayFrame( win32_driver );
      break;
    case XINE_GUI_SEND_DRAWABLE_CHANGED:
    {
      HRESULT result;
      HWND newWndHnd = (HWND) data;

	  /* set cooperative level */
	  result = IDirectDraw_SetCooperativeLevel( win32_driver->ddobj, newWndHnd, DDSCL_NORMAL );
      if( result != DD_OK )
      {
        Error( 0, "SetCooperativeLevel : error 0x%lx", result );
        return 0;
      }
      /* associate our clipper with new window */
	  result = IDirectDrawClipper_SetHWnd( win32_driver->ddclipper, 0, newWndHnd );
      if( result != DD_OK )
      {
        Error( 0, "ddclipper->SetHWnd : error 0x%lx", result );
        return 0;
      }
      /* store our objects in our visual struct */
	  win32_driver->win32_visual->WndHnd = newWndHnd;
	  /* update video area and redraw current frame */
      UpdateRect( win32_driver->win32_visual );
      DisplayFrame( win32_driver );
      break;
    }
  }

  return 0;
}


static int win32_redraw_needed(vo_driver_t* this_gen)
{
  int ret = 0;

  /* TC - May need to revisit this! */
#ifdef TC
  win32_driver_t  *win32_driver = (win32_driver_t *) this_gen;

  if( _x_vo_scale_redraw_needed( &win32_driver->sc ) ) {
    win32_gui_data_exchange(this_gen, GUI_WIN32_MOVED_OR_RESIZED, 0);
    ret = 1;
  }
#endif

  return ret;
}

static void win32_exit( vo_driver_t * vo_driver )
{
  win32_driver_t  *win32_driver = ( win32_driver_t * ) vo_driver;

  free(win32_driver->win32_visual);

  Destroy( win32_driver );
}

static vo_driver_t *open_plugin (video_driver_class_t *class_gen, const void *win32_visual)
     /*vo_driver_t *init_video_out_plugin( config_values_t * config, void * win32_visual )*/
{
  directx_class_t *class = (directx_class_t *)class_gen;
  win32_driver_t  *win32_driver = calloc(1, sizeof(win32_driver_t));


  _x_alphablend_init(&win32_driver->alphablend_extra_data, class->xine);

  win32_driver->xine = class->xine;

  /* Make sure that the DirectX drivers are available and present! */
  /* Not complete yet */

  win32_driver->win32_visual			= (win32_visual_t *)win32_visual;
  win32_driver->vo_driver.get_capabilities	= win32_get_capabilities;
  win32_driver->vo_driver.alloc_frame		= win32_alloc_frame ;
  win32_driver->vo_driver.update_frame_format	= win32_update_frame_format;
  win32_driver->vo_driver.display_frame		= win32_display_frame;
  win32_driver->vo_driver.overlay_blend		= win32_overlay_blend;
  win32_driver->vo_driver.get_property		= win32_get_property;
  win32_driver->vo_driver.set_property		= win32_set_property;
  win32_driver->vo_driver.get_property_min_max	= win32_get_property_min_max;
  win32_driver->vo_driver.gui_data_exchange	= win32_gui_data_exchange;
  win32_driver->vo_driver.dispose		= win32_exit;
  win32_driver->vo_driver.redraw_needed         = win32_redraw_needed;

  win32_driver->hwaccel = class->config->register_enum(class->config,
    "video.directx.hwaccel", 0, config_hwaccel_values,
    _("HW acceleration level"),
    _("Possible values (default full):\n\n"
"full: full acceleration\n"
"scale: disable colorspace conversion (HW scaling only)\n"
"none: disable all acceleration"),
    10, NULL, NULL);

  if (!CreatePrimary( win32_driver )) {
      Destroy( win32_driver );
      return NULL;
  }
  if( !CheckPixelFormat( win32_driver ) )
    {
      Error( 0, "vo_directx : Your screen pixel format is not supported" );
      Destroy( win32_driver );
      return NULL;
    }

#if (NEW_YUV)
  win32_driver->yuv2rgb_factory = yuv2rgb_factory_init( win32_driver->mode, 0, 0 );
  win32_driver->yuv2rgb = win32_driver->yuv2rgb_factory->create_converter(win32_driver->yuv2rgb_factory);
#else
  win32_driver->yuv2rgb = yuv2rgb_init( win32_driver->mode, 0, 0 );
#endif

  return ( vo_driver_t * ) win32_driver;
}

static void *init_class (xine_t *xine, void *visual_gen) {

  directx_class_t    *directx;

  /*
   * from this point on, nothing should go wrong anymore
   */
  directx = calloc(1, sizeof (directx_class_t));

  directx->driver_class.open_plugin     = open_plugin;
  directx->driver_class.identifier      = "DirectX";
  directx->driver_class.description     = N_("xine video output plugin for win32 using directx");
  directx->driver_class.dispose         = default_video_driver_class_dispose;

  directx->xine                         = xine;
  directx->config                       = xine->config;

  return directx;
}

static const vo_info_t vo_info_win32 = {
  7,                        /* priority    */
  XINE_VISUAL_TYPE_DIRECTX  /* visual type */
};

/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_OUT, 22, "vo_directx", XINE_VERSION_CODE, &vo_info_win32, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
