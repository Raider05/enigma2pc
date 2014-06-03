/*
 * Copyright (C) 2012 Edgar Hucek <gimli|@dark-green.com>
 * Copyright (C) 2012-2014 xine developers
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
 * video_out_vaapi.c, VAAPI video extension interface for xine
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#include <sys/types.h>
#if defined(__FreeBSD__)
#include <machine/param.h>
#endif
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/time.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <time.h>
#include <unistd.h>

#define LOG_MODULE "video_out_vaapi"
#define LOG_VERBOSE
/*
#define LOG
*/
/*
#define DEBUG_SURFACE
*/
#include "xine.h"
#include <xine/video_out.h>
#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/vo_scale.h>

#include <GL/glu.h>
#include <GL/glx.h>
#include <GL/glext.h>
#include <GL/gl.h>
#include <dlfcn.h>

#include <va/va_x11.h>
#include <va/va_glx.h>

#include "accel_vaapi.h"

#ifdef HAVE_FFMPEG_AVUTIL_H
#  include <mem.h>
#else
#  include <libavutil/mem.h>
#endif

#ifndef VA_SURFACE_ATTRIB_SETTABLE
#define vaCreateSurfaces(d, f, w, h, s, ns, a, na) \
    vaCreateSurfaces(d, w, h, f, ns, s)
#endif

#define  RENDER_SURFACES  50
#define  SOFT_SURFACES    3
#define  SW_WIDTH         1920
#define  SW_HEIGHT        1080
#define  STABLE_FRAME_COUNTER 4
#define  SW_CONTEXT_INIT_FORMAT -1 //VAProfileH264Main

#if defined VA_SRC_BT601 && defined VA_SRC_BT709
# define USE_VAAPI_COLORSPACE 1
#else
# define USE_VAAPI_COLORSPACE 0
#endif

#define IMGFMT_VAAPI               0x56410000 /* 'VA'00 */
#define IMGFMT_VAAPI_MASK          0xFFFF0000
#define IMGFMT_IS_VAAPI(fmt)       (((fmt) & IMGFMT_VAAPI_MASK) == IMGFMT_VAAPI)
#define IMGFMT_VAAPI_CODEC_MASK    0x000000F0
#define IMGFMT_VAAPI_CODEC(fmt)    ((fmt) & IMGFMT_VAAPI_CODEC_MASK)
#define IMGFMT_VAAPI_CODEC_MPEG2   (0x10)
#define IMGFMT_VAAPI_CODEC_MPEG4   (0x20)
#define IMGFMT_VAAPI_CODEC_H264    (0x30)
#define IMGFMT_VAAPI_CODEC_VC1     (0x40)
#define IMGFMT_VAAPI_MPEG2         (IMGFMT_VAAPI|IMGFMT_VAAPI_CODEC_MPEG2)
#define IMGFMT_VAAPI_MPEG2_IDCT    (IMGFMT_VAAPI|IMGFMT_VAAPI_CODEC_MPEG2|1)
#define IMGFMT_VAAPI_MPEG2_MOCO    (IMGFMT_VAAPI|IMGFMT_VAAPI_CODEC_MPEG2|2)
#define IMGFMT_VAAPI_MPEG4         (IMGFMT_VAAPI|IMGFMT_VAAPI_CODEC_MPEG4)
#define IMGFMT_VAAPI_H263          (IMGFMT_VAAPI|IMGFMT_VAAPI_CODEC_MPEG4|1)
#define IMGFMT_VAAPI_H264          (IMGFMT_VAAPI|IMGFMT_VAAPI_CODEC_H264)
#define IMGFMT_VAAPI_VC1           (IMGFMT_VAAPI|IMGFMT_VAAPI_CODEC_VC1)
#define IMGFMT_VAAPI_WMV3          (IMGFMT_VAAPI|IMGFMT_VAAPI_CODEC_VC1|1)

#define FOVY     60.0f
#define ASPECT   1.0f
#define Z_NEAR   0.1f
#define Z_FAR    100.0f
#define Z_CAMERA 0.869f

#ifndef GLAPIENTRY
#ifdef APIENTRY
#define GLAPIENTRY APIENTRY
#else
#define GLAPIENTRY
#endif
#endif

#if defined(__linux__)
// Linux select() changes its timeout parameter upon return to contain
// the remaining time. Most other unixen leave it unchanged or undefined.
#define SELECT_SETS_REMAINING
#elif defined(__FreeBSD__) || defined(__sun__) || (defined(__MACH__) && defined(__APPLE__))
#define USE_NANOSLEEP
#elif defined(HAVE_PTHREADS) && defined(sgi)
// SGI pthreads has a bug when using pthreads+signals+nanosleep,
// so instead of using nanosleep, wait on a CV which is never signalled.
#include <pthread.h>
#define USE_COND_TIMEDWAIT
#endif

#define LOCKDISPLAY 

#ifdef LOCKDISPLAY
#define DO_LOCKDISPLAY          XLockDisplay(guarded_display)
#define DO_UNLOCKDISPLAY        XUnlockDisplay(guarded_display)
static Display *guarded_display;
#else
#define DO_LOCKDISPLAY
#define DO_UNLOCKDISPLAY
#endif

#define RECT_IS_EQ(a, b) ((a).x1 == (b).x1 && (a).y1 == (b).y1 && (a).x2 == (b).x2 && (a).y2 == (b).y2)

static const char *const scaling_level_enum_names[] = {
  "default",  /* VA_FILTER_SCALING_DEFAULT       */
  "fast",     /* VA_FILTER_SCALING_FAST          */
  "hq",       /* VA_FILTER_SCALING_HQ            */
  "nla",      /* VA_FILTER_SCALING_NL_ANAMORPHIC */
  NULL
};

static const int scaling_level_enum_values[] = {
  VA_FILTER_SCALING_DEFAULT,
  VA_FILTER_SCALING_FAST,
  VA_FILTER_SCALING_HQ,
  VA_FILTER_SCALING_NL_ANAMORPHIC
};

typedef struct vaapi_driver_s vaapi_driver_t;

typedef struct {
    int x0, y0;
    int x1, y1, x2, y2;
} vaapi_rect_t;

typedef struct {
  vo_frame_t         vo_frame;

  int                width, height, format, flags;
  double             ratio;

  vaapi_accel_t     vaapi_accel_data;
} vaapi_frame_t;

typedef struct {
  VADisplayAttribType type;
  int                 value;
  int                 min;
  int                 max;
  int                 atom;

  cfg_entry_t        *entry;

  vaapi_driver_t     *this;

} va_property_t;

struct vaapi_driver_s {

  vo_driver_t        vo_driver;

  config_values_t   *config;

  /* X11 related stuff */
  Display            *display;
  int                 screen;
  Drawable            drawable;
  XColor              black;
  Window              window;

  uint32_t            capabilities;

  int ovl_changed;
  vo_overlay_t       *overlays[XINE_VORAW_MAX_OVL];
  uint32_t           *overlay_bitmap;
  int                 overlay_bitmap_size;
  uint32_t            overlay_bitmap_width;
  uint32_t            overlay_bitmap_height;
  vaapi_rect_t        overlay_bitmap_src;
  vaapi_rect_t        overlay_bitmap_dst;

  uint32_t            vdr_osd_width;
  uint32_t            vdr_osd_height;

  uint32_t            overlay_output_width;
  uint32_t            overlay_output_height;
  vaapi_rect_t        overlay_dirty_rect;
  int                 has_overlay;

  uint32_t            overlay_unscaled_width;
  uint32_t            overlay_unscaled_height;
  vaapi_rect_t        overlay_unscaled_dirty_rect;

  /* all scaling information goes here */
  vo_scale_t          sc;

  xine_t             *xine;

  unsigned int        deinterlace;
  
  int                 valid_opengl_context;
  int                 opengl_render;
  int                 opengl_use_tfp;
  int                 query_va_status;

  GLuint              gl_texture;
  GLXContext          gl_context;
  XVisualInfo         *gl_vinfo;
  Pixmap              gl_pixmap;
  Pixmap              gl_image_pixmap;

  ff_vaapi_context_t  *va_context;

  int                  num_frame_buffers;
  vaapi_frame_t       *frames[RENDER_SURFACES];

  pthread_mutex_t     vaapi_lock;

  unsigned int        init_opengl_render;
  unsigned int        guarded_render;
  unsigned int        scaling_level_enum;
  unsigned int        scaling_level;
  va_property_t       props[VO_NUM_PROPERTIES];
  unsigned int        swap_uv_planes;

  /* color matrix and fullrange emulation */
  int                 cm_state;
  int                 color_matrix;
  int                 vaapi_cm_flags;
#define CSC_MODE_USER_MATRIX      0
#define CSC_MODE_FLAGS            1
#define CSC_MODE_FLAGS_FULLRANGE2 2
#define CSC_MODE_FLAGS_FULLRANGE3 3
  int                 csc_mode;
  int                 have_user_csc_matrix;
  float               user_csc_matrix[12];
};

/* import common color matrix stuff */
#define CM_HAVE_YCGCO_SUPPORT 1
#define CM_DRIVER_T vaapi_driver_t
#include "color_matrix.c"

ff_vaapi_surface_t  *va_render_surfaces   = NULL;
VASurfaceID         *va_surface_ids       = NULL;
VASurfaceID         *va_soft_surface_ids  = NULL;
VAImage             *va_soft_images       = NULL;

static void vaapi_destroy_subpicture(vo_driver_t *this_gen);
static void vaapi_destroy_image(vo_driver_t *this_gen, VAImage *va_image);
static int vaapi_ovl_associate(vo_driver_t *this_gen, int format, int bShow);
static VAStatus vaapi_destroy_soft_surfaces(vo_driver_t *this_gen);
static VAStatus vaapi_destroy_render_surfaces(vo_driver_t *this_gen);
static const char *vaapi_profile_to_string(VAProfile profile);
static int vaapi_set_property (vo_driver_t *this_gen, int property, int value);
static void vaapi_show_display_props(vo_driver_t *this_gen);

static void nv12_to_yv12(const uint8_t *y_src,  int y_src_pitch, 
                         const uint8_t *uv_src, int uv_src_pitch, 
                         uint8_t *y_dst, int y_dst_pitch,
                         uint8_t *u_dst, int u_dst_pitch,
                         uint8_t *v_dst, int v_dst_pitch,
                         int src_width, int src_height, 
                         int dst_width, int dst_height,
                         int src_data_size);

static void yv12_to_nv12(const uint8_t *y_src, int y_src_pitch, 
                         const uint8_t *u_src, int u_src_pitch, 
                         const uint8_t *v_src, int v_src_pitch,
                         uint8_t *y_dst,  int y_dst_pitch,
                         uint8_t *uv_dst, int uv_dst_pitch,
                         int src_width, int src_height, 
                         int dst_width, int dst_height,
                         int dst_data_size);

void (GLAPIENTRY *mpglGenTextures)(GLsizei, GLuint *);
void (GLAPIENTRY *mpglBindTexture)(GLenum, GLuint);
void (GLAPIENTRY *mpglXBindTexImage)(Display *, GLXDrawable, int, const int *);
void (GLAPIENTRY *mpglXReleaseTexImage)(Display *, GLXDrawable, int);
GLXPixmap (GLAPIENTRY *mpglXCreatePixmap)(Display *, GLXFBConfig, Pixmap, const int *);
void (GLAPIENTRY *mpglXDestroyPixmap)(Display *, GLXPixmap);
const GLubyte *(GLAPIENTRY *mpglGetString)(GLenum);
void (GLAPIENTRY *mpglGenPrograms)(GLsizei, GLuint *);

#ifdef LOG
static const char *string_of_VAImageFormat(VAImageFormat *imgfmt)
{
  static char str[5];
  str[0] = imgfmt->fourcc;
  str[1] = imgfmt->fourcc >> 8;
  str[2] = imgfmt->fourcc >> 16;
  str[3] = imgfmt->fourcc >> 24;
  str[4] = '\0';
  return str;
}
#endif

static int vaapi_check_status(vo_driver_t *this_gen, VAStatus vaStatus, const char *msg)
{

  vaapi_driver_t *this = (vaapi_driver_t *) this_gen;

  if (vaStatus != VA_STATUS_SUCCESS) {
    xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " Error : %s: %s\n", msg, vaErrorStr(vaStatus));
    return 0;
  }
  return 1;
}

/* Wrapper for ffmpeg avcodec_decode_video2 */
#if AVVIDEO > 1
static int guarded_avcodec_decode_video2(vo_frame_t *frame_gen, AVCodecContext *avctx, AVFrame *picture,
                                         int *got_picture_ptr, AVPacket *avpkt) {

  vaapi_driver_t  *this = (vaapi_driver_t *) frame_gen->driver;

  int len = 0;


  if(this->guarded_render) {
    lprintf("guarded_avcodec_decode_video2 enter\n");
    pthread_mutex_lock(&this->vaapi_lock);
    //DO_LOCKDISPLAY;
  }

  len = avcodec_decode_video2 (avctx, picture, got_picture_ptr, avpkt);

  if(this->guarded_render) {
    //DO_UNLOCKDISPLAY;
    pthread_mutex_unlock(&this->vaapi_lock);
    lprintf("guarded_avcodec_decode_video2 exit\n");
  }


  return len;
}
#else
static int guarded_avcodec_decode_video(vo_frame_t *frame_gen, AVCodecContext *avctx, AVFrame *picture,
                                        int *got_picture_ptr, uint8_t *buf, int buf_size) {

  vaapi_driver_t  *this = (vaapi_driver_t *) frame_gen->driver;

  int len = 0;


  if(this->guarded_render) {
    lprintf("guarded_avcodec_decode_video enter\n");
    pthread_mutex_lock(&this->vaapi_lock);
    //DO_LOCKDISPLAY;
  }

  len = avcodec_decode_video (avctx, picture, got_picture_ptr, buf, buf_size);

  if(this->guarded_render) {
    //DO_UNLOCKDISPLAY;
    pthread_mutex_unlock(&this->vaapi_lock);
    lprintf("guarded_avcodec_decode_video exit\n");
  }


  return len;
}
#endif

static int guarded_render(vo_frame_t *frame_gen) {
  vaapi_driver_t  *this = (vaapi_driver_t *) frame_gen->driver;

  return this->guarded_render;
}

static ff_vaapi_surface_t *get_vaapi_surface(vo_frame_t *frame_gen) {

  vaapi_driver_t      *this       = (vaapi_driver_t *) frame_gen->driver;
  vaapi_frame_t       *frame      = (vaapi_frame_t *) frame_gen;
  ff_vaapi_context_t  *va_context = this->va_context;
  ff_vaapi_surface_t  *va_surface = NULL;
  VAStatus            vaStatus;

  lprintf("get_vaapi_surface\n");

  if(!va_render_surfaces)
    return NULL;

  if(this->guarded_render) {
    /* Get next VAAPI surface marked as SURFACE_FREE */
    for(;;) {
      int old_head = va_context->va_head;
      va_context->va_head = (va_context->va_head + 1) % ((RENDER_SURFACES));

      va_surface = &va_render_surfaces[old_head];

      if( va_surface->status == SURFACE_FREE ) {

        VASurfaceStatus surf_status = 0;

        if(this->query_va_status) {
          vaStatus = vaQuerySurfaceStatus(va_context->va_display, va_surface->va_surface_id, &surf_status);
          vaapi_check_status(va_context->driver, vaStatus, "vaQuerySurfaceStatus()");
        } else {
          surf_status = VASurfaceReady;
        }

        if(surf_status == VASurfaceReady) {

          va_surface->status = SURFACE_ALOC;

#ifdef DEBUG_SURFACE
          printf("get_vaapi_surface 0x%08x\n", va_surface->va_surface_id);
#endif

          return &va_render_surfaces[old_head];
        } else {
#ifdef DEBUG_SURFACE
          printf("get_vaapi_surface busy\n");
#endif
        }
      }
#ifdef DEBUG_SURFACE
      printf("get_vaapi_surface miss\n");
#endif
    }
  } else {
      va_surface = &va_render_surfaces[frame->vaapi_accel_data.index];
  }

  return va_surface;
}

/* Set VAAPI surface status to render */
static void render_vaapi_surface(vo_frame_t *frame_gen, ff_vaapi_surface_t *va_surface) {
  vaapi_driver_t  *this = (vaapi_driver_t *) frame_gen->driver;
  vaapi_accel_t *accel = (vaapi_accel_t*)frame_gen->accel_data;

  lprintf("render_vaapi_surface\n");

  if(!this->guarded_render || !accel || !va_surface)
    return;

  pthread_mutex_lock(&this->vaapi_lock);
  //DO_LOCKDISPLAY;

  accel->index = va_surface->index;

  va_surface->status = SURFACE_RENDER;
#ifdef DEBUG_SURFACE
  printf("render_vaapi_surface 0x%08x\n", va_surface->va_surface_id);
#endif

  //DO_UNLOCKDISPLAY;
  pthread_mutex_unlock(&this->vaapi_lock);
}

/* Set VAAPI surface status to free */
static void release_vaapi_surface(vo_frame_t *frame_gen, ff_vaapi_surface_t *va_surface) {
  vaapi_driver_t  *this = (vaapi_driver_t *) frame_gen->driver;

  lprintf("release_vaapi_surface\n");

  if(va_surface == NULL || !this->guarded_render) {
    return;
  }

  if(va_surface->status == SURFACE_RENDER) {
    va_surface->status = SURFACE_RENDER_RELEASE;
  } else if (va_surface->status != SURFACE_RENDER_RELEASE) {
    va_surface->status = SURFACE_FREE;
#ifdef DEBUG_SURFACE
    printf("release_surface 0x%08x\n", va_surface->va_surface_id);
#endif
  }
}

static VADisplay vaapi_get_display(Display *display, int opengl_render)
{
  VADisplay ret;

  if(opengl_render) {
    ret = vaGetDisplayGLX(display);
  } else {
    ret = vaGetDisplay(display);
  }

  if(vaDisplayIsValid(ret))
    return ret;
  else
    return 0;
}

typedef struct {
  void *funcptr;
  const char *extstr;
  const char *funcnames[7];
  void *fallback;
} extfunc_desc_t;

#define DEF_FUNC_DESC(name) {&mpgl##name, NULL, {"gl"#name, NULL}, gl ##name}
static const extfunc_desc_t extfuncs[] = {
  DEF_FUNC_DESC(GenTextures),

  {&mpglBindTexture, NULL, {"glBindTexture", "glBindTextureARB", "glBindTextureEXT", NULL}},
  {&mpglXBindTexImage, "GLX_EXT_texture_from_pixmap", {"glXBindTexImageEXT", NULL}},
  {&mpglXReleaseTexImage, "GLX_EXT_texture_from_pixmap", {"glXReleaseTexImageEXT", NULL}},
  {&mpglXCreatePixmap, "GLX_EXT_texture_from_pixmap", {"glXCreatePixmap", NULL}},
  {&mpglXDestroyPixmap, "GLX_EXT_texture_from_pixmap", {"glXDestroyPixmap", NULL}},
  {&mpglGenPrograms, "_program", {"glGenProgramsARB", NULL}},
  {NULL}
};

typedef struct {
  video_driver_class_t driver_class;

  config_values_t     *config;
  xine_t              *xine;
} vaapi_class_t;

static int gl_visual_attr[] = {
  GLX_RGBA,
  GLX_RED_SIZE, 1,
  GLX_GREEN_SIZE, 1,
  GLX_BLUE_SIZE, 1,
  GLX_DOUBLEBUFFER,
  GL_NONE
};

static void delay_usec(unsigned int usec)
{
    // FIXME: xine_usec_sleep?
    int was_error;

#if defined(USE_NANOSLEEP)
    struct timespec elapsed, tv;

    elapsed.tv_sec = 0;
    elapsed.tv_nsec = usec * 1000;

    do {
        errno = 0;
        tv.tv_sec = elapsed.tv_sec;
        tv.tv_nsec = elapsed.tv_nsec;
        was_error = nanosleep(&tv, &elapsed);
    } while (was_error && (errno == EINTR));

#elif defined(USE_COND_TIMEDWAIT)
    // Use a local mutex and cv, so threads remain independent
    pthread_cond_t delay_cond = PTHREAD_COND_INITIALIZER;
    pthread_mutex_t delay_mutex = PTHREAD_MUTEX_INITIALIZER;
    struct timespec elapsed;
    uint64_t future;

    future = get_ticks_usec() + usec;
    elapsed.tv_sec = future / 1000000;
    elapsed.tv_nsec = (future % 1000000) * 1000;

    do {
        errno = 0;
        was_error = pthread_mutex_lock(&delay_mutex);
        was_error = pthread_cond_timedwait(&delay_cond, &delay_mutex, &elapsed);
        was_error = pthread_mutex_unlock(&delay_mutex);
    } while (was_error && (errno == EINTR));

#else // using select()
    struct timeval tv;
# ifndef SELECT_SETS_REMAINING
    uint64_t then, now, elapsed;

    then = get_ticks_usec();
# endif

    tv.tv_sec = 0;
    tv.tv_usec = usec;

    do {
        errno = 0;
# ifndef SELECT_SETS_REMAINING
        // Calculate the time interval left (in case of interrupt)
        now = get_ticks_usec();
        elapsed = now - then;
        then = now;
        if (elapsed >= usec)
            break;
        usec -= elapsed;
        tv.tv_sec = 0;
        tv.tv_usec = usec;
# endif
        was_error = select(0, NULL, NULL, NULL, &tv);
    } while (was_error && (errno == EINTR));
#endif
}

static void vaapi_x11_wait_event(Display *dpy, Window w, int type)
{
  XEvent e;
  while (!XCheckTypedWindowEvent(dpy, w, type, &e))
    delay_usec(10);
}

