/*
 * Copyright (C) 2000-2006 the xine project
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
 * mplayer's noise filter, ported by Jason Tackaberry.  Original filter
 * is copyright 2002 Michael Niedermayer <michaelni@gmx.at>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xine/xine_internal.h>
#include <xine/post.h>
#include <xine/xineutils.h>
#include <math.h>
#include <pthread.h>

#ifdef HAVE_FFMPEG_AVUTIL_H
#  include <mem.h>
#else
#  include <libavutil/mem.h>
#endif

#ifdef ARCH_X86_64
#  define REG_a  "rax"
#  define intarch_t int64_t
#else
#  define REG_a  "eax"
#  define intarch_t int32_t
#endif

#define MAX_NOISE 4096
#define MAX_SHIFT 1024
#define MAX_RES (MAX_NOISE-MAX_SHIFT)

static inline void lineNoise_C(uint8_t *dst, uint8_t *src, int8_t *noise, int len, int shift);
static inline void lineNoiseAvg_C(uint8_t *dst, uint8_t *src, int len, int8_t **shift);

static void (*lineNoise)(uint8_t *dst, uint8_t *src, int8_t *noise, int len, int shift) = lineNoise_C;
static void (*lineNoiseAvg)(uint8_t *dst, uint8_t *src, int len, int8_t **shift) = lineNoiseAvg_C;


typedef struct noise_param_t {
    int strength,
        uniform,
        temporal,
        quality,
        averaged,
        pattern,
        shiftptr;
    int8_t *noise,
           *prev_shift[MAX_RES][3];
} noise_param_t;

static int nonTempRandShift[MAX_RES]= {-1};

static const int patt[4] = {
    -1,0,1,0
};

#define RAND_N(range) ((int) ((double)range*rand()/(RAND_MAX+1.0)))
static int8_t *initNoise(noise_param_t *fp){
    int strength= fp->strength;
    int uniform= fp->uniform;
    int averaged= fp->averaged;
    int pattern= fp->pattern;
    int8_t *noise;
    int i, j;

    noise = av_mallocz(MAX_NOISE*sizeof(int8_t));
    srand(123457);

    for(i=0,j=0; i<MAX_NOISE; i++,j++)
    {
        if(uniform) {
            if (averaged) {
                    if (pattern) {
                    noise[i]= (RAND_N(strength) - strength/2)/6
                        +patt[j%4]*strength*0.25/3;
                } else {
                    noise[i]= (RAND_N(strength) - strength/2)/3;
                    }
            } else {
                    if (pattern) {
                    noise[i]= (RAND_N(strength) - strength/2)/2
                        + patt[j%4]*strength*0.25;
                } else {
                    noise[i]= RAND_N(strength) - strength/2;
                    }
            }
            } else {
            double x1, x2, w, y1;
            do {
                x1 = 2.0 * rand()/(float)RAND_MAX - 1.0;
                x2 = 2.0 * rand()/(float)RAND_MAX - 1.0;
                w = x1 * x1 + x2 * x2;
            } while ( w >= 1.0 );

            w = sqrt( (-2.0 * log( w ) ) / w );
            y1= x1 * w;
            y1*= strength / sqrt(3.0);
            if (pattern) {
                y1 /= 2;
                y1 += patt[j%4]*strength*0.35;
            }
            if     (y1<-128) y1=-128;
            else if(y1> 127) y1= 127;
            if (averaged) y1 /= 3.0;
            noise[i]= (int)y1;
        }
        if (RAND_N(6) == 0) j--;
    }


    for (i = 0; i < MAX_RES; i++)
        for (j = 0; j < 3; j++)
        fp->prev_shift[i][j] = noise + (rand()&(MAX_SHIFT-1));

    if(nonTempRandShift[0]==-1){
        for(i=0; i<MAX_RES; i++){
            nonTempRandShift[i]= rand()&(MAX_SHIFT-1);
        }
    }

    fp->noise= noise;
    fp->shiftptr= 0;
    return noise;
}

static inline void lineNoise_C(uint8_t *dst, uint8_t *src, int8_t *noise, int len, int shift){
    int i;
    noise+= shift;
    for(i=0; i<len; i++)
    {
        int v= src[i]+ noise[i];
        if(v>255)   dst[i]=255; //FIXME optimize
        else if(v<0)    dst[i]=0;
        else        dst[i]=v;
    }
}

#ifdef ARCH_X86
static inline void lineNoise_MMX(uint8_t *dst, uint8_t *src, int8_t *noise, int len, int shift){
    intarch_t mmx_len= len&(~7);
    noise+=shift;

    asm volatile(
        "mov %3, %%"REG_a"      \n\t"
        "pcmpeqb %%mm7, %%mm7       \n\t"
        "psllw $15, %%mm7       \n\t"
        "packsswb %%mm7, %%mm7      \n\t"
	ASMALIGN(4)
        "1:             \n\t"
        "movq (%0, %%"REG_a"), %%mm0    \n\t"
        "movq (%1, %%"REG_a"), %%mm1    \n\t"
        "pxor %%mm7, %%mm0      \n\t"
        "paddsb %%mm1, %%mm0        \n\t"
        "pxor %%mm7, %%mm0      \n\t"
        "movq %%mm0, (%2, %%"REG_a")    \n\t"
        "add $8, %%"REG_a"      \n\t"
        " js 1b             \n\t"
        :: "r" (src+mmx_len), "r" (noise+mmx_len), "r" (dst+mmx_len), "g" (-mmx_len)
        : "%"REG_a
    );
    if(mmx_len!=len)
        lineNoise_C(dst+mmx_len, src+mmx_len, noise+mmx_len, len-mmx_len, 0);
}

//duplicate of previous except movntq
static inline void lineNoise_MMX2(uint8_t *dst, uint8_t *src, int8_t *noise, int len, int shift){
    intarch_t mmx_len= len&(~7);
    noise+=shift;

    asm volatile(
        "mov %3, %%"REG_a"      \n\t"
        "pcmpeqb %%mm7, %%mm7       \n\t"
        "psllw $15, %%mm7       \n\t"
        "packsswb %%mm7, %%mm7      \n\t"
	ASMALIGN(4)
        "1:             \n\t"
        "movq (%0, %%"REG_a"), %%mm0    \n\t"
        "movq (%1, %%"REG_a"), %%mm1    \n\t"
        "pxor %%mm7, %%mm0      \n\t"
        "paddsb %%mm1, %%mm0        \n\t"
        "pxor %%mm7, %%mm0      \n\t"
        "movntq %%mm0, (%2, %%"REG_a")  \n\t"
        "add $8, %%"REG_a"      \n\t"
        " js 1b             \n\t"
        :: "r" (src+mmx_len), "r" (noise+mmx_len), "r" (dst+mmx_len), "g" (-mmx_len)
        : "%"REG_a
    );
    if(mmx_len!=len)
        lineNoise_C(dst+mmx_len, src+mmx_len, noise+mmx_len, len-mmx_len, 0);
}

#endif

/***************************************************************************/

