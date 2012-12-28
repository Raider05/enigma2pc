/*
 * Copyright (C) 2003 by Dirk Meyer
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 * The code is taken from xine-ui/src/xitk/post.c at changed to work with fbxine
 *
 * Modified for VDR xineliboutput plugin by Petri Hintukainen, 2006
 *   - runtime re-configuration (load/unload, enable/disable)
 *   - support for multiple streams
 *   - support for mosaico post plugin (picture-in-picture)
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "post.h"

#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>
#include <syslog.h>

#define LOG_MODULENAME "[xine-post] "
//#include "../logdefs.h"

#define fe_t post_plugins_t

typedef struct {
  xine_post_t                 *post;
  xine_post_api_t             *api;
  xine_post_api_descr_t       *descr;
  xine_post_api_parameter_t   *param;
  char                        *param_data;

  int                          x;
  int                          y;

  int                          readonly;

  char                       **properties_names;
} post_object_t;


static int __pplugin_retrieve_parameters(post_object_t *pobj)
{
  xine_post_in_t             *input_api;

  if((input_api = (xine_post_in_t *) xine_post_input(pobj->post,
                                                     "parameters"))) {
    xine_post_api_t            *post_api;
    xine_post_api_descr_t      *api_descr;
    xine_post_api_parameter_t  *parm;
    int                         pnum = 0;

    post_api = (xine_post_api_t *) input_api->data;

    api_descr = post_api->get_param_descr();

    parm = api_descr->parameter;
    pobj->param_data = (char*)malloc(api_descr->struct_size);

    while(parm->type != POST_PARAM_TYPE_LAST) {

      post_api->get_parameters(pobj->post, pobj->param_data);

      if(!pnum)
        pobj->properties_names = (char **) calloc(2, sizeof(char *));
      else
        pobj->properties_names = (char **)
          realloc(pobj->properties_names, sizeof(char *) * (pnum + 2));

      pobj->properties_names[pnum]     = strdup(parm->name);
      pobj->properties_names[pnum + 1] = NULL;
      pnum++;
      parm++;
    }

    pobj->api      = post_api;
    pobj->descr    = api_descr;
    pobj->param    = api_descr->parameter;

    return 1;
  }

  return 0;
}

static void _pplugin_update_parameter(post_object_t *pobj)
{
  pobj->api->set_parameters(pobj->post, pobj->param_data);
  pobj->api->get_parameters(pobj->post, pobj->param_data);
}

static void __pplugin_update_parameters(xine_post_t *post, char *args)
{
  char *p;
  post_object_t pobj = {
    post = post,
  };

  if(__pplugin_retrieve_parameters(&pobj)) {
    int   i;

    if(pobj.properties_names && args && *args) {
      char *param;

      while((param = xine_strsep(&args, ",")) != NULL) {

        p = param;

        while((*p != '\0') && (*p != '='))
          p++;

        if(p && strlen(p)) {
          int param_num = 0;

          *p++ = '\0';

          while(pobj.properties_names[param_num]
                && strcasecmp(pobj.properties_names[param_num], param))
            param_num++;

          if(pobj.properties_names[param_num]) {

            pobj.param    = pobj.descr->parameter;
            pobj.param    += param_num;
            pobj.readonly = pobj.param->readonly;

            switch(pobj.param->type) {
            case POST_PARAM_TYPE_INT:
              if(!pobj.readonly) {
                if(pobj.param->enum_values) {
                  char **values = pobj.param->enum_values;
                  int    i = 0;

                  while(values[i]) {
                    if(!strcasecmp(values[i], p)) {
                      *(int *)(pobj.param_data + pobj.param->offset) = i;
                      break;
                    }
                    i++;
                  }

                  if( !values[i] )
                    *(int *)(pobj.param_data + pobj.param->offset) = (int) strtol(p, &p, 10);
                } else {
                  *(int *)(pobj.param_data + pobj.param->offset) = (int) strtol(p, &p, 10);
                }
                _pplugin_update_parameter(&pobj);
              }
              break;

            case POST_PARAM_TYPE_DOUBLE:
              if(!pobj.readonly) {
                *(double *)(pobj.param_data + pobj.param->offset) = strtod(p, &p);
                _pplugin_update_parameter(&pobj);
              }
              break;

            case POST_PARAM_TYPE_CHAR:
            case POST_PARAM_TYPE_STRING:
              if(!pobj.readonly) {
                if(pobj.param->type == POST_PARAM_TYPE_CHAR) {
                  int maxlen = pobj.param->size / sizeof(char);

                  snprintf((char *)(pobj.param_data + pobj.param->offset), maxlen, "%s", p);
                  _pplugin_update_parameter(&pobj);
                }
                else
                  fprintf(stderr, "parameter type POST_PARAM_TYPE_STRING not supported yet.\n");
              }
              break;

            case POST_PARAM_TYPE_STRINGLIST: /* unsupported */
              if(!pobj.readonly)
                fprintf(stderr, "parameter type POST_PARAM_TYPE_STRINGLIST not supported yet.\n");
              break;

            case POST_PARAM_TYPE_BOOL:
              if(!pobj.readonly) {
                *(int *)(pobj.param_data + pobj.param->offset) = ((int) strtol(p, &p, 10)) ? 1 : 0;
                _pplugin_update_parameter(&pobj);
              }
              break;
            case POST_PARAM_TYPE_LAST:
              break; /* terminator of parameter list */
            default:
              printf("%s(%d): Unknown post parameter type %d!\n", __FUNCTION__, __LINE__, pobj.param->type);
            }
          } else {
            printf("Unknown post plugin parameter %s !\n", param);
          }
        }
      }

      i = 0;

      while(pobj.properties_names[i]) {
        free(pobj.properties_names[i]);
        i++;
      }

      free(pobj.properties_names);
    }

    free(pobj.param_data);
  }
}