/* X11 Error handler and error functions */
static int vaapi_x11_error_code = 0;
static int (*vaapi_x11_old_error_handler)(Display *, XErrorEvent *);

static int vaapi_x11_error_handler(Display *dpy, XErrorEvent *error)
{
    vaapi_x11_error_code = error->error_code;
    return 0;
}

static void vaapi_x11_trap_errors(void)
{
    vaapi_x11_error_code    = 0;
    vaapi_x11_old_error_handler = XSetErrorHandler(vaapi_x11_error_handler);
}

static int vaapi_x11_untrap_errors(void)
{
    XSetErrorHandler(vaapi_x11_old_error_handler);
    return vaapi_x11_error_code;
}

static void vaapi_appendstr(char **dst, const char *str)
{
    int newsize;
    char *newstr;
    if (!str)
        return;
    newsize = strlen(*dst) + 1 + strlen(str) + 1;
    newstr = realloc(*dst, newsize);
    if (!newstr)
        return;
    *dst = newstr;
    strcat(*dst, " ");
    strcat(*dst, str);
}

/* Return the address of a linked function */
static void *vaapi_getdladdr (const char *s) {
  void *ret = NULL;
  void *handle = dlopen(NULL, RTLD_LAZY);
  if (!handle)
    return NULL;
  ret = dlsym(handle, s);
  dlclose(handle);

  return ret;
}

/* Resolve opengl functions. */
static void vaapi_get_functions(vo_driver_t *this_gen, void *(*getProcAddress)(const GLubyte *),
                         const char *ext2) {
  const extfunc_desc_t *dsc;
  const char *extensions;
  char *allexts;

  if (!getProcAddress)
    getProcAddress = (void *)vaapi_getdladdr;

  /* special case, we need glGetString before starting to find the other functions */
  mpglGetString = getProcAddress("glGetString");
  if (!mpglGetString)
      mpglGetString = glGetString;

  extensions = (const char *)mpglGetString(GL_EXTENSIONS);
  if (!extensions) extensions = "";
  if (!ext2) ext2 = "";
  allexts = malloc(strlen(extensions) + strlen(ext2) + 2);
  strcpy(allexts, extensions);
  strcat(allexts, " ");
  strcat(allexts, ext2);
  lprintf("vaapi_get_functions: OpenGL extensions string:\n%s\n", allexts);
  for (dsc = extfuncs; dsc->funcptr; dsc++) {
    void *ptr = NULL;
    int i;
    if (!dsc->extstr || strstr(allexts, dsc->extstr)) {
      for (i = 0; !ptr && dsc->funcnames[i]; i++)
        ptr = getProcAddress((const GLubyte *)dsc->funcnames[i]);
    }
    if (!ptr)
        ptr = dsc->fallback;
    *(void **)dsc->funcptr = ptr;
  }
  lprintf("\n");
  free(allexts);
}

/* Check if opengl indirect/software rendering is used */
static int vaapi_opengl_verify_direct (x11_visual_t *vis) {
  Window        root, win;
  XVisualInfo  *visinfo;
  GLXContext    ctx;
  XSetWindowAttributes xattr;
  int           ret = 0;

  if (!vis || !vis->display || ! (root = RootWindow (vis->display, vis->screen))) {
    lprintf ("vaapi_opengl_verify_direct: Don't have a root window to verify\n");
    return 0;
  }

  if (! (visinfo = glXChooseVisual (vis->display, vis->screen, gl_visual_attr)))
    return 0;

  if (! (ctx = glXCreateContext (vis->display, visinfo, NULL, 1)))
    return 0;

  memset (&xattr, 0, sizeof (xattr));
  xattr.colormap = XCreateColormap(vis->display, root, visinfo->visual, AllocNone);
  xattr.event_mask = StructureNotifyMask | ExposureMask;

  if ( (win = XCreateWindow (vis->display, root, 0, 0, 1, 1, 0, visinfo->depth,
			                       InputOutput, visinfo->visual,
			                       CWBackPixel | CWBorderPixel | CWColormap | CWEventMask,
			                       &xattr))) {
    if (glXMakeCurrent (vis->display, win, ctx)) {
	    const char *renderer = (const char *) glGetString(GL_RENDERER);
	    if (glXIsDirect (vis->display, ctx) &&
	                ! strstr (renderer, "Software") &&
	                ! strstr (renderer, "Indirect"))
	      ret = 1;
	      glXMakeCurrent (vis->display, None, NULL);
      }
      XDestroyWindow (vis->display, win);
  }
  glXDestroyContext (vis->display, ctx);
  XFreeColormap     (vis->display, xattr.colormap);

  return ret;
}

static int vaapi_glx_bind_texture(vo_driver_t *this_gen)
{
  vaapi_driver_t *this = (vaapi_driver_t *) this_gen;

  glEnable(GL_TEXTURE_2D);
  mpglBindTexture(GL_TEXTURE_2D, this->gl_texture);

  if (this->opengl_use_tfp) {
    vaapi_x11_trap_errors();
    mpglXBindTexImage(this->display, this->gl_pixmap, GLX_FRONT_LEFT_EXT, NULL);
    XSync(this->display, False);
    if (vaapi_x11_untrap_errors())
      xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_glx_bind_texture : Update bind_tex_image failed\n");
  }

  return 0;
}

static int vaapi_glx_unbind_texture(vo_driver_t *this_gen)
{
  vaapi_driver_t *this = (vaapi_driver_t *) this_gen;

  if (this->opengl_use_tfp) {
    vaapi_x11_trap_errors();
    mpglXReleaseTexImage(this->display, this->gl_pixmap, GLX_FRONT_LEFT_EXT);
    if (vaapi_x11_untrap_errors())
      xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_glx_unbind_texture : Failed to release?\n");
  }

  mpglBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_TEXTURE_2D);
  return 0;
}

static void vaapi_glx_render_frame(vo_frame_t *frame_gen, int left, int top, int right, int bottom)
{
  vaapi_driver_t        *this = (vaapi_driver_t *) frame_gen->driver;
  vaapi_frame_t         *frame = (vaapi_frame_t *) frame_gen;
  ff_vaapi_context_t    *va_context = this->va_context;
  int             x1, x2, y1, y2;
  float           tx, ty;

  if (vaapi_glx_bind_texture(frame_gen->driver) < 0)
    return;

  /* Calc texture/rectangle coords */
  x1 = this->sc.output_xoffset;
  y1 = this->sc.output_yoffset;
  x2 = x1 + this->sc.output_width;
  y2 = y1 + this->sc.output_height;
  tx = (float) frame->width  / va_context->width;
  ty = (float) frame->height / va_context->height;

  glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
  /* Draw quad */
  glBegin (GL_QUADS);

    glTexCoord2f (tx, ty);   glVertex2i (x2, y2);
    glTexCoord2f (0,  ty);   glVertex2i (x1, y2);
    glTexCoord2f (0,  0);    glVertex2i (x1, y1);
    glTexCoord2f (tx, 0);    glVertex2i (x2, y1);
    lprintf("render_frame left %d top %d right %d bottom %d\n", x1, y1, x2, y2);

  glEnd ();

  if (vaapi_glx_unbind_texture(frame_gen->driver) < 0)
    return;
}

static void vaapi_glx_flip_page(vo_frame_t *frame_gen, int left, int top, int right, int bottom)
{
  vaapi_driver_t *this = (vaapi_driver_t *) frame_gen->driver;

  glClear(GL_COLOR_BUFFER_BIT);

  vaapi_glx_render_frame(frame_gen, left, top, right, bottom);

  //if (gl_finish)
  //  glFinish();

  glXSwapBuffers(this->display, this->window);

}

static void destroy_glx(vo_driver_t *this_gen)
{
  vaapi_driver_t        *this       = (vaapi_driver_t *) this_gen;
  ff_vaapi_context_t    *va_context = this->va_context;

  if(!this->opengl_render || !va_context->valid_context)
    return;

  //if (gl_finish)
  //  glFinish();

  if(va_context->gl_surface) {
    VAStatus vaStatus = vaDestroySurfaceGLX(va_context->va_display, va_context->gl_surface);
    vaapi_check_status(this_gen, vaStatus, "vaDestroySurfaceGLX()");
    va_context->gl_surface = NULL;
  }

  if(this->gl_context)
    glXMakeCurrent(this->display, None, NULL);

  if(this->gl_pixmap) {
    vaapi_x11_trap_errors();
    mpglXDestroyPixmap(this->display, this->gl_pixmap);
    XSync(this->display, False);
    vaapi_x11_untrap_errors();
    this->gl_pixmap = None;
  }

  if(this->gl_image_pixmap) {
    XFreePixmap(this->display, this->gl_image_pixmap);
    this->gl_image_pixmap = None;
  }

  if(this->gl_texture) {
    glDeleteTextures(1, &this->gl_texture);
    this->gl_texture = GL_NONE;
  }

  if(this->gl_context) {
    glXDestroyContext(this->display, this->gl_context);
    this->gl_context = 0;
  }

  if(this->gl_vinfo) {
    XFree(this->gl_vinfo);
    this->gl_vinfo = NULL;
  }

  this->valid_opengl_context = 0;
}

static GLXFBConfig *get_fbconfig_for_depth(vo_driver_t *this_gen, int depth)
{
  vaapi_driver_t *this = (vaapi_driver_t *) this_gen;

    GLXFBConfig *fbconfigs, *ret = NULL;
    int          n_elements, i, found;
    int          db, stencil, alpha, rgba, value;

    static GLXFBConfig *cached_config = NULL;
    static int          have_cached_config = 0;

    if (have_cached_config)
        return cached_config;

    fbconfigs = glXGetFBConfigs(this->display, this->screen, &n_elements);

    db      = SHRT_MAX;
    stencil = SHRT_MAX;
    rgba    = 0;

    found = n_elements;

    for (i = 0; i < n_elements; i++) {
        XVisualInfo *vi;
        int          visual_depth;

        vi = glXGetVisualFromFBConfig(this->display, fbconfigs[i]);
        if (!vi)
            continue;

        visual_depth = vi->depth;
        XFree(vi);

        if (visual_depth != depth)
            continue;

        glXGetFBConfigAttrib(this->display, fbconfigs[i], GLX_ALPHA_SIZE, &alpha);
        glXGetFBConfigAttrib(this->display, fbconfigs[i], GLX_BUFFER_SIZE, &value);
        if (value != depth && (value - alpha) != depth)
            continue;

        value = 0;
        if (depth == 32) {
            glXGetFBConfigAttrib(this->display, fbconfigs[i],
                                 GLX_BIND_TO_TEXTURE_RGBA_EXT, &value);
            if (value)
                rgba = 1;
        }

        if (!value) {
            if (rgba)
                continue;

            glXGetFBConfigAttrib(this->display, fbconfigs[i],
                                 GLX_BIND_TO_TEXTURE_RGB_EXT, &value);
            if (!value)
                continue;
        }

        glXGetFBConfigAttrib(this->display, fbconfigs[i], GLX_DOUBLEBUFFER, &value);
        if (value > db)
            continue;
        db = value;

        glXGetFBConfigAttrib(this->display, fbconfigs[i], GLX_STENCIL_SIZE, &value);
        if (value > stencil)
            continue;
        stencil = value;

        found = i;
    }

    if (found != n_elements) {
        ret = malloc(sizeof(*ret));
        *ret = fbconfigs[found];
    }

    if (n_elements)
        XFree(fbconfigs);

    have_cached_config = 1;
    cached_config = ret;
    return ret;
}

static int vaapi_glx_config_tfp(vo_driver_t *this_gen, unsigned int width, unsigned int height)
{
  vaapi_driver_t *this = (vaapi_driver_t *) this_gen;

  GLXFBConfig *fbconfig;
  int attribs[7], i = 0;
  const int depth = 24;

  if (!mpglXBindTexImage || !mpglXReleaseTexImage) {
    xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_glx_config_tfp : No GLX texture-from-pixmap extension available\n");
    return 0;
  }

  if (depth != 24 && depth != 32) {
    xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_glx_config_tfp : color depth wrong.\n");
    return 0;
  }

  this->gl_image_pixmap = XCreatePixmap(this->display, this->window, width, height, depth);
  if (!this->gl_image_pixmap) {
    xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_glx_config_tfp : Could not create X11 pixmap\n");
    return 0;
  }

  fbconfig = get_fbconfig_for_depth(this_gen, depth);
  if (!fbconfig) {
    xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_glx_config_tfp : Could not find an FBConfig for 32-bit pixmap\n");
    return 0;
  }

  attribs[i++] = GLX_TEXTURE_TARGET_EXT;
  attribs[i++] = GLX_TEXTURE_2D_EXT;
  attribs[i++] = GLX_TEXTURE_FORMAT_EXT;
  if (depth == 24)
    attribs[i++] = GLX_TEXTURE_FORMAT_RGB_EXT;
  else if (depth == 32)
    attribs[i++] = GLX_TEXTURE_FORMAT_RGBA_EXT;
  attribs[i++] = GLX_MIPMAP_TEXTURE_EXT;
  attribs[i++] = GL_FALSE;
  attribs[i++] = None;

  vaapi_x11_trap_errors();
  this->gl_pixmap = mpglXCreatePixmap(this->display, *fbconfig, this->gl_image_pixmap, attribs);
  XSync(this->display, False);
  if (vaapi_x11_untrap_errors()) {
    xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_glx_config_tfp : Could not create GLX pixmap\n");
    return 0;
  }

  return 1;
}

static int vaapi_glx_config_glx(vo_driver_t *this_gen, unsigned int width, unsigned int height)
{
  vaapi_driver_t        *this = (vaapi_driver_t *) this_gen;
  ff_vaapi_context_t    *va_context = this->va_context;

  this->gl_vinfo = glXChooseVisual(this->display, this->screen, gl_visual_attr);
  if(!this->gl_vinfo) {
    xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_glx_config_glx : error glXChooseVisual\n");
    this->opengl_render = 0;
  }

  glXMakeCurrent(this->display, None, NULL);
  this->gl_context = glXCreateContext (this->display, this->gl_vinfo, NULL, True);
  if (this->gl_context) {
    if(!glXMakeCurrent (this->display, this->window, this->gl_context)) {
      xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_glx_config_glx : error glXMakeCurrent\n");
      goto error;
    }
  } else {
    xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_glx_config_glx : error glXCreateContext\n");
    goto error;
  }

  void *(*getProcAddress)(const GLubyte *);
  const char *(*glXExtStr)(Display *, int);
  char *glxstr = strdup(" ");

  getProcAddress = vaapi_getdladdr("glXGetProcAddress");
  if (!getProcAddress)
    getProcAddress = vaapi_getdladdr("glXGetProcAddressARB");
  glXExtStr = vaapi_getdladdr("glXQueryExtensionsString");
  if (glXExtStr)
      vaapi_appendstr(&glxstr, glXExtStr(this->display, this->screen));
  glXExtStr = vaapi_getdladdr("glXGetClientString");
  if (glXExtStr)
      vaapi_appendstr(&glxstr, glXExtStr(this->display, GLX_EXTENSIONS));
  glXExtStr = vaapi_getdladdr("glXGetServerString");
  if (glXExtStr)
      vaapi_appendstr(&glxstr, glXExtStr(this->display, GLX_EXTENSIONS));

  vaapi_get_functions(this_gen, getProcAddress, glxstr);
  if (!mpglGenPrograms && mpglGetString &&
      getProcAddress &&
      strstr(mpglGetString(GL_EXTENSIONS), "GL_ARB_vertex_program")) {
    xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_glx_config_glx : Broken glXGetProcAddress detected, trying workaround\n");
    vaapi_get_functions(this_gen, NULL, glxstr);
  }
  free(glxstr);

  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glDisable(GL_CULL_FACE);
  glEnable(GL_TEXTURE_2D);
  glDrawBuffer(GL_BACK);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  /* Create TFP resources */
  if(this->opengl_use_tfp && vaapi_glx_config_tfp(this_gen, width, height)) {
    xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_glx_config_glx : Using GLX texture-from-pixmap extension\n");
  } else {
    this->opengl_use_tfp = 0;
  }

  /* Create OpenGL texture */
  /* XXX: assume GL_ARB_texture_non_power_of_two is available */
  glEnable(GL_TEXTURE_2D);
  glGenTextures(1, &this->gl_texture);
  mpglBindTexture(GL_TEXTURE_2D, this->gl_texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  if (!this->opengl_use_tfp) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, NULL);
  }
  mpglBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_TEXTURE_2D);

  glClearColor(0.0, 0.0, 0.0, 1.0);
  glClear(GL_COLOR_BUFFER_BIT);

  if(!this->gl_texture) {
    xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_glx_config_glx : gl_texture NULL\n");
    goto error;
  }

  if(!this->opengl_use_tfp) {
    VAStatus vaStatus = vaCreateSurfaceGLX(va_context->va_display, GL_TEXTURE_2D, this->gl_texture, &va_context->gl_surface);
    if(!vaapi_check_status(this_gen, vaStatus, "vaCreateSurfaceGLX()")) {
      va_context->gl_surface = NULL;
      goto error;
    }
  } else {
    va_context->gl_surface = NULL;
  }

  lprintf("vaapi_glx_config_glx : GL setup done\n");

  this->valid_opengl_context = 1;
  return 1;

error:
  destroy_glx(this_gen);
  this->valid_opengl_context = 0;
  return 0;
}

static uint32_t vaapi_get_capabilities (vo_driver_t *this_gen) {
  vaapi_driver_t *this = (vaapi_driver_t *) this_gen;

  return this->capabilities;
}

static const struct {
  int fmt;
  enum PixelFormat pix_fmt;
#if defined LIBAVCODEC_VERSION_INT && LIBAVCODEC_VERSION_INT >= ((54<<16)|(25<<8))
  enum AVCodecID codec_id;
#else
  enum CodecID codec_id;
#endif
} conversion_map[] = {
  {IMGFMT_VAAPI_MPEG2,     PIX_FMT_VAAPI_VLD,  CODEC_ID_MPEG2VIDEO},
  {IMGFMT_VAAPI_MPEG2_IDCT,PIX_FMT_VAAPI_IDCT, CODEC_ID_MPEG2VIDEO},
  {IMGFMT_VAAPI_MPEG2_MOCO,PIX_FMT_VAAPI_MOCO, CODEC_ID_MPEG2VIDEO},
  {IMGFMT_VAAPI_MPEG4,     PIX_FMT_VAAPI_VLD,  CODEC_ID_MPEG4},
  {IMGFMT_VAAPI_H263,      PIX_FMT_VAAPI_VLD,  CODEC_ID_H263},
  {IMGFMT_VAAPI_H264,      PIX_FMT_VAAPI_VLD,  CODEC_ID_H264},
  {IMGFMT_VAAPI_WMV3,      PIX_FMT_VAAPI_VLD,  CODEC_ID_WMV3},
  {IMGFMT_VAAPI_VC1,       PIX_FMT_VAAPI_VLD,  CODEC_ID_VC1},
  {0, PIX_FMT_NONE}
};

static int vaapi_pixfmt2imgfmt(enum PixelFormat pix_fmt, int codec_id)
{
  int i;
  int fmt;
  for (i = 0; conversion_map[i].pix_fmt != PIX_FMT_NONE; i++) {
    if (conversion_map[i].pix_fmt == pix_fmt &&
        (conversion_map[i].codec_id == 0 ||
        conversion_map[i].codec_id == codec_id)) {
      break;
    }
  }
  fmt = conversion_map[i].fmt;
  return fmt;
}

static int vaapi_has_profile(VAProfile *va_profiles, int va_num_profiles, VAProfile profile)
{
  if (va_profiles && va_num_profiles > 0) {
    int i;
    for (i = 0; i < va_num_profiles; i++) {
      if (va_profiles[i] == profile)
        return 1;
      }
  }
  return 0;
}

static int profile_from_imgfmt(vo_frame_t *frame_gen, enum PixelFormat pix_fmt, int codec_id, int vaapi_mpeg_sofdec)
{
  vo_driver_t         *this_gen   = (vo_driver_t *) frame_gen->driver;
  vaapi_driver_t      *this       = (vaapi_driver_t *) this_gen;
  ff_vaapi_context_t  *va_context = this->va_context;
  VAStatus            vaStatus;
  int                 profile     = -1;
  int                 maj, min;
  int                 i;
  int                 va_num_profiles;
  int                 max_profiles;
  VAProfile           *va_profiles = NULL;
  int                 inited = 0;

  if(va_context->va_display == NULL) {
    lprintf("profile_from_imgfmt vaInitialize\n");
    inited = 1;
    va_context->va_display = vaapi_get_display(this->display, this->opengl_render);
    if(!va_context->va_display)
      goto out;

    vaStatus = vaInitialize(va_context->va_display, &maj, &min);
    if(!vaapi_check_status(this_gen, vaStatus, "vaInitialize()"))
      goto out;

  }

  max_profiles = vaMaxNumProfiles(va_context->va_display);
  va_profiles = calloc(max_profiles, sizeof(*va_profiles));
  if (!va_profiles)
    goto out;

  vaStatus = vaQueryConfigProfiles(va_context->va_display, va_profiles, &va_num_profiles);
  if(!vaapi_check_status(this_gen, vaStatus, "vaQueryConfigProfiles()"))
    goto out;

  xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " VAAPI Supported Profiles : ");
  for (i = 0; i < va_num_profiles; i++) {
    printf("%s ", vaapi_profile_to_string(va_profiles[i]));
  }
  printf("\n");

  uint32_t format = vaapi_pixfmt2imgfmt(pix_fmt, codec_id);

  static const int mpeg2_profiles[] = { VAProfileMPEG2Main, VAProfileMPEG2Simple, -1 };
  static const int mpeg4_profiles[] = { VAProfileMPEG4Main, VAProfileMPEG4AdvancedSimple, VAProfileMPEG4Simple, -1 };
  static const int h264_profiles[]  = { VAProfileH264High, VAProfileH264Main, VAProfileH264Baseline, -1 };
  static const int wmv3_profiles[]  = { VAProfileVC1Main, VAProfileVC1Simple, -1 };
  static const int vc1_profiles[]   = { VAProfileVC1Advanced, -1 };

  const int *profiles = NULL;
  switch (IMGFMT_VAAPI_CODEC(format)) 
  {
    case IMGFMT_VAAPI_CODEC_MPEG2:
      if(!vaapi_mpeg_sofdec) {
        profiles = mpeg2_profiles;
      }
      break;
    case IMGFMT_VAAPI_CODEC_MPEG4:
      profiles = mpeg4_profiles;
      break;
    case IMGFMT_VAAPI_CODEC_H264:
      profiles = h264_profiles;
      break;
    case IMGFMT_VAAPI_CODEC_VC1:
      switch (format) {
        case IMGFMT_VAAPI_WMV3:
          profiles = wmv3_profiles;
          break;
        case IMGFMT_VAAPI_VC1:
            profiles = vc1_profiles;
            break;
      }
      break;
  }

  if (profiles) {
    int i;
    for (i = 0; profiles[i] != -1; i++) {
      if (vaapi_has_profile(va_profiles, va_num_profiles, profiles[i])) {
        profile = profiles[i];
        xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " VAAPI Profile %s supported by your hardware\n", vaapi_profile_to_string(profiles[i]));
        break;
      }
    }
  }