static inline void lineNoiseAvg_C(uint8_t *dst, uint8_t *src, int len, int8_t **shift){
    int i;
    int8_t *src2= (int8_t*)src;

    for(i=0; i<len; i++)
    {
        const int n= shift[0][i] + shift[1][i] + shift[2][i];
        dst[i]= src2[i]+((n*src2[i])>>7);
    }
}

#ifdef ARCH_X86

static inline void lineNoiseAvg_MMX(uint8_t *dst, uint8_t *src, int len, int8_t **shift){
    intarch_t mmx_len= len&(~7);

    asm volatile(
        "mov %5, %%"REG_a"      \n\t"
	ASMALIGN(4)
        "1:             \n\t"
        "movq (%1, %%"REG_a"), %%mm1    \n\t"
        "movq (%0, %%"REG_a"), %%mm0    \n\t"
        "paddb (%2, %%"REG_a"), %%mm1   \n\t"
        "paddb (%3, %%"REG_a"), %%mm1   \n\t"
        "movq %%mm0, %%mm2      \n\t"
        "movq %%mm1, %%mm3      \n\t"
        "punpcklbw %%mm0, %%mm0     \n\t"
        "punpckhbw %%mm2, %%mm2     \n\t"
        "punpcklbw %%mm1, %%mm1     \n\t"
        "punpckhbw %%mm3, %%mm3     \n\t"
        "pmulhw %%mm0, %%mm1        \n\t"
        "pmulhw %%mm2, %%mm3        \n\t"
        "paddw %%mm1, %%mm1     \n\t"
        "paddw %%mm3, %%mm3     \n\t"
        "paddw %%mm0, %%mm1     \n\t"
        "paddw %%mm2, %%mm3     \n\t"
        "psrlw $8, %%mm1        \n\t"
        "psrlw $8, %%mm3        \n\t"
                "packuswb %%mm3, %%mm1      \n\t"
        "movq %%mm1, (%4, %%"REG_a")    \n\t"
        "add $8, %%"REG_a"      \n\t"
        " js 1b             \n\t"
        :: "r" (src+mmx_len), "r" (shift[0]+mmx_len), "r" (shift[1]+mmx_len), "r" (shift[2]+mmx_len),
                   "r" (dst+mmx_len), "g" (-mmx_len)
        : "%"REG_a
    );

    if(mmx_len!=len){
        int8_t *shift2[3]={shift[0]+mmx_len, shift[1]+mmx_len, shift[2]+mmx_len};
        lineNoiseAvg_C(dst+mmx_len, src+mmx_len, len-mmx_len, shift2);
    }
}
#endif