/* -post <name>:option1=value1,option2=value2... */
static post_element_t **pplugin_parse_and_load(fe_t *fe,
                                               int plugin_type,
                                               const char *pchain,
                                               int *post_elements_num)
{
  post_element_t **post_elements = NULL;

  *post_elements_num = 0;

  if(pchain && strlen(pchain)) {
    char *post_chain, *freeme, *p;

    freeme = post_chain = strdup(pchain);

    while((p = xine_strsep(&post_chain, ";"))) {

      if(p && strlen(p)) {
        char          *plugin, *args = NULL;
        xine_post_t   *post;

        while(*p == ' ')
          p++;

        plugin = strdup(p);

        if((p = strchr(plugin, ':')))
          *p++ = '\0';

        if(p && (strlen(p) > 1))
          args = p;
#if 0
        post = xine_post_init(fe->xine, plugin, 0,
                              &fe->audio_port, &fe->video_port);
#else
        if(plugin_type == XINE_POST_TYPE_VIDEO_COMPOSE) {
          post = xine_post_init(fe->xine, plugin, 2,
                                &fe->audio_port, &fe->video_port);
        } else
          post = xine_post_init(fe->xine, plugin, 0,
                                &fe->audio_port, &fe->video_port);
#endif

        if (post && plugin_type) {
          if (post->type != plugin_type) {
            xine_post_dispose(fe->xine, post);
            post = NULL;
          }
        }

        if(post) {

          if(!(*post_elements_num))
            post_elements = (post_element_t **) calloc(2, sizeof(post_element_t *));
          else
            post_elements = (post_element_t **)
              realloc(post_elements, sizeof(post_element_t *) * ((*post_elements_num) + 2));

          post_elements[(*post_elements_num)] = (post_element_t *) calloc(1, sizeof(post_element_t));
          post_elements[(*post_elements_num)]->post = post;
          post_elements[(*post_elements_num)]->name = strdup(plugin);
#if 1
          post_elements[(*post_elements_num)]->args = args ? strdup(args) : NULL;
          post_elements[(*post_elements_num)]->enable = 0;
#endif
          (*post_elements_num)++;
          post_elements[(*post_elements_num)] = NULL;

          __pplugin_update_parameters(post, args);
        }

        free(plugin);
      }
    }
    free(freeme);
  }

  return post_elements;
}