out:
  if(va_profiles)
    free(va_profiles);
  if(inited) {
    vaStatus = vaTerminate(va_context->va_display);
    vaapi_check_status(this_gen, vaStatus, "vaTerminate()");
  }
  return profile;
}


static const char *vaapi_profile_to_string(VAProfile profile)
{
  switch(profile) {
#define PROFILE(profile) \
    case VAProfile##profile: return "VAProfile" #profile
      PROFILE(MPEG2Simple);
      PROFILE(MPEG2Main);
      PROFILE(MPEG4Simple);
      PROFILE(MPEG4AdvancedSimple);
      PROFILE(MPEG4Main);
      PROFILE(H264Baseline);
      PROFILE(H264Main);
      PROFILE(H264High);
      PROFILE(VC1Simple);
      PROFILE(VC1Main);
      PROFILE(VC1Advanced);
#undef PROFILE
    default: break;
  }
  return "<unknown>";
}

static const char *vaapi_entrypoint_to_string(VAEntrypoint entrypoint)
{
  switch(entrypoint)
  {
#define ENTRYPOINT(entrypoint) \
    case VAEntrypoint##entrypoint: return "VAEntrypoint" #entrypoint
      ENTRYPOINT(VLD);
      ENTRYPOINT(IZZ);
      ENTRYPOINT(IDCT);
      ENTRYPOINT(MoComp);
      ENTRYPOINT(Deblocking);
#undef ENTRYPOINT
    default: break;
  }
  return "<unknown>";
}

/* Init subpicture */
static void vaapi_init_subpicture(vaapi_driver_t *this_gen) {
  vaapi_driver_t        *this = (vaapi_driver_t *) this_gen;
  ff_vaapi_context_t    *va_context = this->va_context;

  va_context->va_subpic_width               = 0;
  va_context->va_subpic_height              = 0;
  va_context->va_subpic_id                  = VA_INVALID_ID;
  va_context->va_subpic_image.image_id      = VA_INVALID_ID;

  this->overlay_output_width = this->overlay_output_height = 0;
  this->overlay_unscaled_width = this->overlay_unscaled_height = 0;
  this->ovl_changed = 0;
  this->has_overlay = 0;
  this->overlay_bitmap = NULL;
  this->overlay_bitmap_size = 0;

}

/* Init vaapi context */
static void vaapi_init_va_context(vaapi_driver_t *this_gen) {
  vaapi_driver_t        *this = (vaapi_driver_t *) this_gen;
  ff_vaapi_context_t    *va_context = this->va_context;
  int i;

  va_context->va_config_id              = VA_INVALID_ID;
  va_context->va_context_id             = VA_INVALID_ID;
  va_context->va_profile                = 0;
  va_context->va_colorspace             = 1;
  va_context->is_bound                  = 0;
  va_context->gl_surface                = NULL;
  va_context->soft_head                 = 0;
  va_context->valid_context             = 0;
  va_context->va_head                   = 0;
  va_context->va_soft_head              = 0;

  for(i = 0; i < RENDER_SURFACES; i++) {
    ff_vaapi_surface_t *va_surface      = &va_render_surfaces[i];

    va_surface->index                   = i;
    va_surface->status                  = SURFACE_FREE;
    va_surface->va_surface_id           = VA_INVALID_SURFACE;

    va_surface_ids[i]                   = VA_INVALID_SURFACE;
  }

  for(i = 0; i < SOFT_SURFACES; i++) {
    va_soft_surface_ids[i]              = VA_INVALID_SURFACE;
    va_soft_images[i].image_id          = VA_INVALID_ID;
  }

  va_context->va_image_formats      = NULL;
  va_context->va_num_image_formats  = 0;

  va_context->va_subpic_formats     = NULL;
  va_context->va_num_subpic_formats = 0;
}

/* Close vaapi  */
static void vaapi_close(vo_driver_t *this_gen) {
  vaapi_driver_t        *this = (vaapi_driver_t *) this_gen;
  ff_vaapi_context_t    *va_context = this->va_context;
  VAStatus              vaStatus;

  if(!va_context || !va_context->va_display || !va_context->valid_context)
    return;

  vaapi_ovl_associate(this_gen, 0, 0);

  destroy_glx((vo_driver_t *)this);

  if(va_context->va_context_id != VA_INVALID_ID) {
    vaStatus = vaDestroyContext(va_context->va_display, va_context->va_context_id);
    vaapi_check_status(this_gen, vaStatus, "vaDestroyContext()");
    va_context->va_context_id = VA_INVALID_ID;
  }
  
  vaapi_destroy_subpicture(this_gen);
  vaapi_destroy_soft_surfaces(this_gen);
  vaapi_destroy_render_surfaces(this_gen);

  if(va_context->va_config_id != VA_INVALID_ID) {
    vaStatus = vaDestroyConfig(va_context->va_display, va_context->va_config_id);
    vaapi_check_status(this_gen, vaStatus, "vaDestroyConfig()");
    va_context->va_config_id = VA_INVALID_ID;
  }

  vaStatus = vaTerminate(va_context->va_display);
  vaapi_check_status(this_gen, vaStatus, "vaTerminate()");
  va_context->va_display = NULL;

  if(va_context->va_image_formats) {
    free(va_context->va_image_formats);
    va_context->va_image_formats      = NULL;
    va_context->va_num_image_formats  = 0;
  }
  if(va_context->va_subpic_formats) {
    free(va_context->va_subpic_formats);
    va_context->va_subpic_formats     = NULL;
    va_context->va_num_subpic_formats = 0;
  }

  va_context->valid_context = 0;
}

/* Returns internal VAAPI context */
static ff_vaapi_context_t *get_context(vo_frame_t *frame_gen) {
  vaapi_driver_t        *this = (vaapi_driver_t *) frame_gen->driver;

  return this->va_context;
}

/* Free allocated VAAPI image */
static void vaapi_destroy_image(vo_driver_t *this_gen, VAImage *va_image) {
  vaapi_driver_t        *this = (vaapi_driver_t *) this_gen;
  ff_vaapi_context_t    *va_context = this->va_context;
  VAStatus              vaStatus;

  if(va_image->image_id != VA_INVALID_ID) {
    lprintf("vaapi_destroy_image 0x%08x\n", va_image->image_id);
    vaStatus = vaDestroyImage(va_context->va_display, va_image->image_id);
    vaapi_check_status(this_gen, vaStatus, "vaDestroyImage()");
  }
  va_image->image_id      = VA_INVALID_ID;
  va_image->width         = 0;
  va_image->height        = 0;
}

/* Allocated VAAPI image */
static VAStatus vaapi_create_image(vo_driver_t *this_gen, VASurfaceID va_surface_id, VAImage *va_image, int width, int height, int clear) {
  vaapi_driver_t        *this = (vaapi_driver_t *) this_gen;
  ff_vaapi_context_t    *va_context = this->va_context;

  int i = 0;
  VAStatus vaStatus;

  if(!va_context->valid_context || va_context->va_image_formats == NULL || va_context->va_num_image_formats == 0)
    return VA_STATUS_ERROR_UNKNOWN;

  va_context->is_bound = 0;

  vaStatus = vaDeriveImage(va_context->va_display, va_surface_id, va_image);
  if(vaStatus == VA_STATUS_SUCCESS) {
    if (va_image->image_id != VA_INVALID_ID && va_image->buf != VA_INVALID_ID) {
      va_context->is_bound = 1;
    }
  }

  if(!va_context->is_bound) {
    for (i = 0; i < va_context->va_num_image_formats; i++) {
      if (va_context->va_image_formats[i].fourcc == VA_FOURCC( 'Y', 'V', '1', '2' ) ||
          va_context->va_image_formats[i].fourcc == VA_FOURCC( 'I', '4', '2', '0' ) /*||
          va_context->va_image_formats[i].fourcc == VA_FOURCC( 'N', 'V', '1', '2' ) */) {
        vaStatus = vaCreateImage( va_context->va_display, &va_context->va_image_formats[i], width, height, va_image );
        if(!vaapi_check_status(this_gen, vaStatus, "vaCreateImage()"))
          goto error;
        break;
      }
    }
  }

  void *p_base = NULL;

  vaStatus = vaMapBuffer( va_context->va_display, va_image->buf, &p_base );
  if(!vaapi_check_status(this_gen, vaStatus, "vaMapBuffer()"))
    goto error;

  if(clear) {
    if(va_image->format.fourcc == VA_FOURCC( 'Y', 'V', '1', '2' ) ||
       va_image->format.fourcc == VA_FOURCC( 'I', '4', '2', '0' )) {
      memset((uint8_t*)p_base + va_image->offsets[0],   0, va_image->pitches[0] * va_image->height);
      memset((uint8_t*)p_base + va_image->offsets[1], 128, va_image->pitches[1] * (va_image->height/2));
      memset((uint8_t*)p_base + va_image->offsets[2], 128, va_image->pitches[2] * (va_image->height/2));
    } else if (va_image->format.fourcc == VA_FOURCC( 'N', 'V', '1', '2' ) ) {
      memset((uint8_t*)p_base + va_image->offsets[0],   0, va_image->pitches[0] * va_image->height);
      memset((uint8_t*)p_base + va_image->offsets[1], 128, va_image->pitches[1] * (va_image->height/2));
    }
  }

  vaStatus = vaUnmapBuffer( va_context->va_display, va_image->buf );
  vaapi_check_status(this_gen, vaStatus, "vaUnmapBuffer()");

  lprintf("vaapi_create_image 0x%08x width %d height %d format %s\n", va_image->image_id, va_image->width, va_image->height,
      string_of_VAImageFormat(&va_image->format));

  return VA_STATUS_SUCCESS;

error:
  /* house keeping */
  vaapi_destroy_image(this_gen, va_image);
  return VA_STATUS_ERROR_UNKNOWN;
}

/* Deassociate and free subpicture */
static void vaapi_destroy_subpicture(vo_driver_t *this_gen) {
  vaapi_driver_t        *this = (vaapi_driver_t *) this_gen;
  ff_vaapi_context_t    *va_context = this->va_context;
  VAStatus              vaStatus;

  lprintf("destroy sub 0x%08x 0x%08x 0x%08x\n", va_context->va_subpic_id, 
      va_context->va_subpic_image.image_id, va_context->va_subpic_image.buf);

  if(va_context->va_subpic_id != VA_INVALID_ID) {
    vaStatus = vaDestroySubpicture(va_context->va_display, va_context->va_subpic_id);
    vaapi_check_status(this_gen, vaStatus, "vaDeassociateSubpicture()");
  }
  va_context->va_subpic_id = VA_INVALID_ID;

  vaapi_destroy_image(this_gen, &va_context->va_subpic_image);

}

/* Create VAAPI subpicture */
static VAStatus vaapi_create_subpicture(vo_driver_t *this_gen, int width, int height) {
  vaapi_driver_t      *this = (vaapi_driver_t *) this_gen;
  ff_vaapi_context_t  *va_context = this->va_context;
  VAStatus            vaStatus;

  int i = 0;

  if(!va_context->valid_context || !va_context->va_subpic_formats || va_context->va_num_subpic_formats == 0)
    return VA_STATUS_ERROR_UNKNOWN;

  for (i = 0; i < va_context->va_num_subpic_formats; i++) {
    if ( va_context->va_subpic_formats[i].fourcc == VA_FOURCC('B','G','R','A')) {

      vaStatus = vaCreateImage( va_context->va_display, &va_context->va_subpic_formats[i], width, height, &va_context->va_subpic_image );
      if(!vaapi_check_status(this_gen, vaStatus, "vaCreateImage()"))
        goto error;

      vaStatus = vaCreateSubpicture(va_context->va_display, va_context->va_subpic_image.image_id, &va_context->va_subpic_id );
      if(!vaapi_check_status(this_gen, vaStatus, "vaCreateSubpicture()"))
        goto error;
    }
  }

  if(va_context->va_subpic_image.image_id == VA_INVALID_ID || va_context->va_subpic_id == VA_INVALID_ID)
    goto error;

  void *p_base = NULL;

  lprintf("create sub 0x%08x 0x%08x 0x%08x\n", va_context->va_subpic_id, 
      va_context->va_subpic_image.image_id, va_context->va_subpic_image.buf);

  vaStatus = vaMapBuffer(va_context->va_display, va_context->va_subpic_image.buf, &p_base);
  if(!vaapi_check_status(this_gen, vaStatus, "vaMapBuffer()"))
    goto error;

  memset((uint32_t *)p_base, 0x0, va_context->va_subpic_image.data_size);
  vaStatus = vaUnmapBuffer(va_context->va_display, va_context->va_subpic_image.buf);
  vaapi_check_status(this_gen, vaStatus, "vaUnmapBuffer()");

  this->overlay_output_width  = width;
  this->overlay_output_height = height;

  lprintf("vaapi_create_subpicture 0x%08x format %s\n", va_context->va_subpic_image.image_id, 
      string_of_VAImageFormat(&va_context->va_subpic_image.format));

  return VA_STATUS_SUCCESS;

error:
  /* house keeping */
  if(va_context->va_subpic_id != VA_INVALID_ID)
    vaapi_destroy_subpicture(this_gen);
  va_context->va_subpic_id = VA_INVALID_ID;

  vaapi_destroy_image(this_gen, &va_context->va_subpic_image);

  this->overlay_output_width  = 0;
  this->overlay_output_height = 0;

  return VA_STATUS_ERROR_UNKNOWN;
}

static void vaapi_set_csc_mode(vaapi_driver_t *this, int new_mode)
{
  if (new_mode == CSC_MODE_USER_MATRIX) {
    this->capabilities |= VO_CAP_BRIGHTNESS | VO_CAP_CONTRAST | VO_CAP_SATURATION | VO_CAP_HUE
      | VO_CAP_COLOR_MATRIX | VO_CAP_FULLRANGE;
  } else {
    this->capabilities &=
      ~(VO_CAP_BRIGHTNESS | VO_CAP_CONTRAST | VO_CAP_SATURATION | VO_CAP_HUE | VO_CAP_COLOR_MATRIX | VO_CAP_FULLRANGE);
    if (this->props[VO_PROP_BRIGHTNESS].atom)
      this->capabilities |= VO_CAP_BRIGHTNESS;
    if (this->props[VO_PROP_CONTRAST].atom)
      this->capabilities |= VO_CAP_CONTRAST;
    if (this->props[VO_PROP_SATURATION].atom)
      this->capabilities |= VO_CAP_SATURATION;
    if (this->props[VO_PROP_HUE].atom)
      this->capabilities |= VO_CAP_HUE;
#if (defined VA_SRC_BT601) && ((defined VA_SRC_BT709) || (defined VA_SRC_SMPTE_240))
    this->capabilities |= VO_CAP_COLOR_MATRIX;
#endif
    if (new_mode != CSC_MODE_FLAGS) {
      if ((this->capabilities & (VO_CAP_BRIGHTNESS | VO_CAP_CONTRAST)) == (VO_CAP_BRIGHTNESS | VO_CAP_CONTRAST))
        this->capabilities |= VO_CAP_FULLRANGE;
    }
  }

  this->csc_mode = new_mode;
  this->color_matrix = 0;
}

static const char * const vaapi_csc_mode_labels[] = {
  "user_matrix", "simple", "simple+2", "simple+3", NULL
};

/* normalize to 0.0 ~ 2.0 */
static float vaapi_normalized_prop (vaapi_driver_t *this, int prop) {
  int range = (this->props[prop].max - this->props[prop].min) >> 1;

  if (range)
    return (float)(this->props[prop].value - this->props[prop].min) / (float)range;
  return 1.0;
}

static void vaapi_update_csc (vaapi_driver_t *that, vaapi_frame_t *frame) {
  int color_matrix;
  int i;

  color_matrix = cm_from_frame (&frame->vo_frame);

  if (that->color_matrix != color_matrix) {

    /* revert unsupported modes */
    i = that->csc_mode;
    if (i == CSC_MODE_USER_MATRIX && !that->have_user_csc_matrix)
      i = CSC_MODE_FLAGS_FULLRANGE3;
    if (i == CSC_MODE_FLAGS_FULLRANGE3 && !that->props[VO_PROP_SATURATION].atom)
      i = CSC_MODE_FLAGS_FULLRANGE2;
    if (i == CSC_MODE_FLAGS_FULLRANGE2 && !that->props[VO_PROP_BRIGHTNESS].atom)
      i = CSC_MODE_FLAGS;
    if (i != that->csc_mode) {
      xprintf (that->xine, XINE_VERBOSITY_LOG,
        _("video_out_vaapi: driver does not support \"%s\" colorspace conversion mode\n"),
        vaapi_csc_mode_labels[that->csc_mode]);
      vaapi_set_csc_mode (that, i);
    }

    that->color_matrix = color_matrix;

    if (that->csc_mode == CSC_MODE_USER_MATRIX) {
      /* WOW - full support */
      float hue = (vaapi_normalized_prop (that, VO_PROP_HUE) - 1.0) * 3.14159265359;
      float saturation = vaapi_normalized_prop (that, VO_PROP_SATURATION);
      float contrast = vaapi_normalized_prop (that, VO_PROP_CONTRAST);
      float brightness = (vaapi_normalized_prop (that, VO_PROP_BRIGHTNESS) - 1.0) * 128.0;
      float *matrix = that->user_csc_matrix;
      float uvcos = saturation * cos( hue );
      float uvsin = saturation * sin( hue );
      int i;
      VADisplayAttribute attr;

      if ((color_matrix >> 1) == 8) {
        /* YCgCo. This is really quite simple. */
        uvsin *= contrast;
        uvcos *= contrast;
        /* matrix[rgb][yuv1] */
        matrix[1] = -1.0 * uvcos - 1.0 * uvsin;
        matrix[2] =  1.0 * uvcos - 1.0 * uvsin;
        matrix[5] =  1.0 * uvcos;
        matrix[6] =                1.0 * uvsin;
        matrix[9] = -1.0 * uvcos + 1.0 * uvsin;
        matrix[10] = -1.0 * uvcos - 1.0 * uvsin;
        for (i = 0; i < 12; i += 4) {
          matrix[i] = contrast;
          matrix[i + 3] = (brightness * contrast - 128.0 * (matrix[i + 1] + matrix[i + 2])) / 255.0;
        }
      } else {
        /* YCbCr */
        float kb, kr;
        float vr, vg, ug, ub;
        float ygain, yoffset;

        switch (color_matrix >> 1) {
          case 1:  kb = 0.0722; kr = 0.2126; break; /* ITU-R 709 */
          case 4:  kb = 0.1100; kr = 0.3000; break; /* FCC */
          case 7:  kb = 0.0870; kr = 0.2120; break; /* SMPTE 240 */
          default: kb = 0.1140; kr = 0.2990;        /* ITU-R 601 */
        }
        vr = 2.0 * (1.0 - kr);
        vg = -2.0 * kr * (1.0 - kr) / (1.0 - kb - kr);
        ug = -2.0 * kb * (1.0 - kb) / (1.0 - kb - kr);
        ub = 2.0 * (1.0 - kb);

        if (color_matrix & 1) {
          /* fullrange mode */
          yoffset = brightness;
          ygain = contrast;
          uvcos *= contrast * 255.0 / 254.0;
          uvsin *= contrast * 255.0 / 254.0;
        } else {
          /* mpeg range */
          yoffset = brightness - 16.0;
          ygain = contrast * 255.0 / 219.0;
          uvcos *= contrast * 255.0 / 224.0;
          uvsin *= contrast * 255.0 / 224.0;
        }

        /* matrix[rgb][yuv1] */
        matrix[1] = -uvsin * vr;
        matrix[2] = uvcos * vr;
        matrix[5] = uvcos * ug - uvsin * vg;
        matrix[6] = uvcos * vg + uvsin * ug;
        matrix[9] = uvcos * ub;
        matrix[10] = uvsin * ub;
        for (i = 0; i < 12; i += 4) {
          matrix[i] = ygain;
          matrix[i + 3] = (yoffset * ygain - 128.0 * (matrix[i + 1] + matrix[i + 2])) / 255.0;
        }
      }

      attr.type   = VADisplayAttribCSCMatrix;
      /* libva design bug: VADisplayAttribute.value is plain int.
        On 64bit system, a pointer value put here will overwrite the following "flags" field too. */
      memcpy (&(attr.value), &matrix, sizeof (float *));
      vaSetDisplayAttributes (that->va_context->va_display, &attr, 1);

      xprintf (that->xine, XINE_VERBOSITY_LOG,"video_out_vaapi: b %d c %d s %d h %d [%s]\n",
        that->props[VO_PROP_BRIGHTNESS].value,
        that->props[VO_PROP_CONTRAST].value,
        that->props[VO_PROP_SATURATION].value,
        that->props[VO_PROP_HUE].value,
        cm_names[color_matrix]);

    } else {
      /* fall back to old style */
      int brightness = that->props[VO_PROP_BRIGHTNESS].value;
      int contrast   = that->props[VO_PROP_CONTRAST].value;
      int saturation = that->props[VO_PROP_SATURATION].value;
      int hue        = that->props[VO_PROP_HUE].value;
      int i;
      VADisplayAttribute attr[4];

      /* The fallback rhapsody */
#if defined(VA_SRC_BT601) && (defined(VA_SRC_BT709) || defined(VA_SRC_SMPTE_240))
      i = color_matrix >> 1;
      switch (i) {
        case 1:
#if defined(VA_SRC_BT709)
          that->vaapi_cm_flags = VA_SRC_BT709;
#elif defined(VA_SRC_SMPTE_240)
          that->vaapi_cm_flags = VA_SRC_SMPTE_240;
          i = 7;
#endif
        break;
        case 7:
#if defined(VA_SRC_SMPTE_240)
          that->vaapi_cm_flags = VA_SRC_SMPTE_240;
#elif defined(VA_SRC_BT709)
          that->vaapi_cm_flags = VA_SRC_BT709;
          i = 1;
#endif
        break;
        default:
          that->vaapi_cm_flags = VA_SRC_BT601;
          i = 5;
      }
#else
      that->vaapi_cm_flags = 0;
      i = 2; /* undefined */
#endif
      color_matrix &= 1;
      color_matrix |= i << 1;

      if ((that->csc_mode != CSC_MODE_FLAGS) && (color_matrix & 1)) {
        int a, b;
        /* fullrange mode. XXX assuming TV set style bcs controls 0% - 200% */
        if (that->csc_mode == CSC_MODE_FLAGS_FULLRANGE3) {
          saturation -= that->props[VO_PROP_SATURATION].min;
          saturation  = (saturation * (112 * 255) + (127 * 219 / 2)) / (127 * 219);
          saturation += that->props[VO_PROP_SATURATION].min;
          if (saturation > that->props[VO_PROP_SATURATION].max)
            saturation = that->props[VO_PROP_SATURATION].max;
        }

        contrast -= that->props[VO_PROP_CONTRAST].min;
        contrast  = (contrast * 219 + 127) / 255;
        a         = contrast * (that->props[VO_PROP_BRIGHTNESS].max - that->props[VO_PROP_BRIGHTNESS].min);
        contrast += that->props[VO_PROP_CONTRAST].min;
        b         = 256 * (that->props[VO_PROP_CONTRAST].max - that->props[VO_PROP_CONTRAST].min);

        brightness += (16 * a + b / 2) / b;
        if (brightness > that->props[VO_PROP_BRIGHTNESS].max)
          brightness = that->props[VO_PROP_BRIGHTNESS].max;
      }

      i = 0;
      if (that->props[VO_PROP_BRIGHTNESS].atom) {
        attr[i].type  = that->props[VO_PROP_BRIGHTNESS].type;
        attr[i].value = brightness;
        i++;
      }
      if (that->props[VO_PROP_CONTRAST].atom) {
        attr[i].type  = that->props[VO_PROP_CONTRAST].type;
        attr[i].value = contrast;
        i++;
      }
      if (that->props[VO_PROP_SATURATION].atom) {
        attr[i].type  = that->props[VO_PROP_SATURATION].type;
        attr[i].value = saturation;
        i++;
      }
      if (that->props[VO_PROP_HUE].atom) {
        attr[i].type  = that->props[VO_PROP_HUE].type;
        attr[i].value = hue;
        i++;
      }
      if (i)
        vaSetDisplayAttributes (that->va_context->va_display, attr, i);

      xprintf (that->xine, XINE_VERBOSITY_LOG,"video_out_vaapi: %s b %d c %d s %d h %d [%s]\n",
        color_matrix & 1 ? "modified" : "",
        brightness, contrast, saturation, hue, cm_names[color_matrix]);
    }
  }
}