/***************************************************************************/

static void noise(uint8_t *dst, uint8_t *src, int dstStride, int srcStride, int width, int height, noise_param_t *fp)
{
    int8_t *noise= fp->noise;
    int y;
    int shift=0;

    if(!noise)
    {
        if(src==dst) return;

        if(dstStride==srcStride) memcpy(dst, src, srcStride*height);
        else
        {
            for(y=0; y<height; y++)
            {
                memcpy(dst, src, width);
                dst+= dstStride;
                src+= srcStride;
            }
        }
        return;
    }

    for(y=0; y<height; y++)
    {
        if(fp->temporal)    shift=  rand()&(MAX_SHIFT  -1);
        else                shift= nonTempRandShift[y];

        if(fp->quality==0) shift&= ~7;
        if (fp->averaged) {
            lineNoiseAvg(dst, src, width, fp->prev_shift[y]);
            fp->prev_shift[y][fp->shiftptr] = noise + shift;
        } else {
            lineNoise(dst, src, noise, width, shift);
        }
        dst+= dstStride;
        src+= srcStride;
    }
    fp->shiftptr++;
    if (fp->shiftptr == 3) fp->shiftptr = 0;
}



/* plugin class initialization function */
void *noise_init_plugin(xine_t *xine, void *);

typedef struct post_plugin_noise_s post_plugin_noise_t;

/*
 * this is the struct used by "parameters api"
 */
typedef struct noise_parameters_s {
    int luma_strength, chroma_strength,
        type, quality, pattern;
} noise_parameters_t;

static const char *const enum_types[] = {"uniform", "gaussian", NULL};
static const char *const enum_quality[] = {"fixed", "temporal", "averaged temporal", NULL};

/*
 * description of params struct
 */
START_PARAM_DESCR( noise_parameters_t )
PARAM_ITEM( POST_PARAM_TYPE_INT, luma_strength, NULL, 0, 100, 0,
    "Amount of noise to add to luma channel" )
PARAM_ITEM( POST_PARAM_TYPE_INT, chroma_strength, NULL, 0, 100, 0,
    "Amount of noise to add to chroma channel" )
PARAM_ITEM( POST_PARAM_TYPE_INT, quality, enum_quality, 0, 0, 0,
    "Quality level of noise" )
PARAM_ITEM( POST_PARAM_TYPE_INT, type, enum_types, 0, 0, 0,
    "Type of noise" )
PARAM_ITEM( POST_PARAM_TYPE_BOOL, pattern, NULL, 0, 1, 0,
    "Mix random noise with a (semi)regular pattern" )
END_PARAM_DESCR( param_descr )


/* plugin structure */
struct post_plugin_noise_s {
  post_plugin_t post;