void pplugin_parse_and_store_post(fe_t *fe, int plugin_type,
                                  const char *post_chain)
{
  post_element_t ***_post_elements;
  int *_post_elements_num;
  post_element_t **posts = NULL;
  int              num;

  switch(plugin_type) {
    case XINE_POST_TYPE_VIDEO_FILTER:
      _post_elements = &fe->post_video_elements;
      _post_elements_num = &fe->post_video_elements_num;
      break;
    case XINE_POST_TYPE_VIDEO_COMPOSE:
      _post_elements = &fe->post_pip_elements;
      _post_elements_num = &fe->post_pip_elements_num;
      break;
    case XINE_POST_TYPE_AUDIO_VISUALIZATION:
      _post_elements = &fe->post_vis_elements;
      _post_elements_num = &fe->post_vis_elements_num;
      break;
    default:
      _post_elements = &fe->post_audio_elements;
      _post_elements_num = &fe->post_audio_elements_num;
      break;
  }

  if((posts = pplugin_parse_and_load(fe, plugin_type, post_chain, &num))) {
    if(*_post_elements_num) {
      int i;
      int ptot = *_post_elements_num + num;

      *_post_elements = (post_element_t **) realloc(*_post_elements,
                                                    sizeof(post_element_t *) * (ptot + 1));
      for(i = *_post_elements_num; i <  ptot; i++)
        (*_post_elements)[i] = posts[i - *_post_elements_num];

      (*_post_elements)[i] = NULL;
      (*_post_elements_num) += num;
    }
    else {
      *_post_elements     = posts;
      *_post_elements_num = num;
    }
#if 1
//    if(SysLogLevel > 2) {
      /* dump list of all loaded plugins */
      int ptot = *_post_elements_num;
      int i;
      char s[4096]="";
      for(i=0; i<ptot; i++)
        if((*_post_elements)[i])
          if(((*_post_elements)[i])->post) {
            if(((*_post_elements)[i])->enable)
              strcat(s, "*");
            if(((*_post_elements)[i])->name)
              strcat(s, ((*_post_elements)[i])->name);
            else
              strcat(s, "<no name!>");
            strcat(s, " ");
            }
	    printf("    loaded plugins (type %d.%d): %s\n",
              (plugin_type>>16), (plugin_type&0xffff), s);
//    }
#endif
  }
}


void vpplugin_parse_and_store_post(fe_t *fe, const char *post_chain)
{
  pplugin_parse_and_store_post(fe, XINE_POST_TYPE_VIDEO_FILTER, post_chain);
  pplugin_parse_and_store_post(fe, XINE_POST_TYPE_VIDEO_COMPOSE, post_chain);
}


void applugin_parse_and_store_post(fe_t *fe, const char *post_chain)
{
  pplugin_parse_and_store_post(fe, XINE_POST_TYPE_AUDIO_FILTER, post_chain);
  pplugin_parse_and_store_post(fe, XINE_POST_TYPE_AUDIO_VISUALIZATION, post_chain);
}


static void _vpplugin_unwire(fe_t *fe)
{
  xine_post_out_t  *vo_source;
  vo_source = xine_get_video_source(fe->video_source);
  (void) xine_post_wire_video_port(vo_source, fe->video_port);
}


static void _applugin_unwire(fe_t *fe)
{
  xine_post_out_t  *ao_source;
  ao_source = xine_get_audio_source(fe->audio_source);
  (void) xine_post_wire_audio_port(ao_source, fe->audio_port);
}