static void vaapi_property_callback (void *property_gen, xine_cfg_entry_t *entry) {
  va_property_t       *property   = (va_property_t *) property_gen;
  vaapi_driver_t      *this       = property->this;
  ff_vaapi_context_t  *va_context = this->va_context;

  pthread_mutex_lock(&this->vaapi_lock);
  DO_LOCKDISPLAY;

  VADisplayAttribute attr;

  attr.type   = property->type;
  attr.value  = entry->num_value;

  lprintf("vaapi_property_callback property=%d, value=%d\n", property->type, entry->num_value );

  /*VAStatus vaStatus = */ vaSetDisplayAttributes(va_context->va_display, &attr, 1);
  //vaapi_check_status((vo_driver_t *)this, vaStatus, "vaSetDisplayAttributes()");

  vaapi_show_display_props((vo_driver_t*)this);

  DO_UNLOCKDISPLAY;
  pthread_mutex_unlock(&this->vaapi_lock);
}

/* called xlocked */
static void vaapi_check_capability (vaapi_driver_t *this,
         int property, VADisplayAttribute attr, 
         const char *config_name,
         const char *config_desc,
         const char *config_help) {
  int int_default = 0;
  cfg_entry_t *entry;

  this->props[property].type   = attr.type;
  this->props[property].min    = attr.min_value;
  this->props[property].max    = attr.max_value;
  int_default                  = attr.value;
  this->props[property].atom   = 1;

  if (config_name) {
    /* is this a boolean property ? */
    if ((attr.min_value == 0) && (attr.max_value == 1)) {
      this->config->register_bool (this->config, config_name, int_default,
           config_desc,
           config_help, 20, vaapi_property_callback, &this->props[property]);

    } else {
      this->config->register_range (this->config, config_name, int_default,
            this->props[property].min, this->props[property].max,
            config_desc,
            config_help, 20, vaapi_property_callback, &this->props[property]);
    }

    entry = this->config->lookup_entry (this->config, config_name);
    if((entry->num_value < this->props[property].min) ||
       (entry->num_value > this->props[property].max)) {

      this->config->update_num(this->config, config_name,
             ((this->props[property].min + this->props[property].max) >> 1));

      entry = this->config->lookup_entry (this->config, config_name);
    }

    this->props[property].entry = entry;

    vaapi_set_property(&this->vo_driver, property, entry->num_value);
  } else {
    this->props[property].value  = int_default;
  }
}

static void vaapi_show_display_props(vo_driver_t *this_gen) {
  /*
  vaapi_driver_t      *this = (vaapi_driver_t *) this_gen;

  if(this->capabilities & VO_CAP_BRIGHTNESS)
    xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_init : brightness     : %d\n", this->props[VO_PROP_BRIGHTNESS].value);
  if(this->capabilities & VO_CAP_CONTRAST)
    xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_init : contrast       : %d\n", this->props[VO_PROP_CONTRAST].value);
  if(this->capabilities & VO_CAP_HUE)
    xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_init : hue            : %d\n", this->props[VO_PROP_HUE].value);
  if(this->capabilities & VO_CAP_SATURATION)
    xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_init : saturation     : %d\n", this->props[VO_PROP_SATURATION].value); 
  */
}

/* VAAPI display attributes. */
static void vaapi_display_attribs(vo_driver_t *this_gen) {
  vaapi_driver_t      *this = (vaapi_driver_t *) this_gen;
  ff_vaapi_context_t  *va_context = this->va_context;

  int num_display_attrs, max_display_attrs;
  VAStatus vaStatus;
  VADisplayAttribute *display_attrs;
  int i;

  max_display_attrs = vaMaxNumDisplayAttributes(va_context->va_display);
  display_attrs = calloc(max_display_attrs, sizeof(*display_attrs));

  if (display_attrs) {
    num_display_attrs = 0;
    vaStatus = vaQueryDisplayAttributes(va_context->va_display,
                                        display_attrs, &num_display_attrs);
    if(vaapi_check_status(this_gen, vaStatus, "vaQueryDisplayAttributes()")) {
      for (i = 0; i < num_display_attrs; i++) {
        xprintf (this->xine, XINE_VERBOSITY_DEBUG,
          "video_out_vaapi: display attribute #%d = %d [%d .. %d], flags %d\n",
          (int)display_attrs[i].type,
          display_attrs[i].value, display_attrs[i].min_value, display_attrs[i].max_value,
          display_attrs[i].flags);
        switch (display_attrs[i].type) {
          case VADisplayAttribBrightness:
            if( ( display_attrs[i].flags & VA_DISPLAY_ATTRIB_GETTABLE ) &&
                ( display_attrs[i].flags & VA_DISPLAY_ATTRIB_SETTABLE ) ) {
              this->capabilities |= VO_CAP_BRIGHTNESS;
              vaapi_check_capability(this, VO_PROP_BRIGHTNESS, display_attrs[i], "video.output.vaapi_brightness", "Brightness setting", "Brightness setting");
            }
            break;
          case VADisplayAttribContrast:
            if( ( display_attrs[i].flags & VA_DISPLAY_ATTRIB_GETTABLE ) &&
                ( display_attrs[i].flags & VA_DISPLAY_ATTRIB_SETTABLE ) ) {
              this->capabilities |= VO_CAP_CONTRAST;
              vaapi_check_capability(this, VO_PROP_CONTRAST, display_attrs[i], "video.output.vaapi_contrast", "Contrast setting", "Contrast setting");
            }
            break;
          case VADisplayAttribHue:
            if( ( display_attrs[i].flags & VA_DISPLAY_ATTRIB_GETTABLE ) &&
                ( display_attrs[i].flags & VA_DISPLAY_ATTRIB_SETTABLE ) ) {
              this->capabilities |= VO_CAP_HUE;
              vaapi_check_capability(this, VO_PROP_HUE, display_attrs[i], "video.output.vaapi_hue", "Hue setting", "Hue setting");
            }
            break;
          case VADisplayAttribSaturation:
            if( ( display_attrs[i].flags & VA_DISPLAY_ATTRIB_GETTABLE ) &&
                ( display_attrs[i].flags & VA_DISPLAY_ATTRIB_SETTABLE ) ) {
              this->capabilities |= VO_CAP_SATURATION;
              vaapi_check_capability(this, VO_PROP_SATURATION, display_attrs[i], "video.output.vaapi_saturation", "Saturation setting", "Saturation setting");
            }
            break;
          case VADisplayAttribCSCMatrix:
            if (display_attrs[i].flags & VA_DISPLAY_ATTRIB_SETTABLE) {
              this->have_user_csc_matrix = 1;
            }
            break;
          default:
            break;
        }
      }
    }
    free(display_attrs);
  }

  if (this->have_user_csc_matrix) {
    /* make sure video eq is full usable for user matrix mode */
    if (!this->props[VO_PROP_BRIGHTNESS].atom) {
      this->props[VO_PROP_BRIGHTNESS].min   = -1000;
      this->props[VO_PROP_BRIGHTNESS].max   =  1000;
      this->props[VO_PROP_BRIGHTNESS].value =  0;
    }
    if (!this->props[VO_PROP_CONTRAST].atom) {
      this->props[VO_PROP_CONTRAST].min = this->props[VO_PROP_BRIGHTNESS].min;
      this->props[VO_PROP_CONTRAST].max = this->props[VO_PROP_BRIGHTNESS].max;
      this->props[VO_PROP_CONTRAST].value
        = (this->props[VO_PROP_CONTRAST].max - this->props[VO_PROP_CONTRAST].min) >> 1;
    }
    if (!this->props[VO_PROP_SATURATION].atom) {
      this->props[VO_PROP_SATURATION].min = this->props[VO_PROP_CONTRAST].min;
      this->props[VO_PROP_SATURATION].max = this->props[VO_PROP_CONTRAST].max;
      this->props[VO_PROP_SATURATION].value
        = (this->props[VO_PROP_CONTRAST].max - this->props[VO_PROP_CONTRAST].min) >> 1;
    }
    if (!this->props[VO_PROP_HUE].atom) {
      this->props[VO_PROP_HUE].min = this->props[VO_PROP_BRIGHTNESS].min;
      this->props[VO_PROP_HUE].min = this->props[VO_PROP_BRIGHTNESS].max;
      this->props[VO_PROP_HUE].value
        = (this->props[VO_PROP_BRIGHTNESS].max - this->props[VO_PROP_BRIGHTNESS].min) >> 1;
    }
  }

  vaapi_show_display_props(this_gen);
}

static void vaapi_set_background_color(vo_driver_t *this_gen) {
  vaapi_driver_t      *this = (vaapi_driver_t *)this_gen;
  ff_vaapi_context_t  *va_context = this->va_context;
  //VAStatus            vaStatus;

  if(!va_context->valid_context)
    return;

  VADisplayAttribute attr;
  memset( &attr, 0, sizeof(attr) );

  attr.type  = VADisplayAttribBackgroundColor;
  attr.value = 0x000000;

  /*vaStatus =*/ vaSetDisplayAttributes(va_context->va_display, &attr, 1);
  //vaapi_check_status(this_gen, vaStatus, "vaSetDisplayAttributes()");
}

static VAStatus vaapi_destroy_render_surfaces(vo_driver_t *this_gen) {
  vaapi_driver_t      *this = (vaapi_driver_t *)this_gen;
  ff_vaapi_context_t  *va_context = this->va_context;
  int                 i;
  VAStatus            vaStatus;

  for(i = 0; i < RENDER_SURFACES; i++) {
    if(va_surface_ids[i] != VA_INVALID_SURFACE) {
      vaStatus = vaSyncSurface(va_context->va_display, va_surface_ids[i]);
      vaapi_check_status(this_gen, vaStatus, "vaSyncSurface()");
      vaStatus = vaDestroySurfaces(va_context->va_display, &va_surface_ids[i], 1);
      vaapi_check_status(this_gen, vaStatus, "vaDestroySurfaces()");
      va_surface_ids[i] = VA_INVALID_SURFACE;

      ff_vaapi_surface_t *va_surface  = &va_render_surfaces[i];
      va_surface->index               = i;
      va_surface->status              = SURFACE_FREE;
      va_surface->va_surface_id       = va_surface_ids[i];
    }
  }

  return VA_STATUS_SUCCESS;
}

static VAStatus vaapi_destroy_soft_surfaces(vo_driver_t *this_gen) {
  vaapi_driver_t      *this = (vaapi_driver_t *)this_gen;
  ff_vaapi_context_t  *va_context = this->va_context;
  int                 i;
  VAStatus            vaStatus;


  for(i = 0; i < SOFT_SURFACES; i++) {
    if(va_soft_images[i].image_id != VA_INVALID_ID)
      vaapi_destroy_image((vo_driver_t *)this, &va_soft_images[i]);
    va_soft_images[i].image_id = VA_INVALID_ID;

    if(va_soft_surface_ids[i] != VA_INVALID_SURFACE) {
#ifdef DEBUG_SURFACE
      printf("vaapi_close destroy render surface 0x%08x\n", va_soft_surface_ids[i]);
#endif
      vaStatus = vaSyncSurface(va_context->va_display, va_soft_surface_ids[i]);
      vaapi_check_status(this_gen, vaStatus, "vaSyncSurface()");
      vaStatus = vaDestroySurfaces(va_context->va_display, &va_soft_surface_ids[i], 1);
      vaapi_check_status(this_gen, vaStatus, "vaDestroySurfaces()");
      va_soft_surface_ids[i] = VA_INVALID_SURFACE;
    }
  }

  va_context->sw_width  = 0;
  va_context->sw_height = 0;
  return VA_STATUS_SUCCESS;
}

static VAStatus vaapi_init_soft_surfaces(vo_driver_t *this_gen, int width, int height) {
  vaapi_driver_t      *this = (vaapi_driver_t *)this_gen;
  ff_vaapi_context_t  *va_context = this->va_context;
  VAStatus            vaStatus;
  int                 i;

  vaapi_destroy_soft_surfaces(this_gen);

  vaStatus = vaCreateSurfaces(va_context->va_display, VA_RT_FORMAT_YUV420, width, height, va_soft_surface_ids, SOFT_SURFACES, NULL, 0);
  if(!vaapi_check_status(this_gen, vaStatus, "vaCreateSurfaces()"))
    goto error;

  /* allocate software surfaces */
  for(i = 0; i < SOFT_SURFACES; i++) {
    ff_vaapi_surface_t *va_surface  = &va_render_surfaces[i];

    vaStatus = vaapi_create_image((vo_driver_t *)this, va_soft_surface_ids[i], &va_soft_images[i], width, height, 1);
    if(!vaapi_check_status(this_gen, vaStatus, "vaapi_create_image()")) {
      va_soft_images[i].image_id = VA_INVALID_ID;
      goto error;
    }

    va_surface->index = i;

    if(!va_context->is_bound) {
      vaStatus = vaPutImage(va_context->va_display, va_soft_surface_ids[i], va_soft_images[i].image_id,
               0, 0, va_soft_images[i].width, va_soft_images[i].height,
               0, 0, va_soft_images[i].width, va_soft_images[i].height);
      vaapi_check_status(this_gen, vaStatus, "vaPutImage()");
    }
#ifdef DEBUG_SURFACE
    printf("vaapi_init_soft_surfaces 0x%08x\n", va_soft_surface_ids[i]);
#endif
  }

  va_context->sw_width  = width;
  va_context->sw_height = height;
  return VA_STATUS_SUCCESS;

error:
  va_context->sw_width  = 0;
  va_context->sw_height = 0;
  vaapi_destroy_soft_surfaces(this_gen);
  return VA_STATUS_ERROR_UNKNOWN;
}

static VAStatus vaapi_init_internal(vo_driver_t *this_gen, int va_profile, int width, int height, int softrender) {
  vaapi_driver_t      *this = (vaapi_driver_t *)this_gen;
  ff_vaapi_context_t  *va_context = this->va_context;
  VAConfigAttrib      va_attrib;
  int                 maj, min, i;
  VAStatus            vaStatus;

  vaapi_close(this_gen);
  vaapi_init_va_context(this);

  this->va_context->va_display = vaapi_get_display(this->display, this->opengl_render);

  if(!this->va_context->va_display)
    goto error;

  vaStatus = vaInitialize(this->va_context->va_display, &maj, &min);
  if(!vaapi_check_status((vo_driver_t *)this, vaStatus, "vaInitialize()"))
    goto error;

  lprintf("libva: %d.%d\n", maj, min);

  va_context->valid_context = 1;

  int fmt_count = 0;
  fmt_count = vaMaxNumImageFormats( va_context->va_display );
  va_context->va_image_formats = calloc( fmt_count, sizeof(*va_context->va_image_formats) );

  vaStatus = vaQueryImageFormats(va_context->va_display, va_context->va_image_formats, &va_context->va_num_image_formats);
  if(!vaapi_check_status(this_gen, vaStatus, "vaQueryImageFormats()"))
    goto error;

  fmt_count = vaMaxNumSubpictureFormats( va_context->va_display );
  va_context->va_subpic_formats = calloc( fmt_count, sizeof(*va_context->va_subpic_formats) );

  vaStatus = vaQuerySubpictureFormats( va_context->va_display , va_context->va_subpic_formats, 0, &va_context->va_num_subpic_formats );
  if(!vaapi_check_status(this_gen, vaStatus, "vaQuerySubpictureFormats()"))
    goto error;
  
  const char *vendor = vaQueryVendorString(va_context->va_display);
  xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_open: Vendor : %s\n", vendor);
    
  this->query_va_status = 1;
  char *p = (char *)vendor;
  for(i = 0; i < strlen(vendor); i++, p++) {
    if(strncmp(p, "VDPAU", strlen("VDPAU")) == 0) {
      xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_open: Enable Splitted-Desktop Systems VDPAU-VIDEO workarounds.\n");
      this->query_va_status = 0;
      this->opengl_use_tfp = 0;
      break;
    }
  }

  vaapi_set_background_color(this_gen);
  vaapi_display_attribs((vo_driver_t *)this);

  va_context->width = width;
  va_context->height = height;
  va_context->va_profile = va_profile;

  xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_init : Context width %d height %d\n", va_context->width, va_context->height);

  /* allocate decoding surfaces */
  vaStatus = vaCreateSurfaces(va_context->va_display, VA_RT_FORMAT_YUV420, va_context->width, va_context->height, va_surface_ids, RENDER_SURFACES, NULL, 0);
  if(!vaapi_check_status(this_gen, vaStatus, "vaCreateSurfaces()"))
    goto error;

  /* hardware decoding needs more setup */
  if(!softrender && va_profile >= 0) {
    xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_init : Profile: %d (%s) Entrypoint %d (%s) Surfaces %d\n", va_context->va_profile, vaapi_profile_to_string(va_context->va_profile), VAEntrypointVLD, vaapi_entrypoint_to_string(VAEntrypointVLD), RENDER_SURFACES);

    memset( &va_attrib, 0, sizeof(va_attrib) );
    va_attrib.type = VAConfigAttribRTFormat;

    vaStatus = vaGetConfigAttributes(va_context->va_display, va_context->va_profile, VAEntrypointVLD, &va_attrib, 1);
    if(!vaapi_check_status(this_gen, vaStatus, "vaGetConfigAttributes()"))
      goto error;
  
    if( (va_attrib.value & VA_RT_FORMAT_YUV420) == 0 )
      goto error;

    vaStatus = vaCreateConfig(va_context->va_display, va_context->va_profile, VAEntrypointVLD, &va_attrib, 1, &va_context->va_config_id);
    if(!vaapi_check_status(this_gen, vaStatus, "vaCreateConfig()")) {
      va_context->va_config_id = VA_INVALID_ID;
      goto error;
    }

    vaStatus = vaCreateContext(va_context->va_display, va_context->va_config_id, va_context->width, va_context->height,
                               VA_PROGRESSIVE, va_surface_ids, RENDER_SURFACES, &va_context->va_context_id);
    if(!vaapi_check_status(this_gen, vaStatus, "vaCreateContext()")) {
      va_context->va_context_id = VA_INVALID_ID;
      goto error;
    }
  }

  /* xine was told to allocate RENDER_SURFACES frames. assign the frames the rendering surfaces. */
  for(i = 0; i < RENDER_SURFACES; i++) {
    ff_vaapi_surface_t *va_surface  = &va_render_surfaces[i];
    va_surface->index               = i;
    va_surface->status              = SURFACE_FREE;
    va_surface->va_surface_id       = va_surface_ids[i];

    if(this->frames[i]) {
      vaapi_frame_t *frame                  = this->frames[i];
      frame->vaapi_accel_data.index         = i;

      VAImage va_image;
      vaStatus = vaapi_create_image(va_context->driver, va_surface_ids[i], &va_image, width, height, 1);
      if(vaapi_check_status(va_context->driver, vaStatus, "vaapi_create_image()") && !va_context->is_bound) {
        vaStatus = vaPutImage(va_context->va_display, va_surface_ids[i], va_image.image_id,
                              0, 0, va_image.width, va_image.height,
                              0, 0, va_image.width, va_image.height);
        vaapi_destroy_image(va_context->driver, &va_image);
      }
    }
#ifdef DEBUG_SURFACE
    printf("vaapi_init_internal 0x%08x\n", va_surface_ids[i]);
#endif
  }

  vaStatus = vaapi_init_soft_surfaces(this_gen, width, height);
  if(!vaapi_check_status(this_gen, vaStatus, "vaapi_init_soft_surfaces()")) {
    vaapi_destroy_soft_surfaces(this_gen);
    goto error;
  }

  xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_init : guarded render : %d\n", this->guarded_render);
  xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_init : glxrender      : %d\n", this->opengl_render);
  xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_init : glxrender tfp  : %d\n", this->opengl_use_tfp);
  xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_init : is_bound       : %d\n", va_context->is_bound);
  xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_init : scaling level  : name %s value 0x%08x\n", scaling_level_enum_names[this->scaling_level_enum], this->scaling_level);

  this->init_opengl_render = 1;

  return VA_STATUS_SUCCESS;

error:
  vaapi_close(this_gen);
  vaapi_init_va_context(this);
  va_context->valid_context = 0;
  xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_init : error init vaapi\n");

  return VA_STATUS_ERROR_UNKNOWN;
}