  /* private data */
  noise_param_t params[2]; // luma and chroma
  xine_post_in_t     params_input;

  pthread_mutex_t    lock;
};


static int set_parameters (xine_post_t *this_gen, void *param_gen)
{
    post_plugin_noise_t *this = (post_plugin_noise_t *)this_gen;
    noise_parameters_t *param = (noise_parameters_t *)param_gen;
    int i;

    pthread_mutex_lock (&this->lock);
    for (i = 0; i < 2; i++) {
        this->params[i].uniform = (param->type == 0);
        this->params[i].temporal = (param->quality >= 1);
        this->params[i].averaged = (param->quality == 2);
        this->params[i].quality = 1;
        this->params[i].pattern = param->pattern;
    }
    this->params[0].strength = param->luma_strength;
    this->params[1].strength = param->chroma_strength;
    pthread_mutex_unlock (&this->lock);
    initNoise(&this->params[0]);
    initNoise(&this->params[1]);
    return 1;
}

static int get_parameters (xine_post_t *this_gen, void *param_gen)
{
    post_plugin_noise_t *this = (post_plugin_noise_t *)this_gen;
    noise_parameters_t *param = (noise_parameters_t *)param_gen;

    pthread_mutex_lock (&this->lock);
    param->type = (this->params[0].uniform == 0);
    if (this->params[0].averaged)
        param->quality = 2;
    else if (this->params[0].temporal)
        param->quality = 1;
    else
        param->quality = 0;
    param->pattern = this->params[0].pattern;
    param->luma_strength = this->params[0].strength;
    param->chroma_strength = this->params[1].strength;
    pthread_mutex_unlock (&this->lock);
    return 1;
}

static xine_post_api_descr_t *get_param_descr (void) {
    return &param_descr;
}

static char *get_help (void) {
  return _("Adds random noise to the video.\n"
           "\n"
           "Parameters:\n"
           "  luma_strength: strength of noise added to luma channel "
           "(0-100, default: 8)\n"
           "  chroma_strength: strength of noise added to chroma channel "
           "(0-100, default: 5)\n"
           "  quality: quality level of the noise.  fixed: constant noise "
           "pattern; temporal: noise pattern changes between frames; "
           "averaged temporal: smoother noise pattern that changes between "
           "frames.  (default: averaged temporal)\n"
           "  type: Type of noise: uniform or gaussian.  (default: "
           "gaussian)\n"
           "  pattern: Mix random noise with a (semi)regular pattern. "
           "(default: False)\n"
           "\n"
           "* mplayer's noise (C) Michael Niedermayer\n"
           );
}

static xine_post_api_t post_api = {
    set_parameters,
    get_parameters,
    get_param_descr,
    get_help,
};


/* plugin class functions */
static post_plugin_t *noise_open_plugin(post_class_t *class_gen, int inputs,
                     xine_audio_port_t **audio_target,
                     xine_video_port_t **video_target);

/* plugin instance functions */
static void           noise_dispose(post_plugin_t *this_gen);

/* frame intercept check */
static int            noise_intercept_frame(post_video_port_t *port, vo_frame_t *frame);

/* replaced vo_frame functions */
static int            noise_draw(vo_frame_t *frame, xine_stream_t *stream);


void *noise_init_plugin(xine_t *xine, void *data)
{
    post_class_t *class = (post_class_t *)xine_xmalloc(sizeof(post_class_t));

    if (!class)
        return NULL;

    class->open_plugin     = noise_open_plugin;
    class->identifier      = "noise";
    class->description     = N_("Adds noise");
    class->dispose         = default_post_class_dispose;

#ifdef ARCH_X86
    if (xine_mm_accel() & MM_ACCEL_X86_MMX) {
        lineNoise = lineNoise_MMX;
        lineNoiseAvg = lineNoiseAvg_MMX;
    } else if (xine_mm_accel() & MM_ACCEL_X86_MMXEXT) {
        lineNoise = lineNoise_MMX2;
    }
#endif
    return class;
}