static void _vpplugin_rewire_from_post_elements(fe_t *fe, post_element_t **post_elements, int post_elements_num)
{
  if(post_elements_num) {
    xine_post_out_t   *vo_source;
    int                i = 0;

    for(i = (post_elements_num - 1); i >= 0; i--) {
      const char *const *outs = xine_post_list_outputs(post_elements[i]->post);
      const xine_post_out_t *vo_out = xine_post_output(post_elements[i]->post, (char *) *outs);
      if(i == (post_elements_num - 1)) {
        printf("        wiring %10s[out] -> [in]video_out\n", post_elements[i]->name);
        xine_post_wire_video_port((xine_post_out_t *) vo_out, fe->video_port);
      }
      else {
        const xine_post_in_t *vo_in;
        int                   err;

        /* look for standard input names */
        vo_in = xine_post_input(post_elements[i + 1]->post, "video");
        if( !vo_in )
          vo_in = xine_post_input(post_elements[i + 1]->post, "video in");

        printf("        wiring %10s[out] -> [in]%-10s \n",
               post_elements[i]->name, post_elements[i+1]->name);
        err = xine_post_wire((xine_post_out_t *) vo_out,
                             (xine_post_in_t *) vo_in);
      }
    }

    if(fe->post_pip_enable &&
       !strcmp(post_elements[0]->name, "mosaico") &&
       fe->pip_stream) {
      vo_source = xine_get_video_source(fe->pip_stream);
      printf("        wiring %10s[out] -> [in1]%-10s ", "pip stream\n", post_elements[0]->name);
      xine_post_wire_video_port(vo_source,
                                post_elements[0]->post->video_input[1]);
    }

    vo_source = xine_get_video_source(fe->video_source);
    printf("        wiring %10s[out] -> [in]%-10s", "stream\n", post_elements[0]->name);
    xine_post_wire_video_port(vo_source,
                              post_elements[0]->post->video_input[0]);
  }
}


static void _applugin_rewire_from_post_elements(fe_t *fe, post_element_t **post_elements, int post_elements_num)
{
  if(post_elements_num) {
    xine_post_out_t   *ao_source;
    int                i = 0;

    for(i = (post_elements_num - 1); i >= 0; i--) {
      const char *const *outs = xine_post_list_outputs(post_elements[i]->post);
      const xine_post_out_t *ao_out = xine_post_output(post_elements[i]->post, (char *) *outs);

      if(i == (post_elements_num - 1)) {
        printf("        wiring %10s[out] -> [in]audio_out\n", post_elements[i]->name);
        xine_post_wire_audio_port((xine_post_out_t *) ao_out, fe->audio_port);
      }
      else {
        const xine_post_in_t *ao_in;
        int                   err;

        /* look for standard input names */
        ao_in = xine_post_input(post_elements[i + 1]->post, "audio");
        if( !ao_in )
          ao_in = xine_post_input(post_elements[i + 1]->post, "audio in");

        printf("        wiring %10s[out] -> [in]%-10s \n",
               post_elements[i]->name, post_elements[i+1]->name);
        err = xine_post_wire((xine_post_out_t *) ao_out, (xine_post_in_t *) ao_in);
      }
    }

    ao_source = xine_get_audio_source(fe->audio_source);
    printf("        wiring %10s[out] -> [in]%-10s", "stream", post_elements[0]->name);
    xine_post_wire_audio_port(ao_source, post_elements[0]->post->audio_input[0]);
  }
}