/* 
 * Init VAAPI. This function is called from the decoder side.
 * When the decoder uses software decoding vaapi_init is not called.
 * Therefore we do it in vaapi_display_frame to get a valid VAAPI context
 */ 
static VAStatus vaapi_init(vo_frame_t *frame_gen, int va_profile, int width, int height, int softrender) {
  if(!frame_gen)
    return VA_STATUS_ERROR_UNKNOWN;

  vo_driver_t         *this_gen   = (vo_driver_t *) frame_gen->driver;
  vaapi_driver_t      *this       = (vaapi_driver_t *) this_gen;
  ff_vaapi_context_t  *va_context = this->va_context;

  VAStatus vaStatus;

  unsigned int last_sub_img_fmt = va_context->last_sub_image_fmt;

  if(last_sub_img_fmt)
    vaapi_ovl_associate(this_gen, frame_gen->format, 0);

  if(!this->guarded_render) {
    pthread_mutex_lock(&this->vaapi_lock);
    DO_LOCKDISPLAY;
  }

  vaStatus = vaapi_init_internal(this_gen, va_profile, width, height, softrender);

  if(!this->guarded_render) {
    DO_UNLOCKDISPLAY;
    pthread_mutex_unlock(&this->vaapi_lock);
  }

  if(last_sub_img_fmt)
    vaapi_ovl_associate(this_gen, frame_gen->format, this->has_overlay);

  return vaStatus;
}

static void vaapi_frame_proc_slice (vo_frame_t *vo_img, uint8_t **src)
{
  vo_img->proc_called = 1;
}

static void vaapi_frame_field (vo_frame_t *vo_img, int which_field)
{
}

static void vaapi_frame_dispose (vo_frame_t *vo_img) {
  vaapi_driver_t *this  = (vaapi_driver_t *) vo_img->driver;
  vaapi_frame_t  *frame = (vaapi_frame_t *) vo_img ;
  vaapi_accel_t  *accel = &frame->vaapi_accel_data;

  lprintf("vaapi_frame_dispose\n");

  av_free (frame->vo_frame.base[0]);
  av_free (frame->vo_frame.base[1]);
  av_free (frame->vo_frame.base[2]);

  if(this->guarded_render) {
    ff_vaapi_surface_t *va_surface = &va_render_surfaces[accel->index];
    va_surface->status = SURFACE_FREE;
  }

  free (frame);
}

static vo_frame_t *vaapi_alloc_frame (vo_driver_t *this_gen) {
  vaapi_driver_t  *this = (vaapi_driver_t *) this_gen;
  vaapi_frame_t   *frame;

  frame = (vaapi_frame_t *) calloc(1, sizeof(vaapi_frame_t));

  if (!frame)
    return NULL;

  this->frames[this->num_frame_buffers++] = frame;

  frame->vo_frame.base[0] = frame->vo_frame.base[1] = frame->vo_frame.base[2] = NULL;
  frame->width = frame->height = frame->format = frame->flags = 0;

  frame->vo_frame.accel_data = &frame->vaapi_accel_data;

  pthread_mutex_init (&frame->vo_frame.mutex, NULL);

  /*
   * supply required functions
   */
  frame->vo_frame.proc_duplicate_frame_data         = NULL;
  frame->vo_frame.proc_provide_standard_frame_data  = NULL;
  frame->vo_frame.proc_slice                        = vaapi_frame_proc_slice;
  frame->vo_frame.proc_frame                        = NULL;
  frame->vo_frame.field                             = vaapi_frame_field;
  frame->vo_frame.dispose                           = vaapi_frame_dispose;
  frame->vo_frame.driver                            = this_gen;

  frame->vaapi_accel_data.vo_frame                  = &frame->vo_frame;
  frame->vaapi_accel_data.vaapi_init                = &vaapi_init;
  frame->vaapi_accel_data.profile_from_imgfmt       = &profile_from_imgfmt;
  frame->vaapi_accel_data.get_context               = &get_context;

#if AVVIDEO > 1
  frame->vaapi_accel_data.avcodec_decode_video2     = &guarded_avcodec_decode_video2;
#else
  frame->vaapi_accel_data.avcodec_decode_video      = &guarded_avcodec_decode_video;
#endif

  frame->vaapi_accel_data.get_vaapi_surface         = &get_vaapi_surface;
  frame->vaapi_accel_data.render_vaapi_surface      = &render_vaapi_surface;
  frame->vaapi_accel_data.release_vaapi_surface     = &release_vaapi_surface;
  frame->vaapi_accel_data.guarded_render            = &guarded_render;

  lprintf("alloc frame\n");

  return (vo_frame_t *) frame;
}


/* Display OSD */
static int vaapi_ovl_associate(vo_driver_t *this_gen, int format, int bShow) {
  vaapi_driver_t      *this = (vaapi_driver_t *) this_gen;
  ff_vaapi_context_t  *va_context = this->va_context;
  VAStatus vaStatus;

  if(!va_context->valid_context)
    return 0;

  if(va_context->last_sub_image_fmt && !bShow) {
    if(va_context->va_subpic_id != VA_INVALID_ID) {
      if(va_context->last_sub_image_fmt == XINE_IMGFMT_VAAPI) {
        vaStatus = vaDeassociateSubpicture(va_context->va_display, va_context->va_subpic_id,
                                va_surface_ids, RENDER_SURFACES);
        vaapi_check_status(this_gen, vaStatus, "vaDeassociateSubpicture()");
      } else if(va_context->last_sub_image_fmt == XINE_IMGFMT_YV12 ||
                va_context->last_sub_image_fmt == XINE_IMGFMT_YUY2) {
        vaStatus = vaDeassociateSubpicture(va_context->va_display, va_context->va_subpic_id,
                                va_soft_surface_ids, SOFT_SURFACES);
        vaapi_check_status(this_gen, vaStatus, "vaDeassociateSubpicture()");
      }
    }
    va_context->last_sub_image_fmt = 0;
    return 1;
  }
  
  if(!va_context->last_sub_image_fmt && bShow) {
    unsigned int flags = 0;
    unsigned int output_width = va_context->width;
    unsigned int output_height = va_context->height;
    unsigned char *p_dest;
    uint32_t *p_src;
    void *p_base = NULL;

    VAStatus vaStatus;
    int i;

    vaapi_destroy_subpicture(this_gen);
    vaStatus = vaapi_create_subpicture(this_gen, this->overlay_bitmap_width, this->overlay_bitmap_height);
    if(!vaapi_check_status(this_gen, vaStatus, "vaapi_create_subpicture()"))
      return 0;

    vaStatus = vaMapBuffer(va_context->va_display, va_context->va_subpic_image.buf, &p_base);
    if(!vaapi_check_status(this_gen, vaStatus, "vaMapBuffer()"))
      return 0;

    p_src = this->overlay_bitmap;
    p_dest = p_base;
    for (i = 0; i < this->overlay_bitmap_height; i++) {
        xine_fast_memcpy(p_dest, p_src, this->overlay_bitmap_width * sizeof(uint32_t));
        p_dest += va_context->va_subpic_image.pitches[0];
        p_src += this->overlay_bitmap_width;
    }

    vaStatus = vaUnmapBuffer(va_context->va_display, va_context->va_subpic_image.buf);
    vaapi_check_status(this_gen, vaStatus, "vaUnmapBuffer()");

    lprintf( "vaapi_ovl_associate: overlay_width=%d overlay_height=%d unscaled %d va_subpic_id 0x%08x ovl_changed %d has_overlay %d bShow %d overlay_bitmap_width %d overlay_bitmap_height %d va_context->width %d va_context->height %d\n", 
           this->overlay_output_width, this->overlay_output_height, this->has_overlay, 
           va_context->va_subpic_id, this->ovl_changed, this->has_overlay, bShow,
           this->overlay_bitmap_width, this->overlay_bitmap_height,
           va_context->width, va_context->height);

    if(format == XINE_IMGFMT_VAAPI) {
      lprintf("vaapi_ovl_associate hw\n");
      vaStatus = vaAssociateSubpicture(va_context->va_display, va_context->va_subpic_id,
                              va_surface_ids, RENDER_SURFACES,
                              0, 0, va_context->va_subpic_image.width, va_context->va_subpic_image.height,
                              0, 0, output_width, output_height, flags);
    } else {
      lprintf("vaapi_ovl_associate sw\n");
      vaStatus = vaAssociateSubpicture(va_context->va_display, va_context->va_subpic_id,
                              va_soft_surface_ids, SOFT_SURFACES,
                              0, 0, va_context->va_subpic_image.width, va_context->va_subpic_image.height,
                              0, 0, va_soft_images[0].width, va_soft_images[0].height, flags);
    }

    if(vaapi_check_status(this_gen, vaStatus, "vaAssociateSubpicture()")) {
      va_context->last_sub_image_fmt = format;
      return 1;
    }
  }
  return 0;
}

static void vaapi_overlay_begin (vo_driver_t *this_gen,
			      vo_frame_t *frame_gen, int changed) {
  vaapi_driver_t      *this       = (vaapi_driver_t *) this_gen;
  ff_vaapi_context_t  *va_context = this->va_context;

  if ( !changed )
    return;

  this->has_overlay = 0;
  ++this->ovl_changed;

  /* Apply OSD layer. */
  if(va_context->valid_context) {
    lprintf("vaapi_overlay_begin chaned %d\n", changed);

    pthread_mutex_lock(&this->vaapi_lock);
    DO_LOCKDISPLAY;

    vaapi_ovl_associate(this_gen, frame_gen->format, this->has_overlay);

    DO_UNLOCKDISPLAY;
    pthread_mutex_unlock(&this->vaapi_lock);
  }
}

static void vaapi_overlay_blend (vo_driver_t *this_gen,
			      vo_frame_t *frame_gen, vo_overlay_t *overlay) {
  vaapi_driver_t  *this = (vaapi_driver_t *) this_gen;

  int i = this->ovl_changed;

  if (!i)
    return;

  if (--i >= XINE_VORAW_MAX_OVL)
    return;

  if (overlay->width <= 0 || overlay->height <= 0 || (!overlay->rle && (!overlay->argb_layer || !overlay->argb_layer->buffer)))
    return;

  if (overlay->rle)
    lprintf("overlay[%d] rle %s%s %dx%d@%d,%d hili rect %d,%d-%d,%d\n", i,
            overlay->unscaled ? " unscaled ": " scaled ",
            (overlay->rgb_clut > 0 || overlay->hili_rgb_clut > 0) ? " rgb ": " ycbcr ",
            overlay->width, overlay->height, overlay->x, overlay->y,
            overlay->hili_left, overlay->hili_top,
            overlay->hili_right, overlay->hili_bottom);
  if (overlay->argb_layer && overlay->argb_layer->buffer)
    lprintf("overlay[%d] argb %s %dx%d@%d,%d dirty rect %d,%d-%d,%d\n", i,
            overlay->unscaled ? " unscaled ": " scaled ",
            overlay->width, overlay->height, overlay->x, overlay->y,
            overlay->argb_layer->x1, overlay->argb_layer->y1,
            overlay->argb_layer->x2, overlay->argb_layer->y2);


  this->overlays[i] = overlay;

  ++this->ovl_changed;
}

static void vaapi_overlay_end (vo_driver_t *this_gen, vo_frame_t *frame_gen) {
  vaapi_driver_t      *this       = (vaapi_driver_t *) this_gen;
  vaapi_frame_t       *frame      = (vaapi_frame_t *) frame_gen;
  ff_vaapi_context_t  *va_context = this->va_context;

  int novls = this->ovl_changed;
  if (novls < 2) {
    this->ovl_changed = 0;
    return;
  }
  --novls;

  uint32_t output_width = frame->width;
  uint32_t output_height = frame->height;
  uint32_t unscaled_width = 0, unscaled_height = 0;
  vo_overlay_t *first_scaled = NULL, *first_unscaled = NULL;
  vaapi_rect_t dirty_rect, unscaled_dirty_rect;
  int has_rle = 0;

  int i;
  for (i = 0; i < novls; ++i) {
    vo_overlay_t *ovl = this->overlays[i];

    if (ovl->rle)
      has_rle = 1;

    if (ovl->unscaled) {
      if (first_unscaled) {
        if (ovl->x < unscaled_dirty_rect.x1)
          unscaled_dirty_rect.x1 = ovl->x;
        if (ovl->y < unscaled_dirty_rect.y1)
          unscaled_dirty_rect.y1 = ovl->y;
        if ((ovl->x + ovl->width) > unscaled_dirty_rect.x2)
          unscaled_dirty_rect.x2 = ovl->x + ovl->width;
        if ((ovl->y + ovl->height) > unscaled_dirty_rect.y2)
          unscaled_dirty_rect.y2 = ovl->y + ovl->height;
      } else {
        first_unscaled = ovl;
        unscaled_dirty_rect.x1 = ovl->x;
        unscaled_dirty_rect.y1 = ovl->y;
        unscaled_dirty_rect.x2 = ovl->x + ovl->width;
        unscaled_dirty_rect.y2 = ovl->y + ovl->height;
      }

      unscaled_width = unscaled_dirty_rect.x2;
      unscaled_height = unscaled_dirty_rect.y2;
    } else {
      if (first_scaled) {
        if (ovl->x < dirty_rect.x1)
          dirty_rect.x1 = ovl->x;
        if (ovl->y < dirty_rect.y1)
          dirty_rect.y1 = ovl->y;
        if ((ovl->x + ovl->width) > dirty_rect.x2)
          dirty_rect.x2 = ovl->x + ovl->width;
        if ((ovl->y + ovl->height) > dirty_rect.y2)
          dirty_rect.y2 = ovl->y + ovl->height;
      } else {
        first_scaled = ovl;
        dirty_rect.x1 = ovl->x;
        dirty_rect.y1 = ovl->y;
        dirty_rect.x2 = ovl->x + ovl->width;
        dirty_rect.y2 = ovl->y + ovl->height;
      }

      if (dirty_rect.x2 > output_width)
        output_width = dirty_rect.x2;
      if (dirty_rect.y2 > output_height)
        output_height = dirty_rect.y2;

    }
  }

  int need_init = 0;

  lprintf("dirty_rect.x0 %d dirty_rect.y0 %d dirty_rect.x2 %d dirty_rect.y2 %d output_width %d output_height %d\n",
      dirty_rect.x0, dirty_rect.y0, dirty_rect.x2, dirty_rect.y2, output_width, output_height);

  if (first_scaled) {
    vaapi_rect_t dest;
    dest.x1 = first_scaled->x;
    dest.y1 = first_scaled->y;
    dest.x2 = first_scaled->x + first_scaled->width;
    dest.y2 = first_scaled->y + first_scaled->height;
    if (!RECT_IS_EQ(dest, dirty_rect))
      need_init = 1;
  }

  int need_unscaled_init = (first_unscaled &&
                                  (first_unscaled->x != unscaled_dirty_rect.x1 ||
                                   first_unscaled->y != unscaled_dirty_rect.y1 ||
                                   (first_unscaled->x + first_unscaled->width) != unscaled_dirty_rect.x2 ||
                                   (first_unscaled->y + first_unscaled->height) != unscaled_dirty_rect.y2));

  if (first_scaled) {
    this->overlay_output_width = output_width;
    this->overlay_output_height = output_height;

    need_init = 1;

    this->overlay_dirty_rect = dirty_rect;
  }

  if (first_unscaled) {
    this->overlay_unscaled_width = unscaled_width;
    this->overlay_unscaled_height = unscaled_height;

    need_unscaled_init = 1;
    this->overlay_unscaled_dirty_rect = unscaled_dirty_rect;
  }

  if (has_rle || need_init || need_unscaled_init) {
    lprintf("has_rle %d need_init %d need_unscaled_init %d unscaled_width %d unscaled_height %d output_width %d output_height %d\n", 
        has_rle, need_init, need_unscaled_init, unscaled_width, unscaled_height, output_width, output_height);
    if (need_init) {
      this->overlay_bitmap_width = output_width;
      this->overlay_bitmap_height = output_height;
    }
    if (need_unscaled_init) {

      if(this->vdr_osd_width) 
        this->overlay_bitmap_width =  (this->vdr_osd_width >  this->sc.gui_width) ? this->vdr_osd_width : this->sc.gui_width;
      else
        this->overlay_bitmap_width =  (unscaled_width >  this->sc.gui_width) ? unscaled_width : this->sc.gui_width;

      if(this->vdr_osd_height) 
        this->overlay_bitmap_height = (this->vdr_osd_height > this->sc.gui_height) ? this->vdr_osd_height : this->sc.gui_height;
      else
        this->overlay_bitmap_height = (unscaled_height > this->sc.gui_height) ? unscaled_height : this->sc.gui_height;

    } else if (need_init) {

      if(this->vdr_osd_width) 
        this->overlay_bitmap_width =  (this->vdr_osd_width >  this->sc.gui_width) ? this->vdr_osd_width : this->sc.gui_width;
      else
        this->overlay_bitmap_width =  (output_width >  this->sc.gui_width) ? output_width : this->sc.gui_width;

      if(this->vdr_osd_height) 
        this->overlay_bitmap_height = (this->vdr_osd_height > this->sc.gui_height) ? this->vdr_osd_height : this->sc.gui_height;
      else
        this->overlay_bitmap_height = (output_height > this->sc.gui_height) ? output_height : this->sc.gui_height;

    }
  }

  if ((this->overlay_bitmap_width * this->overlay_bitmap_height) > this->overlay_bitmap_size) {
    this->overlay_bitmap_size = this->overlay_bitmap_width * this->overlay_bitmap_height;
    free(this->overlay_bitmap);
    this->overlay_bitmap = calloc( this->overlay_bitmap_size, sizeof(uint32_t));
  } else {
    memset(this->overlay_bitmap, 0x0, this->overlay_bitmap_size * sizeof(uint32_t));
  }

  for (i = 0; i < novls; ++i) {
    vo_overlay_t *ovl = this->overlays[i];
    uint32_t *bitmap = NULL;

    if (ovl->rle) {
      if(ovl->width<=0 || ovl->height<=0)
        continue;

      if (!ovl->rgb_clut || !ovl->hili_rgb_clut)
        _x_overlay_clut_yuv2rgb (ovl, this->color_matrix);

      bitmap = malloc(ovl->width * ovl->height * sizeof(uint32_t));

      _x_overlay_to_argb32(ovl, bitmap, ovl->width, "BGRA");

      lprintf("width %d height %d pos %d %d\n", ovl->width, ovl->height, pos, ovl->width * ovl->height);
    } else {
      pthread_mutex_lock(&ovl->argb_layer->mutex);
      bitmap = ovl->argb_layer->buffer;
    }

    /* Blit overlay to destination */
    uint32_t pitch = ovl->width * sizeof(uint32_t);
    uint32_t *copy_dst = this->overlay_bitmap;
    uint32_t *copy_src = NULL;
    uint32_t height = 0;

    copy_src = bitmap;
  
    copy_dst += ovl->y * this->overlay_bitmap_width;

    lprintf("overlay_bitmap_width %d overlay_bitmap_height %d  ovl->x %d ovl->y %d ovl->width %d ovl->height %d width %d height %d\n",
      this->overlay_bitmap_width, this->overlay_bitmap_height, ovl->x, ovl->y, ovl->width, ovl->height, this->overlay_bitmap_width, this->overlay_bitmap_height);

    for(height = 0; height < ovl->height; height++) {
      if((height + ovl->y) >= this->overlay_bitmap_height)
        break;

      xine_fast_memcpy(copy_dst + ovl->x, copy_src, pitch);
      copy_dst += this->overlay_bitmap_width;
      copy_src += ovl->width;
    }

    if (ovl->rle) {
      if(bitmap) {
        free(bitmap);
        bitmap = NULL;
      }
    }

    if (!ovl->rle)
      pthread_mutex_unlock(&ovl->argb_layer->mutex);

  }

  this->ovl_changed = 0;
  this->has_overlay = (first_scaled != NULL) | (first_unscaled != NULL);

  lprintf("this->has_overlay %d\n", this->has_overlay);
  /* Apply OSD layer. */
  if(va_context->valid_context) {
    pthread_mutex_lock(&this->vaapi_lock);
    DO_LOCKDISPLAY;

    vaapi_ovl_associate(this_gen, frame_gen->format, this->has_overlay);

    DO_UNLOCKDISPLAY;
    pthread_mutex_unlock(&this->vaapi_lock);
  }
}