static post_plugin_t *noise_open_plugin(post_class_t *class_gen, int inputs,
                     xine_audio_port_t **audio_target,
                     xine_video_port_t **video_target)
{
    post_plugin_noise_t *this = calloc(1, sizeof(post_plugin_noise_t));
    post_in_t         *input;
    xine_post_in_t    *input_api;
    post_out_t        *output;
    post_video_port_t *port;
    noise_parameters_t params;

    if (!this || !video_target || !video_target[0]) {
      free(this);
      return NULL;
    }

    _x_post_init(&this->post, 0, 1);

    memset(&params, 0, sizeof(noise_parameters_t));
    params.luma_strength = 8;
    params.chroma_strength = 5;
    params.type = 1;
    params.quality = 2;

    pthread_mutex_init(&this->lock, NULL);

    port = _x_post_intercept_video_port(&this->post, video_target[0], &input, &output);
    port->intercept_frame       = noise_intercept_frame;
    port->new_frame->draw       = noise_draw;

    input_api       = &this->params_input;
    input_api->name = "parameters";
    input_api->type = XINE_POST_DATA_PARAMETERS;
    input_api->data = &post_api;
    xine_list_push_back(this->post.input, input_api);

    input->xine_in.name     = "video";
    output->xine_out.name   = "filtered video";

    this->post.xine_post.video_input[0] = &port->new_port;

    this->post.dispose = noise_dispose;

    set_parameters ((xine_post_t *)this, &params);

    return &this->post;
}

static void noise_dispose(post_plugin_t *this_gen)
{
    post_plugin_noise_t *this = (post_plugin_noise_t *)this_gen;

    if (_x_post_dispose(this_gen)) {
        pthread_mutex_destroy(&this->lock);
	av_free(this->params[0].noise);
	av_free(this->params[1].noise);
        free(this);
    }
}


static int noise_intercept_frame(post_video_port_t *port, vo_frame_t *frame)
{
    return (frame->format == XINE_IMGFMT_YV12 || frame->format == XINE_IMGFMT_YUY2);
}


static int noise_draw(vo_frame_t *frame, xine_stream_t *stream)
{
    post_video_port_t *port = (post_video_port_t *)frame->port;
    post_plugin_noise_t *this = (post_plugin_noise_t *)port->post;
    vo_frame_t *out_frame;
    int skip;

    if (frame->bad_frame ||
        (this->params[0].strength == 0 && this->params[1].strength == 0)) {
        _x_post_frame_copy_down(frame, frame->next);
        skip = frame->next->draw(frame->next, stream);
        _x_post_frame_copy_up(frame, frame->next);
        return skip;
    }

    frame->lock(frame);
    out_frame = port->original_port->get_frame(port->original_port,
        frame->width, frame->height, frame->ratio, frame->format,
        frame->flags | VO_BOTH_FIELDS);

    _x_post_frame_copy_down(frame, out_frame);
    pthread_mutex_lock (&this->lock);

    if (frame->format == XINE_IMGFMT_YV12) {
        noise(out_frame->base[0], frame->base[0],
              out_frame->pitches[0], frame->pitches[0],
              frame->width, frame->height, &this->params[0]);
        noise(out_frame->base[1], frame->base[1],
              out_frame->pitches[1], frame->pitches[1],
              frame->width/2, frame->height/2, &this->params[1]);
        noise(out_frame->base[2], frame->base[2],
              out_frame->pitches[2], frame->pitches[2],
              frame->width/2, frame->height/2, &this->params[1]);
    } else {
        // Chroma strength is ignored for YUY2.
        noise(out_frame->base[0], frame->base[0],
              out_frame->pitches[0], frame->pitches[0],
              frame->width * 2, frame->height, &this->params[0]);
    }

#ifdef ARCH_X86
    if (xine_mm_accel() & MM_ACCEL_X86_MMX)
        asm volatile ("emms\n\t");
    if (xine_mm_accel() & MM_ACCEL_X86_MMXEXT)
        asm volatile ("sfence\n\t");
#endif

    pthread_mutex_unlock (&this->lock);
    skip = out_frame->draw(out_frame, stream);
    _x_post_frame_copy_up(frame, out_frame);

    out_frame->free(out_frame);
    frame->free(frame);
    return skip;
}