static post_element_t **_pplugin_join_deinterlace_and_post_elements(fe_t *fe, int *post_elements_num)
{
  post_element_t **post_elements;
  int i = 0, j = 0, n = 0, p = 0;
  static const char *order[] = {"autocrop", "thread", "tvtime", "swscale", NULL};

  *post_elements_num = 0;
  if( fe->post_video_enable )
    *post_elements_num += fe->post_video_elements_num;

  if( fe->post_pip_enable )
    *post_elements_num += fe->post_pip_elements_num;

  if( *post_elements_num == 0 )
    return NULL;

  post_elements = (post_element_t**) calloc( (*post_elements_num), sizeof(post_element_t *));

  if(fe->post_pip_enable)
    for( i = 0; i < fe->post_pip_elements_num; i++ ) {
      if(fe->post_pip_elements[i]->enable)
        post_elements[i+j-n] = fe->post_pip_elements[i];
      else
        n++;
    }

  if(fe->post_video_enable)
    for( j = 0; j < fe->post_video_elements_num; j++ ) {
      if(fe->post_video_elements[j]->enable) {
        post_elements[i+j-n] = fe->post_video_elements[j];
      } else
        n++;
    }

  *post_elements_num -= n;
  if( *post_elements_num == 0 ) {
    free(post_elements);
    return NULL;
  }

  /* in some special cases order is important. By default plugin order
   * in post plugin chain is post plugin loading order.
   * But, we want:
   *
   *   1. autocrop       - less data to process for other plugins
   *                     - accepts only YV12
   *   2. deinterlace    - blur etc. makes deinterlacing difficult.
   *                     - upscales chroma (YV12->YUY2) in some modes.
   *   3. anything else
   *
   * So let's move those two to beginning ...
   */
  n = 0;
  while(order[p]) {
    for(i = 0; i<*post_elements_num; i++)
      if(!strcmp(post_elements[i]->name, order[p])) {
        if(i != n) {
          post_element_t *tmp = post_elements[i];
          for(j=i; j>n; j--)
            post_elements[j] = post_elements[j-1];
          post_elements[n] = tmp;
          printf("      moved %s to post slot %d\n", order[p], n);
        }
        n++;
        break;
      }
    p++;
  }

  return post_elements;
}

static post_element_t **_pplugin_join_visualization_and_post_elements(fe_t *fe, int *post_elements_num)
{
  post_element_t **post_elements;
  int i = 0, j = 0, n = 0;

  *post_elements_num = 0;
  if( fe->post_audio_enable )
    *post_elements_num += fe->post_audio_elements_num;

  if( fe->post_vis_enable )
    *post_elements_num += fe->post_vis_elements_num;

  if( *post_elements_num == 0 )
    return NULL;

  post_elements = (post_element_t**) calloc( (*post_elements_num), sizeof(post_element_t *));

  if(fe->post_audio_enable)
    for( j = 0; j < fe->post_audio_elements_num; j++ ) {
      if(fe->post_audio_elements[j]->enable)
        post_elements[i+j-n] = fe->post_audio_elements[j];
      else
        n++;
    }

  if(fe->post_vis_enable)
    for( i = 0; i < fe->post_vis_elements_num; i++ ) {
      if(fe->post_vis_elements[i]->enable)
        post_elements[i+j-n] = fe->post_vis_elements[i];
      else
        n++;
    }

  *post_elements_num -= n;
  if( *post_elements_num == 0 ) {
    free(post_elements);
    return NULL;
  }

  return post_elements;
}

static void _vpplugin_rewire(fe_t *fe)
{
  static post_element_t **post_elements;
  int post_elements_num;

  post_elements = _pplugin_join_deinterlace_and_post_elements(fe, &post_elements_num);

  if( post_elements ) {
    _vpplugin_rewire_from_post_elements(fe, post_elements, post_elements_num);

    free(post_elements);
  }
}

static void _applugin_rewire(fe_t *fe)
{
  static post_element_t **post_elements;
  int post_elements_num;

  post_elements = _pplugin_join_visualization_and_post_elements(fe, &post_elements_num);

  if( post_elements ) {
    _applugin_rewire_from_post_elements(fe, post_elements, post_elements_num);

    free(post_elements);
  }
}

void vpplugin_rewire_posts(fe_t *fe)
{
  /*TRACELINE;*/
  _vpplugin_unwire(fe);
  _vpplugin_rewire(fe);
}