static void vaapi_resize_glx_window (vo_driver_t *this_gen, int width, int height) {
  vaapi_driver_t  *this = (vaapi_driver_t *) this_gen;

  if(this->valid_opengl_context) {
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(FOVY, ASPECT, Z_NEAR, Z_FAR);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(-0.5f, -0.5f, -Z_CAMERA);
    glScalef(1.0f / (GLfloat)width, 
             -1.0f / (GLfloat)height,
             1.0f / (GLfloat)width);
    glTranslatef(0.0f, -1.0f * (GLfloat)height, 0.0f);
  }
}

static int vaapi_redraw_needed (vo_driver_t *this_gen) {
  vaapi_driver_t      *this       = (vaapi_driver_t *) this_gen;
  int                 ret = 0;

  _x_vo_scale_compute_ideal_size( &this->sc );

  if ( _x_vo_scale_redraw_needed( &this->sc ) ) {
    _x_vo_scale_compute_output_size( &this->sc );

    XMoveResizeWindow(this->display, this->window, 
                      0, 0, this->sc.gui_width, this->sc.gui_height);

    vaapi_resize_glx_window(this_gen, this->sc.gui_width, this->sc.gui_height);

    ret = 1;
  }

  if (this->color_matrix == 0)
    ret = 1;

  return ret;
}

static void vaapi_provide_standard_frame_data (vo_frame_t *orig, xine_current_frame_data_t *data)
{
  vaapi_driver_t      *driver     = (vaapi_driver_t *) orig->driver;
  ff_vaapi_context_t  *va_context = driver->va_context;

  vaapi_accel_t       *accel      = (vaapi_accel_t *) orig->accel_data;
  vo_frame_t          *this       = accel->vo_frame;
  ff_vaapi_surface_t  *va_surface = &va_render_surfaces[accel->index];

  uint32_t  pitches[3];
  uint8_t   *base[3];

  if(driver == NULL) {
    return;
  }

  if (this->format != XINE_IMGFMT_VAAPI) {
    xprintf(driver->xine, XINE_VERBOSITY_LOG, LOG_MODULE "vaapi_provide_standard_frame_data: unexpected frame format 0x%08x!\n", this->format);
    return;
  }

  if( !accel || va_surface->va_surface_id == VA_INVALID_SURFACE )
    return;

  lprintf("vaapi_provide_standard_frame_data %s 0x%08x width %d height %d\n", 
      (this->format == XINE_IMGFMT_VAAPI) ? "XINE_IMGFMT_VAAPI" : ((this->format == XINE_IMGFMT_YV12) ? "XINE_IMGFMT_YV12" : "XINE_IMGFMT_YUY2"),
      va_surface->va_surface_id, this->width, this->height);

  pthread_mutex_lock(&driver->vaapi_lock);
  DO_LOCKDISPLAY;

  int width = va_context->width;
  int height = va_context->height;

  data->format = XINE_IMGFMT_YV12;
  data->img_size = width * height
                   + ((width + 1) / 2) * ((height + 1) / 2)
                   + ((width + 1) / 2) * ((height + 1) / 2);
  if (data->img) {
    pitches[0] = width;
    pitches[2] = width / 2;
    pitches[1] = width / 2;
    base[0] = data->img;
    base[2] = data->img + width * height;
    base[1] = data->img + width * height + width * this->height / 4;

    VAImage   va_image;
    VAStatus  vaStatus;
    void      *p_base;

    vaStatus = vaSyncSurface(va_context->va_display, va_surface->va_surface_id);
    vaapi_check_status(va_context->driver, vaStatus, "vaSyncSurface()");

    VASurfaceStatus surf_status = 0;

    if(driver->query_va_status) {
      vaStatus = vaQuerySurfaceStatus(va_context->va_display, va_surface->va_surface_id, &surf_status);
      vaapi_check_status(va_context->driver, vaStatus, "vaQuerySurfaceStatus()");
    } else {
      surf_status = VASurfaceReady;
    }

    if(surf_status != VASurfaceReady)
      goto error;

    vaStatus = vaapi_create_image(va_context->driver, va_surface->va_surface_id, &va_image, width, height, 0);
    if(!vaapi_check_status(va_context->driver, vaStatus, "vaapi_create_image()"))
      goto error;

    lprintf("vaapi_provide_standard_frame_data accel->va_surface_id 0x%08x va_image.image_id 0x%08x va_context->width %d va_context->height %d va_image.width %d va_image.height %d width %d height %d size1 %d size2 %d %d %d %d status %d num_planes %d\n", 
       va_surface->va_surface_id, va_image.image_id, va_context->width, va_context->height, va_image.width, va_image.height, width, height, va_image.data_size, data->img_size, 
       va_image.pitches[0], va_image.pitches[1], va_image.pitches[2], surf_status, va_image.num_planes);

    if(va_image.image_id == VA_INVALID_ID)
      goto error;

    if(!va_context->is_bound) {
      vaStatus = vaGetImage(va_context->va_display, va_surface->va_surface_id, 0, 0,
                          va_image.width, va_image.height, va_image.image_id);
    } else {
      vaStatus = VA_STATUS_SUCCESS;
    }

    if(vaapi_check_status(va_context->driver, vaStatus, "vaGetImage()")) {
      vaStatus = vaMapBuffer( va_context->va_display, va_image.buf, &p_base ) ;
      if(vaapi_check_status(va_context->driver, vaStatus, "vaMapBuffer()")) {

        /*
        uint8_t *src[3] = { NULL, };
        src[0] = (uint8_t *)p_base + va_image.offsets[0];
        src[1] = (uint8_t *)p_base + va_image.offsets[1];
        src[2] = (uint8_t *)p_base + va_image.offsets[2];
        */

        if( va_image.format.fourcc == VA_FOURCC( 'Y', 'V', '1', '2' ) ||
            va_image.format.fourcc == VA_FOURCC( 'I', '4', '2', '0' ) ) {
          lprintf("VAAPI YV12 image\n");

          yv12_to_yv12(
            (uint8_t*)p_base + va_image.offsets[0], va_image.pitches[0],
            base[0], pitches[0],
            (uint8_t*)p_base + va_image.offsets[1], va_image.pitches[1],
            base[1], pitches[1],
            (uint8_t*)p_base + va_image.offsets[2], va_image.pitches[2],
            base[2], pitches[2],
            va_image.width, va_image.height);

        } else if( va_image.format.fourcc == VA_FOURCC( 'N', 'V', '1', '2' ) ) {
          lprintf("VAAPI NV12 image\n");

          lprintf("va_image.offsets[0] %d va_image.offsets[1] %d va_image.offsets[2] %d size %d size %d size %d width %d height %d width %d height %d\n",
              va_image.offsets[0], va_image.offsets[1], va_image.offsets[2], va_image.data_size, va_image.width * va_image.height,
              data->img_size, width, height, va_image.width, va_image.height);

          base[0] = data->img;
          base[1] = data->img + width * height;
          base[2] = data->img + width * height + width * height / 4;

          nv12_to_yv12((uint8_t *)p_base + va_image.offsets[0], va_image.pitches[0],
                       (uint8_t *)p_base + va_image.offsets[1], va_image.pitches[1],
                       base[0], pitches[0],
                       base[1], pitches[1],
                       base[2], pitches[2],
                       va_image.width,  va_image.height, 
                       width, height, 
                       va_image.data_size);

        } else {
          printf("vaapi_provide_standard_frame_data unsupported image format\n");
        }

        vaStatus = vaUnmapBuffer(va_context->va_display, va_image.buf);
        vaapi_check_status(va_context->driver, vaStatus, "vaUnmapBuffer()");
        vaapi_destroy_image(va_context->driver, &va_image);
      }
    }
  }

error:
  DO_UNLOCKDISPLAY;
  pthread_mutex_unlock(&driver->vaapi_lock);
}

static void vaapi_duplicate_frame_data (vo_frame_t *this_gen, vo_frame_t *original)
{
  vaapi_driver_t      *driver     = (vaapi_driver_t *) original->driver;
  ff_vaapi_context_t  *va_context = driver->va_context;

  vaapi_frame_t *this = (vaapi_frame_t *)this_gen;
  vaapi_frame_t *orig = (vaapi_frame_t *)original;

  vaapi_accel_t      *accel_this = &this->vaapi_accel_data;
  vaapi_accel_t      *accel_orig = &orig->vaapi_accel_data;

  ff_vaapi_surface_t *va_surface_this = &va_render_surfaces[accel_this->index];
  ff_vaapi_surface_t *va_surface_orig = &va_render_surfaces[accel_orig->index];

  lprintf("vaapi_duplicate_frame_data %s %s 0x%08x 0x%08x\n", 
      (this_gen->format == XINE_IMGFMT_VAAPI) ? "XINE_IMGFMT_VAAPI" : ((this_gen->format == XINE_IMGFMT_YV12) ? "XINE_IMGFMT_YV12" : "XINE_IMGFMT_YUY2"),
      (original->format == XINE_IMGFMT_VAAPI) ? "XINE_IMGFMT_VAAPI" : ((original->format == XINE_IMGFMT_YV12) ? "XINE_IMGFMT_YV12" : "XINE_IMGFMT_YUY2"),
      va_surface_this->va_surface_id, va_surface_orig->va_surface_id);

  if (orig->vo_frame.format != XINE_IMGFMT_VAAPI) {
    xprintf(driver->xine, XINE_VERBOSITY_LOG, LOG_MODULE "vaapi_duplicate_frame_data: unexpected frame format 0x%08x!\n", orig->format);
    return;
  }

  if (this->vo_frame.format != XINE_IMGFMT_VAAPI) {
    xprintf(driver->xine, XINE_VERBOSITY_LOG, LOG_MODULE "vaapi_duplicate_frame_data: unexpected frame format 0x%08x!\n", this->format);
    return;
  }

  pthread_mutex_lock(&driver->vaapi_lock);
  DO_LOCKDISPLAY;

  VAImage   va_image_orig;
  VAImage   va_image_this;
  VAStatus  vaStatus;
  void      *p_base_orig = NULL;
  void      *p_base_this = NULL;

  vaStatus = vaSyncSurface(va_context->va_display, va_surface_orig->va_surface_id);
  vaapi_check_status(va_context->driver, vaStatus, "vaSyncSurface()");

  int this_width = va_context->width;
  int this_height = va_context->height;
  int orig_width = va_context->width;
  int orig_height = va_context->height;

  vaStatus = vaapi_create_image(va_context->driver, va_surface_orig->va_surface_id, &va_image_orig, orig_width, orig_height, 0);
  if(!vaapi_check_status(va_context->driver, vaStatus, "vaapi_create_image()")) {
    va_image_orig.image_id = VA_INVALID_ID;
    goto error;
  }

  vaStatus = vaapi_create_image(va_context->driver, va_surface_this->va_surface_id, &va_image_this, this_width, this_height, 0);
  if(!vaapi_check_status(va_context->driver, vaStatus, "vaapi_create_image()")) {
    va_image_this.image_id = VA_INVALID_ID;
    goto error;
  }

  if(va_image_orig.image_id == VA_INVALID_ID || va_image_this.image_id == VA_INVALID_ID) {
    printf("vaapi_duplicate_frame_data invalid image\n");
    goto error;
  }

  lprintf("vaapi_duplicate_frame_data va_image_orig.image_id 0x%08x va_image_orig.width %d va_image_orig.height %d width %d height %d size %d %d %d %d\n", 
       va_image_orig.image_id, va_image_orig.width, va_image_orig.height, this->width, this->height, va_image_orig.data_size, 
       va_image_orig.pitches[0], va_image_orig.pitches[1], va_image_orig.pitches[2]);

  if(!va_context->is_bound) {
    vaStatus = vaGetImage(va_context->va_display, va_surface_orig->va_surface_id, 0, 0,
                          va_image_orig.width, va_image_orig.height, va_image_orig.image_id);
  } else {
    vaStatus = VA_STATUS_SUCCESS;
  }

  if(vaapi_check_status(va_context->driver, vaStatus, "vaGetImage()")) {
    
    if(!va_context->is_bound) {
      vaStatus = vaPutImage(va_context->va_display, va_surface_this->va_surface_id, va_image_orig.image_id,
                            0, 0, va_image_orig.width, va_image_orig.height,
                            0, 0, va_image_this.width, va_image_this.height);
      vaapi_check_status(va_context->driver, vaStatus, "vaPutImage()");
    } else {
      vaStatus = vaMapBuffer( va_context->va_display, va_image_orig.buf, &p_base_orig ) ;
      if(!vaapi_check_status(va_context->driver, vaStatus, "vaMapBuffer()"))
        goto error;

      vaStatus = vaMapBuffer( va_context->va_display, va_image_this.buf, &p_base_this ) ;
      if(!vaapi_check_status(va_context->driver, vaStatus, "vaMapBuffer()"))
        goto error;

      int size = (va_image_orig.data_size > va_image_this.data_size) ? va_image_this.data_size : va_image_orig.data_size;
      xine_fast_memcpy((uint8_t *) p_base_this, (uint8_t *) p_base_orig, size);

    }
  }

error:
  if(p_base_orig) {
    vaStatus = vaUnmapBuffer(va_context->va_display, va_image_orig.buf);
    vaapi_check_status(va_context->driver, vaStatus, "vaUnmapBuffer()");
  }
  if(p_base_this) {
    vaStatus = vaUnmapBuffer(va_context->va_display, va_image_this.buf);
    vaapi_check_status(va_context->driver, vaStatus, "vaUnmapBuffer()");
  }

  vaapi_destroy_image(va_context->driver, &va_image_orig);
  vaapi_destroy_image(va_context->driver, &va_image_this);

  DO_UNLOCKDISPLAY;
  pthread_mutex_unlock(&driver->vaapi_lock);
}

static void vaapi_update_frame_format (vo_driver_t *this_gen,
				    vo_frame_t *frame_gen,
				    uint32_t width, uint32_t height,
				    double ratio, int format, int flags) {
  vaapi_driver_t      *this       = (vaapi_driver_t *) this_gen;
  vaapi_frame_t       *frame      = (vaapi_frame_t*)frame_gen;
  vaapi_accel_t       *accel      = &frame->vaapi_accel_data;

  lprintf("vaapi_update_frame_format\n");

  pthread_mutex_lock(&this->vaapi_lock);
  DO_LOCKDISPLAY;

  lprintf("vaapi_update_frame_format %s %s width %d height %d\n", 
        (frame->format == XINE_IMGFMT_VAAPI) ? "XINE_IMGFMT_VAAPI" : ((frame->format == XINE_IMGFMT_YV12) ? "XINE_IMGFMT_YV12" : "XINE_IMGFMT_YUY2") ,
        (format == XINE_IMGFMT_VAAPI) ? "XINE_IMGFMT_VAAPI" : ((format == XINE_IMGFMT_YV12) ? "XINE_IMGFMT_YV12" : "XINE_IMGFMT_YUY2") ,
        width, height);

  frame->vo_frame.width = width;
  frame->vo_frame.height = height;

  if ((frame->width != width)
      || (frame->height != height)
      || (frame->format != format)) {

    // (re-) allocate render space
    av_freep (&frame->vo_frame.base[0]);
    av_freep (&frame->vo_frame.base[1]);
    av_freep (&frame->vo_frame.base[2]);

    /* set init_vaapi on frame formats XINE_IMGFMT_YV12/XINE_IMGFMT_YUY2 only.
     * for XINE_IMGFMT_VAAPI the init was already done.
     */
    if (format == XINE_IMGFMT_YV12) {
      frame->vo_frame.pitches[0] = 8*((width + 7) / 8);
      frame->vo_frame.pitches[1] = 8*((width + 15) / 16);
      frame->vo_frame.pitches[2] = 8*((width + 15) / 16);
      frame->vo_frame.base[0] = av_mallocz (frame->vo_frame.pitches[0] * height + FF_INPUT_BUFFER_PADDING_SIZE);
      frame->vo_frame.base[1] = av_mallocz (frame->vo_frame.pitches[1] * ((height+1)/2) + FF_INPUT_BUFFER_PADDING_SIZE);
      frame->vo_frame.base[2] = av_mallocz (frame->vo_frame.pitches[2] * ((height+1)/2) + FF_INPUT_BUFFER_PADDING_SIZE);
      frame->vo_frame.proc_duplicate_frame_data = NULL;
      frame->vo_frame.proc_provide_standard_frame_data = NULL;
      lprintf("XINE_IMGFMT_YV12 width %d height %d\n", width, height);
    } else if (format == XINE_IMGFMT_YUY2){
      frame->vo_frame.pitches[0] = 8*((width + 3) / 4);
      frame->vo_frame.base[0] = av_mallocz (frame->vo_frame.pitches[0] * height + FF_INPUT_BUFFER_PADDING_SIZE);
      frame->vo_frame.proc_duplicate_frame_data = NULL;
      frame->vo_frame.proc_provide_standard_frame_data = NULL;
      lprintf("XINE_IMGFMT_YUY2 width %d height %d\n", width, height);
    } else if (format == XINE_IMGFMT_VAAPI) {
      frame->vo_frame.proc_duplicate_frame_data = vaapi_duplicate_frame_data;
      frame->vo_frame.proc_provide_standard_frame_data = vaapi_provide_standard_frame_data;
      lprintf("XINE_IMGFMT_VAAPI width %d height %d\n", width, height);
    }

    frame->width  = width;
    frame->height = height;
    frame->format = format;
    frame->flags  = flags;
    vaapi_frame_field ((vo_frame_t *)frame, flags);
  }

  if(this->guarded_render) {
    ff_vaapi_surface_t *va_surface = &va_render_surfaces[accel->index];

    if(va_surface->status == SURFACE_RENDER_RELEASE) {
      va_surface->status = SURFACE_FREE;
#ifdef DEBUG_SURFACE
      printf("release_surface vaapi_update_frame_format 0x%08x\n", va_surface->va_surface_id);
#endif
    } else if(va_surface->status == SURFACE_RENDER) {
      va_surface->status = SURFACE_RELEASE;
#ifdef DEBUG_SURFACE
      printf("release_surface vaapi_update_frame_format 0x%08x\n", va_surface->va_surface_id);
#endif
    }
  }

  DO_UNLOCKDISPLAY;
  pthread_mutex_unlock(&this->vaapi_lock);

  frame->ratio  = ratio;
  frame->vo_frame.future_frame = NULL;
}

static inline uint8_t clip_uint8_vlc( int32_t a )
{
  if( a&(~255) ) return (-a)>>31;
  else           return a;
}


static void nv12_to_yv12(const uint8_t *y_src,  int y_src_pitch, 
                         const uint8_t *uv_src, int uv_src_pitch, 
                         uint8_t *y_dst, int y_dst_pitch,
                         uint8_t *u_dst, int u_dst_pitch,
                         uint8_t *v_dst, int v_dst_pitch,
                         int src_width, int src_height, 
                         int dst_width, int dst_height,
                         int src_data_size) {

  int y_src_size  = src_height * y_src_pitch;
  int y, x;

  int uv_src_size = src_height * uv_src_pitch / 2;
  if((y_src_size + uv_src_size) != (src_data_size))
    printf("nv12_to_yv12 strange %d\n", (y_src_size + uv_src_size) - (src_data_size));

  int height  = (src_height > dst_height) ? dst_height : src_height;
  int width   = (src_width > dst_width) ? dst_width : src_width;

  for(y = 0; y < height; y++) {
    xine_fast_memcpy(y_dst, y_src, width);
    y_src += y_src_pitch;
    y_dst += y_dst_pitch;
  }

  for(y = 0; y < height; y++) {
    const uint8_t *uv_src_tmp = uv_src;
    for(x = 0; x < u_dst_pitch; x++) {
      if(((y * uv_src_pitch) + x) < uv_src_size) {
        *(u_dst + x) = *(uv_src_tmp    );
        *(v_dst + x) = *(uv_src_tmp + 1);
      }
      uv_src_tmp += 2;
    }
    uv_src += uv_src_pitch;
    u_dst += u_dst_pitch;
    v_dst += v_dst_pitch;
  }
}