void applugin_rewire_posts(fe_t *fe)
{
  /*TRACELINE;*/
  _applugin_unwire(fe);
  _applugin_rewire(fe);
}

static int _pplugin_enable_post(post_plugins_t *fe, const char *name,
                                const char *args,
                                post_element_t **post_elements,
                                int post_elements_num,
                                int *found)
{
  int i, result = 0;

  for(i=0; i<post_elements_num; i++)
    if(post_elements[i])
      if(!strcmp(post_elements[i]->name, name)) {
        if(post_elements[i]->enable == 0) {
          post_elements[i]->enable = 1;
          result = 1;
        }

        *found = 1;

        if(args && *args) {
          if(post_elements[i]->enable != 2) {
            char *tmp = strdup(args);
            __pplugin_update_parameters(post_elements[i]->post, tmp);
            free(tmp);
            if(post_elements[i]->args)
              free(post_elements[i]->args);
            post_elements[i]->args = strdup(args);
          } else {
            printf("  * enable post %s, parameters fixed in command line.\n", name);
            printf("      requested: %s\n", args ? : "none");
            printf("      using    : %s\n", post_elements[i]->args ? : "none");
			printf(" * enable post \n");
          }
        }
      }

  return result;
}

static int _vpplugin_enable_post(post_plugins_t *fe, const char *name,
                                 const char *args, int *found)
{
  int result = 0;
  if(!*found)
    result = _pplugin_enable_post(fe, name, args, fe->post_video_elements,
                                  fe->post_video_elements_num, found);
  if(!*found)
    result = _pplugin_enable_post(fe, name, args, fe->post_pip_elements,
                                  fe->post_pip_elements_num, found);
  return result;
}

static int _applugin_enable_post(post_plugins_t *fe, const char *name,
                                 const char *args, int *found)
{
  int result = 0;
  if(!*found)
    result = _pplugin_enable_post(fe, name, args, fe->post_audio_elements,
                                  fe->post_audio_elements_num, found);
  if(!*found)
    result = _pplugin_enable_post(fe, name, args, fe->post_vis_elements,
                                  fe->post_vis_elements_num, found);
  return result;
}

static char * _pp_name_strdup(const char *initstr)
{
  char *name = strdup(initstr), *pt;

  if(NULL != (pt = strchr(name, ':')))
    *pt = 0;

  return name;
}

static const char * _pp_args(const char *initstr)
{
  char *pt = (char*)strchr(initstr, ':');
  if(pt && *(pt+1))
    return pt+1;
  return NULL;
}

int vpplugin_enable_post(post_plugins_t *fe, const char *initstr,
                         int *found)
{
  char *name = _pp_name_strdup(initstr);
  const char *args = _pp_args(initstr);

  int result = _vpplugin_enable_post(fe, name, args, found);

  printf("  * enable post %s --> %s, %s\n", name,
         *found ? "found"   : "not found",
         result ? "enabled" : "no action");

  if(!*found) {
    printf("  * loading post %s\n", initstr);
    vpplugin_parse_and_store_post(fe, initstr);
    result = _vpplugin_enable_post(fe, name, NULL, found);

    printf("  * enable post %s --> %s, %s\n", name,
           *found ? "found"   : "not found",
           result ? "enabled" : "no action");
  }

  if(result)
    _vpplugin_unwire(fe);

  free(name);
  return result;
}

int applugin_enable_post(post_plugins_t *fe, const char *initstr,
                         int *found)
{
  const char * args = _pp_args(initstr);
  char *name = _pp_name_strdup(initstr);

  int result = _applugin_enable_post(fe, name, args, found);

  printf("  * enable post %s --> %s, %s\n", name,
         *found ? "found"   : "not found",
         result ? "enabled" : "no action");

  if(!*found) {
    printf("  * loading post %s\n", initstr);
    applugin_parse_and_store_post(fe, initstr);
    result = _applugin_enable_post(fe, name, NULL, found);
    printf("  * enable post %s --> %s, %s\n", name,
           *found ? "found"   : "not found",
           result ? "enabled" : "no action");
  }

  if(result)
    _applugin_unwire(fe);

  free(name);
  return result;
}

static int _pplugin_disable_post(post_plugins_t *fe, const char *name,
                                 post_element_t **post_elements,
                                 int post_elements_num)
{
  int i, result = 0;
  /*TRACELINE;*/
  if(post_elements)
    for(i = 0; i < post_elements_num; i++)
      if(post_elements[i])
        if(!name || !strcmp(post_elements[i]->name, name))
          if(post_elements[i]->enable == 1) {
            post_elements[i]->enable = 0;
            result = 1;
          }
  return result;
}

int vpplugin_disable_post(post_plugins_t *fe, const char *name)
{
  /*TRACELINE;*/
  if(_pplugin_disable_post(fe, name, fe->post_video_elements,
                           fe->post_video_elements_num) ||
     _pplugin_disable_post(fe, name, fe->post_pip_elements,
                           fe->post_pip_elements_num)) {
    _vpplugin_unwire(fe);
    return 1;
  }
  return 0;
}

int applugin_disable_post(post_plugins_t *fe, const char *name)
{
  /*TRACELINE;*/
  if(_pplugin_disable_post(fe, name, fe->post_audio_elements,
                           fe->post_audio_elements_num) ||
     _pplugin_disable_post(fe, name, fe->post_vis_elements,
                           fe->post_vis_elements_num)) {
    _applugin_unwire(fe);
    return 1;
  }
  return 0;
}

static int _pplugin_unload_post(post_plugins_t *fe, const char *name,
                                post_element_t ***post_elements,
                                int *post_elements_num)
{
  /* does not unwrire plugins ! */
  int i, j, result = 0;
  /*TRACELINE;*/

  if(!*post_elements || !*post_elements_num)
    return 0;

  for(i=0; i < *post_elements_num; i++) {
    if((*post_elements)[i]) {
      if(!name || !strcmp((*post_elements)[i]->name, name)) {

        if((*post_elements)[i]->enable == 0 || !name) {
          xine_post_dispose(fe->xine, (*post_elements)[i]->post);

          free((*post_elements)[i]->name);

          if((*post_elements)[i]->args)
            free((*post_elements)[i]->args);

          free((*post_elements)[i]);

          for(j=i; j < *post_elements_num - 1; j++)
            (*post_elements)[j] = (*post_elements)[j+1];

          (*post_elements_num) --;
          (*post_elements)[(*post_elements_num)] = 0;

          result = 1;
          i--;

        } else {
          printf("Unload %s failed: plugin enabled and in use\n",
                 (*post_elements)[i]->name);
        }
      }
    }
  }

  if(*post_elements_num <= 0) {
    if(*post_elements)
      free(*post_elements);
    *post_elements = NULL;
  }

  return result;
}

int vpplugin_unload_post(post_plugins_t *fe, const char *name)
{
  int result = vpplugin_disable_post(fe, name);

  /* unload already disabled plugins too (result=0) */
  _pplugin_unload_post(fe, name, &fe->post_video_elements,
                       &fe->post_video_elements_num);
  _pplugin_unload_post(fe, name, &fe->post_pip_elements,
                       &fe->post_pip_elements_num);

  /* result indicates only unwiring condition, not unload result */
  return result;
}

int applugin_unload_post(post_plugins_t *fe, const char *name)
{
  int result = applugin_disable_post(fe, name);

  /* unload already disabled plugins too (result=0) */
  _pplugin_unload_post(fe, name, &fe->post_audio_elements,
                       &fe->post_audio_elements_num);
  _pplugin_unload_post(fe, name, &fe->post_vis_elements,
                       &fe->post_vis_elements_num);

  /* result indicates only unwiring condition, not unload result */
  return result;
}


/* end of post.c */