static void yv12_to_nv12(const uint8_t *y_src, int y_src_pitch, 
                         const uint8_t *u_src, int u_src_pitch, 
                         const uint8_t *v_src, int v_src_pitch,
                         uint8_t *y_dst,  int y_dst_pitch,
                         uint8_t *uv_dst, int uv_dst_pitch,
                         int src_width, int src_height, 
                         int dst_width, int dst_height,
                         int dst_data_size) {

  int y_dst_size  = dst_height * y_dst_pitch;
  int y, x;

  lprintf("yv12_to_nv12 converter\n");

  int uv_dst_size = dst_height * uv_dst_pitch / 2;
  if((y_dst_size + uv_dst_size) != (dst_data_size))
    printf("yv12_to_nv12 strange %d\n", (y_dst_size + uv_dst_size) - (dst_data_size));

  int height  = (src_height > dst_height) ? dst_height : src_height;
  int width   = (src_width > dst_width) ? dst_width : src_width;

  for(y = 0; y < height; y++) {
    xine_fast_memcpy(y_dst, y_src, width);
    y_src += y_src_pitch;
    y_dst += y_dst_pitch;
  }

  for(y = 0; y < height; y++) {
    uint8_t *uv_dst_tmp = uv_dst;
    for(x = 0; x < u_src_pitch; x++) {
      if(((y * uv_dst_pitch) + x) < uv_dst_size) {
        *(uv_dst_tmp    ) = *(u_src + x);
        *(uv_dst_tmp + 1) = *(v_src + x);
      }
      uv_dst_tmp += 2;
    }
    uv_dst += uv_dst_pitch;
    u_src += u_src_pitch;
    v_src += v_src_pitch;
  }
}

static void yuy2_to_nv12(const uint8_t *src_yuy2_map, int yuy2_pitch, 
                         uint8_t *y_dst,  int y_dst_pitch,
                         uint8_t *uv_dst, int uv_dst_pitch,
                         int src_width, int src_height, 
                         int dst_width, int dst_height,
                         int dst_data_size) {

  int height  = (src_height > dst_height) ? dst_height : src_height;
  int width   = (src_width > dst_width) ? dst_width : src_width;

  int y, x;
  /*int uv_dst_size = dst_height * uv_dst_pitch / 2;*/

  const uint8_t *yuy2_map = src_yuy2_map;
  for(y = 0; y < height; y++) {
    uint8_t *y_dst_tmp = y_dst;
    const uint8_t *yuy2_src_tmp = yuy2_map;
    for(x = 0; x < width / 2; x++) {
      *(y_dst_tmp++   ) = *(yuy2_src_tmp++);
      yuy2_src_tmp++;
      *(y_dst_tmp++   ) = *(yuy2_src_tmp++);
      yuy2_src_tmp++;
    }
    y_dst += y_dst_pitch;
    yuy2_map += yuy2_pitch;
  }

  yuy2_map = src_yuy2_map;
  uint8_t *uv_dst_tmp = uv_dst;
  for(y = 0; y < height; y++) {
    for(x = 0; x < width; x++) {
      *(uv_dst_tmp + (height*width/4) ) = *(yuy2_map + (height*width/2));
      *(uv_dst_tmp + (height*width/4) + 2 ) = *(yuy2_map + (height*width/2) + 2);
    }
    uv_dst += uv_dst_pitch / 2;
    yuy2_map += yuy2_pitch;
  }

}


static VAStatus vaapi_software_render_frame(vo_driver_t *this_gen, vo_frame_t *frame_gen, 
                                             VAImage *va_image, VASurfaceID va_surface_id) {
  vaapi_driver_t     *this            = (vaapi_driver_t *) this_gen;
  vaapi_frame_t      *frame           = (vaapi_frame_t *) frame_gen;
  ff_vaapi_context_t *va_context      = this->va_context;
  void               *p_base          = NULL;
  VAStatus           vaStatus; 

  if(va_image == NULL || va_image->image_id == VA_INVALID_ID ||
     va_surface_id == VA_INVALID_SURFACE || !va_context->valid_context)
    return VA_STATUS_ERROR_UNKNOWN;

  lprintf("vaapi_software_render_frame : va_surface_id 0x%08x va_image.image_id 0x%08x width %d height %d f_width %d f_height %d sw_width %d sw_height %d\n", 
      va_surface_id, va_image->image_id, va_image->width, va_image->height, frame->width, frame->height,
      va_context->sw_width, va_context->sw_height);

  if(frame->width != va_image->width || frame->height != va_image->height)
    return VA_STATUS_SUCCESS;

  vaStatus = vaMapBuffer( va_context->va_display, va_image->buf, &p_base ) ;
  if(!vaapi_check_status(va_context->driver, vaStatus, "vaMapBuffer()"))
    return vaStatus;


  uint8_t *dst[3] = { NULL, };
  uint32_t  pitches[3];

  if(this->swap_uv_planes) {
    dst[0] = (uint8_t *)p_base + va_image->offsets[0]; pitches[0] = va_image->pitches[0];
    dst[1] = (uint8_t *)p_base + va_image->offsets[1]; pitches[1] = va_image->pitches[1];
    dst[2] = (uint8_t *)p_base + va_image->offsets[2]; pitches[2] = va_image->pitches[2];
  } else {
    dst[0] = (uint8_t *)p_base + va_image->offsets[0]; pitches[0] = va_image->pitches[0];
    dst[1] = (uint8_t *)p_base + va_image->offsets[2]; pitches[1] = va_image->pitches[1];
    dst[2] = (uint8_t *)p_base + va_image->offsets[1]; pitches[2] = va_image->pitches[2];
  }

  /* Copy xine frames into VAAPI images */
  if(frame->format == XINE_IMGFMT_YV12) {

    if (va_image->format.fourcc == VA_FOURCC( 'Y', 'V', '1', '2' ) ||
        va_image->format.fourcc == VA_FOURCC( 'I', '4', '2', '0' ) ) {
      lprintf("vaapi_software_render_frame yv12 -> yv12 convert\n");

      yv12_to_yv12(
              /* Y */
              frame_gen->base[0], frame_gen->pitches[0],
              dst[0], pitches[0],
              /* U */
              frame_gen->base[1], frame_gen->pitches[1],
              dst[1], pitches[1],
              /* V */
              frame_gen->base[2], frame_gen->pitches[2],
              dst[2], pitches[2],
              /* width x height */
              frame_gen->width, frame_gen->height);

    } else if (va_image->format.fourcc == VA_FOURCC( 'N', 'V', '1', '2' )) {
      lprintf("vaapi_software_render_frame yv12 -> nv12 convert\n");

      yv12_to_nv12(frame_gen->base[0], frame_gen->pitches[0],
                   frame_gen->base[1], frame_gen->pitches[1],
                   frame_gen->base[2], frame_gen->pitches[2],
                   (uint8_t *)p_base + va_image->offsets[0], va_image->pitches[0],
                   (uint8_t *)p_base + va_image->offsets[1], va_image->pitches[1],
                   frame_gen->width, frame_gen->height, 
                   va_image->width,  va_image->height, 
                   va_image->data_size);

    }
  } else if (frame->format == XINE_IMGFMT_YUY2) {

    if (va_image->format.fourcc == VA_FOURCC( 'Y', 'V', '1', '2' ) ||
        va_image->format.fourcc == VA_FOURCC( 'I', '4', '2', '0' ) ) {
      lprintf("vaapi_software_render_frame yuy2 -> yv12 convert\n");

      yuy2_to_yv12(frame_gen->base[0], frame_gen->pitches[0],
                  dst[0], pitches[0],
                  dst[1], pitches[1],
                  dst[2], pitches[2],
                  frame_gen->width, frame_gen->height);

    } else if (va_image->format.fourcc == VA_FOURCC( 'N', 'V', '1', '2' )) {
      lprintf("vaapi_software_render_frame yuy2 -> nv12 convert\n");

      yuy2_to_nv12(frame_gen->base[0], frame_gen->pitches[0],
                   (uint8_t *)p_base + va_image->offsets[0], va_image->pitches[0],
                   (uint8_t *)p_base + va_image->offsets[1], va_image->pitches[1],
                   frame_gen->width, frame_gen->height, 
                   va_image->width,  va_image->height, 
                   va_image->data_size);
    }

  }

  vaStatus = vaUnmapBuffer(va_context->va_display, va_image->buf);
  if(!vaapi_check_status(this_gen, vaStatus, "vaUnmapBuffer()"))
    return vaStatus;

  if(!va_context->is_bound) {
    vaStatus = vaPutImage(va_context->va_display, va_surface_id, va_image->image_id,
                        0, 0, va_image->width, va_image->height,
                        0, 0, va_image->width, va_image->height);
    if(!vaapi_check_status(va_context->driver, vaStatus, "vaPutImage()"))
      return vaStatus;
  }

  return VA_STATUS_SUCCESS;
}

static VAStatus vaapi_hardware_render_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen, 
                                             VASurfaceID va_surface_id) {
  vaapi_driver_t     *this            = (vaapi_driver_t *) this_gen;
  vaapi_frame_t      *frame           = (vaapi_frame_t *) frame_gen;
  ff_vaapi_context_t *va_context      = this->va_context;
  VAStatus           vaStatus         = VA_STATUS_ERROR_UNKNOWN; 
  int                i                = 0;
  int                interlaced_frame = !frame->vo_frame.progressive_frame;
  int                top_field_first  = frame->vo_frame.top_field_first;
  int                width, height;

  if(frame->format == XINE_IMGFMT_VAAPI) {
    width  = va_context->width;
    height = va_context->height;
  } else {
    width  = (frame->width > va_context->sw_width) ? va_context->sw_width : frame->width;
    height = (frame->height > va_context->sw_height) ? va_context->sw_height : frame->height;
  }

  if(!va_context->valid_context || va_surface_id == VA_INVALID_SURFACE)
    return VA_STATUS_ERROR_UNKNOWN;

  if(this->opengl_render && !this->valid_opengl_context)
    return VA_STATUS_ERROR_UNKNOWN;

  /* Final VAAPI rendering. The deinterlacing can be controled by xine config.*/
  unsigned int deint = this->deinterlace;
  for(i = 0; i <= !!((deint > 1) && interlaced_frame); i++) {
    unsigned int flags = (deint && (interlaced_frame) ? (((!!(top_field_first)) ^ i) == 0 ? VA_BOTTOM_FIELD : VA_TOP_FIELD) : VA_FRAME_PICTURE);

    vaapi_update_csc (this, frame);
    flags |= this->vaapi_cm_flags;

    flags |= VA_CLEAR_DRAWABLE;
    flags |= this->scaling_level;

    lprintf("Putsrfc srfc 0x%08X flags 0x%08x %dx%d -> %dx%d interlaced %d top_field_first %d\n", 
            va_surface_id, flags, width, height, 
            this->sc.output_width, this->sc.output_height,
            interlaced_frame, top_field_first);

    if(this->opengl_render) {

      vaapi_x11_trap_errors();

      if(this->opengl_use_tfp) {
        lprintf("opengl render tfp\n");
        vaStatus = vaPutSurface(va_context->va_display, va_surface_id, this->gl_image_pixmap,
                 0, 0, width, height, 0, 0, width, height, NULL, 0, flags);
        if(!vaapi_check_status(this_gen, vaStatus, "vaPutSurface()"))
          return vaStatus;
      } else {
        lprintf("opengl render\n");
        vaStatus = vaCopySurfaceGLX(va_context->va_display, va_context->gl_surface, va_surface_id, flags);
        if(!vaapi_check_status(this_gen, vaStatus, "vaCopySurfaceGLX()"))
          return vaStatus;
      }
      if(vaapi_x11_untrap_errors())
        return VA_STATUS_ERROR_UNKNOWN;
      
      vaapi_glx_flip_page(frame_gen, 0, 0, va_context->width, va_context->height);

    } else {

      vaStatus = vaPutSurface(va_context->va_display, va_surface_id, this->window,
                   this->sc.displayed_xoffset, this->sc.displayed_yoffset,
                   this->sc.displayed_width, this->sc.displayed_height,
                   this->sc.output_xoffset, this->sc.output_yoffset,
                   this->sc.output_width, this->sc.output_height,
                   NULL, 0, flags);
      if(!vaapi_check_status(this_gen, vaStatus, "vaPutSurface()"))
        return vaStatus;
    }
    // workaround by johns from vdrportal.de
    usleep(1 * 1000);
  }
  return VA_STATUS_SUCCESS;
}

/* Used in vaapi_display_frame to determine how long displaying a frame takes
   - if slower than 60fps, print a message
*/
/*
static double timeOfDay()
{
    struct timeval t;
    gettimeofday( &t, NULL );
    return ((double)t.tv_sec) + (((double)t.tv_usec)/1000000.0);
}
*/

static void vaapi_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen) {
  vaapi_driver_t     *this          = (vaapi_driver_t *) this_gen;
  vaapi_frame_t      *frame         = (vaapi_frame_t *) frame_gen;
  vaapi_accel_t      *accel         = &frame->vaapi_accel_data;
  ff_vaapi_context_t *va_context    = this->va_context;
  ff_vaapi_surface_t *va_surface    = &va_render_surfaces[accel->index];
  VASurfaceID        va_surface_id  = VA_INVALID_SURFACE;
  VAImage            *va_image      = NULL;
  VAStatus           vaStatus;

  lprintf("vaapi_display_frame\n");

  /*
  if((frame->height < 17 || frame->width < 17) && ((frame->format == XINE_IMGFMT_YV12) || (frame->format == XINE_IMGFMT_YUY2))) {
    frame->vo_frame.free( frame_gen );
    xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " frame size to small width %d height %d\n", frame->height, frame->width);
    return;
  }
  */

  /*
   * let's see if this frame is different in size / aspect
   * ratio from the previous one
   */

  if ( (frame->width != this->sc.delivered_width)
       || (frame->height != this->sc.delivered_height)
       || (frame->ratio != this->sc.delivered_ratio)
       || (frame->vo_frame.crop_left != this->sc.crop_left)
       || (frame->vo_frame.crop_right != this->sc.crop_right)
       || (frame->vo_frame.crop_top != this->sc.crop_top)
       || (frame->vo_frame.crop_bottom != this->sc.crop_bottom) ) {
    lprintf("frame format changed\n");
    this->sc.force_redraw = 1;
  }

  /*
   * tell gui that we are about to display a frame,
   * ask for offset and output size
   */
  this->sc.delivered_height = frame->height;
  this->sc.delivered_width  = frame->width;
  this->sc.delivered_ratio  = frame->ratio;

  this->sc.crop_left        = frame->vo_frame.crop_left;
  this->sc.crop_right       = frame->vo_frame.crop_right;
  this->sc.crop_top         = frame->vo_frame.crop_top;
  this->sc.crop_bottom      = frame->vo_frame.crop_bottom;

  pthread_mutex_lock(&this->vaapi_lock);
  DO_LOCKDISPLAY;

  lprintf("vaapi_display_frame %s frame->width %d frame->height %d va_context->sw_width %d va_context->sw_height %d valid_context %d\n",
        (frame->format == XINE_IMGFMT_VAAPI) ? "XINE_IMGFMT_VAAPI" : ((frame->format == XINE_IMGFMT_YV12) ? "XINE_IMGFMT_YV12" : "XINE_IMGFMT_YUY2") ,
        frame->width, frame->height, va_context->sw_width, va_context->sw_height, va_context->valid_context);

  if( ((frame->format == XINE_IMGFMT_YV12) || (frame->format == XINE_IMGFMT_YUY2)) 
      && ((frame->width != va_context->sw_width) ||(frame->height != va_context->sw_height )) ) {

    lprintf("vaapi_display_frame %s frame->width %d frame->height %d\n", 
        (frame->format == XINE_IMGFMT_VAAPI) ? "XINE_IMGFMT_VAAPI" : ((frame->format == XINE_IMGFMT_YV12) ? "XINE_IMGFMT_YV12" : "XINE_IMGFMT_YUY2") ,
        frame->width, frame->height);

    unsigned int last_sub_img_fmt = va_context->last_sub_image_fmt;

    if(last_sub_img_fmt)
      vaapi_ovl_associate(this_gen, frame_gen->format, 0);

    if(!va_context->valid_context) {
      lprintf("vaapi_display_frame init full context\n");
      vaapi_init_internal(frame_gen->driver, SW_CONTEXT_INIT_FORMAT, frame->width, frame->height, 0);
    } else {
      lprintf("vaapi_display_frame init soft surfaces\n");
      vaapi_init_soft_surfaces(frame_gen->driver, frame->width, frame->height);
    }

    this->sc.force_redraw = 1;
    this->init_opengl_render = 1;

    if(last_sub_img_fmt)
      vaapi_ovl_associate(this_gen, frame_gen->format, this->has_overlay);
  }

  DO_UNLOCKDISPLAY;
  pthread_mutex_unlock(&this->vaapi_lock);

  vaapi_redraw_needed (this_gen);

  /* posible race could happen while the lock is opened */
  if(!this->va_context || !this->va_context->valid_context)
    return;

  pthread_mutex_lock(&this->vaapi_lock);
  DO_LOCKDISPLAY;

  /* initialize opengl rendering */
  if(this->opengl_render && this->init_opengl_render &&  va_context->valid_context) {
    unsigned int last_sub_img_fmt = va_context->last_sub_image_fmt;

    if(last_sub_img_fmt)
      vaapi_ovl_associate(this_gen, frame_gen->format, 0);

    destroy_glx(this_gen);

    vaapi_glx_config_glx(frame_gen->driver, va_context->width, va_context->height);

    vaapi_resize_glx_window(frame_gen->driver, this->sc.gui_width, this->sc.gui_height);

    if(last_sub_img_fmt)
      vaapi_ovl_associate(this_gen, frame_gen->format, this->has_overlay);

    this->sc.force_redraw = 1;
    this->init_opengl_render = 0;
  }

  /*
  double start_time;
  double end_time;
  double elapse_time;
  int factor;

  start_time = timeOfDay();
  */

  if(va_context->valid_context && ( (frame->format == XINE_IMGFMT_VAAPI) || (frame->format == XINE_IMGFMT_YV12) || (frame->format == XINE_IMGFMT_YUY2) )) {

    if((frame->format == XINE_IMGFMT_YUY2) || (frame->format == XINE_IMGFMT_YV12)) {
      va_surface_id = va_soft_surface_ids[va_context->va_soft_head];
      va_image = &va_soft_images[va_context->va_soft_head];
      va_context->va_soft_head = (va_context->va_soft_head + 1) % (SOFT_SURFACES);
    }

    if(this->guarded_render) {
      if(frame->format == XINE_IMGFMT_VAAPI) {
        ff_vaapi_surface_t *va_surface = &va_render_surfaces[accel->index];
        if(va_surface->status == SURFACE_RENDER || va_surface->status == SURFACE_RENDER_RELEASE) {
          va_surface_id = va_surface->va_surface_id;
        }
        va_image      = NULL;
      }
#ifdef DEBUG_SURFACE
      printf("vaapi_display_frame va_surface 0x%08x status %d index %d\n", va_surface_id, va_surface->status, accel->index);
#endif
    } else {
      if(frame->format == XINE_IMGFMT_VAAPI) {
        va_surface_id = va_surface->va_surface_id;
        va_image      = NULL;
      }
    }

    lprintf("2: 0x%08x\n", va_surface_id);

    VASurfaceStatus surf_status = 0;
    if(va_surface_id != VA_INVALID_SURFACE) {

      if(this->query_va_status) {
        vaStatus = vaQuerySurfaceStatus(va_context->va_display, va_surface_id, &surf_status);
        vaapi_check_status(this_gen, vaStatus, "vaQuerySurfaceStatus()");
      } else {
        surf_status = VASurfaceReady;
      }

      if(surf_status != VASurfaceReady) {
        va_surface_id = VA_INVALID_SURFACE;
        va_image = NULL;
#ifdef DEBUG_SURFACE
        printf("Surface srfc 0x%08X not ready for render\n", va_surface_id);
#endif
      }
    } else {
#ifdef DEBUG_SURFACE
      printf("Invalid srfc 0x%08X\n", va_surface_id);
#endif
    }

    if(va_surface_id != VA_INVALID_SURFACE) {

      lprintf("vaapi_display_frame: 0x%08x %d %d\n", va_surface_id, va_context->width, va_context->height);
    
      vaStatus = vaSyncSurface(va_context->va_display, va_surface_id);
      vaapi_check_status(this_gen, vaStatus, "vaSyncSurface()");

      /* transfer image data to a VAAPI surface */
      if((frame->format == XINE_IMGFMT_YUY2 || frame->format == XINE_IMGFMT_YV12))
        vaapi_software_render_frame(this_gen, frame_gen, va_image, va_surface_id);

      vaapi_hardware_render_frame(this_gen, frame_gen, va_surface_id);

    }
  } else {
    xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " unsupported image format %s width %d height %d valid_context %d\n", 
        (frame->format == XINE_IMGFMT_VAAPI) ? "XINE_IMGFMT_VAAPI" : ((frame->format == XINE_IMGFMT_YV12) ? "XINE_IMGFMT_YV12" : "XINE_IMGFMT_YUY2") ,
        frame->width, frame->height, va_context->valid_context);
  }

  XSync(this->display, False);

  //end_time = timeOfDay();

  if(this->guarded_render) {
    ff_vaapi_surface_t *va_surface = &va_render_surfaces[accel->index];

    if(va_surface->status == SURFACE_RENDER_RELEASE) {
      va_surface->status = SURFACE_FREE;
#ifdef DEBUG_SURFACE
      printf("release_surface vaapi_display_frame 0x%08x\n", va_surface->va_surface_id);
#endif
    } else if(va_surface->status == SURFACE_RENDER) {
      va_surface->status = SURFACE_RELEASE;
#ifdef DEBUG_SURFACE
      printf("release_surface vaapi_display_frame 0x%08x\n", va_surface->va_surface_id);
#endif
    }
  }

  DO_UNLOCKDISPLAY;

  frame->vo_frame.free( frame_gen );

  pthread_mutex_unlock(&this->vaapi_lock);

  /*
  elapse_time = end_time - start_time;
  factor = (int)(elapse_time/(1.0/60.0));

  if( factor > 1 )
  {
    xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " PutImage %dX interval (%fs)\n", factor, elapse_time );
  }
  */
}

static int vaapi_get_property (vo_driver_t *this_gen, int property) {
  vaapi_driver_t *this = (vaapi_driver_t *) this_gen;

  if ((property < 0) || (property >= VO_NUM_PROPERTIES)) return 0;

  switch (property) {
    case VO_PROP_WINDOW_WIDTH:
      this->props[property].value = this->sc.gui_width;
      break;
    case VO_PROP_WINDOW_HEIGHT:
      this->props[property].value = this->sc.gui_height;
      break;
    case VO_PROP_OUTPUT_WIDTH:
      this->props[property].value = this->sc.output_width;
      break;
    case VO_PROP_OUTPUT_HEIGHT:
      this->props[property].value = this->sc.output_height;
      break;
    case VO_PROP_OUTPUT_XOFFSET:
      this->props[property].value = this->sc.output_xoffset;
      break;
    case VO_PROP_OUTPUT_YOFFSET:
      this->props[property].value = this->sc.output_yoffset;
      break;
    case VO_PROP_MAX_NUM_FRAMES:
      if(!this->guarded_render)
        this->props[property].value = RENDER_SURFACES;
      else
        this->props[property].value = 2;
      break;
  } 

  lprintf("vaapi_get_property property=%d, value=%d\n", property, this->props[property].value );

  return this->props[property].value;
}

static int vaapi_set_property (vo_driver_t *this_gen, int property, int value) {

  vaapi_driver_t      *this       = (vaapi_driver_t *) this_gen;
  ff_vaapi_context_t  *va_context = this->va_context;

  lprintf("vaapi_set_property property=%d, value=%d\n", property, value );

  if ((property < 0) || (property >= VO_NUM_PROPERTIES)) return 0;

  if ((property == VO_PROP_BRIGHTNESS)
    || (property == VO_PROP_CONTRAST)
    || (property == VO_PROP_SATURATION)
    || (property == VO_PROP_HUE)) {
    /* defer these to vaapi_update_csc () */
    if((value < this->props[property].min) || (value > this->props[property].max))
      value = (this->props[property].min + this->props[property].max) >> 1;
    this->props[property].value = value;
    this->color_matrix = 0;
    return value;
  }

  if(this->props[property].atom) {
    VADisplayAttribute attr;

    if((value < this->props[property].min) || (value > this->props[property].max))
      value = (this->props[property].min + this->props[property].max) >> 1;

    this->props[property].value = value;
    attr.type   = this->props[property].type;
    attr.value  = value;

    if(va_context && va_context->valid_context) {
      vaSetDisplayAttributes(va_context->va_display, &attr, 1);
      //vaapi_check_status((vo_driver_t *)this, vaStatus, "vaSetDisplayAttributes()");
    }

    if (this->props[property].entry)
      this->props[property].entry->num_value = this->props[property].value;

    vaapi_show_display_props((vo_driver_t*)this);

    return this->props[property].value;
  } else {
    switch (property) {

      case VO_PROP_ASPECT_RATIO:
        if (value>=XINE_VO_ASPECT_NUM_RATIOS)
  	      value = XINE_VO_ASPECT_AUTO;
        this->props[property].value = value;
        this->sc.user_ratio = value;
        _x_vo_scale_compute_ideal_size (&this->sc);
        this->sc.force_redraw = 1;
        break;

      case VO_PROP_ZOOM_X:
        if ((value >= XINE_VO_ZOOM_MIN) && (value <= XINE_VO_ZOOM_MAX)) {
          this->props[property].value = value;
  	      this->sc.zoom_factor_x = (double)value / (double)XINE_VO_ZOOM_STEP;
          _x_vo_scale_compute_ideal_size (&this->sc);
          this->sc.force_redraw = 1;
        }
        break;

      case VO_PROP_ZOOM_Y:
        if ((value >= XINE_VO_ZOOM_MIN) && (value <= XINE_VO_ZOOM_MAX)) {
          this->props[property].value = value;
          this->sc.zoom_factor_y = (double)value / (double)XINE_VO_ZOOM_STEP;
  	      _x_vo_scale_compute_ideal_size (&this->sc);
  	      this->sc.force_redraw = 1;
        }
        break;
    }
  }
  return value;
}

static void vaapi_get_property_min_max (vo_driver_t *this_gen,
				     int property, int *min, int *max) {
  vaapi_driver_t *this = (vaapi_driver_t *) this_gen;

  *min = this->props[property].min;
  *max = this->props[property].max;
}

static int vaapi_gui_data_exchange (vo_driver_t *this_gen,
				 int data_type, void *data) {
  vaapi_driver_t     *this       = (vaapi_driver_t *) this_gen;

  lprintf("vaapi_gui_data_exchange %d\n", data_type);

  switch (data_type) {
#ifndef XINE_DISABLE_DEPRECATED_FEATURES
  case XINE_GUI_SEND_COMPLETION_EVENT:
    break;
#endif

  case XINE_GUI_SEND_EXPOSE_EVENT: {
    pthread_mutex_lock(&this->vaapi_lock);
    DO_LOCKDISPLAY;
    lprintf("XINE_GUI_SEND_EXPOSE_EVENT:\n");
    this->sc.force_redraw = 1;
    this->init_opengl_render = 1;
    DO_UNLOCKDISPLAY;
    pthread_mutex_unlock(&this->vaapi_lock);
  }
  break;

  case XINE_GUI_SEND_WILL_DESTROY_DRAWABLE: {
    printf("XINE_GUI_SEND_WILL_DESTROY_DRAWABLE\n");
  }
  break;

  case XINE_GUI_SEND_DRAWABLE_CHANGED: {
    pthread_mutex_lock(&this->vaapi_lock);
    DO_LOCKDISPLAY;
    lprintf("XINE_GUI_SEND_DRAWABLE_CHANGED\n");

    this->drawable = (Drawable) data;

    XReparentWindow(this->display, this->window, this->drawable, 0, 0);

    this->sc.force_redraw = 1;
    this->init_opengl_render = 1;

    DO_UNLOCKDISPLAY;
    pthread_mutex_unlock(&this->vaapi_lock);
  }
  break;

  case XINE_GUI_SEND_TRANSLATE_GUI_TO_VIDEO: {
    int x1, y1, x2, y2;
    x11_rectangle_t *rect = data;

    _x_vo_scale_translate_gui2video(&this->sc, rect->x, rect->y, &x1, &y1);
    _x_vo_scale_translate_gui2video(&this->sc, rect->x + rect->w, rect->y + rect->h, &x2, &y2);
    rect->x = x1;
    rect->y = y1;
    rect->w = x2-x1;
    rect->h = y2-y1;
  } 
  break;

  default:
    return -1;
  }

  return 0;
}

static void vaapi_dispose_locked (vo_driver_t *this_gen) {
  vaapi_driver_t      *this = (vaapi_driver_t *) this_gen;
  ff_vaapi_context_t  *va_context = this->va_context;

  // vaapi_lock is locked at this point, either from vaapi_dispose or vaapi_open_plugin

  DO_LOCKDISPLAY;

  vaapi_close(this_gen);
  free(va_context);

  if(this->overlay_bitmap)
    free(this->overlay_bitmap);

  if(va_surface_ids)
    free(va_surface_ids);
  if(va_soft_surface_ids)
    free(va_soft_surface_ids);
  if(va_render_surfaces)
    free(va_render_surfaces);
  if(va_soft_images)
    free(va_soft_images);

  XDestroyWindow(this->display, this->window);
  DO_UNLOCKDISPLAY;

  pthread_mutex_unlock(&this->vaapi_lock);
  pthread_mutex_destroy(&this->vaapi_lock);

  cm_close (this);

  free (this);
}

static void vaapi_dispose (vo_driver_t *this_gen) {
  lprintf("vaapi_dispose\n");
  pthread_mutex_lock(&((vaapi_driver_t *)this_gen)->vaapi_lock);
  vaapi_dispose_locked(this_gen);
}

static void vaapi_vdr_osd_width_flag( void *this_gen, xine_cfg_entry_t *entry )
{
  vaapi_driver_t  *this  = (vaapi_driver_t *) this_gen;

  this->vdr_osd_width = entry->num_value;
}

static void vaapi_vdr_osd_height_flag( void *this_gen, xine_cfg_entry_t *entry )
{
  vaapi_driver_t  *this  = (vaapi_driver_t *) this_gen;

  this->vdr_osd_height = entry->num_value;
}

static void vaapi_deinterlace_flag( void *this_gen, xine_cfg_entry_t *entry )
{
  vaapi_driver_t  *this  = (vaapi_driver_t *) this_gen;

  this->deinterlace = entry->num_value;
  if(this->deinterlace > 2)
    this->deinterlace = 2;
}

static void vaapi_opengl_render( void *this_gen, xine_cfg_entry_t *entry )
{
  vaapi_driver_t  *this  = (vaapi_driver_t *) this_gen;

  this->opengl_render = entry->num_value;
}

static void vaapi_opengl_use_tfp( void *this_gen, xine_cfg_entry_t *entry )
{
  vaapi_driver_t  *this  = (vaapi_driver_t *) this_gen;

  this->opengl_use_tfp = entry->num_value;
}

static void vaapi_guarded_render( void *this_gen, xine_cfg_entry_t *entry )
{
  vaapi_driver_t  *this  = (vaapi_driver_t *) this_gen;

  this->guarded_render = entry->num_value;
}

static void vaapi_scaling_level( void *this_gen, xine_cfg_entry_t *entry )
{
  vaapi_driver_t  *this  = (vaapi_driver_t *) this_gen;

  this->scaling_level = entry->num_value;
}

static void vaapi_swap_uv_planes(void *this_gen, xine_cfg_entry_t *entry) 
{
  vaapi_driver_t  *this  = (vaapi_driver_t *) this_gen;

  this->swap_uv_planes = entry->num_value;
}

static void vaapi_csc_mode(void *this_gen, xine_cfg_entry_t *entry) 
{
  vaapi_driver_t  *this  = (vaapi_driver_t *) this_gen;
  int new_mode = entry->num_value;

  /* skip unchanged */
  if (new_mode == this->csc_mode)
    return;

  vaapi_set_csc_mode (this, new_mode);
}

static vo_driver_t *vaapi_open_plugin (video_driver_class_t *class_gen, const void *visual_gen) {

  vaapi_class_t           *class  = (vaapi_class_t *) class_gen;
  x11_visual_t            *visual = (x11_visual_t *) visual_gen;
  vaapi_driver_t          *this;
  config_values_t         *config = class->config;
  XSetWindowAttributes    xswa;
  unsigned long           xswa_mask;
  XWindowAttributes       wattr;
  unsigned long           black_pixel;
  XVisualInfo             visualInfo;
  XVisualInfo             *vi;
  int                     depth;
  const int               x11_event_mask = ExposureMask | 
                                           StructureNotifyMask;

  this = (vaapi_driver_t *) calloc(1, sizeof(vaapi_driver_t));
  if (!this)
    return NULL;

  this->config                  = config;
  this->xine                    = class->xine;

  this->display                 = visual->display;
  this->screen                  = visual->screen;
  this->drawable                = visual->d;

  this->va_context              = calloc(1, sizeof(ff_vaapi_context_t));

#ifdef LOCKDISPLAY
  guarded_display     = visual->display;
#endif

  /* number of video frames from config - register it with the default value. */
  int frame_num = config->register_num (config, "engine.buffers.video_num_frames", RENDER_SURFACES, /* default */
       _("default number of video frames"),
       _("The default number of video frames to request "
         "from xine video out driver. Some drivers will "
         "override this setting with their own values."),
      20, NULL, this);

  /* now make sure we have at least 22 frames, to prevent
   * locks with vdpau_h264 */
  if(frame_num != RENDER_SURFACES)
    config->update_num(config,"engine.buffers.video_num_frames", RENDER_SURFACES);

  this->opengl_render = config->register_bool( config, "video.output.vaapi_opengl_render", 0,
        _("vaapi: opengl output rendering"),
        _("vaapi: opengl output rendering"),
        20, vaapi_opengl_render, this );
  
  this->init_opengl_render = 1;

  this->opengl_use_tfp = config->register_bool( config, "video.output.vaapi_opengl_use_tfp", 0,
        _("vaapi: opengl rendering tfp"),
        _("vaapi: opengl rendering tfp"),
        20, vaapi_opengl_use_tfp, this );

  if(this->opengl_render) {
      this->opengl_render = vaapi_opengl_verify_direct ((x11_visual_t *)visual_gen);
      if(!this->opengl_render)
        xprintf (this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_open: Opengl indirect/software rendering does not work. Fallback to plain VAAPI output !!!!\n");
  }

  this->valid_opengl_context            = 0;
  this->gl_vinfo                        = NULL;
  this->gl_pixmap                       = None;
  this->gl_image_pixmap                 = None;
  this->gl_texture                      = GL_NONE;

  this->num_frame_buffers               = 0;

  va_render_surfaces                    = calloc(RENDER_SURFACES + 1, sizeof(ff_vaapi_surface_t));
  va_surface_ids                        = calloc(RENDER_SURFACES + 1, sizeof(VASurfaceID));
  va_soft_surface_ids                   = calloc(SOFT_SURFACES + 1, sizeof(VASurfaceID));
  va_soft_images                        = calloc(SOFT_SURFACES + 1, sizeof(VAImage));

  vaapi_init_va_context(this);
  vaapi_init_subpicture(this);

  _x_vo_scale_init (&this->sc, 1, 0, config );

  this->sc.frame_output_cb      = visual->frame_output_cb;
  this->sc.dest_size_cb         = visual->dest_size_cb;
  this->sc.user_data            = visual->user_data;
  this->sc.user_ratio           = XINE_VO_ASPECT_AUTO;

  black_pixel         = BlackPixel(this->display, this->screen);

  XGetWindowAttributes(this->display, this->drawable, &wattr);

  depth = wattr.depth;
  if (depth != 15 && depth != 16 && depth != 24 && depth != 32)
    depth = 24;

  vi = &visualInfo;
  XMatchVisualInfo(this->display, this->screen, depth, TrueColor, vi);

  xswa_mask             = CWBorderPixel | CWBackPixel | CWColormap;
  xswa.border_pixel     = black_pixel;
  xswa.background_pixel = black_pixel;
  xswa.colormap         = CopyFromParent;

  this->window = XCreateWindow(this->display, this->drawable,
                             0, 0, 1, 1, 0, depth,
                             InputOutput, vi->visual, xswa_mask, &xswa);

  if(this->window == None)
    return NULL;

  XSelectInput(this->display, this->window, x11_event_mask);

  XMapWindow(this->display, this->window);
  vaapi_x11_wait_event(this->display, this->window, MapNotify);

  if(vi != &visualInfo)
    XFree(vi);

  this->capabilities            = VO_CAP_YV12 | VO_CAP_YUY2 | VO_CAP_CROP | VO_CAP_UNSCALED_OVERLAY | VO_CAP_ARGB_LAYER_OVERLAY | VO_CAP_VAAPI | VO_CAP_CUSTOM_EXTENT_OVERLAY;

  this->vo_driver.get_capabilities     = vaapi_get_capabilities;
  this->vo_driver.alloc_frame          = vaapi_alloc_frame;
  this->vo_driver.update_frame_format  = vaapi_update_frame_format;
  this->vo_driver.overlay_begin        = vaapi_overlay_begin;
  this->vo_driver.overlay_blend        = vaapi_overlay_blend;
  this->vo_driver.overlay_end          = vaapi_overlay_end;
  this->vo_driver.display_frame        = vaapi_display_frame;
  this->vo_driver.get_property         = vaapi_get_property;
  this->vo_driver.set_property         = vaapi_set_property;
  this->vo_driver.get_property_min_max = vaapi_get_property_min_max;
  this->vo_driver.gui_data_exchange    = vaapi_gui_data_exchange;
  this->vo_driver.dispose              = vaapi_dispose;
  this->vo_driver.redraw_needed        = vaapi_redraw_needed;

  this->deinterlace                    = 0;
  this->vdr_osd_width                  = 0;
  this->vdr_osd_height                 = 0;

  this->vdr_osd_width = config->register_num( config, "video.output.vaapi_vdr_osd_width", 0,
        _("vaapi: VDR osd width workaround."),
        _("vaapi: VDR osd width workaround."),
        10, vaapi_vdr_osd_width_flag, this );

  this->vdr_osd_height = config->register_num( config, "video.output.vaapi_vdr_osd_height", 0,
        _("vaapi: VDR osd height workaround."),
        _("vaapi: VDR osd height workaround."),
        10, vaapi_vdr_osd_height_flag, this );

  this->deinterlace = config->register_num( config, "video.output.vaapi_deinterlace", 0,
        _("vaapi: set deinterlace to 0 ( none ), 1 ( top field ), 2 ( bob )."),
        _("vaapi: set deinterlace to 0 ( none ), 1 ( top field ), 2 ( bob )."),
        10, vaapi_deinterlace_flag, this );

  this->guarded_render = config->register_num( config, "video.output.vaapi_guarded_render", 1,
        _("vaapi: set vaapi_guarded_render to 0 ( no ) 1 ( yes )"),
        _("vaapi: set vaapi_guarded_render to 0 ( no ) 1 ( yes )"),
        10, vaapi_guarded_render, this );

  this->scaling_level_enum = config->register_enum(config, "video.output.vaapi_scaling_level", 0,
    (char **)scaling_level_enum_names,
        _("vaapi: set scaling level to : default (default) fast (fast) hq (HQ) nla (anamorphic)"),
        _("vaapi: set scaling level to : default (default) fast (fast) hq (HQ) nla (anamorphic)"),
    10, vaapi_scaling_level, this);

  this->scaling_level = scaling_level_enum_values[this->scaling_level_enum];

  this->swap_uv_planes = config->register_bool( config, "video.output.vaapi_swap_uv_planes", 0,
    _("vaapi: swap UV planes."),
    _("vaapi: this is a workaround for buggy drivers ( intel IronLake ).\n"
      "There the UV planes are swapped.\n"),
    10, vaapi_swap_uv_planes, this);


  pthread_mutex_init(&this->vaapi_lock, NULL);

  pthread_mutex_lock(&this->vaapi_lock);

  int i;
  for (i = 0; i < VO_NUM_PROPERTIES; i++) {
    this->props[i].value = 0;
    this->props[i].min   = 0;
    this->props[i].max   = 0;
    this->props[i].atom  = 0;
    this->props[i].entry = NULL;
    this->props[i].this  = this;
  }

  cm_init (this);

  this->sc.user_ratio                        =
    this->props[VO_PROP_ASPECT_RATIO].value  = XINE_VO_ASPECT_AUTO;
  this->props[VO_PROP_ZOOM_X].value          = 100;
  this->props[VO_PROP_ZOOM_Y].value          = 100;

  this->va_context->last_sub_surface_id      = VA_INVALID_SURFACE;
  this->va_context->last_sub_image_fmt       = 0;

  if(vaapi_init_internal((vo_driver_t *)this, SW_CONTEXT_INIT_FORMAT, SW_WIDTH, SW_HEIGHT, 0) != VA_STATUS_SUCCESS) {
    vaapi_dispose_locked((vo_driver_t *)this);
    return NULL;
  }
  vaapi_close((vo_driver_t *)this);
  this->va_context->valid_context = 0;
  this->va_context->driver        = (vo_driver_t *)this;

  pthread_mutex_unlock(&this->vaapi_lock);

  this->csc_mode = this->xine->config->register_enum (this->xine->config, "video.output.vaapi_csc_mode", 3,
    (char **)vaapi_csc_mode_labels,
    _("VAAPI color conversion method"),
    _("How to handle color conversion in VAAPI:\n\n"
      "user_matrix: The best way - if your driver supports it.\n"
      "simple:      Switch SD/HD colorspaces, and let decoders convert fullrange video.\n"
      "simple+2:    Switch SD/HD colorspaces, and emulate fullrange color by modifying\n"
      "             brightness/contrast settings.\n"
      "simple+3:    Like above, but adjust saturation as well.\n\n"
      "Hint: play \"test://rgb_levels.bmp\" while trying this.\n"),
    10,
    vaapi_csc_mode, this);
  vaapi_set_csc_mode (this, this->csc_mode);

  xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_open: Deinterlace : %d\n", this->deinterlace);
  xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_open: Render surfaces : %d\n", RENDER_SURFACES);
  xprintf(this->xine, XINE_VERBOSITY_LOG, LOG_MODULE " vaapi_open: Opengl render : %d\n", this->opengl_render);

  return &this->vo_driver;
}

/*
 * class functions
 */
static void *vaapi_init_class (xine_t *xine, void *visual_gen) {
  vaapi_class_t        *this = (vaapi_class_t *) calloc(1, sizeof(vaapi_class_t));

  this->driver_class.open_plugin     = vaapi_open_plugin;
  this->driver_class.identifier      = "vaapi";
  this->driver_class.description     = N_("xine video output plugin using VAAPI");
  this->driver_class.dispose         = default_video_driver_class_dispose;
  this->config                       = xine->config;
  this->xine                         = xine;

  return this;
}

static const vo_info_t vo_info_vaapi = {
  9,                      /* priority    */
  XINE_VISUAL_TYPE_X11    /* visual type */
};

/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_OUT, 22, "vaapi", XINE_VERSION_CODE, &vo_info_vaapi, vaapi_init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
