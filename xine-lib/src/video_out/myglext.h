#ifndef __myglext_h_
#define __myglext_h_

#ifdef __cplusplus
extern "C" {
#endif

/*
** License Applicability. Except to the extent portions of this file are
** made subject to an alternative license as permitted in the SGI Free
** Software License B, Version 1.1 (the "License"), the contents of this
** file are subject only to the provisions of the License. You may not use
** this file except in compliance with the License. You may obtain a copy
** of the License at Silicon Graphics, Inc., attn: Legal Services, 1600
** Amphitheatre Parkway, Mountain View, CA 94043-1351, or at:
**
** http://oss.sgi.com/projects/FreeB
**
** Note that, as provided in the License, the Software is distributed on an
** "AS IS" basis, with ALL EXPRESS AND IMPLIED WARRANTIES AND CONDITIONS
** DISCLAIMED, INCLUDING, WITHOUT LIMITATION, ANY IMPLIED WARRANTIES AND
** CONDITIONS OF MERCHANTABILITY, SATISFACTORY QUALITY, FITNESS FOR A
** PARTICULAR PURPOSE, AND NON-INFRINGEMENT.
**
** Original Code. The Original Code is: OpenGL Sample Implementation,
** Version 1.2.1, released January 26, 2000, developed by Silicon Graphics,
** Inc. The Original Code is Copyright (c) 1991-2004 Silicon Graphics, Inc.
** Copyright in any portions created by third parties is as indicated
** elsewhere herein. All Rights Reserved.
**
** Additional Notice Provisions: This software was created using the
** OpenGL(R) version 1.2.1 Sample Implementation published by SGI, but has
** not been independently verified as being compliant with the OpenGL(R)
** version 1.2.1 Specification.
*/

#if defined(_WIN32) && !defined(APIENTRY) && !defined(__CYGWIN__) && !defined(__SCITECH_SNAP__)
#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#endif

#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef APIENTRYP
#define APIENTRYP APIENTRY *
#endif
#ifndef GLAPI
#define GLAPI extern
#endif

/*************************************************************/

/* Header file version number, required by OpenGL ABI for Linux */
/* myglext.h last updated 2005/03/17 */
/* Current version at http://oss.sgi.com/projects/ogl-sample/registry/ */
#define MYGL_GLEXT_VERSION 27

#ifndef MYGL_VERSION_1_2
#define MYGL_UNSIGNED_BYTE_3_3_2            0x8032
#define MYGL_UNSIGNED_SHORT_4_4_4_4         0x8033
#define MYGL_UNSIGNED_SHORT_5_5_5_1         0x8034
#define MYGL_UNSIGNED_INT_8_8_8_8           0x8035
#define MYGL_UNSIGNED_INT_10_10_10_2        0x8036
#define MYGL_RESCALE_NORMAL                 0x803A
#define MYGL_TEXTURE_BINDING_3D             0x806A
#define MYGL_PACK_SKIP_IMAGES               0x806B
#define MYGL_PACK_IMAGE_HEIGHT              0x806C
#define MYGL_UNPACK_SKIP_IMAGES             0x806D
#define MYGL_UNPACK_IMAGE_HEIGHT            0x806E
#define MYGL_TEXTURE_3D                     0x806F
#define MYGL_PROXY_TEXTURE_3D               0x8070
#define MYGL_TEXTURE_DEPTH                  0x8071
#define MYGL_TEXTURE_WRAP_R                 0x8072
#define MYGL_MAX_3D_TEXTURE_SIZE            0x8073
#define MYGL_UNSIGNED_BYTE_2_3_3_REV        0x8362
#define MYGL_UNSIGNED_SHORT_5_6_5           0x8363
#define MYGL_UNSIGNED_SHORT_5_6_5_REV       0x8364
#define MYGL_UNSIGNED_SHORT_4_4_4_4_REV     0x8365
#define MYGL_UNSIGNED_SHORT_1_5_5_5_REV     0x8366
#define MYGL_UNSIGNED_INT_8_8_8_8_REV       0x8367
#define MYGL_UNSIGNED_INT_2_10_10_10_REV    0x8368
#define MYGL_BGR                            0x80E0
#define MYGL_BGRA                           0x80E1
#define MYGL_MAX_ELEMENTS_VERTICES          0x80E8
#define MYGL_MAX_ELEMENTS_INDICES           0x80E9
#define MYGL_CLAMP_TO_EDGE                  0x812F
#define MYGL_TEXTURE_MIN_LOD                0x813A
#define MYGL_TEXTURE_MAX_LOD                0x813B
#define MYGL_TEXTURE_BASE_LEVEL             0x813C
#define MYGL_TEXTURE_MAX_LEVEL              0x813D
#define MYGL_LIGHT_MODEL_COLOR_CONTROL      0x81F8
#define MYGL_SINGLE_COLOR                   0x81F9
#define MYGL_SEPARATE_SPECULAR_COLOR        0x81FA
#define MYGL_SMOOTH_POINT_SIZE_RANGE        0x0B12
#define MYGL_SMOOTH_POINT_SIZE_GRANULARITY  0x0B13
#define MYGL_SMOOTH_LINE_WIDTH_RANGE        0x0B22
#define MYGL_SMOOTH_LINE_WIDTH_GRANULARITY  0x0B23
#define MYGL_ALIASED_POINT_SIZE_RANGE       0x846D
#define MYGL_ALIASED_LINE_WIDTH_RANGE       0x846E
#endif

#ifndef MYGL_ARB_imaging
#define MYGL_CONSTANT_COLOR                 0x8001
#define MYGL_ONE_MINUS_CONSTANT_COLOR       0x8002
#define MYGL_CONSTANT_ALPHA                 0x8003
#define MYGL_ONE_MINUS_CONSTANT_ALPHA       0x8004
#define MYGL_BLEND_COLOR                    0x8005
#define MYGL_FUNC_ADD                       0x8006
#define MYGL_MIN                            0x8007
#define MYGL_MAX                            0x8008
#define MYGL_BLEND_EQUATION                 0x8009
#define MYGL_FUNC_SUBTRACT                  0x800A
#define MYGL_FUNC_REVERSE_SUBTRACT          0x800B
#define MYGL_CONVOLUTION_1D                 0x8010
#define MYGL_CONVOLUTION_2D                 0x8011
#define MYGL_SEPARABLE_2D                   0x8012
#define MYGL_CONVOLUTION_BORDER_MODE        0x8013
#define MYGL_CONVOLUTION_FILTER_SCALE       0x8014
#define MYGL_CONVOLUTION_FILTER_BIAS        0x8015
#define MYGL_REDUCE                         0x8016
#define MYGL_CONVOLUTION_FORMAT             0x8017
#define MYGL_CONVOLUTION_WIDTH              0x8018
#define MYGL_CONVOLUTION_HEIGHT             0x8019
#define MYGL_MAX_CONVOLUTION_WIDTH          0x801A
#define MYGL_MAX_CONVOLUTION_HEIGHT         0x801B
#define MYGL_POST_CONVOLUTION_RED_SCALE     0x801C
#define MYGL_POST_CONVOLUTION_GREEN_SCALE   0x801D
#define MYGL_POST_CONVOLUTION_BLUE_SCALE    0x801E
#define MYGL_POST_CONVOLUTION_ALPHA_SCALE   0x801F
#define MYGL_POST_CONVOLUTION_RED_BIAS      0x8020
#define MYGL_POST_CONVOLUTION_GREEN_BIAS    0x8021
#define MYGL_POST_CONVOLUTION_BLUE_BIAS     0x8022
#define MYGL_POST_CONVOLUTION_ALPHA_BIAS    0x8023
#define MYGL_HISTOGRAM                      0x8024
#define MYGL_PROXY_HISTOGRAM                0x8025
#define MYGL_HISTOGRAM_WIDTH                0x8026
#define MYGL_HISTOGRAM_FORMAT               0x8027
#define MYGL_HISTOGRAM_RED_SIZE             0x8028
#define MYGL_HISTOGRAM_GREEN_SIZE           0x8029
#define MYGL_HISTOGRAM_BLUE_SIZE            0x802A
#define MYGL_HISTOGRAM_ALPHA_SIZE           0x802B
#define MYGL_HISTOGRAM_LUMINANCE_SIZE       0x802C
#define MYGL_HISTOGRAM_SINK                 0x802D
#define MYGL_MINMAX                         0x802E
#define MYGL_MINMAX_FORMAT                  0x802F
#define MYGL_MINMAX_SINK                    0x8030
#define MYGL_TABLE_TOO_LARGE                0x8031
#define MYGL_COLOR_MATRIX                   0x80B1
#define MYGL_COLOR_MATRIX_STACK_DEPTH       0x80B2
#define MYGL_MAX_COLOR_MATRIX_STACK_DEPTH   0x80B3
#define MYGL_POST_COLOR_MATRIX_RED_SCALE    0x80B4
#define MYGL_POST_COLOR_MATRIX_GREEN_SCALE  0x80B5
#define MYGL_POST_COLOR_MATRIX_BLUE_SCALE   0x80B6
#define MYGL_POST_COLOR_MATRIX_ALPHA_SCALE  0x80B7
#define MYGL_POST_COLOR_MATRIX_RED_BIAS     0x80B8
#define MYGL_POST_COLOR_MATRIX_GREEN_BIAS   0x80B9
#define MYGL_POST_COLOR_MATRIX_BLUE_BIAS    0x80BA
#define MYGL_POST_COLOR_MATRIX_ALPHA_BIAS   0x80BB
#define MYGL_COLOR_TABLE                    0x80D0
#define MYGL_POST_CONVOLUTION_COLOR_TABLE   0x80D1
#define MYGL_POST_COLOR_MATRIX_COLOR_TABLE  0x80D2
#define MYGL_PROXY_COLOR_TABLE              0x80D3
#define MYGL_PROXY_POST_CONVOLUTION_COLOR_TABLE 0x80D4
#define MYGL_PROXY_POST_COLOR_MATRIX_COLOR_TABLE 0x80D5
#define MYGL_COLOR_TABLE_SCALE              0x80D6
#define MYGL_COLOR_TABLE_BIAS               0x80D7
#define MYGL_COLOR_TABLE_FORMAT             0x80D8
#define MYGL_COLOR_TABLE_WIDTH              0x80D9
#define MYGL_COLOR_TABLE_RED_SIZE           0x80DA
#define MYGL_COLOR_TABLE_GREEN_SIZE         0x80DB
#define MYGL_COLOR_TABLE_BLUE_SIZE          0x80DC
#define MYGL_COLOR_TABLE_ALPHA_SIZE         0x80DD
#define MYGL_COLOR_TABLE_LUMINANCE_SIZE     0x80DE
#define MYGL_COLOR_TABLE_INTENSITY_SIZE     0x80DF
#define MYGL_CONSTANT_BORDER                0x8151
#define MYGL_REPLICATE_BORDER               0x8153
#define MYGL_CONVOLUTION_BORDER_COLOR       0x8154
#endif

#ifndef MYGL_VERSION_1_3
#define MYGL_TEXTURE0                       0x84C0
#define MYGL_TEXTURE1                       0x84C1
#define MYGL_TEXTURE2                       0x84C2
#define MYGL_TEXTURE3                       0x84C3
#define MYGL_TEXTURE4                       0x84C4
#define MYGL_TEXTURE5                       0x84C5
#define MYGL_TEXTURE6                       0x84C6
#define MYGL_TEXTURE7                       0x84C7
#define MYGL_TEXTURE8                       0x84C8
#define MYGL_TEXTURE9                       0x84C9
#define MYGL_TEXTURE10                      0x84CA
#define MYGL_TEXTURE11                      0x84CB
#define MYGL_TEXTURE12                      0x84CC
#define MYGL_TEXTURE13                      0x84CD
#define MYGL_TEXTURE14                      0x84CE
#define MYGL_TEXTURE15                      0x84CF
#define MYGL_TEXTURE16                      0x84D0
#define MYGL_TEXTURE17                      0x84D1
#define MYGL_TEXTURE18                      0x84D2
#define MYGL_TEXTURE19                      0x84D3
#define MYGL_TEXTURE20                      0x84D4
#define MYGL_TEXTURE21                      0x84D5
#define MYGL_TEXTURE22                      0x84D6
#define MYGL_TEXTURE23                      0x84D7
#define MYGL_TEXTURE24                      0x84D8
#define MYGL_TEXTURE25                      0x84D9
#define MYGL_TEXTURE26                      0x84DA
#define MYGL_TEXTURE27                      0x84DB
#define MYGL_TEXTURE28                      0x84DC
#define MYGL_TEXTURE29                      0x84DD
#define MYGL_TEXTURE30                      0x84DE
#define MYGL_TEXTURE31                      0x84DF
#define MYGL_ACTIVE_TEXTURE                 0x84E0
#define MYGL_CLIENT_ACTIVE_TEXTURE          0x84E1
#define MYGL_MAX_TEXTURE_UNITS              0x84E2
#define MYGL_TRANSPOSE_MODELVIEW_MATRIX     0x84E3
#define MYGL_TRANSPOSE_PROJECTION_MATRIX    0x84E4
#define MYGL_TRANSPOSE_TEXTURE_MATRIX       0x84E5
#define MYGL_TRANSPOSE_COLOR_MATRIX         0x84E6
#define MYGL_MULTISAMPLE                    0x809D
#define MYGL_SAMPLE_ALPHA_TO_COVERAGE       0x809E
#define MYGL_SAMPLE_ALPHA_TO_ONE            0x809F
#define MYGL_SAMPLE_COVERAGE                0x80A0
#define MYGL_SAMPLE_BUFFERS                 0x80A8
#define MYGL_SAMPLES                        0x80A9
#define MYGL_SAMPLE_COVERAGE_VALUE          0x80AA
#define MYGL_SAMPLE_COVERAGE_INVERT         0x80AB
#define MYGL_MULTISAMPLE_BIT                0x20000000
#define MYGL_NORMAL_MAP                     0x8511
#define MYGL_REFLECTION_MAP                 0x8512
#define MYGL_TEXTURE_CUBE_MAP               0x8513
#define MYGL_TEXTURE_BINDING_CUBE_MAP       0x8514
#define MYGL_TEXTURE_CUBE_MAP_POSITIVE_X    0x8515
#define MYGL_TEXTURE_CUBE_MAP_NEGATIVE_X    0x8516
#define MYGL_TEXTURE_CUBE_MAP_POSITIVE_Y    0x8517
#define MYGL_TEXTURE_CUBE_MAP_NEGATIVE_Y    0x8518
#define MYGL_TEXTURE_CUBE_MAP_POSITIVE_Z    0x8519
#define MYGL_TEXTURE_CUBE_MAP_NEGATIVE_Z    0x851A
#define MYGL_PROXY_TEXTURE_CUBE_MAP         0x851B
#define MYGL_MAX_CUBE_MAP_TEXTURE_SIZE      0x851C
#define MYGL_COMPRESSED_ALPHA               0x84E9
#define MYGL_COMPRESSED_LUMINANCE           0x84EA
#define MYGL_COMPRESSED_LUMINANCE_ALPHA     0x84EB
#define MYGL_COMPRESSED_INTENSITY           0x84EC
#define MYGL_COMPRESSED_RGB                 0x84ED
#define MYGL_COMPRESSED_RGBA                0x84EE
#define MYGL_TEXTURE_COMPRESSION_HINT       0x84EF
#define MYGL_TEXTURE_COMPRESSED_IMAGE_SIZE  0x86A0
#define MYGL_TEXTURE_COMPRESSED             0x86A1
#define MYGL_NUM_COMPRESSED_TEXTURE_FORMATS 0x86A2
#define MYGL_COMPRESSED_TEXTURE_FORMATS     0x86A3
#define MYGL_CLAMP_TO_BORDER                0x812D
#define MYGL_COMBINE                        0x8570
#define MYGL_COMBINE_RGB                    0x8571
#define MYGL_COMBINE_ALPHA                  0x8572
#define MYGL_SOURCE0_RGB                    0x8580
#define MYGL_SOURCE1_RGB                    0x8581
#define MYGL_SOURCE2_RGB                    0x8582
#define MYGL_SOURCE0_ALPHA                  0x8588
#define MYGL_SOURCE1_ALPHA                  0x8589
#define MYGL_SOURCE2_ALPHA                  0x858A
#define MYGL_OPERAND0_RGB                   0x8590
#define MYGL_OPERAND1_RGB                   0x8591
#define MYGL_OPERAND2_RGB                   0x8592
#define MYGL_OPERAND0_ALPHA                 0x8598
#define MYGL_OPERAND1_ALPHA                 0x8599
#define MYGL_OPERAND2_ALPHA                 0x859A
#define MYGL_RGB_SCALE                      0x8573
#define MYGL_ADD_SIGNED                     0x8574
#define MYGL_INTERPOLATE                    0x8575
#define MYGL_SUBTRACT                       0x84E7
#define MYGL_CONSTANT                       0x8576
#define MYGL_PRIMARY_COLOR                  0x8577
#define MYGL_PREVIOUS                       0x8578
#define MYGL_DOT3_RGB                       0x86AE
#define MYGL_DOT3_RGBA                      0x86AF
#endif

#ifndef MYGL_VERSION_1_4
#define MYGL_BLEND_DST_RGB                  0x80C8
#define MYGL_BLEND_SRC_RGB                  0x80C9
#define MYGL_BLEND_DST_ALPHA                0x80CA
#define MYGL_BLEND_SRC_ALPHA                0x80CB
#define MYGL_POINT_SIZE_MIN                 0x8126
#define MYGL_POINT_SIZE_MAX                 0x8127
#define MYGL_POINT_FADE_THRESHOLD_SIZE      0x8128
#define MYGL_POINT_DISTANCE_ATTENUATION     0x8129
#define MYGL_GENERATE_MIPMAP                0x8191
#define MYGL_GENERATE_MIPMAP_HINT           0x8192
#define MYGL_DEPTH_COMPONENT16              0x81A5
#define MYGL_DEPTH_COMPONENT24              0x81A6
#define MYGL_DEPTH_COMPONENT32              0x81A7
#define MYGL_MIRRORED_REPEAT                0x8370
#define MYGL_FOG_COORDINATE_SOURCE          0x8450
#define MYGL_FOG_COORDINATE                 0x8451
#define MYGL_FRAGMENT_DEPTH                 0x8452
#define MYGL_CURRENT_FOG_COORDINATE         0x8453
#define MYGL_FOG_COORDINATE_ARRAY_TYPE      0x8454
#define MYGL_FOG_COORDINATE_ARRAY_STRIDE    0x8455
#define MYGL_FOG_COORDINATE_ARRAY_POINTER   0x8456
#define MYGL_FOG_COORDINATE_ARRAY           0x8457
#define MYGL_COLOR_SUM                      0x8458
#define MYGL_CURRENT_SECONDARY_COLOR        0x8459
#define MYGL_SECONDARY_COLOR_ARRAY_SIZE     0x845A
#define MYGL_SECONDARY_COLOR_ARRAY_TYPE     0x845B
#define MYGL_SECONDARY_COLOR_ARRAY_STRIDE   0x845C
#define MYGL_SECONDARY_COLOR_ARRAY_POINTER  0x845D
#define MYGL_SECONDARY_COLOR_ARRAY          0x845E
#define MYGL_MAX_TEXTURE_LOD_BIAS           0x84FD
#define MYGL_TEXTURE_FILTER_CONTROL         0x8500
#define MYGL_TEXTURE_LOD_BIAS               0x8501
#define MYGL_INCR_WRAP                      0x8507
#define MYGL_DECR_WRAP                      0x8508
#define MYGL_TEXTURE_DEPTH_SIZE             0x884A
#define MYGL_DEPTH_TEXTURE_MODE             0x884B
#define MYGL_TEXTURE_COMPARE_MODE           0x884C
#define MYGL_TEXTURE_COMPARE_FUNC           0x884D
#define MYGL_COMPARE_R_TO_TEXTURE           0x884E
#endif

#ifndef MYGL_VERSION_1_5
#define MYGL_BUFFER_SIZE                    0x8764
#define MYGL_BUFFER_USAGE                   0x8765
#define MYGL_QUERY_COUNTER_BITS             0x8864
#define MYGL_CURRENT_QUERY                  0x8865
#define MYGL_QUERY_RESULT                   0x8866
#define MYGL_QUERY_RESULT_AVAILABLE         0x8867
#define MYGL_ARRAY_BUFFER                   0x8892
#define MYGL_ELEMENT_ARRAY_BUFFER           0x8893
#define MYGL_ARRAY_BUFFER_BINDING           0x8894
#define MYGL_ELEMENT_ARRAY_BUFFER_BINDING   0x8895
#define MYGL_VERTEX_ARRAY_BUFFER_BINDING    0x8896
#define MYGL_NORMAL_ARRAY_BUFFER_BINDING    0x8897
#define MYGL_COLOR_ARRAY_BUFFER_BINDING     0x8898
#define MYGL_INDEX_ARRAY_BUFFER_BINDING     0x8899
#define MYGL_TEXTURE_COORD_ARRAY_BUFFER_BINDING 0x889A
#define MYGL_EDGE_FLAG_ARRAY_BUFFER_BINDING 0x889B
#define MYGL_SECONDARY_COLOR_ARRAY_BUFFER_BINDING 0x889C
#define MYGL_FOG_COORDINATE_ARRAY_BUFFER_BINDING 0x889D
#define MYGL_WEIGHT_ARRAY_BUFFER_BINDING    0x889E
#define MYGL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING 0x889F
#define MYGL_READ_ONLY                      0x88B8
#define MYGL_WRITE_ONLY                     0x88B9
#define MYGL_READ_WRITE                     0x88BA
#define MYGL_BUFFER_ACCESS                  0x88BB
#define MYGL_BUFFER_MAPPED                  0x88BC
#define MYGL_BUFFER_MAP_POINTER             0x88BD
#define MYGL_STREAM_DRAW                    0x88E0
#define MYGL_STREAM_READ                    0x88E1
#define MYGL_STREAM_COPY                    0x88E2
#define MYGL_STATIC_DRAW                    0x88E4
#define MYGL_STATIC_READ                    0x88E5
#define MYGL_STATIC_COPY                    0x88E6
#define MYGL_DYNAMIC_DRAW                   0x88E8
#define MYGL_DYNAMIC_READ                   0x88E9
#define MYGL_DYNAMIC_COPY                   0x88EA
#define MYGL_SAMPLES_PASSED                 0x8914
#define MYGL_FOG_COORD_SRC                  MYGL_FOG_COORDINATE_SOURCE
#define MYGL_FOG_COORD                      MYGL_FOG_COORDINATE
#define MYGL_CURRENT_FOG_COORD              MYGL_CURRENT_FOG_COORDINATE
#define MYGL_FOG_COORD_ARRAY_TYPE           MYGL_FOG_COORDINATE_ARRAY_TYPE
#define MYGL_FOG_COORD_ARRAY_STRIDE         MYGL_FOG_COORDINATE_ARRAY_STRIDE
#define MYGL_FOG_COORD_ARRAY_POINTER        MYGL_FOG_COORDINATE_ARRAY_POINTER
#define MYGL_FOG_COORD_ARRAY                MYGL_FOG_COORDINATE_ARRAY
#define MYGL_FOG_COORD_ARRAY_BUFFER_BINDING MYGL_FOG_COORDINATE_ARRAY_BUFFER_BINDING
#define MYGL_SRC0_RGB                       MYGL_SOURCE0_RGB
#define MYGL_SRC1_RGB                       MYGL_SOURCE1_RGB
#define MYGL_SRC2_RGB                       MYGL_SOURCE2_RGB
#define MYGL_SRC0_ALPHA                     MYGL_SOURCE0_ALPHA
#define MYGL_SRC1_ALPHA                     MYGL_SOURCE1_ALPHA
#define MYGL_SRC2_ALPHA                     MYGL_SOURCE2_ALPHA
#endif

#ifndef MYGL_VERSION_2_0
#define MYGL_BLEND_EQUATION_RGB             MYGL_BLEND_EQUATION
#define MYGL_VERTEX_ATTRIB_ARRAY_ENABLED    0x8622
#define MYGL_VERTEX_ATTRIB_ARRAY_SIZE       0x8623
#define MYGL_VERTEX_ATTRIB_ARRAY_STRIDE     0x8624
#define MYGL_VERTEX_ATTRIB_ARRAY_TYPE       0x8625
#define MYGL_CURRENT_VERTEX_ATTRIB          0x8626
#define MYGL_VERTEX_PROGRAM_POINT_SIZE      0x8642
#define MYGL_VERTEX_PROGRAM_TWO_SIDE        0x8643
#define MYGL_VERTEX_ATTRIB_ARRAY_POINTER    0x8645
#define MYGL_STENCIL_BACK_FUNC              0x8800
#define MYGL_STENCIL_BACK_FAIL              0x8801
#define MYGL_STENCIL_BACK_PASS_DEPTH_FAIL   0x8802
#define MYGL_STENCIL_BACK_PASS_DEPTH_PASS   0x8803
#define MYGL_MAX_DRAW_BUFFERS               0x8824
#define MYGL_DRAW_BUFFER0                   0x8825
#define MYGL_DRAW_BUFFER1                   0x8826
#define MYGL_DRAW_BUFFER2                   0x8827
#define MYGL_DRAW_BUFFER3                   0x8828
#define MYGL_DRAW_BUFFER4                   0x8829
#define MYGL_DRAW_BUFFER5                   0x882A
#define MYGL_DRAW_BUFFER6                   0x882B
#define MYGL_DRAW_BUFFER7                   0x882C
#define MYGL_DRAW_BUFFER8                   0x882D
#define MYGL_DRAW_BUFFER9                   0x882E
#define MYGL_DRAW_BUFFER10                  0x882F
#define MYGL_DRAW_BUFFER11                  0x8830
#define MYGL_DRAW_BUFFER12                  0x8831
#define MYGL_DRAW_BUFFER13                  0x8832
#define MYGL_DRAW_BUFFER14                  0x8833
#define MYGL_DRAW_BUFFER15                  0x8834
#define MYGL_BLEND_EQUATION_ALPHA           0x883D
#define MYGL_POINT_SPRITE                   0x8861
#define MYGL_COORD_REPLACE                  0x8862
#define MYGL_MAX_VERTEX_ATTRIBS             0x8869
#define MYGL_VERTEX_ATTRIB_ARRAY_NORMALIZED 0x886A
#define MYGL_MAX_TEXTURE_COORDS             0x8871
#define MYGL_MAX_TEXTURE_IMAGE_UNITS        0x8872
#define MYGL_FRAGMENT_SHADER                0x8B30
#define MYGL_VERTEX_SHADER                  0x8B31
#define MYGL_MAX_FRAGMENT_UNIFORM_COMPONENTS 0x8B49
#define MYGL_MAX_VERTEX_UNIFORM_COMPONENTS  0x8B4A
#define MYGL_MAX_VARYING_FLOATS             0x8B4B
#define MYGL_MAX_VERTEX_TEXTURE_IMAGE_UNITS 0x8B4C
#define MYGL_MAX_COMBINED_TEXTURE_IMAGE_UNITS 0x8B4D
#define MYGL_SHADER_TYPE                    0x8B4F
#define MYGL_FLOAT_VEC2                     0x8B50
#define MYGL_FLOAT_VEC3                     0x8B51
#define MYGL_FLOAT_VEC4                     0x8B52
#define MYGL_INT_VEC2                       0x8B53
#define MYGL_INT_VEC3                       0x8B54
#define MYGL_INT_VEC4                       0x8B55
#define MYGL_BOOL                           0x8B56
#define MYGL_BOOL_VEC2                      0x8B57
#define MYGL_BOOL_VEC3                      0x8B58
#define MYGL_BOOL_VEC4                      0x8B59
#define MYGL_FLOAT_MAT2                     0x8B5A
#define MYGL_FLOAT_MAT3                     0x8B5B
#define MYGL_FLOAT_MAT4                     0x8B5C
#define MYGL_SAMPLER_1D                     0x8B5D
#define MYGL_SAMPLER_2D                     0x8B5E
#define MYGL_SAMPLER_3D                     0x8B5F
#define MYGL_SAMPLER_CUBE                   0x8B60
#define MYGL_SAMPLER_1D_SHADOW              0x8B61
#define MYGL_SAMPLER_2D_SHADOW              0x8B62
#define MYGL_DELETE_STATUS                  0x8B80
#define MYGL_COMPILE_STATUS                 0x8B81
#define MYGL_LINK_STATUS                    0x8B82
#define MYGL_VALIDATE_STATUS                0x8B83
#define MYGL_INFO_LOG_LENGTH                0x8B84
#define MYGL_ATTACHED_SHADERS               0x8B85
#define MYGL_ACTIVE_UNIFORMS                0x8B86
#define MYGL_ACTIVE_UNIFORM_MAX_LENGTH      0x8B87
#define MYGL_SHADER_SOURCE_LENGTH           0x8B88
#define MYGL_ACTIVE_ATTRIBUTES              0x8B89
#define MYGL_ACTIVE_ATTRIBUTE_MAX_LENGTH    0x8B8A
#define MYGL_FRAGMENT_SHADER_DERIVATIVE_HINT 0x8B8B
#define MYGL_SHADING_LANGUAGE_VERSION       0x8B8C
#define MYGL_CURRENT_PROGRAM                0x8B8D
#define MYGL_POINT_SPRITE_COORD_ORIGIN      0x8CA0
#define MYGL_LOWER_LEFT                     0x8CA1
#define MYGL_UPPER_LEFT                     0x8CA2
#define MYGL_STENCIL_BACK_REF               0x8CA3
#define MYGL_STENCIL_BACK_VALUE_MASK        0x8CA4
#define MYGL_STENCIL_BACK_WRITEMASK         0x8CA5
#endif

#ifndef MYGL_ARB_multitexture
#define MYGL_TEXTURE0_ARB                   0x84C0
#define MYGL_TEXTURE1_ARB                   0x84C1
#define MYGL_TEXTURE2_ARB                   0x84C2
#define MYGL_TEXTURE3_ARB                   0x84C3
#define MYGL_TEXTURE4_ARB                   0x84C4
#define MYGL_TEXTURE5_ARB                   0x84C5
#define MYGL_TEXTURE6_ARB                   0x84C6
#define MYGL_TEXTURE7_ARB                   0x84C7
#define MYGL_TEXTURE8_ARB                   0x84C8
#define MYGL_TEXTURE9_ARB                   0x84C9
#define MYGL_TEXTURE10_ARB                  0x84CA
#define MYGL_TEXTURE11_ARB                  0x84CB
#define MYGL_TEXTURE12_ARB                  0x84CC
#define MYGL_TEXTURE13_ARB                  0x84CD
#define MYGL_TEXTURE14_ARB                  0x84CE
#define MYGL_TEXTURE15_ARB                  0x84CF
#define MYGL_TEXTURE16_ARB                  0x84D0
#define MYGL_TEXTURE17_ARB                  0x84D1
#define MYGL_TEXTURE18_ARB                  0x84D2
#define MYGL_TEXTURE19_ARB                  0x84D3
#define MYGL_TEXTURE20_ARB                  0x84D4
#define MYGL_TEXTURE21_ARB                  0x84D5
#define MYGL_TEXTURE22_ARB                  0x84D6
#define MYGL_TEXTURE23_ARB                  0x84D7
#define MYGL_TEXTURE24_ARB                  0x84D8
#define MYGL_TEXTURE25_ARB                  0x84D9
#define MYGL_TEXTURE26_ARB                  0x84DA
#define MYGL_TEXTURE27_ARB                  0x84DB
#define MYGL_TEXTURE28_ARB                  0x84DC
#define MYGL_TEXTURE29_ARB                  0x84DD
#define MYGL_TEXTURE30_ARB                  0x84DE
#define MYGL_TEXTURE31_ARB                  0x84DF
#define MYGL_ACTIVE_TEXTURE_ARB             0x84E0
#define MYGL_CLIENT_ACTIVE_TEXTURE_ARB      0x84E1
#define MYGL_MAX_TEXTURE_UNITS_ARB          0x84E2
#endif

#ifndef MYGL_ARB_transpose_matrix
#define MYGL_TRANSPOSE_MODELVIEW_MATRIX_ARB 0x84E3
#define MYGL_TRANSPOSE_PROJECTION_MATRIX_ARB 0x84E4
#define MYGL_TRANSPOSE_TEXTURE_MATRIX_ARB   0x84E5
#define MYGL_TRANSPOSE_COLOR_MATRIX_ARB     0x84E6
#endif

#ifndef MYGL_ARB_multisample
#define MYGL_MULTISAMPLE_ARB                0x809D
#define MYGL_SAMPLE_ALPHA_TO_COVERAGE_ARB   0x809E
#define MYGL_SAMPLE_ALPHA_TO_ONE_ARB        0x809F
#define MYGL_SAMPLE_COVERAGE_ARB            0x80A0
#define MYGL_SAMPLE_BUFFERS_ARB             0x80A8
#define MYGL_SAMPLES_ARB                    0x80A9
#define MYGL_SAMPLE_COVERAGE_VALUE_ARB      0x80AA
#define MYGL_SAMPLE_COVERAGE_INVERT_ARB     0x80AB
#define MYGL_MULTISAMPLE_BIT_ARB            0x20000000
#endif

#ifndef MYGL_ARB_texture_env_add
#endif

#ifndef MYGL_ARB_texture_cube_map
#define MYGL_NORMAL_MAP_ARB                 0x8511
#define MYGL_REFLECTION_MAP_ARB             0x8512
#define MYGL_TEXTURE_CUBE_MAP_ARB           0x8513
#define MYGL_TEXTURE_BINDING_CUBE_MAP_ARB   0x8514
#define MYGL_TEXTURE_CUBE_MAP_POSITIVE_X_ARB 0x8515
#define MYGL_TEXTURE_CUBE_MAP_NEGATIVE_X_ARB 0x8516
#define MYGL_TEXTURE_CUBE_MAP_POSITIVE_Y_ARB 0x8517
#define MYGL_TEXTURE_CUBE_MAP_NEGATIVE_Y_ARB 0x8518
#define MYGL_TEXTURE_CUBE_MAP_POSITIVE_Z_ARB 0x8519
#define MYGL_TEXTURE_CUBE_MAP_NEGATIVE_Z_ARB 0x851A
#define MYGL_PROXY_TEXTURE_CUBE_MAP_ARB     0x851B
#define MYGL_MAX_CUBE_MAP_TEXTURE_SIZE_ARB  0x851C
#endif

#ifndef MYGL_ARB_texture_compression
#define MYGL_COMPRESSED_ALPHA_ARB           0x84E9
#define MYGL_COMPRESSED_LUMINANCE_ARB       0x84EA
#define MYGL_COMPRESSED_LUMINANCE_ALPHA_ARB 0x84EB
#define MYGL_COMPRESSED_INTENSITY_ARB       0x84EC
#define MYGL_COMPRESSED_RGB_ARB             0x84ED
#define MYGL_COMPRESSED_RGBA_ARB            0x84EE
#define MYGL_TEXTURE_COMPRESSION_HINT_ARB   0x84EF
#define MYGL_TEXTURE_COMPRESSED_IMAGE_SIZE_ARB 0x86A0
#define MYGL_TEXTURE_COMPRESSED_ARB         0x86A1
#define MYGL_NUM_COMPRESSED_TEXTURE_FORMATS_ARB 0x86A2
#define MYGL_COMPRESSED_TEXTURE_FORMATS_ARB 0x86A3
#endif

#ifndef MYGL_ARB_texture_border_clamp
#define MYGL_CLAMP_TO_BORDER_ARB            0x812D
#endif

#ifndef MYGL_ARB_point_parameters
#define MYGL_POINT_SIZE_MIN_ARB             0x8126
#define MYGL_POINT_SIZE_MAX_ARB             0x8127
#define MYGL_POINT_FADE_THRESHOLD_SIZE_ARB  0x8128
#define MYGL_POINT_DISTANCE_ATTENUATION_ARB 0x8129
#endif

#ifndef MYGL_ARB_vertex_blend
#define MYGL_MAX_VERTEX_UNITS_ARB           0x86A4
#define MYGL_ACTIVE_VERTEX_UNITS_ARB        0x86A5
#define MYGL_WEIGHT_SUM_UNITY_ARB           0x86A6
#define MYGL_VERTEX_BLEND_ARB               0x86A7
#define MYGL_CURRENT_WEIGHT_ARB             0x86A8
#define MYGL_WEIGHT_ARRAY_TYPE_ARB          0x86A9
#define MYGL_WEIGHT_ARRAY_STRIDE_ARB        0x86AA
#define MYGL_WEIGHT_ARRAY_SIZE_ARB          0x86AB
#define MYGL_WEIGHT_ARRAY_POINTER_ARB       0x86AC
#define MYGL_WEIGHT_ARRAY_ARB               0x86AD
#define MYGL_MODELVIEW0_ARB                 0x1700
#define MYGL_MODELVIEW1_ARB                 0x850A
#define MYGL_MODELVIEW2_ARB                 0x8722
#define MYGL_MODELVIEW3_ARB                 0x8723
#define MYGL_MODELVIEW4_ARB                 0x8724
#define MYGL_MODELVIEW5_ARB                 0x8725
#define MYGL_MODELVIEW6_ARB                 0x8726
#define MYGL_MODELVIEW7_ARB                 0x8727
#define MYGL_MODELVIEW8_ARB                 0x8728
#define MYGL_MODELVIEW9_ARB                 0x8729
#define MYGL_MODELVIEW10_ARB                0x872A
#define MYGL_MODELVIEW11_ARB                0x872B
#define MYGL_MODELVIEW12_ARB                0x872C
#define MYGL_MODELVIEW13_ARB                0x872D
#define MYGL_MODELVIEW14_ARB                0x872E
#define MYGL_MODELVIEW15_ARB                0x872F
#define MYGL_MODELVIEW16_ARB                0x8730
#define MYGL_MODELVIEW17_ARB                0x8731
#define MYGL_MODELVIEW18_ARB                0x8732
#define MYGL_MODELVIEW19_ARB                0x8733
#define MYGL_MODELVIEW20_ARB                0x8734
#define MYGL_MODELVIEW21_ARB                0x8735
#define MYGL_MODELVIEW22_ARB                0x8736
#define MYGL_MODELVIEW23_ARB                0x8737
#define MYGL_MODELVIEW24_ARB                0x8738
#define MYGL_MODELVIEW25_ARB                0x8739
#define MYGL_MODELVIEW26_ARB                0x873A
#define MYGL_MODELVIEW27_ARB                0x873B
#define MYGL_MODELVIEW28_ARB                0x873C
#define MYGL_MODELVIEW29_ARB                0x873D
#define MYGL_MODELVIEW30_ARB                0x873E
#define MYGL_MODELVIEW31_ARB                0x873F
#endif

#ifndef MYGL_ARB_matrix_palette
#define MYGL_MATRIX_PALETTE_ARB             0x8840
#define MYGL_MAX_MATRIX_PALETTE_STACK_DEPTH_ARB 0x8841
#define MYGL_MAX_PALETTE_MATRICES_ARB       0x8842
#define MYGL_CURRENT_PALETTE_MATRIX_ARB     0x8843
#define MYGL_MATRIX_INDEX_ARRAY_ARB         0x8844
#define MYGL_CURRENT_MATRIX_INDEX_ARB       0x8845
#define MYGL_MATRIX_INDEX_ARRAY_SIZE_ARB    0x8846
#define MYGL_MATRIX_INDEX_ARRAY_TYPE_ARB    0x8847
#define MYGL_MATRIX_INDEX_ARRAY_STRIDE_ARB  0x8848
#define MYGL_MATRIX_INDEX_ARRAY_POINTER_ARB 0x8849
#endif

#ifndef MYGL_ARB_texture_env_combine
#define MYGL_COMBINE_ARB                    0x8570
#define MYGL_COMBINE_RGB_ARB                0x8571
#define MYGL_COMBINE_ALPHA_ARB              0x8572
#define MYGL_SOURCE0_RGB_ARB                0x8580
#define MYGL_SOURCE1_RGB_ARB                0x8581
#define MYGL_SOURCE2_RGB_ARB                0x8582
#define MYGL_SOURCE0_ALPHA_ARB              0x8588
#define MYGL_SOURCE1_ALPHA_ARB              0x8589
#define MYGL_SOURCE2_ALPHA_ARB              0x858A
#define MYGL_OPERAND0_RGB_ARB               0x8590
#define MYGL_OPERAND1_RGB_ARB               0x8591
#define MYGL_OPERAND2_RGB_ARB               0x8592
#define MYGL_OPERAND0_ALPHA_ARB             0x8598
#define MYGL_OPERAND1_ALPHA_ARB             0x8599
#define MYGL_OPERAND2_ALPHA_ARB             0x859A
#define MYGL_RGB_SCALE_ARB                  0x8573
#define MYGL_ADD_SIGNED_ARB                 0x8574
#define MYGL_INTERPOLATE_ARB                0x8575
#define MYGL_SUBTRACT_ARB                   0x84E7
#define MYGL_CONSTANT_ARB                   0x8576
#define MYGL_PRIMARY_COLOR_ARB              0x8577
#define MYGL_PREVIOUS_ARB                   0x8578
#endif

#ifndef MYGL_ARB_texture_env_crossbar
#endif

#ifndef MYGL_ARB_texture_env_dot3
#define MYGL_DOT3_RGB_ARB                   0x86AE
#define MYGL_DOT3_RGBA_ARB                  0x86AF
#endif

#ifndef MYGL_ARB_texture_mirrored_repeat
#define MYGL_MIRRORED_REPEAT_ARB            0x8370
#endif

#ifndef MYGL_ARB_depth_texture
#define MYGL_DEPTH_COMPONENT16_ARB          0x81A5
#define MYGL_DEPTH_COMPONENT24_ARB          0x81A6
#define MYGL_DEPTH_COMPONENT32_ARB          0x81A7
#define MYGL_TEXTURE_DEPTH_SIZE_ARB         0x884A
#define MYGL_DEPTH_TEXTURE_MODE_ARB         0x884B
#endif

#ifndef MYGL_ARB_shadow
#define MYGL_TEXTURE_COMPARE_MODE_ARB       0x884C
#define MYGL_TEXTURE_COMPARE_FUNC_ARB       0x884D
#define MYGL_COMPARE_R_TO_TEXTURE_ARB       0x884E
#endif

#ifndef MYGL_ARB_shadow_ambient
#define MYGL_TEXTURE_COMPARE_FAIL_VALUE_ARB 0x80BF
#endif

#ifndef MYGL_ARB_window_pos
#endif

#ifndef MYGL_ARB_vertex_program
#define MYGL_COLOR_SUM_ARB                  0x8458
#define MYGL_VERTEX_PROGRAM_ARB             0x8620
#define MYGL_VERTEX_ATTRIB_ARRAY_ENABLED_ARB 0x8622
#define MYGL_VERTEX_ATTRIB_ARRAY_SIZE_ARB   0x8623
#define MYGL_VERTEX_ATTRIB_ARRAY_STRIDE_ARB 0x8624
#define MYGL_VERTEX_ATTRIB_ARRAY_TYPE_ARB   0x8625
#define MYGL_CURRENT_VERTEX_ATTRIB_ARB      0x8626
#define MYGL_PROGRAM_LENGTH_ARB             0x8627
#define MYGL_PROGRAM_STRING_ARB             0x8628
#define MYGL_MAX_PROGRAM_MATRIX_STACK_DEPTH_ARB 0x862E
#define MYGL_MAX_PROGRAM_MATRICES_ARB       0x862F
#define MYGL_CURRENT_MATRIX_STACK_DEPTH_ARB 0x8640
#define MYGL_CURRENT_MATRIX_ARB             0x8641
#define MYGL_VERTEX_PROGRAM_POINT_SIZE_ARB  0x8642
#define MYGL_VERTEX_PROGRAM_TWO_SIDE_ARB    0x8643
#define MYGL_VERTEX_ATTRIB_ARRAY_POINTER_ARB 0x8645
#define MYGL_PROGRAM_ERROR_POSITION_ARB     0x864B
#define MYGL_PROGRAM_BINDING_ARB            0x8677
#define MYGL_MAX_VERTEX_ATTRIBS_ARB         0x8869
#define MYGL_VERTEX_ATTRIB_ARRAY_NORMALIZED_ARB 0x886A
#define MYGL_PROGRAM_ERROR_STRING_ARB       0x8874
#define MYGL_PROGRAM_FORMAT_ASCII_ARB       0x8875
#define MYGL_PROGRAM_FORMAT_ARB             0x8876
#define MYGL_PROGRAM_INSTRUCTIONS_ARB       0x88A0
#define MYGL_MAX_PROGRAM_INSTRUCTIONS_ARB   0x88A1
#define MYGL_PROGRAM_NATIVE_INSTRUCTIONS_ARB 0x88A2
#define MYGL_MAX_PROGRAM_NATIVE_INSTRUCTIONS_ARB 0x88A3
#define MYGL_PROGRAM_TEMPORARIES_ARB        0x88A4
#define MYGL_MAX_PROGRAM_TEMPORARIES_ARB    0x88A5
#define MYGL_PROGRAM_NATIVE_TEMPORARIES_ARB 0x88A6
#define MYGL_MAX_PROGRAM_NATIVE_TEMPORARIES_ARB 0x88A7
#define MYGL_PROGRAM_PARAMETERS_ARB         0x88A8
#define MYGL_MAX_PROGRAM_PARAMETERS_ARB     0x88A9
#define MYGL_PROGRAM_NATIVE_PARAMETERS_ARB  0x88AA
#define MYGL_MAX_PROGRAM_NATIVE_PARAMETERS_ARB 0x88AB
#define MYGL_PROGRAM_ATTRIBS_ARB            0x88AC
#define MYGL_MAX_PROGRAM_ATTRIBS_ARB        0x88AD
#define MYGL_PROGRAM_NATIVE_ATTRIBS_ARB     0x88AE
#define MYGL_MAX_PROGRAM_NATIVE_ATTRIBS_ARB 0x88AF
#define MYGL_PROGRAM_ADDRESS_REGISTERS_ARB  0x88B0
#define MYGL_MAX_PROGRAM_ADDRESS_REGISTERS_ARB 0x88B1
#define MYGL_PROGRAM_NATIVE_ADDRESS_REGISTERS_ARB 0x88B2
#define MYGL_MAX_PROGRAM_NATIVE_ADDRESS_REGISTERS_ARB 0x88B3
#define MYGL_MAX_PROGRAM_LOCAL_PARAMETERS_ARB 0x88B4
#define MYGL_MAX_PROGRAM_ENV_PARAMETERS_ARB 0x88B5
#define MYGL_PROGRAM_UNDER_NATIVE_LIMITS_ARB 0x88B6
#define MYGL_TRANSPOSE_CURRENT_MATRIX_ARB   0x88B7
#define MYGL_MATRIX0_ARB                    0x88C0
#define MYGL_MATRIX1_ARB                    0x88C1
#define MYGL_MATRIX2_ARB                    0x88C2
#define MYGL_MATRIX3_ARB                    0x88C3
#define MYGL_MATRIX4_ARB                    0x88C4
#define MYGL_MATRIX5_ARB                    0x88C5
#define MYGL_MATRIX6_ARB                    0x88C6
#define MYGL_MATRIX7_ARB                    0x88C7
#define MYGL_MATRIX8_ARB                    0x88C8
#define MYGL_MATRIX9_ARB                    0x88C9
#define MYGL_MATRIX10_ARB                   0x88CA
#define MYGL_MATRIX11_ARB                   0x88CB
#define MYGL_MATRIX12_ARB                   0x88CC
#define MYGL_MATRIX13_ARB                   0x88CD
#define MYGL_MATRIX14_ARB                   0x88CE
#define MYGL_MATRIX15_ARB                   0x88CF
#define MYGL_MATRIX16_ARB                   0x88D0
#define MYGL_MATRIX17_ARB                   0x88D1
#define MYGL_MATRIX18_ARB                   0x88D2
#define MYGL_MATRIX19_ARB                   0x88D3
#define MYGL_MATRIX20_ARB                   0x88D4
#define MYGL_MATRIX21_ARB                   0x88D5
#define MYGL_MATRIX22_ARB                   0x88D6
#define MYGL_MATRIX23_ARB                   0x88D7
#define MYGL_MATRIX24_ARB                   0x88D8
#define MYGL_MATRIX25_ARB                   0x88D9
#define MYGL_MATRIX26_ARB                   0x88DA
#define MYGL_MATRIX27_ARB                   0x88DB
#define MYGL_MATRIX28_ARB                   0x88DC
#define MYGL_MATRIX29_ARB                   0x88DD
#define MYGL_MATRIX30_ARB                   0x88DE
#define MYGL_MATRIX31_ARB                   0x88DF
#endif

#ifndef MYGL_ARB_fragment_program
#define MYGL_FRAGMENT_PROGRAM_ARB           0x8804
#define MYGL_PROGRAM_ALU_INSTRUCTIONS_ARB   0x8805
#define MYGL_PROGRAM_TEX_INSTRUCTIONS_ARB   0x8806
#define MYGL_PROGRAM_TEX_INDIRECTIONS_ARB   0x8807
#define MYGL_PROGRAM_NATIVE_ALU_INSTRUCTIONS_ARB 0x8808
#define MYGL_PROGRAM_NATIVE_TEX_INSTRUCTIONS_ARB 0x8809
#define MYGL_PROGRAM_NATIVE_TEX_INDIRECTIONS_ARB 0x880A
#define MYGL_MAX_PROGRAM_ALU_INSTRUCTIONS_ARB 0x880B
#define MYGL_MAX_PROGRAM_TEX_INSTRUCTIONS_ARB 0x880C
#define MYGL_MAX_PROGRAM_TEX_INDIRECTIONS_ARB 0x880D
#define MYGL_MAX_PROGRAM_NATIVE_ALU_INSTRUCTIONS_ARB 0x880E
#define MYGL_MAX_PROGRAM_NATIVE_TEX_INSTRUCTIONS_ARB 0x880F
#define MYGL_MAX_PROGRAM_NATIVE_TEX_INDIRECTIONS_ARB 0x8810
#define MYGL_MAX_TEXTURE_COORDS_ARB         0x8871
#define MYGL_MAX_TEXTURE_IMAGE_UNITS_ARB    0x8872
#endif

#ifndef MYGL_ARB_vertex_buffer_object
#define MYGL_BUFFER_SIZE_ARB                0x8764
#define MYGL_BUFFER_USAGE_ARB               0x8765
#define MYGL_ARRAY_BUFFER_ARB               0x8892
#define MYGL_ELEMENT_ARRAY_BUFFER_ARB       0x8893
#define MYGL_ARRAY_BUFFER_BINDING_ARB       0x8894
#define MYGL_ELEMENT_ARRAY_BUFFER_BINDING_ARB 0x8895
#define MYGL_VERTEX_ARRAY_BUFFER_BINDING_ARB 0x8896
#define MYGL_NORMAL_ARRAY_BUFFER_BINDING_ARB 0x8897
#define MYGL_COLOR_ARRAY_BUFFER_BINDING_ARB 0x8898
#define MYGL_INDEX_ARRAY_BUFFER_BINDING_ARB 0x8899
#define MYGL_TEXTURE_COORD_ARRAY_BUFFER_BINDING_ARB 0x889A
#define MYGL_EDGE_FLAG_ARRAY_BUFFER_BINDING_ARB 0x889B
#define MYGL_SECONDARY_COLOR_ARRAY_BUFFER_BINDING_ARB 0x889C
#define MYGL_FOG_COORDINATE_ARRAY_BUFFER_BINDING_ARB 0x889D
#define MYGL_WEIGHT_ARRAY_BUFFER_BINDING_ARB 0x889E
#define MYGL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING_ARB 0x889F
#define MYGL_READ_ONLY_ARB                  0x88B8
#define MYGL_WRITE_ONLY_ARB                 0x88B9
#define MYGL_READ_WRITE_ARB                 0x88BA
#define MYGL_BUFFER_ACCESS_ARB              0x88BB
#define MYGL_BUFFER_MAPPED_ARB              0x88BC
#define MYGL_BUFFER_MAP_POINTER_ARB         0x88BD
#define MYGL_STREAM_DRAW_ARB                0x88E0
#define MYGL_STREAM_READ_ARB                0x88E1
#define MYGL_STREAM_COPY_ARB                0x88E2
#define MYGL_STATIC_DRAW_ARB                0x88E4
#define MYGL_STATIC_READ_ARB                0x88E5
#define MYGL_STATIC_COPY_ARB                0x88E6
#define MYGL_DYNAMIC_DRAW_ARB               0x88E8
#define MYGL_DYNAMIC_READ_ARB               0x88E9
#define MYGL_DYNAMIC_COPY_ARB               0x88EA
#endif

#ifndef MYGL_ARB_occlusion_query
#define MYGL_QUERY_COUNTER_BITS_ARB         0x8864
#define MYGL_CURRENT_QUERY_ARB              0x8865
#define MYGL_QUERY_RESULT_ARB               0x8866
#define MYGL_QUERY_RESULT_AVAILABLE_ARB     0x8867
#define MYGL_SAMPLES_PASSED_ARB             0x8914
#endif

#ifndef MYGL_ARB_shader_objects
#define MYGL_PROGRAM_OBJECT_ARB             0x8B40
#define MYGL_SHADER_OBJECT_ARB              0x8B48
#define MYGL_OBJECT_TYPE_ARB                0x8B4E
#define MYGL_OBJECT_SUBTYPE_ARB             0x8B4F
#define MYGL_FLOAT_VEC2_ARB                 0x8B50
#define MYGL_FLOAT_VEC3_ARB                 0x8B51
#define MYGL_FLOAT_VEC4_ARB                 0x8B52
#define MYGL_INT_VEC2_ARB                   0x8B53
#define MYGL_INT_VEC3_ARB                   0x8B54
#define MYGL_INT_VEC4_ARB                   0x8B55
#define MYGL_BOOL_ARB                       0x8B56
#define MYGL_BOOL_VEC2_ARB                  0x8B57
#define MYGL_BOOL_VEC3_ARB                  0x8B58
#define MYGL_BOOL_VEC4_ARB                  0x8B59
#define MYGL_FLOAT_MAT2_ARB                 0x8B5A
#define MYGL_FLOAT_MAT3_ARB                 0x8B5B
#define MYGL_FLOAT_MAT4_ARB                 0x8B5C
#define MYGL_SAMPLER_1D_ARB                 0x8B5D
#define MYGL_SAMPLER_2D_ARB                 0x8B5E
#define MYGL_SAMPLER_3D_ARB                 0x8B5F
#define MYGL_SAMPLER_CUBE_ARB               0x8B60
#define MYGL_SAMPLER_1D_SHADOW_ARB          0x8B61
#define MYGL_SAMPLER_2D_SHADOW_ARB          0x8B62
#define MYGL_SAMPLER_2D_RECT_ARB            0x8B63
#define MYGL_SAMPLER_2D_RECT_SHADOW_ARB     0x8B64
#define MYGL_OBJECT_DELETE_STATUS_ARB       0x8B80
#define MYGL_OBJECT_COMPILE_STATUS_ARB      0x8B81
#define MYGL_OBJECT_LINK_STATUS_ARB         0x8B82
#define MYGL_OBJECT_VALIDATE_STATUS_ARB     0x8B83
#define MYGL_OBJECT_INFO_LOG_LENGTH_ARB     0x8B84
#define MYGL_OBJECT_ATTACHED_OBJECTS_ARB    0x8B85
#define MYGL_OBJECT_ACTIVE_UNIFORMS_ARB     0x8B86
#define MYGL_OBJECT_ACTIVE_UNIFORM_MAX_LENGTH_ARB 0x8B87
#define MYGL_OBJECT_SHADER_SOURCE_LENGTH_ARB 0x8B88
#endif

#ifndef MYGL_ARB_vertex_shader
#define MYGL_VERTEX_SHADER_ARB              0x8B31
#define MYGL_MAX_VERTEX_UNIFORM_COMPONENTS_ARB 0x8B4A
#define MYGL_MAX_VARYING_FLOATS_ARB         0x8B4B
#define MYGL_MAX_VERTEX_TEXTURE_IMAGE_UNITS_ARB 0x8B4C
#define MYGL_MAX_COMBINED_TEXTURE_IMAGE_UNITS_ARB 0x8B4D
#define MYGL_OBJECT_ACTIVE_ATTRIBUTES_ARB   0x8B89
#define MYGL_OBJECT_ACTIVE_ATTRIBUTE_MAX_LENGTH_ARB 0x8B8A
#endif

#ifndef MYGL_ARB_fragment_shader
#define MYGL_FRAGMENT_SHADER_ARB            0x8B30
#define MYGL_MAX_FRAGMENT_UNIFORM_COMPONENTS_ARB 0x8B49
#define MYGL_FRAGMENT_SHADER_DERIVATIVE_HINT_ARB 0x8B8B
#endif

#ifndef MYGL_ARB_shading_language_100
#define MYGL_SHADING_LANGUAGE_VERSION_ARB   0x8B8C
#endif

#ifndef MYGL_ARB_texture_non_power_of_two
#endif

#ifndef MYGL_ARB_point_sprite
#define MYGL_POINT_SPRITE_ARB               0x8861
#define MYGL_COORD_REPLACE_ARB              0x8862
#endif

#ifndef MYGL_ARB_fragment_program_shadow
#endif

#ifndef MYGL_ARB_draw_buffers
#define MYGL_MAX_DRAW_BUFFERS_ARB           0x8824
#define MYGL_DRAW_BUFFER0_ARB               0x8825
#define MYGL_DRAW_BUFFER1_ARB               0x8826
#define MYGL_DRAW_BUFFER2_ARB               0x8827
#define MYGL_DRAW_BUFFER3_ARB               0x8828
#define MYGL_DRAW_BUFFER4_ARB               0x8829
#define MYGL_DRAW_BUFFER5_ARB               0x882A
#define MYGL_DRAW_BUFFER6_ARB               0x882B
#define MYGL_DRAW_BUFFER7_ARB               0x882C
#define MYGL_DRAW_BUFFER8_ARB               0x882D
#define MYGL_DRAW_BUFFER9_ARB               0x882E
#define MYGL_DRAW_BUFFER10_ARB              0x882F
#define MYGL_DRAW_BUFFER11_ARB              0x8830
#define MYGL_DRAW_BUFFER12_ARB              0x8831
#define MYGL_DRAW_BUFFER13_ARB              0x8832
#define MYGL_DRAW_BUFFER14_ARB              0x8833
#define MYGL_DRAW_BUFFER15_ARB              0x8834
#endif

#ifndef MYGL_ARB_texture_rectangle
#define MYGL_TEXTURE_RECTANGLE_ARB          0x84F5
#define MYGL_TEXTURE_BINDING_RECTANGLE_ARB  0x84F6
#define MYGL_PROXY_TEXTURE_RECTANGLE_ARB    0x84F7
#define MYGL_MAX_RECTANGLE_TEXTURE_SIZE_ARB 0x84F8
#endif

#ifndef MYGL_ARB_color_buffer_float
#define MYGL_RGBA_FLOAT_MODE_ARB            0x8820
#define MYGL_CLAMP_VERTEX_COLOR_ARB         0x891A
#define MYGL_CLAMP_FRAGMENT_COLOR_ARB       0x891B
#define MYGL_CLAMP_READ_COLOR_ARB           0x891C
#define MYGL_FIXED_ONLY_ARB                 0x891D
#endif

#ifndef MYGL_ARB_half_float_pixel
#define MYGL_HALF_FLOAT_ARB                 0x140B
#endif

#ifndef MYGL_ARB_texture_float
#define MYGL_TEXTURE_RED_TYPE_ARB           0x8C10
#define MYGL_TEXTURE_GREEN_TYPE_ARB         0x8C11
#define MYGL_TEXTURE_BLUE_TYPE_ARB          0x8C12
#define MYGL_TEXTURE_ALPHA_TYPE_ARB         0x8C13
#define MYGL_TEXTURE_LUMINANCE_TYPE_ARB     0x8C14
#define MYGL_TEXTURE_INTENSITY_TYPE_ARB     0x8C15
#define MYGL_TEXTURE_DEPTH_TYPE_ARB         0x8C16
#define MYGL_UNSIGNED_NORMALIZED_ARB        0x8C17
#define MYGL_RGBA32F_ARB                    0x8814
#define MYGL_RGB32F_ARB                     0x8815
#define MYGL_ALPHA32F_ARB                   0x8816
#define MYGL_INTENSITY32F_ARB               0x8817
#define MYGL_LUMINANCE32F_ARB               0x8818
#define MYGL_LUMINANCE_ALPHA32F_ARB         0x8819
#define MYGL_RGBA16F_ARB                    0x881A
#define MYGL_RGB16F_ARB                     0x881B
#define MYGL_ALPHA16F_ARB                   0x881C
#define MYGL_INTENSITY16F_ARB               0x881D
#define MYGL_LUMINANCE16F_ARB               0x881E
#define MYGL_LUMINANCE_ALPHA16F_ARB         0x881F
#endif

#ifndef MYGL_ARB_pixel_buffer_object
#define MYGL_PIXEL_PACK_BUFFER_ARB          0x88EB
#define MYGL_PIXEL_UNPACK_BUFFER_ARB        0x88EC
#define MYGL_PIXEL_PACK_BUFFER_BINDING_ARB  0x88ED
#define MYGL_PIXEL_UNPACK_BUFFER_BINDING_ARB 0x88EF
#endif

#ifndef MYGL_EXT_abgr
#define MYGL_ABGR_EXT                       0x8000
#endif

#ifndef MYGL_EXT_blend_color
#define MYGL_CONSTANT_COLOR_EXT             0x8001
#define MYGL_ONE_MINUS_CONSTANT_COLOR_EXT   0x8002
#define MYGL_CONSTANT_ALPHA_EXT             0x8003
#define MYGL_ONE_MINUS_CONSTANT_ALPHA_EXT   0x8004
#define MYGL_BLEND_COLOR_EXT                0x8005
#endif

#ifndef MYGL_EXT_polygon_offset
#define MYGL_POLYGON_OFFSET_EXT             0x8037
#define MYGL_POLYGON_OFFSET_FACTOR_EXT      0x8038
#define MYGL_POLYGON_OFFSET_BIAS_EXT        0x8039
#endif

#ifndef MYGL_EXT_texture
#define MYGL_ALPHA4_EXT                     0x803B
#define MYGL_ALPHA8_EXT                     0x803C
#define MYGL_ALPHA12_EXT                    0x803D
#define MYGL_ALPHA16_EXT                    0x803E
#define MYGL_LUMINANCE4_EXT                 0x803F
#define MYGL_LUMINANCE8_EXT                 0x8040
#define MYGL_LUMINANCE12_EXT                0x8041
#define MYGL_LUMINANCE16_EXT                0x8042
#define MYGL_LUMINANCE4_ALPHA4_EXT          0x8043
#define MYGL_LUMINANCE6_ALPHA2_EXT          0x8044
#define MYGL_LUMINANCE8_ALPHA8_EXT          0x8045
#define MYGL_LUMINANCE12_ALPHA4_EXT         0x8046
#define MYGL_LUMINANCE12_ALPHA12_EXT        0x8047
#define MYGL_LUMINANCE16_ALPHA16_EXT        0x8048
#define MYGL_INTENSITY_EXT                  0x8049
#define MYGL_INTENSITY4_EXT                 0x804A
#define MYGL_INTENSITY8_EXT                 0x804B
#define MYGL_INTENSITY12_EXT                0x804C
#define MYGL_INTENSITY16_EXT                0x804D
#define MYGL_RGB2_EXT                       0x804E
#define MYGL_RGB4_EXT                       0x804F
#define MYGL_RGB5_EXT                       0x8050
#define MYGL_RGB8_EXT                       0x8051
#define MYGL_RGB10_EXT                      0x8052
#define MYGL_RGB12_EXT                      0x8053
#define MYGL_RGB16_EXT                      0x8054
#define MYGL_RGBA2_EXT                      0x8055
#define MYGL_RGBA4_EXT                      0x8056
#define MYGL_RGB5_A1_EXT                    0x8057
#define MYGL_RGBA8_EXT                      0x8058
#define MYGL_RGB10_A2_EXT                   0x8059
#define MYGL_RGBA12_EXT                     0x805A
#define MYGL_RGBA16_EXT                     0x805B
#define MYGL_TEXTURE_RED_SIZE_EXT           0x805C
#define MYGL_TEXTURE_GREEN_SIZE_EXT         0x805D
#define MYGL_TEXTURE_BLUE_SIZE_EXT          0x805E
#define MYGL_TEXTURE_ALPHA_SIZE_EXT         0x805F
#define MYGL_TEXTURE_LUMINANCE_SIZE_EXT     0x8060
#define MYGL_TEXTURE_INTENSITY_SIZE_EXT     0x8061
#define MYGL_REPLACE_EXT                    0x8062
#define MYGL_PROXY_TEXTURE_1D_EXT           0x8063
#define MYGL_PROXY_TEXTURE_2D_EXT           0x8064
#define MYGL_TEXTURE_TOO_LARGE_EXT          0x8065
#endif

#ifndef MYGL_EXT_texture3D
#define MYGL_PACK_SKIP_IMAGES_EXT           0x806B
#define MYGL_PACK_IMAGE_HEIGHT_EXT          0x806C
#define MYGL_UNPACK_SKIP_IMAGES_EXT         0x806D
#define MYGL_UNPACK_IMAGE_HEIGHT_EXT        0x806E
#define MYGL_TEXTURE_3D_EXT                 0x806F
#define MYGL_PROXY_TEXTURE_3D_EXT           0x8070
#define MYGL_TEXTURE_DEPTH_EXT              0x8071
#define MYGL_TEXTURE_WRAP_R_EXT             0x8072
#define MYGL_MAX_3D_TEXTURE_SIZE_EXT        0x8073
#endif

#ifndef MYGL_SGIS_texture_filter4
#define MYGL_FILTER4_SGIS                   0x8146
#define MYGL_TEXTURE_FILTER4_SIZE_SGIS      0x8147
#endif

#ifndef MYGL_EXT_subtexture
#endif

#ifndef MYGL_EXT_copy_texture
#endif

#ifndef MYGL_EXT_histogram
#define MYGL_HISTOGRAM_EXT                  0x8024
#define MYGL_PROXY_HISTOGRAM_EXT            0x8025
#define MYGL_HISTOGRAM_WIDTH_EXT            0x8026
#define MYGL_HISTOGRAM_FORMAT_EXT           0x8027
#define MYGL_HISTOGRAM_RED_SIZE_EXT         0x8028
#define MYGL_HISTOGRAM_GREEN_SIZE_EXT       0x8029
#define MYGL_HISTOGRAM_BLUE_SIZE_EXT        0x802A
#define MYGL_HISTOGRAM_ALPHA_SIZE_EXT       0x802B
#define MYGL_HISTOGRAM_LUMINANCE_SIZE_EXT   0x802C
#define MYGL_HISTOGRAM_SINK_EXT             0x802D
#define MYGL_MINMAX_EXT                     0x802E
#define MYGL_MINMAX_FORMAT_EXT              0x802F
#define MYGL_MINMAX_SINK_EXT                0x8030
#define MYGL_TABLE_TOO_LARGE_EXT            0x8031
#endif

#ifndef MYGL_EXT_convolution
#define MYGL_CONVOLUTION_1D_EXT             0x8010
#define MYGL_CONVOLUTION_2D_EXT             0x8011
#define MYGL_SEPARABLE_2D_EXT               0x8012
#define MYGL_CONVOLUTION_BORDER_MODE_EXT    0x8013
#define MYGL_CONVOLUTION_FILTER_SCALE_EXT   0x8014
#define MYGL_CONVOLUTION_FILTER_BIAS_EXT    0x8015
#define MYGL_REDUCE_EXT                     0x8016
#define MYGL_CONVOLUTION_FORMAT_EXT         0x8017
#define MYGL_CONVOLUTION_WIDTH_EXT          0x8018
#define MYGL_CONVOLUTION_HEIGHT_EXT         0x8019
#define MYGL_MAX_CONVOLUTION_WIDTH_EXT      0x801A
#define MYGL_MAX_CONVOLUTION_HEIGHT_EXT     0x801B
#define MYGL_POST_CONVOLUTION_RED_SCALE_EXT 0x801C
#define MYGL_POST_CONVOLUTION_GREEN_SCALE_EXT 0x801D
#define MYGL_POST_CONVOLUTION_BLUE_SCALE_EXT 0x801E
#define MYGL_POST_CONVOLUTION_ALPHA_SCALE_EXT 0x801F
#define MYGL_POST_CONVOLUTION_RED_BIAS_EXT  0x8020
#define MYGL_POST_CONVOLUTION_GREEN_BIAS_EXT 0x8021
#define MYGL_POST_CONVOLUTION_BLUE_BIAS_EXT 0x8022
#define MYGL_POST_CONVOLUTION_ALPHA_BIAS_EXT 0x8023
#endif

#ifndef MYGL_SGI_color_matrix
#define MYGL_COLOR_MATRIX_SGI               0x80B1
#define MYGL_COLOR_MATRIX_STACK_DEPTH_SGI   0x80B2
#define MYGL_MAX_COLOR_MATRIX_STACK_DEPTH_SGI 0x80B3
#define MYGL_POST_COLOR_MATRIX_RED_SCALE_SGI 0x80B4
#define MYGL_POST_COLOR_MATRIX_GREEN_SCALE_SGI 0x80B5
#define MYGL_POST_COLOR_MATRIX_BLUE_SCALE_SGI 0x80B6
#define MYGL_POST_COLOR_MATRIX_ALPHA_SCALE_SGI 0x80B7
#define MYGL_POST_COLOR_MATRIX_RED_BIAS_SGI 0x80B8
#define MYGL_POST_COLOR_MATRIX_GREEN_BIAS_SGI 0x80B9
#define MYGL_POST_COLOR_MATRIX_BLUE_BIAS_SGI 0x80BA
#define MYGL_POST_COLOR_MATRIX_ALPHA_BIAS_SGI 0x80BB
#endif

#ifndef MYGL_SGI_color_table
#define MYGL_COLOR_TABLE_SGI                0x80D0
#define MYGL_POST_CONVOLUTION_COLOR_TABLE_SGI 0x80D1
#define MYGL_POST_COLOR_MATRIX_COLOR_TABLE_SGI 0x80D2
#define MYGL_PROXY_COLOR_TABLE_SGI          0x80D3
#define MYGL_PROXY_POST_CONVOLUTION_COLOR_TABLE_SGI 0x80D4
#define MYGL_PROXY_POST_COLOR_MATRIX_COLOR_TABLE_SGI 0x80D5
#define MYGL_COLOR_TABLE_SCALE_SGI          0x80D6
#define MYGL_COLOR_TABLE_BIAS_SGI           0x80D7
#define MYGL_COLOR_TABLE_FORMAT_SGI         0x80D8
#define MYGL_COLOR_TABLE_WIDTH_SGI          0x80D9
#define MYGL_COLOR_TABLE_RED_SIZE_SGI       0x80DA
#define MYGL_COLOR_TABLE_GREEN_SIZE_SGI     0x80DB
#define MYGL_COLOR_TABLE_BLUE_SIZE_SGI      0x80DC
#define MYGL_COLOR_TABLE_ALPHA_SIZE_SGI     0x80DD
#define MYGL_COLOR_TABLE_LUMINANCE_SIZE_SGI 0x80DE
#define MYGL_COLOR_TABLE_INTENSITY_SIZE_SGI 0x80DF
#endif

#ifndef MYGL_SGIS_pixel_texture
#define MYGL_PIXEL_TEXTURE_SGIS             0x8353
#define MYGL_PIXEL_FRAGMENT_RGB_SOURCE_SGIS 0x8354
#define MYGL_PIXEL_FRAGMENT_ALPHA_SOURCE_SGIS 0x8355
#define MYGL_PIXEL_GROUP_COLOR_SGIS         0x8356
#endif

#ifndef MYGL_SGIX_pixel_texture
#define MYGL_PIXEL_TEX_GEN_SGIX             0x8139
#define MYGL_PIXEL_TEX_GEN_MODE_SGIX        0x832B
#endif

#ifndef MYGL_SGIS_texture4D
#define MYGL_PACK_SKIP_VOLUMES_SGIS         0x8130
#define MYGL_PACK_IMAGE_DEPTH_SGIS          0x8131
#define MYGL_UNPACK_SKIP_VOLUMES_SGIS       0x8132
#define MYGL_UNPACK_IMAGE_DEPTH_SGIS        0x8133
#define MYGL_TEXTURE_4D_SGIS                0x8134
#define MYGL_PROXY_TEXTURE_4D_SGIS          0x8135
#define MYGL_TEXTURE_4DSIZE_SGIS            0x8136
#define MYGL_TEXTURE_WRAP_Q_SGIS            0x8137
#define MYGL_MAX_4D_TEXTURE_SIZE_SGIS       0x8138
#define MYGL_TEXTURE_4D_BINDING_SGIS        0x814F
#endif

#ifndef MYGL_SGI_texture_color_table
#define MYGL_TEXTURE_COLOR_TABLE_SGI        0x80BC
#define MYGL_PROXY_TEXTURE_COLOR_TABLE_SGI  0x80BD
#endif

#ifndef MYGL_EXT_cmyka
#define MYGL_CMYK_EXT                       0x800C
#define MYGL_CMYKA_EXT                      0x800D
#define MYGL_PACK_CMYK_HINT_EXT             0x800E
#define MYGL_UNPACK_CMYK_HINT_EXT           0x800F
#endif

#ifndef MYGL_EXT_texture_object
#define MYGL_TEXTURE_PRIORITY_EXT           0x8066
#define MYGL_TEXTURE_RESIDENT_EXT           0x8067
#define MYGL_TEXTURE_1D_BINDING_EXT         0x8068
#define MYGL_TEXTURE_2D_BINDING_EXT         0x8069
#define MYGL_TEXTURE_3D_BINDING_EXT         0x806A
#endif

#ifndef MYGL_SGIS_detail_texture
#define MYGL_DETAIL_TEXTURE_2D_SGIS         0x8095
#define MYGL_DETAIL_TEXTURE_2D_BINDING_SGIS 0x8096
#define MYGL_LINEAR_DETAIL_SGIS             0x8097
#define MYGL_LINEAR_DETAIL_ALPHA_SGIS       0x8098
#define MYGL_LINEAR_DETAIL_COLOR_SGIS       0x8099
#define MYGL_DETAIL_TEXTURE_LEVEL_SGIS      0x809A
#define MYGL_DETAIL_TEXTURE_MODE_SGIS       0x809B
#define MYGL_DETAIL_TEXTURE_FUNC_POINTS_SGIS 0x809C
#endif

#ifndef MYGL_SGIS_sharpen_texture
#define MYGL_LINEAR_SHARPEN_SGIS            0x80AD
#define MYGL_LINEAR_SHARPEN_ALPHA_SGIS      0x80AE
#define MYGL_LINEAR_SHARPEN_COLOR_SGIS      0x80AF
#define MYGL_SHARPEN_TEXTURE_FUNC_POINTS_SGIS 0x80B0
#endif

#ifndef MYGL_EXT_packed_pixels
#define MYGL_UNSIGNED_BYTE_3_3_2_EXT        0x8032
#define MYGL_UNSIGNED_SHORT_4_4_4_4_EXT     0x8033
#define MYGL_UNSIGNED_SHORT_5_5_5_1_EXT     0x8034
#define MYGL_UNSIGNED_INT_8_8_8_8_EXT       0x8035
#define MYGL_UNSIGNED_INT_10_10_10_2_EXT    0x8036
#endif

#ifndef MYGL_SGIS_texture_lod
#define MYGL_TEXTURE_MIN_LOD_SGIS           0x813A
#define MYGL_TEXTURE_MAX_LOD_SGIS           0x813B
#define MYGL_TEXTURE_BASE_LEVEL_SGIS        0x813C
#define MYGL_TEXTURE_MAX_LEVEL_SGIS         0x813D
#endif

#ifndef MYGL_SGIS_multisample
#define MYGL_MULTISAMPLE_SGIS               0x809D
#define MYGL_SAMPLE_ALPHA_TO_MASK_SGIS      0x809E
#define MYGL_SAMPLE_ALPHA_TO_ONE_SGIS       0x809F
#define MYGL_SAMPLE_MASK_SGIS               0x80A0
#define MYGL_1PASS_SGIS                     0x80A1
#define MYGL_2PASS_0_SGIS                   0x80A2
#define MYGL_2PASS_1_SGIS                   0x80A3
#define MYGL_4PASS_0_SGIS                   0x80A4
#define MYGL_4PASS_1_SGIS                   0x80A5
#define MYGL_4PASS_2_SGIS                   0x80A6
#define MYGL_4PASS_3_SGIS                   0x80A7
#define MYGL_SAMPLE_BUFFERS_SGIS            0x80A8
#define MYGL_SAMPLES_SGIS                   0x80A9
#define MYGL_SAMPLE_MASK_VALUE_SGIS         0x80AA
#define MYGL_SAMPLE_MASK_INVERT_SGIS        0x80AB
#define MYGL_SAMPLE_PATTERN_SGIS            0x80AC
#endif

#ifndef MYGL_EXT_rescale_normal
#define MYGL_RESCALE_NORMAL_EXT             0x803A
#endif

#ifndef MYGL_EXT_vertex_array
#define MYGL_VERTEX_ARRAY_EXT               0x8074
#define MYGL_NORMAL_ARRAY_EXT               0x8075
#define MYGL_COLOR_ARRAY_EXT                0x8076
#define MYGL_INDEX_ARRAY_EXT                0x8077
#define MYGL_TEXTURE_COORD_ARRAY_EXT        0x8078
#define MYGL_EDGE_FLAG_ARRAY_EXT            0x8079
#define MYGL_VERTEX_ARRAY_SIZE_EXT          0x807A
#define MYGL_VERTEX_ARRAY_TYPE_EXT          0x807B
#define MYGL_VERTEX_ARRAY_STRIDE_EXT        0x807C
#define MYGL_VERTEX_ARRAY_COUNT_EXT         0x807D
#define MYGL_NORMAL_ARRAY_TYPE_EXT          0x807E
#define MYGL_NORMAL_ARRAY_STRIDE_EXT        0x807F
#define MYGL_NORMAL_ARRAY_COUNT_EXT         0x8080
#define MYGL_COLOR_ARRAY_SIZE_EXT           0x8081
#define MYGL_COLOR_ARRAY_TYPE_EXT           0x8082
#define MYGL_COLOR_ARRAY_STRIDE_EXT         0x8083
#define MYGL_COLOR_ARRAY_COUNT_EXT          0x8084
#define MYGL_INDEX_ARRAY_TYPE_EXT           0x8085
#define MYGL_INDEX_ARRAY_STRIDE_EXT         0x8086
#define MYGL_INDEX_ARRAY_COUNT_EXT          0x8087
#define MYGL_TEXTURE_COORD_ARRAY_SIZE_EXT   0x8088
#define MYGL_TEXTURE_COORD_ARRAY_TYPE_EXT   0x8089
#define MYGL_TEXTURE_COORD_ARRAY_STRIDE_EXT 0x808A
#define MYGL_TEXTURE_COORD_ARRAY_COUNT_EXT  0x808B
#define MYGL_EDGE_FLAG_ARRAY_STRIDE_EXT     0x808C
#define MYGL_EDGE_FLAG_ARRAY_COUNT_EXT      0x808D
#define MYGL_VERTEX_ARRAY_POINTER_EXT       0x808E
#define MYGL_NORMAL_ARRAY_POINTER_EXT       0x808F
#define MYGL_COLOR_ARRAY_POINTER_EXT        0x8090
#define MYGL_INDEX_ARRAY_POINTER_EXT        0x8091
#define MYGL_TEXTURE_COORD_ARRAY_POINTER_EXT 0x8092
#define MYGL_EDGE_FLAG_ARRAY_POINTER_EXT    0x8093
#endif

#ifndef MYGL_EXT_misc_attribute
#endif

#ifndef MYGL_SGIS_generate_mipmap
#define MYGL_GENERATE_MIPMAP_SGIS           0x8191
#define MYGL_GENERATE_MIPMAP_HINT_SGIS      0x8192
#endif

#ifndef MYGL_SGIX_clipmap
#define MYGL_LINEAR_CLIPMAP_LINEAR_SGIX     0x8170
#define MYGL_TEXTURE_CLIPMAP_CENTER_SGIX    0x8171
#define MYGL_TEXTURE_CLIPMAP_FRAME_SGIX     0x8172
#define MYGL_TEXTURE_CLIPMAP_OFFSET_SGIX    0x8173
#define MYGL_TEXTURE_CLIPMAP_VIRTUAL_DEPTH_SGIX 0x8174
#define MYGL_TEXTURE_CLIPMAP_LOD_OFFSET_SGIX 0x8175
#define MYGL_TEXTURE_CLIPMAP_DEPTH_SGIX     0x8176
#define MYGL_MAX_CLIPMAP_DEPTH_SGIX         0x8177
#define MYGL_MAX_CLIPMAP_VIRTUAL_DEPTH_SGIX 0x8178
#define MYGL_NEAREST_CLIPMAP_NEAREST_SGIX   0x844D
#define MYGL_NEAREST_CLIPMAP_LINEAR_SGIX    0x844E
#define MYGL_LINEAR_CLIPMAP_NEAREST_SGIX    0x844F
#endif

#ifndef MYGL_SGIX_shadow
#define MYGL_TEXTURE_COMPARE_SGIX           0x819A
#define MYGL_TEXTURE_COMPARE_OPERATOR_SGIX  0x819B
#define MYGL_TEXTURE_LEQUAL_R_SGIX          0x819C
#define MYGL_TEXTURE_GEQUAL_R_SGIX          0x819D
#endif

#ifndef MYGL_SGIS_texture_edge_clamp
#define MYGL_CLAMP_TO_EDGE_SGIS             0x812F
#endif

#ifndef MYGL_SGIS_texture_border_clamp
#define MYGL_CLAMP_TO_BORDER_SGIS           0x812D
#endif

#ifndef MYGL_EXT_blend_minmax
#define MYGL_FUNC_ADD_EXT                   0x8006
#define MYGL_MIN_EXT                        0x8007
#define MYGL_MAX_EXT                        0x8008
#define MYGL_BLEND_EQUATION_EXT             0x8009
#endif

#ifndef MYGL_EXT_blend_subtract
#define MYGL_FUNC_SUBTRACT_EXT              0x800A
#define MYGL_FUNC_REVERSE_SUBTRACT_EXT      0x800B
#endif

#ifndef MYGL_EXT_blend_logic_op
#endif

#ifndef MYGL_SGIX_interlace
#define MYGL_INTERLACE_SGIX                 0x8094
#endif

#ifndef MYGL_SGIX_pixel_tiles
#define MYGL_PIXEL_TILE_BEST_ALIGNMENT_SGIX 0x813E
#define MYGL_PIXEL_TILE_CACHE_INCREMENT_SGIX 0x813F
#define MYGL_PIXEL_TILE_WIDTH_SGIX          0x8140
#define MYGL_PIXEL_TILE_HEIGHT_SGIX         0x8141
#define MYGL_PIXEL_TILE_GRID_WIDTH_SGIX     0x8142
#define MYGL_PIXEL_TILE_GRID_HEIGHT_SGIX    0x8143
#define MYGL_PIXEL_TILE_GRID_DEPTH_SGIX     0x8144
#define MYGL_PIXEL_TILE_CACHE_SIZE_SGIX     0x8145
#endif

#ifndef MYGL_SGIS_texture_select
#define MYGL_DUAL_ALPHA4_SGIS               0x8110
#define MYGL_DUAL_ALPHA8_SGIS               0x8111
#define MYGL_DUAL_ALPHA12_SGIS              0x8112
#define MYGL_DUAL_ALPHA16_SGIS              0x8113
#define MYGL_DUAL_LUMINANCE4_SGIS           0x8114
#define MYGL_DUAL_LUMINANCE8_SGIS           0x8115
#define MYGL_DUAL_LUMINANCE12_SGIS          0x8116
#define MYGL_DUAL_LUMINANCE16_SGIS          0x8117
#define MYGL_DUAL_INTENSITY4_SGIS           0x8118
#define MYGL_DUAL_INTENSITY8_SGIS           0x8119
#define MYGL_DUAL_INTENSITY12_SGIS          0x811A
#define MYGL_DUAL_INTENSITY16_SGIS          0x811B
#define MYGL_DUAL_LUMINANCE_ALPHA4_SGIS     0x811C
#define MYGL_DUAL_LUMINANCE_ALPHA8_SGIS     0x811D
#define MYGL_QUAD_ALPHA4_SGIS               0x811E
#define MYGL_QUAD_ALPHA8_SGIS               0x811F
#define MYGL_QUAD_LUMINANCE4_SGIS           0x8120
#define MYGL_QUAD_LUMINANCE8_SGIS           0x8121
#define MYGL_QUAD_INTENSITY4_SGIS           0x8122
#define MYGL_QUAD_INTENSITY8_SGIS           0x8123
#define MYGL_DUAL_TEXTURE_SELECT_SGIS       0x8124
#define MYGL_QUAD_TEXTURE_SELECT_SGIS       0x8125
#endif

#ifndef MYGL_SGIX_sprite
#define MYGL_SPRITE_SGIX                    0x8148
#define MYGL_SPRITE_MODE_SGIX               0x8149
#define MYGL_SPRITE_AXIS_SGIX               0x814A
#define MYGL_SPRITE_TRANSLATION_SGIX        0x814B
#define MYGL_SPRITE_AXIAL_SGIX              0x814C
#define MYGL_SPRITE_OBJECT_ALIGNED_SGIX     0x814D
#define MYGL_SPRITE_EYE_ALIGNED_SGIX        0x814E
#endif

#ifndef MYGL_SGIX_texture_multi_buffer
#define MYGL_TEXTURE_MULTI_BUFFER_HINT_SGIX 0x812E
#endif

#ifndef MYGL_EXT_point_parameters
#define MYGL_POINT_SIZE_MIN_EXT             0x8126
#define MYGL_POINT_SIZE_MAX_EXT             0x8127
#define MYGL_POINT_FADE_THRESHOLD_SIZE_EXT  0x8128
#define MYGL_DISTANCE_ATTENUATION_EXT       0x8129
#endif

#ifndef MYGL_SGIS_point_parameters
#define MYGL_POINT_SIZE_MIN_SGIS            0x8126
#define MYGL_POINT_SIZE_MAX_SGIS            0x8127
#define MYGL_POINT_FADE_THRESHOLD_SIZE_SGIS 0x8128
#define MYGL_DISTANCE_ATTENUATION_SGIS      0x8129
#endif

#ifndef MYGL_SGIX_instruments
#define MYGL_INSTRUMENT_BUFFER_POINTER_SGIX 0x8180
#define MYGL_INSTRUMENT_MEASUREMENTS_SGIX   0x8181
#endif

#ifndef MYGL_SGIX_texture_scale_bias
#define MYGL_POST_TEXTURE_FILTER_BIAS_SGIX  0x8179
#define MYGL_POST_TEXTURE_FILTER_SCALE_SGIX 0x817A
#define MYGL_POST_TEXTURE_FILTER_BIAS_RANGE_SGIX 0x817B
#define MYGL_POST_TEXTURE_FILTER_SCALE_RANGE_SGIX 0x817C
#endif

#ifndef MYGL_SGIX_framezoom
#define MYGL_FRAMEZOOM_SGIX                 0x818B
#define MYGL_FRAMEZOOM_FACTOR_SGIX          0x818C
#define MYGL_MAX_FRAMEZOOM_FACTOR_SGIX      0x818D
#endif

#ifndef MYGL_SGIX_tag_sample_buffer
#endif

#ifndef MYGL_FfdMaskSGIX
#define MYGL_TEXTURE_DEFORMATION_BIT_SGIX   0x00000001
#define MYGL_GEOMETRY_DEFORMATION_BIT_SGIX  0x00000002
#endif

#ifndef MYGL_SGIX_polynomial_ffd
#define MYGL_GEOMETRY_DEFORMATION_SGIX      0x8194
#define MYGL_TEXTURE_DEFORMATION_SGIX       0x8195
#define MYGL_DEFORMATIONS_MASK_SGIX         0x8196
#define MYGL_MAX_DEFORMATION_ORDER_SGIX     0x8197
#endif

#ifndef MYGL_SGIX_reference_plane
#define MYGL_REFERENCE_PLANE_SGIX           0x817D
#define MYGL_REFERENCE_PLANE_EQUATION_SGIX  0x817E
#endif

#ifndef MYGL_SGIX_flush_raster
#endif

#ifndef MYGL_SGIX_depth_texture
#define MYGL_DEPTH_COMPONENT16_SGIX         0x81A5
#define MYGL_DEPTH_COMPONENT24_SGIX         0x81A6
#define MYGL_DEPTH_COMPONENT32_SGIX         0x81A7
#endif

#ifndef MYGL_SGIS_fog_function
#define MYGL_FOG_FUNC_SGIS                  0x812A
#define MYGL_FOG_FUNC_POINTS_SGIS           0x812B
#define MYGL_MAX_FOG_FUNC_POINTS_SGIS       0x812C
#endif

#ifndef MYGL_SGIX_fog_offset
#define MYGL_FOG_OFFSET_SGIX                0x8198
#define MYGL_FOG_OFFSET_VALUE_SGIX          0x8199
#endif

#ifndef MYGL_HP_image_transform
#define MYGL_IMAGE_SCALE_X_HP               0x8155
#define MYGL_IMAGE_SCALE_Y_HP               0x8156
#define MYGL_IMAGE_TRANSLATE_X_HP           0x8157
#define MYGL_IMAGE_TRANSLATE_Y_HP           0x8158
#define MYGL_IMAGE_ROTATE_ANGLE_HP          0x8159
#define MYGL_IMAGE_ROTATE_ORIGIN_X_HP       0x815A
#define MYGL_IMAGE_ROTATE_ORIGIN_Y_HP       0x815B
#define MYGL_IMAGE_MAG_FILTER_HP            0x815C
#define MYGL_IMAGE_MIN_FILTER_HP            0x815D
#define MYGL_IMAGE_CUBIC_WEIGHT_HP          0x815E
#define MYGL_CUBIC_HP                       0x815F
#define MYGL_AVERAGE_HP                     0x8160
#define MYGL_IMAGE_TRANSFORM_2D_HP          0x8161
#define MYGL_POST_IMAGE_TRANSFORM_COLOR_TABLE_HP 0x8162
#define MYGL_PROXY_POST_IMAGE_TRANSFORM_COLOR_TABLE_HP 0x8163
#endif

#ifndef MYGL_HP_convolution_border_modes
#define MYGL_IGNORE_BORDER_HP               0x8150
#define MYGL_CONSTANT_BORDER_HP             0x8151
#define MYGL_REPLICATE_BORDER_HP            0x8153
#define MYGL_CONVOLUTION_BORDER_COLOR_HP    0x8154
#endif

#ifndef MYGL_INGR_palette_buffer
#endif

#ifndef MYGL_SGIX_texture_add_env
#define MYGL_TEXTURE_ENV_BIAS_SGIX          0x80BE
#endif

#ifndef MYGL_EXT_color_subtable
#endif

#ifndef MYGL_PGI_vertex_hints
#define MYGL_VERTEX_DATA_HINT_PGI           0x1A22A
#define MYGL_VERTEX_CONSISTENT_HINT_PGI     0x1A22B
#define MYGL_MATERIAL_SIDE_HINT_PGI         0x1A22C
#define MYGL_MAX_VERTEX_HINT_PGI            0x1A22D
#define MYGL_COLOR3_BIT_PGI                 0x00010000
#define MYGL_COLOR4_BIT_PGI                 0x00020000
#define MYGL_EDGEFLAG_BIT_PGI               0x00040000
#define MYGL_INDEX_BIT_PGI                  0x00080000
#define MYGL_MAT_AMBIENT_BIT_PGI            0x00100000
#define MYGL_MAT_AMBIENT_AND_DIFFUSE_BIT_PGI 0x00200000
#define MYGL_MAT_DIFFUSE_BIT_PGI            0x00400000
#define MYGL_MAT_EMISSION_BIT_PGI           0x00800000
#define MYGL_MAT_COLOR_INDEXES_BIT_PGI      0x01000000
#define MYGL_MAT_SHININESS_BIT_PGI          0x02000000
#define MYGL_MAT_SPECULAR_BIT_PGI           0x04000000
#define MYGL_NORMAL_BIT_PGI                 0x08000000
#define MYGL_TEXCOORD1_BIT_PGI              0x10000000
#define MYGL_TEXCOORD2_BIT_PGI              0x20000000
#define MYGL_TEXCOORD3_BIT_PGI              0x40000000
#define MYGL_TEXCOORD4_BIT_PGI              0x80000000
#define MYGL_VERTEX23_BIT_PGI               0x00000004
#define MYGL_VERTEX4_BIT_PGI                0x00000008
#endif

#ifndef MYGL_PGI_misc_hints
#define MYGL_PREFER_DOUBLEBUFFER_HINT_PGI   0x1A1F8
#define MYGL_CONSERVE_MEMORY_HINT_PGI       0x1A1FD
#define MYGL_RECLAIM_MEMORY_HINT_PGI        0x1A1FE
#define MYGL_NATIVE_GRAPHICS_HANDLE_PGI     0x1A202
#define MYGL_NATIVE_GRAPHICS_BEGIN_HINT_PGI 0x1A203
#define MYGL_NATIVE_GRAPHICS_END_HINT_PGI   0x1A204
#define MYGL_ALWAYS_FAST_HINT_PGI           0x1A20C
#define MYGL_ALWAYS_SOFT_HINT_PGI           0x1A20D
#define MYGL_ALLOW_DRAW_OBJ_HINT_PGI        0x1A20E
#define MYGL_ALLOW_DRAW_WIN_HINT_PGI        0x1A20F
#define MYGL_ALLOW_DRAW_FRG_HINT_PGI        0x1A210
#define MYGL_ALLOW_DRAW_MEM_HINT_PGI        0x1A211
#define MYGL_STRICT_DEPTHFUNC_HINT_PGI      0x1A216
#define MYGL_STRICT_LIGHTING_HINT_PGI       0x1A217
#define MYGL_STRICT_SCISSOR_HINT_PGI        0x1A218
#define MYGL_FULL_STIPPLE_HINT_PGI          0x1A219
#define MYGL_CLIP_NEAR_HINT_PGI             0x1A220
#define MYGL_CLIP_FAR_HINT_PGI              0x1A221
#define MYGL_WIDE_LINE_HINT_PGI             0x1A222
#define MYGL_BACK_NORMALS_HINT_PGI          0x1A223
#endif

#ifndef MYGL_EXT_paletted_texture
#define MYGL_COLOR_INDEX1_EXT               0x80E2
#define MYGL_COLOR_INDEX2_EXT               0x80E3
#define MYGL_COLOR_INDEX4_EXT               0x80E4
#define MYGL_COLOR_INDEX8_EXT               0x80E5
#define MYGL_COLOR_INDEX12_EXT              0x80E6
#define MYGL_COLOR_INDEX16_EXT              0x80E7
#define MYGL_TEXTURE_INDEX_SIZE_EXT         0x80ED
#endif

#ifndef MYGL_EXT_clip_volume_hint
#define MYGL_CLIP_VOLUME_CLIPPING_HINT_EXT  0x80F0
#endif

#ifndef MYGL_SGIX_list_priority
#define MYGL_LIST_PRIORITY_SGIX             0x8182
#endif

#ifndef MYGL_SGIX_ir_instrument1
#define MYGL_IR_INSTRUMENT1_SGIX            0x817F
#endif

#ifndef MYGL_SGIX_calligraphic_fragment
#define MYGL_CALLIGRAPHIC_FRAGMENT_SGIX     0x8183
#endif

#ifndef MYGL_SGIX_texture_lod_bias
#define MYGL_TEXTURE_LOD_BIAS_S_SGIX        0x818E
#define MYGL_TEXTURE_LOD_BIAS_T_SGIX        0x818F
#define MYGL_TEXTURE_LOD_BIAS_R_SGIX        0x8190
#endif

#ifndef MYGL_SGIX_shadow_ambient
#define MYGL_SHADOW_AMBIENT_SGIX            0x80BF
#endif

#ifndef MYGL_EXT_index_texture
#endif

#ifndef MYGL_EXT_index_material
#define MYGL_INDEX_MATERIAL_EXT             0x81B8
#define MYGL_INDEX_MATERIAL_PARAMETER_EXT   0x81B9
#define MYGL_INDEX_MATERIAL_FACE_EXT        0x81BA
#endif

#ifndef MYGL_EXT_index_func
#define MYGL_INDEX_TEST_EXT                 0x81B5
#define MYGL_INDEX_TEST_FUNC_EXT            0x81B6
#define MYGL_INDEX_TEST_REF_EXT             0x81B7
#endif

#ifndef MYGL_EXT_index_array_formats
#define MYGL_IUI_V2F_EXT                    0x81AD
#define MYGL_IUI_V3F_EXT                    0x81AE
#define MYGL_IUI_N3F_V2F_EXT                0x81AF
#define MYGL_IUI_N3F_V3F_EXT                0x81B0
#define MYGL_T2F_IUI_V2F_EXT                0x81B1
#define MYGL_T2F_IUI_V3F_EXT                0x81B2
#define MYGL_T2F_IUI_N3F_V2F_EXT            0x81B3
#define MYGL_T2F_IUI_N3F_V3F_EXT            0x81B4
#endif

#ifndef MYGL_EXT_compiled_vertex_array
#define MYGL_ARRAY_ELEMENT_LOCK_FIRST_EXT   0x81A8
#define MYGL_ARRAY_ELEMENT_LOCK_COUNT_EXT   0x81A9
#endif

#ifndef MYGL_EXT_cull_vertex
#define MYGL_CULL_VERTEX_EXT                0x81AA
#define MYGL_CULL_VERTEX_EYE_POSITION_EXT   0x81AB
#define MYGL_CULL_VERTEX_OBJECT_POSITION_EXT 0x81AC
#endif

#ifndef MYGL_SGIX_ycrcb
#define MYGL_YCRCB_422_SGIX                 0x81BB
#define MYGL_YCRCB_444_SGIX                 0x81BC
#endif

#ifndef MYGL_SGIX_fragment_lighting
#define MYGL_FRAGMENT_LIGHTING_SGIX         0x8400
#define MYGL_FRAGMENT_COLOR_MATERIAL_SGIX   0x8401
#define MYGL_FRAGMENT_COLOR_MATERIAL_FACE_SGIX 0x8402
#define MYGL_FRAGMENT_COLOR_MATERIAL_PARAMETER_SGIX 0x8403
#define MYGL_MAX_FRAGMENT_LIGHTS_SGIX       0x8404
#define MYGL_MAX_ACTIVE_LIGHTS_SGIX         0x8405
#define MYGL_CURRENT_RASTER_NORMAL_SGIX     0x8406
#define MYGL_LIGHT_ENV_MODE_SGIX            0x8407
#define MYGL_FRAGMENT_LIGHT_MODEL_LOCAL_VIEWER_SGIX 0x8408
#define MYGL_FRAGMENT_LIGHT_MODEL_TWO_SIDE_SGIX 0x8409
#define MYGL_FRAGMENT_LIGHT_MODEL_AMBIENT_SGIX 0x840A
#define MYGL_FRAGMENT_LIGHT_MODEL_NORMAL_INTERPOLATION_SGIX 0x840B
#define MYGL_FRAGMENT_LIGHT0_SGIX           0x840C
#define MYGL_FRAGMENT_LIGHT1_SGIX           0x840D
#define MYGL_FRAGMENT_LIGHT2_SGIX           0x840E
#define MYGL_FRAGMENT_LIGHT3_SGIX           0x840F
#define MYGL_FRAGMENT_LIGHT4_SGIX           0x8410
#define MYGL_FRAGMENT_LIGHT5_SGIX           0x8411
#define MYGL_FRAGMENT_LIGHT6_SGIX           0x8412
#define MYGL_FRAGMENT_LIGHT7_SGIX           0x8413
#endif

#ifndef MYGL_IBM_rasterpos_clip
#define MYGL_RASTER_POSITION_UNCLIPPED_IBM  0x19262
#endif

#ifndef MYGL_HP_texture_lighting
#define MYGL_TEXTURE_LIGHTING_MODE_HP       0x8167
#define MYGL_TEXTURE_POST_SPECULAR_HP       0x8168
#define MYGL_TEXTURE_PRE_SPECULAR_HP        0x8169
#endif

#ifndef MYGL_EXT_draw_range_elements
#define MYGL_MAX_ELEMENTS_VERTICES_EXT      0x80E8
#define MYGL_MAX_ELEMENTS_INDICES_EXT       0x80E9
#endif

#ifndef MYGL_WIN_phong_shading
#define MYGL_PHONG_WIN                      0x80EA
#define MYGL_PHONG_HINT_WIN                 0x80EB
#endif

#ifndef MYGL_WIN_specular_fog
#define MYGL_FOG_SPECULAR_TEXTURE_WIN       0x80EC
#endif

#ifndef MYGL_EXT_light_texture
#define MYGL_FRAGMENT_MATERIAL_EXT          0x8349
#define MYGL_FRAGMENT_NORMAL_EXT            0x834A
#define MYGL_FRAGMENT_COLOR_EXT             0x834C
#define MYGL_ATTENUATION_EXT                0x834D
#define MYGL_SHADOW_ATTENUATION_EXT         0x834E
#define MYGL_TEXTURE_APPLICATION_MODE_EXT   0x834F
#define MYGL_TEXTURE_LIGHT_EXT              0x8350
#define MYGL_TEXTURE_MATERIAL_FACE_EXT      0x8351
#define MYGL_TEXTURE_MATERIAL_PARAMETER_EXT 0x8352
/* reuse MYGL_FRAGMENT_DEPTH_EXT */
#endif

#ifndef MYGL_SGIX_blend_alpha_minmax
#define MYGL_ALPHA_MIN_SGIX                 0x8320
#define MYGL_ALPHA_MAX_SGIX                 0x8321
#endif

#ifndef MYGL_SGIX_impact_pixel_texture
#define MYGL_PIXEL_TEX_GEN_Q_CEILING_SGIX   0x8184
#define MYGL_PIXEL_TEX_GEN_Q_ROUND_SGIX     0x8185
#define MYGL_PIXEL_TEX_GEN_Q_FLOOR_SGIX     0x8186
#define MYGL_PIXEL_TEX_GEN_ALPHA_REPLACE_SGIX 0x8187
#define MYGL_PIXEL_TEX_GEN_ALPHA_NO_REPLACE_SGIX 0x8188
#define MYGL_PIXEL_TEX_GEN_ALPHA_LS_SGIX    0x8189
#define MYGL_PIXEL_TEX_GEN_ALPHA_MS_SGIX    0x818A
#endif

#ifndef MYGL_EXT_bgra
#define MYGL_BGR_EXT                        0x80E0
#define MYGL_BGRA_EXT                       0x80E1
#endif

#ifndef MYGL_SGIX_async
#define MYGL_ASYNC_MARKER_SGIX              0x8329
#endif

#ifndef MYGL_SGIX_async_pixel
#define MYGL_ASYNC_TEX_IMAGE_SGIX           0x835C
#define MYGL_ASYNC_DRAW_PIXELS_SGIX         0x835D
#define MYGL_ASYNC_READ_PIXELS_SGIX         0x835E
#define MYGL_MAX_ASYNC_TEX_IMAGE_SGIX       0x835F
#define MYGL_MAX_ASYNC_DRAW_PIXELS_SGIX     0x8360
#define MYGL_MAX_ASYNC_READ_PIXELS_SGIX     0x8361
#endif

#ifndef MYGL_SGIX_async_histogram
#define MYGL_ASYNC_HISTOGRAM_SGIX           0x832C
#define MYGL_MAX_ASYNC_HISTOGRAM_SGIX       0x832D
#endif

#ifndef MYGL_INTEL_texture_scissor
#endif

#ifndef MYGL_INTEL_parallel_arrays
#define MYGL_PARALLEL_ARRAYS_INTEL          0x83F4
#define MYGL_VERTEX_ARRAY_PARALLEL_POINTERS_INTEL 0x83F5
#define MYGL_NORMAL_ARRAY_PARALLEL_POINTERS_INTEL 0x83F6
#define MYGL_COLOR_ARRAY_PARALLEL_POINTERS_INTEL 0x83F7
#define MYGL_TEXTURE_COORD_ARRAY_PARALLEL_POINTERS_INTEL 0x83F8
#endif

#ifndef MYGL_HP_occlusion_test
#define MYGL_OCCLUSION_TEST_HP              0x8165
#define MYGL_OCCLUSION_TEST_RESULT_HP       0x8166
#endif

#ifndef MYGL_EXT_pixel_transform
#define MYGL_PIXEL_TRANSFORM_2D_EXT         0x8330
#define MYGL_PIXEL_MAG_FILTER_EXT           0x8331
#define MYGL_PIXEL_MIN_FILTER_EXT           0x8332
#define MYGL_PIXEL_CUBIC_WEIGHT_EXT         0x8333
#define MYGL_CUBIC_EXT                      0x8334
#define MYGL_AVERAGE_EXT                    0x8335
#define MYGL_PIXEL_TRANSFORM_2D_STACK_DEPTH_EXT 0x8336
#define MYGL_MAX_PIXEL_TRANSFORM_2D_STACK_DEPTH_EXT 0x8337
#define MYGL_PIXEL_TRANSFORM_2D_MATRIX_EXT  0x8338
#endif

#ifndef MYGL_EXT_pixel_transform_color_table
#endif

#ifndef MYGL_EXT_shared_texture_palette
#define MYGL_SHARED_TEXTURE_PALETTE_EXT     0x81FB
#endif

#ifndef MYGL_EXT_separate_specular_color
#define MYGL_LIGHT_MODEL_COLOR_CONTROL_EXT  0x81F8
#define MYGL_SINGLE_COLOR_EXT               0x81F9
#define MYGL_SEPARATE_SPECULAR_COLOR_EXT    0x81FA
#endif

#ifndef MYGL_EXT_secondary_color
#define MYGL_COLOR_SUM_EXT                  0x8458
#define MYGL_CURRENT_SECONDARY_COLOR_EXT    0x8459
#define MYGL_SECONDARY_COLOR_ARRAY_SIZE_EXT 0x845A
#define MYGL_SECONDARY_COLOR_ARRAY_TYPE_EXT 0x845B
#define MYGL_SECONDARY_COLOR_ARRAY_STRIDE_EXT 0x845C
#define MYGL_SECONDARY_COLOR_ARRAY_POINTER_EXT 0x845D
#define MYGL_SECONDARY_COLOR_ARRAY_EXT      0x845E
#endif

#ifndef MYGL_EXT_texture_perturb_normal
#define MYGL_PERTURB_EXT                    0x85AE
#define MYGL_TEXTURE_NORMAL_EXT             0x85AF
#endif

#ifndef MYGL_EXT_multi_draw_arrays
#endif

#ifndef MYGL_EXT_fog_coord
#define MYGL_FOG_COORDINATE_SOURCE_EXT      0x8450
#define MYGL_FOG_COORDINATE_EXT             0x8451
#define MYGL_FRAGMENT_DEPTH_EXT             0x8452
#define MYGL_CURRENT_FOG_COORDINATE_EXT     0x8453
#define MYGL_FOG_COORDINATE_ARRAY_TYPE_EXT  0x8454
#define MYGL_FOG_COORDINATE_ARRAY_STRIDE_EXT 0x8455
#define MYGL_FOG_COORDINATE_ARRAY_POINTER_EXT 0x8456
#define MYGL_FOG_COORDINATE_ARRAY_EXT       0x8457
#endif

#ifndef MYGL_REND_screen_coordinates
#define MYGL_SCREEN_COORDINATES_REND        0x8490
#define MYGL_INVERTED_SCREEN_W_REND         0x8491
#endif

#ifndef MYGL_EXT_coordinate_frame
#define MYGL_TANGENT_ARRAY_EXT              0x8439
#define MYGL_BINORMAL_ARRAY_EXT             0x843A
#define MYGL_CURRENT_TANGENT_EXT            0x843B
#define MYGL_CURRENT_BINORMAL_EXT           0x843C
#define MYGL_TANGENT_ARRAY_TYPE_EXT         0x843E
#define MYGL_TANGENT_ARRAY_STRIDE_EXT       0x843F
#define MYGL_BINORMAL_ARRAY_TYPE_EXT        0x8440
#define MYGL_BINORMAL_ARRAY_STRIDE_EXT      0x8441
#define MYGL_TANGENT_ARRAY_POINTER_EXT      0x8442
#define MYGL_BINORMAL_ARRAY_POINTER_EXT     0x8443
#define MYGL_MAP1_TANGENT_EXT               0x8444
#define MYGL_MAP2_TANGENT_EXT               0x8445
#define MYGL_MAP1_BINORMAL_EXT              0x8446
#define MYGL_MAP2_BINORMAL_EXT              0x8447
#endif

#ifndef MYGL_EXT_texture_env_combine
#define MYGL_COMBINE_EXT                    0x8570
#define MYGL_COMBINE_RGB_EXT                0x8571
#define MYGL_COMBINE_ALPHA_EXT              0x8572
#define MYGL_RGB_SCALE_EXT                  0x8573
#define MYGL_ADD_SIGNED_EXT                 0x8574
#define MYGL_INTERPOLATE_EXT                0x8575
#define MYGL_CONSTANT_EXT                   0x8576
#define MYGL_PRIMARY_COLOR_EXT              0x8577
#define MYGL_PREVIOUS_EXT                   0x8578
#define MYGL_SOURCE0_RGB_EXT                0x8580
#define MYGL_SOURCE1_RGB_EXT                0x8581
#define MYGL_SOURCE2_RGB_EXT                0x8582
#define MYGL_SOURCE0_ALPHA_EXT              0x8588
#define MYGL_SOURCE1_ALPHA_EXT              0x8589
#define MYGL_SOURCE2_ALPHA_EXT              0x858A
#define MYGL_OPERAND0_RGB_EXT               0x8590
#define MYGL_OPERAND1_RGB_EXT               0x8591
#define MYGL_OPERAND2_RGB_EXT               0x8592
#define MYGL_OPERAND0_ALPHA_EXT             0x8598
#define MYGL_OPERAND1_ALPHA_EXT             0x8599
#define MYGL_OPERAND2_ALPHA_EXT             0x859A
#endif

#ifndef MYGL_APPLE_specular_vector
#define MYGL_LIGHT_MODEL_SPECULAR_VECTOR_APPLE 0x85B0
#endif

#ifndef MYGL_APPLE_transform_hint
#define MYGL_TRANSFORM_HINT_APPLE           0x85B1
#endif

#ifndef MYGL_SGIX_fog_scale
#define MYGL_FOG_SCALE_SGIX                 0x81FC
#define MYGL_FOG_SCALE_VALUE_SGIX           0x81FD
#endif

#ifndef MYGL_SUNX_constant_data
#define MYGL_UNPACK_CONSTANT_DATA_SUNX      0x81D5
#define MYGL_TEXTURE_CONSTANT_DATA_SUNX     0x81D6
#endif

#ifndef MYGL_SUN_global_alpha
#define MYGL_GLOBAL_ALPHA_SUN               0x81D9
#define MYGL_GLOBAL_ALPHA_FACTOR_SUN        0x81DA
#endif

#ifndef MYGL_SUN_triangle_list
#define MYGL_RESTART_SUN                    0x0001
#define MYGL_REPLACE_MIDDLE_SUN             0x0002
#define MYGL_REPLACE_OLDEST_SUN             0x0003
#define MYGL_TRIANGLE_LIST_SUN              0x81D7
#define MYGL_REPLACEMENT_CODE_SUN           0x81D8
#define MYGL_REPLACEMENT_CODE_ARRAY_SUN     0x85C0
#define MYGL_REPLACEMENT_CODE_ARRAY_TYPE_SUN 0x85C1
#define MYGL_REPLACEMENT_CODE_ARRAY_STRIDE_SUN 0x85C2
#define MYGL_REPLACEMENT_CODE_ARRAY_POINTER_SUN 0x85C3
#define MYGL_R1UI_V3F_SUN                   0x85C4
#define MYGL_R1UI_C4UB_V3F_SUN              0x85C5
#define MYGL_R1UI_C3F_V3F_SUN               0x85C6
#define MYGL_R1UI_N3F_V3F_SUN               0x85C7
#define MYGL_R1UI_C4F_N3F_V3F_SUN           0x85C8
#define MYGL_R1UI_T2F_V3F_SUN               0x85C9
#define MYGL_R1UI_T2F_N3F_V3F_SUN           0x85CA
#define MYGL_R1UI_T2F_C4F_N3F_V3F_SUN       0x85CB
#endif

#ifndef MYGL_SUN_vertex
#endif

#ifndef MYGL_EXT_blend_func_separate
#define MYGL_BLEND_DST_RGB_EXT              0x80C8
#define MYGL_BLEND_SRC_RGB_EXT              0x80C9
#define MYGL_BLEND_DST_ALPHA_EXT            0x80CA
#define MYGL_BLEND_SRC_ALPHA_EXT            0x80CB
#endif

#ifndef MYGL_INGR_color_clamp
#define MYGL_RED_MIN_CLAMP_INGR             0x8560
#define MYGL_GREEN_MIN_CLAMP_INGR           0x8561
#define MYGL_BLUE_MIN_CLAMP_INGR            0x8562
#define MYGL_ALPHA_MIN_CLAMP_INGR           0x8563
#define MYGL_RED_MAX_CLAMP_INGR             0x8564
#define MYGL_GREEN_MAX_CLAMP_INGR           0x8565
#define MYGL_BLUE_MAX_CLAMP_INGR            0x8566
#define MYGL_ALPHA_MAX_CLAMP_INGR           0x8567
#endif

#ifndef MYGL_INGR_interlace_read
#define MYGL_INTERLACE_READ_INGR            0x8568
#endif

#ifndef MYGL_EXT_stencil_wrap
#define MYGL_INCR_WRAP_EXT                  0x8507
#define MYGL_DECR_WRAP_EXT                  0x8508
#endif

#ifndef MYGL_EXT_422_pixels
#define MYGL_422_EXT                        0x80CC
#define MYGL_422_REV_EXT                    0x80CD
#define MYGL_422_AVERAGE_EXT                0x80CE
#define MYGL_422_REV_AVERAGE_EXT            0x80CF
#endif

#ifndef MYGL_NV_texgen_reflection
#define MYGL_NORMAL_MAP_NV                  0x8511
#define MYGL_REFLECTION_MAP_NV              0x8512
#endif

#ifndef MYGL_EXT_texture_cube_map
#define MYGL_NORMAL_MAP_EXT                 0x8511
#define MYGL_REFLECTION_MAP_EXT             0x8512
#define MYGL_TEXTURE_CUBE_MAP_EXT           0x8513
#define MYGL_TEXTURE_BINDING_CUBE_MAP_EXT   0x8514
#define MYGL_TEXTURE_CUBE_MAP_POSITIVE_X_EXT 0x8515
#define MYGL_TEXTURE_CUBE_MAP_NEGATIVE_X_EXT 0x8516
#define MYGL_TEXTURE_CUBE_MAP_POSITIVE_Y_EXT 0x8517
#define MYGL_TEXTURE_CUBE_MAP_NEGATIVE_Y_EXT 0x8518
#define MYGL_TEXTURE_CUBE_MAP_POSITIVE_Z_EXT 0x8519
#define MYGL_TEXTURE_CUBE_MAP_NEGATIVE_Z_EXT 0x851A
#define MYGL_PROXY_TEXTURE_CUBE_MAP_EXT     0x851B
#define MYGL_MAX_CUBE_MAP_TEXTURE_SIZE_EXT  0x851C
#endif

#ifndef MYGL_SUN_convolution_border_modes
#define MYGL_WRAP_BORDER_SUN                0x81D4
#endif

#ifndef MYGL_EXT_texture_env_add
#endif

#ifndef MYGL_EXT_texture_lod_bias
#define MYGL_MAX_TEXTURE_LOD_BIAS_EXT       0x84FD
#define MYGL_TEXTURE_FILTER_CONTROL_EXT     0x8500
#define MYGL_TEXTURE_LOD_BIAS_EXT           0x8501
#endif

#ifndef MYGL_EXT_texture_filter_anisotropic
#define MYGL_TEXTURE_MAX_ANISOTROPY_EXT     0x84FE
#define MYGL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

#ifndef MYGL_EXT_vertex_weighting
#define MYGL_MODELVIEW0_STACK_DEPTH_EXT     MYGL_MODELVIEW_STACK_DEPTH
#define MYGL_MODELVIEW1_STACK_DEPTH_EXT     0x8502
#define MYGL_MODELVIEW0_MATRIX_EXT          MYGL_MODELVIEW_MATRIX
#define MYGL_MODELVIEW1_MATRIX_EXT          0x8506
#define MYGL_VERTEX_WEIGHTING_EXT           0x8509
#define MYGL_MODELVIEW0_EXT                 MYGL_MODELVIEW
#define MYGL_MODELVIEW1_EXT                 0x850A
#define MYGL_CURRENT_VERTEX_WEIGHT_EXT      0x850B
#define MYGL_VERTEX_WEIGHT_ARRAY_EXT        0x850C
#define MYGL_VERTEX_WEIGHT_ARRAY_SIZE_EXT   0x850D
#define MYGL_VERTEX_WEIGHT_ARRAY_TYPE_EXT   0x850E
#define MYGL_VERTEX_WEIGHT_ARRAY_STRIDE_EXT 0x850F
#define MYGL_VERTEX_WEIGHT_ARRAY_POINTER_EXT 0x8510
#endif

#ifndef MYGL_NV_light_max_exponent
#define MYGL_MAX_SHININESS_NV               0x8504
#define MYGL_MAX_SPOT_EXPONENT_NV           0x8505
#endif

#ifndef MYGL_NV_vertex_array_range
#define MYGL_VERTEX_ARRAY_RANGE_NV          0x851D
#define MYGL_VERTEX_ARRAY_RANGE_LENGTH_NV   0x851E
#define MYGL_VERTEX_ARRAY_RANGE_VALID_NV    0x851F
#define MYGL_MAX_VERTEX_ARRAY_RANGE_ELEMENT_NV 0x8520
#define MYGL_VERTEX_ARRAY_RANGE_POINTER_NV  0x8521
#endif

#ifndef MYGL_NV_register_combiners
#define MYGL_REGISTER_COMBINERS_NV          0x8522
#define MYGL_VARIABLE_A_NV                  0x8523
#define MYGL_VARIABLE_B_NV                  0x8524
#define MYGL_VARIABLE_C_NV                  0x8525
#define MYGL_VARIABLE_D_NV                  0x8526
#define MYGL_VARIABLE_E_NV                  0x8527
#define MYGL_VARIABLE_F_NV                  0x8528
#define MYGL_VARIABLE_G_NV                  0x8529
#define MYGL_CONSTANT_COLOR0_NV             0x852A
#define MYGL_CONSTANT_COLOR1_NV             0x852B
#define MYGL_PRIMARY_COLOR_NV               0x852C
#define MYGL_SECONDARY_COLOR_NV             0x852D
#define MYGL_SPARE0_NV                      0x852E
#define MYGL_SPARE1_NV                      0x852F
#define MYGL_DISCARD_NV                     0x8530
#define MYGL_E_TIMES_F_NV                   0x8531
#define MYGL_SPARE0_PLUS_SECONDARY_COLOR_NV 0x8532
#define MYGL_UNSIGNED_IDENTITY_NV           0x8536
#define MYGL_UNSIGNED_INVERT_NV             0x8537
#define MYGL_EXPAND_NORMAL_NV               0x8538
#define MYGL_EXPAND_NEGATE_NV               0x8539
#define MYGL_HALF_BIAS_NORMAL_NV            0x853A
#define MYGL_HALF_BIAS_NEGATE_NV            0x853B
#define MYGL_SIGNED_IDENTITY_NV             0x853C
#define MYGL_SIGNED_NEGATE_NV               0x853D
#define MYGL_SCALE_BY_TWO_NV                0x853E
#define MYGL_SCALE_BY_FOUR_NV               0x853F
#define MYGL_SCALE_BY_ONE_HALF_NV           0x8540
#define MYGL_BIAS_BY_NEGATIVE_ONE_HALF_NV   0x8541
#define MYGL_COMBINER_INPUT_NV              0x8542
#define MYGL_COMBINER_MAPPING_NV            0x8543
#define MYGL_COMBINER_COMPONENT_USAGE_NV    0x8544
#define MYGL_COMBINER_AB_DOT_PRODUCT_NV     0x8545
#define MYGL_COMBINER_CD_DOT_PRODUCT_NV     0x8546
#define MYGL_COMBINER_MUX_SUM_NV            0x8547
#define MYGL_COMBINER_SCALE_NV              0x8548
#define MYGL_COMBINER_BIAS_NV               0x8549
#define MYGL_COMBINER_AB_OUTPUT_NV          0x854A
#define MYGL_COMBINER_CD_OUTPUT_NV          0x854B
#define MYGL_COMBINER_SUM_OUTPUT_NV         0x854C
#define MYGL_MAX_GENERAL_COMBINERS_NV       0x854D
#define MYGL_NUM_GENERAL_COMBINERS_NV       0x854E
#define MYGL_COLOR_SUM_CLAMP_NV             0x854F
#define MYGL_COMBINER0_NV                   0x8550
#define MYGL_COMBINER1_NV                   0x8551
#define MYGL_COMBINER2_NV                   0x8552
#define MYGL_COMBINER3_NV                   0x8553
#define MYGL_COMBINER4_NV                   0x8554
#define MYGL_COMBINER5_NV                   0x8555
#define MYGL_COMBINER6_NV                   0x8556
#define MYGL_COMBINER7_NV                   0x8557
/* reuse MYGL_TEXTURE0_ARB */
/* reuse MYGL_TEXTURE1_ARB */
/* reuse MYGL_ZERO */
/* reuse MYGL_NONE */
/* reuse MYGL_FOG */
#endif

#ifndef MYGL_NV_fog_distance
#define MYGL_FOG_DISTANCE_MODE_NV           0x855A
#define MYGL_EYE_RADIAL_NV                  0x855B
#define MYGL_EYE_PLANE_ABSOLUTE_NV          0x855C
/* reuse MYGL_EYE_PLANE */
#endif

#ifndef MYGL_NV_texgen_emboss
#define MYGL_EMBOSS_LIGHT_NV                0x855D
#define MYGL_EMBOSS_CONSTANT_NV             0x855E
#define MYGL_EMBOSS_MAP_NV                  0x855F
#endif

#ifndef MYGL_NV_blend_square
#endif

#ifndef MYGL_NV_texture_env_combine4
#define MYGL_COMBINE4_NV                    0x8503
#define MYGL_SOURCE3_RGB_NV                 0x8583
#define MYGL_SOURCE3_ALPHA_NV               0x858B
#define MYGL_OPERAND3_RGB_NV                0x8593
#define MYGL_OPERAND3_ALPHA_NV              0x859B
#endif

#ifndef MYGL_MESA_resize_buffers
#endif

#ifndef MYGL_MESA_window_pos
#endif

#ifndef MYGL_EXT_texture_compression_s3tc
#define MYGL_COMPRESSED_RGB_S3TC_DXT1_EXT   0x83F0
#define MYGL_COMPRESSED_RGBA_S3TC_DXT1_EXT  0x83F1
#define MYGL_COMPRESSED_RGBA_S3TC_DXT3_EXT  0x83F2
#define MYGL_COMPRESSED_RGBA_S3TC_DXT5_EXT  0x83F3
#endif

#ifndef MYGL_IBM_cull_vertex
#define MYGL_CULL_VERTEX_IBM                103050
#endif

#ifndef MYGL_IBM_multimode_draw_arrays
#endif

#ifndef MYGL_IBM_vertex_array_lists
#define MYGL_VERTEX_ARRAY_LIST_IBM          103070
#define MYGL_NORMAL_ARRAY_LIST_IBM          103071
#define MYGL_COLOR_ARRAY_LIST_IBM           103072
#define MYGL_INDEX_ARRAY_LIST_IBM           103073
#define MYGL_TEXTURE_COORD_ARRAY_LIST_IBM   103074
#define MYGL_EDGE_FLAG_ARRAY_LIST_IBM       103075
#define MYGL_FOG_COORDINATE_ARRAY_LIST_IBM  103076
#define MYGL_SECONDARY_COLOR_ARRAY_LIST_IBM 103077
#define MYGL_VERTEX_ARRAY_LIST_STRIDE_IBM   103080
#define MYGL_NORMAL_ARRAY_LIST_STRIDE_IBM   103081
#define MYGL_COLOR_ARRAY_LIST_STRIDE_IBM    103082
#define MYGL_INDEX_ARRAY_LIST_STRIDE_IBM    103083
#define MYGL_TEXTURE_COORD_ARRAY_LIST_STRIDE_IBM 103084
#define MYGL_EDGE_FLAG_ARRAY_LIST_STRIDE_IBM 103085
#define MYGL_FOG_COORDINATE_ARRAY_LIST_STRIDE_IBM 103086
#define MYGL_SECONDARY_COLOR_ARRAY_LIST_STRIDE_IBM 103087
#endif

#ifndef MYGL_SGIX_subsample
#define MYGL_PACK_SUBSAMPLE_RATE_SGIX       0x85A0
#define MYGL_UNPACK_SUBSAMPLE_RATE_SGIX     0x85A1
#define MYGL_PIXEL_SUBSAMPLE_4444_SGIX      0x85A2
#define MYGL_PIXEL_SUBSAMPLE_2424_SGIX      0x85A3
#define MYGL_PIXEL_SUBSAMPLE_4242_SGIX      0x85A4
#endif

#ifndef MYGL_SGIX_ycrcb_subsample
#endif

#ifndef MYGL_SGIX_ycrcba
#define MYGL_YCRCB_SGIX                     0x8318
#define MYGL_YCRCBA_SGIX                    0x8319
#endif

#ifndef MYGL_SGI_depth_pass_instrument
#define MYGL_DEPTH_PASS_INSTRUMENT_SGIX     0x8310
#define MYGL_DEPTH_PASS_INSTRUMENT_COUNTERS_SGIX 0x8311
#define MYGL_DEPTH_PASS_INSTRUMENT_MAX_SGIX 0x8312
#endif

#ifndef MYGL_3DFX_texture_compression_FXT1
#define MYGL_COMPRESSED_RGB_FXT1_3DFX       0x86B0
#define MYGL_COMPRESSED_RGBA_FXT1_3DFX      0x86B1
#endif

#ifndef MYGL_3DFX_multisample
#define MYGL_MULTISAMPLE_3DFX               0x86B2
#define MYGL_SAMPLE_BUFFERS_3DFX            0x86B3
#define MYGL_SAMPLES_3DFX                   0x86B4
#define MYGL_MULTISAMPLE_BIT_3DFX           0x20000000
#endif

#ifndef MYGL_3DFX_tbuffer
#endif

#ifndef MYGL_EXT_multisample
#define MYGL_MULTISAMPLE_EXT                0x809D
#define MYGL_SAMPLE_ALPHA_TO_MASK_EXT       0x809E
#define MYGL_SAMPLE_ALPHA_TO_ONE_EXT        0x809F
#define MYGL_SAMPLE_MASK_EXT                0x80A0
#define MYGL_1PASS_EXT                      0x80A1
#define MYGL_2PASS_0_EXT                    0x80A2
#define MYGL_2PASS_1_EXT                    0x80A3
#define MYGL_4PASS_0_EXT                    0x80A4
#define MYGL_4PASS_1_EXT                    0x80A5
#define MYGL_4PASS_2_EXT                    0x80A6
#define MYGL_4PASS_3_EXT                    0x80A7
#define MYGL_SAMPLE_BUFFERS_EXT             0x80A8
#define MYGL_SAMPLES_EXT                    0x80A9
#define MYGL_SAMPLE_MASK_VALUE_EXT          0x80AA
#define MYGL_SAMPLE_MASK_INVERT_EXT         0x80AB
#define MYGL_SAMPLE_PATTERN_EXT             0x80AC
#define MYGL_MULTISAMPLE_BIT_EXT            0x20000000
#endif

#ifndef MYGL_SGIX_vertex_preclip
#define MYGL_VERTEX_PRECLIP_SGIX            0x83EE
#define MYGL_VERTEX_PRECLIP_HINT_SGIX       0x83EF
#endif

#ifndef MYGL_SGIX_convolution_accuracy
#define MYGL_CONVOLUTION_HINT_SGIX          0x8316
#endif

#ifndef MYGL_SGIX_resample
#define MYGL_PACK_RESAMPLE_SGIX             0x842C
#define MYGL_UNPACK_RESAMPLE_SGIX           0x842D
#define MYGL_RESAMPLE_REPLICATE_SGIX        0x842E
#define MYGL_RESAMPLE_ZERO_FILL_SGIX        0x842F
#define MYGL_RESAMPLE_DECIMATE_SGIX         0x8430
#endif

#ifndef MYGL_SGIS_point_line_texgen
#define MYGL_EYE_DISTANCE_TO_POINT_SGIS     0x81F0
#define MYGL_OBJECT_DISTANCE_TO_POINT_SGIS  0x81F1
#define MYGL_EYE_DISTANCE_TO_LINE_SGIS      0x81F2
#define MYGL_OBJECT_DISTANCE_TO_LINE_SGIS   0x81F3
#define MYGL_EYE_POINT_SGIS                 0x81F4
#define MYGL_OBJECT_POINT_SGIS              0x81F5
#define MYGL_EYE_LINE_SGIS                  0x81F6
#define MYGL_OBJECT_LINE_SGIS               0x81F7
#endif

#ifndef MYGL_SGIS_texture_color_mask
#define MYGL_TEXTURE_COLOR_WRITEMASK_SGIS   0x81EF
#endif

#ifndef MYGL_EXT_texture_env_dot3
#define MYGL_DOT3_RGB_EXT                   0x8740
#define MYGL_DOT3_RGBA_EXT                  0x8741
#endif

#ifndef MYGL_ATI_texture_mirror_once
#define MYGL_MIRROR_CLAMP_ATI               0x8742
#define MYGL_MIRROR_CLAMP_TO_EDGE_ATI       0x8743
#endif

#ifndef MYGL_NV_fence
#define MYGL_ALL_COMPLETED_NV               0x84F2
#define MYGL_FENCE_STATUS_NV                0x84F3
#define MYGL_FENCE_CONDITION_NV             0x84F4
#endif

#ifndef MYGL_IBM_texture_mirrored_repeat
#define MYGL_MIRRORED_REPEAT_IBM            0x8370
#endif

#ifndef MYGL_NV_evaluators
#define MYGL_EVAL_2D_NV                     0x86C0
#define MYGL_EVAL_TRIANGULAR_2D_NV          0x86C1
#define MYGL_MAP_TESSELLATION_NV            0x86C2
#define MYGL_MAP_ATTRIB_U_ORDER_NV          0x86C3
#define MYGL_MAP_ATTRIB_V_ORDER_NV          0x86C4
#define MYGL_EVAL_FRACTIONAL_TESSELLATION_NV 0x86C5
#define MYGL_EVAL_VERTEX_ATTRIB0_NV         0x86C6
#define MYGL_EVAL_VERTEX_ATTRIB1_NV         0x86C7
#define MYGL_EVAL_VERTEX_ATTRIB2_NV         0x86C8
#define MYGL_EVAL_VERTEX_ATTRIB3_NV         0x86C9
#define MYGL_EVAL_VERTEX_ATTRIB4_NV         0x86CA
#define MYGL_EVAL_VERTEX_ATTRIB5_NV         0x86CB
#define MYGL_EVAL_VERTEX_ATTRIB6_NV         0x86CC
#define MYGL_EVAL_VERTEX_ATTRIB7_NV         0x86CD
#define MYGL_EVAL_VERTEX_ATTRIB8_NV         0x86CE
#define MYGL_EVAL_VERTEX_ATTRIB9_NV         0x86CF
#define MYGL_EVAL_VERTEX_ATTRIB10_NV        0x86D0
#define MYGL_EVAL_VERTEX_ATTRIB11_NV        0x86D1
#define MYGL_EVAL_VERTEX_ATTRIB12_NV        0x86D2
#define MYGL_EVAL_VERTEX_ATTRIB13_NV        0x86D3
#define MYGL_EVAL_VERTEX_ATTRIB14_NV        0x86D4
#define MYGL_EVAL_VERTEX_ATTRIB15_NV        0x86D5
#define MYGL_MAX_MAP_TESSELLATION_NV        0x86D6
#define MYGL_MAX_RATIONAL_EVAL_ORDER_NV     0x86D7
#endif

#ifndef MYGL_NV_packed_depth_stencil
#define MYGL_DEPTH_STENCIL_NV               0x84F9
#define MYGL_UNSIGNED_INT_24_8_NV           0x84FA
#endif

#ifndef MYGL_NV_register_combiners2
#define MYGL_PER_STAGE_CONSTANTS_NV         0x8535
#endif

#ifndef MYGL_NV_texture_compression_vtc
#endif

#ifndef MYGL_NV_texture_rectangle
#define MYGL_TEXTURE_RECTANGLE_NV           0x84F5
#define MYGL_TEXTURE_BINDING_RECTANGLE_NV   0x84F6
#define MYGL_PROXY_TEXTURE_RECTANGLE_NV     0x84F7
#define MYGL_MAX_RECTANGLE_TEXTURE_SIZE_NV  0x84F8
#endif

#ifndef MYGL_NV_texture_shader
#define MYGL_OFFSET_TEXTURE_RECTANGLE_NV    0x864C
#define MYGL_OFFSET_TEXTURE_RECTANGLE_SCALE_NV 0x864D
#define MYGL_DOT_PRODUCT_TEXTURE_RECTANGLE_NV 0x864E
#define MYGL_RGBA_UNSIGNED_DOT_PRODUCT_MAPPING_NV 0x86D9
#define MYGL_UNSIGNED_INT_S8_S8_8_8_NV      0x86DA
#define MYGL_UNSIGNED_INT_8_8_S8_S8_REV_NV  0x86DB
#define MYGL_DSDT_MAG_INTENSITY_NV          0x86DC
#define MYGL_SHADER_CONSISTENT_NV           0x86DD
#define MYGL_TEXTURE_SHADER_NV              0x86DE
#define MYGL_SHADER_OPERATION_NV            0x86DF
#define MYGL_CULL_MODES_NV                  0x86E0
#define MYGL_OFFSET_TEXTURE_MATRIX_NV       0x86E1
#define MYGL_OFFSET_TEXTURE_SCALE_NV        0x86E2
#define MYGL_OFFSET_TEXTURE_BIAS_NV         0x86E3
#define MYGL_OFFSET_TEXTURE_2D_MATRIX_NV    MYGL_OFFSET_TEXTURE_MATRIX_NV
#define MYGL_OFFSET_TEXTURE_2D_SCALE_NV     MYGL_OFFSET_TEXTURE_SCALE_NV
#define MYGL_OFFSET_TEXTURE_2D_BIAS_NV      MYGL_OFFSET_TEXTURE_BIAS_NV
#define MYGL_PREVIOUS_TEXTURE_INPUT_NV      0x86E4
#define MYGL_CONST_EYE_NV                   0x86E5
#define MYGL_PASS_THROUGH_NV                0x86E6
#define MYGL_CULL_FRAGMENT_NV               0x86E7
#define MYGL_OFFSET_TEXTURE_2D_NV           0x86E8
#define MYGL_DEPENDENT_AR_TEXTURE_2D_NV     0x86E9
#define MYGL_DEPENDENT_GB_TEXTURE_2D_NV     0x86EA
#define MYGL_DOT_PRODUCT_NV                 0x86EC
#define MYGL_DOT_PRODUCT_DEPTH_REPLACE_NV   0x86ED
#define MYGL_DOT_PRODUCT_TEXTURE_2D_NV      0x86EE
#define MYGL_DOT_PRODUCT_TEXTURE_CUBE_MAP_NV 0x86F0
#define MYGL_DOT_PRODUCT_DIFFUSE_CUBE_MAP_NV 0x86F1
#define MYGL_DOT_PRODUCT_REFLECT_CUBE_MAP_NV 0x86F2
#define MYGL_DOT_PRODUCT_CONST_EYE_REFLECT_CUBE_MAP_NV 0x86F3
#define MYGL_HILO_NV                        0x86F4
#define MYGL_DSDT_NV                        0x86F5
#define MYGL_DSDT_MAG_NV                    0x86F6
#define MYGL_DSDT_MAG_VIB_NV                0x86F7
#define MYGL_HILO16_NV                      0x86F8
#define MYGL_SIGNED_HILO_NV                 0x86F9
#define MYGL_SIGNED_HILO16_NV               0x86FA
#define MYGL_SIGNED_RGBA_NV                 0x86FB
#define MYGL_SIGNED_RGBA8_NV                0x86FC
#define MYGL_SIGNED_RGB_NV                  0x86FE
#define MYGL_SIGNED_RGB8_NV                 0x86FF
#define MYGL_SIGNED_LUMINANCE_NV            0x8701
#define MYGL_SIGNED_LUMINANCE8_NV           0x8702
#define MYGL_SIGNED_LUMINANCE_ALPHA_NV      0x8703
#define MYGL_SIGNED_LUMINANCE8_ALPHA8_NV    0x8704
#define MYGL_SIGNED_ALPHA_NV                0x8705
#define MYGL_SIGNED_ALPHA8_NV               0x8706
#define MYGL_SIGNED_INTENSITY_NV            0x8707
#define MYGL_SIGNED_INTENSITY8_NV           0x8708
#define MYGL_DSDT8_NV                       0x8709
#define MYGL_DSDT8_MAG8_NV                  0x870A
#define MYGL_DSDT8_MAG8_INTENSITY8_NV       0x870B
#define MYGL_SIGNED_RGB_UNSIGNED_ALPHA_NV   0x870C
#define MYGL_SIGNED_RGB8_UNSIGNED_ALPHA8_NV 0x870D
#define MYGL_HI_SCALE_NV                    0x870E
#define MYGL_LO_SCALE_NV                    0x870F
#define MYGL_DS_SCALE_NV                    0x8710
#define MYGL_DT_SCALE_NV                    0x8711
#define MYGL_MAGNITUDE_SCALE_NV             0x8712
#define MYGL_VIBRANCE_SCALE_NV              0x8713
#define MYGL_HI_BIAS_NV                     0x8714
#define MYGL_LO_BIAS_NV                     0x8715
#define MYGL_DS_BIAS_NV                     0x8716
#define MYGL_DT_BIAS_NV                     0x8717
#define MYGL_MAGNITUDE_BIAS_NV              0x8718
#define MYGL_VIBRANCE_BIAS_NV               0x8719
#define MYGL_TEXTURE_BORDER_VALUES_NV       0x871A
#define MYGL_TEXTURE_HI_SIZE_NV             0x871B
#define MYGL_TEXTURE_LO_SIZE_NV             0x871C
#define MYGL_TEXTURE_DS_SIZE_NV             0x871D
#define MYGL_TEXTURE_DT_SIZE_NV             0x871E
#define MYGL_TEXTURE_MAG_SIZE_NV            0x871F
#endif

#ifndef MYGL_NV_texture_shader2
#define MYGL_DOT_PRODUCT_TEXTURE_3D_NV      0x86EF
#endif

#ifndef MYGL_NV_vertex_array_range2
#define MYGL_VERTEX_ARRAY_RANGE_WITHOUT_FLUSH_NV 0x8533
#endif

#ifndef MYGL_NV_vertex_program
#define MYGL_VERTEX_PROGRAM_NV              0x8620
#define MYGL_VERTEX_STATE_PROGRAM_NV        0x8621
#define MYGL_ATTRIB_ARRAY_SIZE_NV           0x8623
#define MYGL_ATTRIB_ARRAY_STRIDE_NV         0x8624
#define MYGL_ATTRIB_ARRAY_TYPE_NV           0x8625
#define MYGL_CURRENT_ATTRIB_NV              0x8626
#define MYGL_PROGRAM_LENGTH_NV              0x8627
#define MYGL_PROGRAM_STRING_NV              0x8628
#define MYGL_MODELVIEW_PROJECTION_NV        0x8629
#define MYGL_IDENTITY_NV                    0x862A
#define MYGL_INVERSE_NV                     0x862B
#define MYGL_TRANSPOSE_NV                   0x862C
#define MYGL_INVERSE_TRANSPOSE_NV           0x862D
#define MYGL_MAX_TRACK_MATRIX_STACK_DEPTH_NV 0x862E
#define MYGL_MAX_TRACK_MATRICES_NV          0x862F
#define MYGL_MATRIX0_NV                     0x8630
#define MYGL_MATRIX1_NV                     0x8631
#define MYGL_MATRIX2_NV                     0x8632
#define MYGL_MATRIX3_NV                     0x8633
#define MYGL_MATRIX4_NV                     0x8634
#define MYGL_MATRIX5_NV                     0x8635
#define MYGL_MATRIX6_NV                     0x8636
#define MYGL_MATRIX7_NV                     0x8637
#define MYGL_CURRENT_MATRIX_STACK_DEPTH_NV  0x8640
#define MYGL_CURRENT_MATRIX_NV              0x8641
#define MYGL_VERTEX_PROGRAM_POINT_SIZE_NV   0x8642
#define MYGL_VERTEX_PROGRAM_TWO_SIDE_NV     0x8643
#define MYGL_PROGRAM_PARAMETER_NV           0x8644
#define MYGL_ATTRIB_ARRAY_POINTER_NV        0x8645
#define MYGL_PROGRAM_TARGET_NV              0x8646
#define MYGL_PROGRAM_RESIDENT_NV            0x8647
#define MYGL_TRACK_MATRIX_NV                0x8648
#define MYGL_TRACK_MATRIX_TRANSFORM_NV      0x8649
#define MYGL_VERTEX_PROGRAM_BINDING_NV      0x864A
#define MYGL_PROGRAM_ERROR_POSITION_NV      0x864B
#define MYGL_VERTEX_ATTRIB_ARRAY0_NV        0x8650
#define MYGL_VERTEX_ATTRIB_ARRAY1_NV        0x8651
#define MYGL_VERTEX_ATTRIB_ARRAY2_NV        0x8652
#define MYGL_VERTEX_ATTRIB_ARRAY3_NV        0x8653
#define MYGL_VERTEX_ATTRIB_ARRAY4_NV        0x8654
#define MYGL_VERTEX_ATTRIB_ARRAY5_NV        0x8655
#define MYGL_VERTEX_ATTRIB_ARRAY6_NV        0x8656
#define MYGL_VERTEX_ATTRIB_ARRAY7_NV        0x8657
#define MYGL_VERTEX_ATTRIB_ARRAY8_NV        0x8658
#define MYGL_VERTEX_ATTRIB_ARRAY9_NV        0x8659
#define MYGL_VERTEX_ATTRIB_ARRAY10_NV       0x865A
#define MYGL_VERTEX_ATTRIB_ARRAY11_NV       0x865B
#define MYGL_VERTEX_ATTRIB_ARRAY12_NV       0x865C
#define MYGL_VERTEX_ATTRIB_ARRAY13_NV       0x865D
#define MYGL_VERTEX_ATTRIB_ARRAY14_NV       0x865E
#define MYGL_VERTEX_ATTRIB_ARRAY15_NV       0x865F
#define MYGL_MAP1_VERTEX_ATTRIB0_4_NV       0x8660
#define MYGL_MAP1_VERTEX_ATTRIB1_4_NV       0x8661
#define MYGL_MAP1_VERTEX_ATTRIB2_4_NV       0x8662
#define MYGL_MAP1_VERTEX_ATTRIB3_4_NV       0x8663
#define MYGL_MAP1_VERTEX_ATTRIB4_4_NV       0x8664
#define MYGL_MAP1_VERTEX_ATTRIB5_4_NV       0x8665
#define MYGL_MAP1_VERTEX_ATTRIB6_4_NV       0x8666
#define MYGL_MAP1_VERTEX_ATTRIB7_4_NV       0x8667
#define MYGL_MAP1_VERTEX_ATTRIB8_4_NV       0x8668
#define MYGL_MAP1_VERTEX_ATTRIB9_4_NV       0x8669
#define MYGL_MAP1_VERTEX_ATTRIB10_4_NV      0x866A
#define MYGL_MAP1_VERTEX_ATTRIB11_4_NV      0x866B
#define MYGL_MAP1_VERTEX_ATTRIB12_4_NV      0x866C
#define MYGL_MAP1_VERTEX_ATTRIB13_4_NV      0x866D
#define MYGL_MAP1_VERTEX_ATTRIB14_4_NV      0x866E
#define MYGL_MAP1_VERTEX_ATTRIB15_4_NV      0x866F
#define MYGL_MAP2_VERTEX_ATTRIB0_4_NV       0x8670
#define MYGL_MAP2_VERTEX_ATTRIB1_4_NV       0x8671
#define MYGL_MAP2_VERTEX_ATTRIB2_4_NV       0x8672
#define MYGL_MAP2_VERTEX_ATTRIB3_4_NV       0x8673
#define MYGL_MAP2_VERTEX_ATTRIB4_4_NV       0x8674
#define MYGL_MAP2_VERTEX_ATTRIB5_4_NV       0x8675
#define MYGL_MAP2_VERTEX_ATTRIB6_4_NV       0x8676
#define MYGL_MAP2_VERTEX_ATTRIB7_4_NV       0x8677
#define MYGL_MAP2_VERTEX_ATTRIB8_4_NV       0x8678
#define MYGL_MAP2_VERTEX_ATTRIB9_4_NV       0x8679
#define MYGL_MAP2_VERTEX_ATTRIB10_4_NV      0x867A
#define MYGL_MAP2_VERTEX_ATTRIB11_4_NV      0x867B
#define MYGL_MAP2_VERTEX_ATTRIB12_4_NV      0x867C
#define MYGL_MAP2_VERTEX_ATTRIB13_4_NV      0x867D
#define MYGL_MAP2_VERTEX_ATTRIB14_4_NV      0x867E
#define MYGL_MAP2_VERTEX_ATTRIB15_4_NV      0x867F
#endif

#ifndef MYGL_SGIX_texture_coordinate_clamp
#define MYGL_TEXTURE_MAX_CLAMP_S_SGIX       0x8369
#define MYGL_TEXTURE_MAX_CLAMP_T_SGIX       0x836A
#define MYGL_TEXTURE_MAX_CLAMP_R_SGIX       0x836B
#endif

#ifndef MYGL_SGIX_scalebias_hint
#define MYGL_SCALEBIAS_HINT_SGIX            0x8322
#endif

#ifndef MYGL_OML_interlace
#define MYGL_INTERLACE_OML                  0x8980
#define MYGL_INTERLACE_READ_OML             0x8981
#endif

#ifndef MYGL_OML_subsample
#define MYGL_FORMAT_SUBSAMPLE_24_24_OML     0x8982
#define MYGL_FORMAT_SUBSAMPLE_244_244_OML   0x8983
#endif

#ifndef MYGL_OML_resample
#define MYGL_PACK_RESAMPLE_OML              0x8984
#define MYGL_UNPACK_RESAMPLE_OML            0x8985
#define MYGL_RESAMPLE_REPLICATE_OML         0x8986
#define MYGL_RESAMPLE_ZERO_FILL_OML         0x8987
#define MYGL_RESAMPLE_AVERAGE_OML           0x8988
#define MYGL_RESAMPLE_DECIMATE_OML          0x8989
#endif

#ifndef MYGL_NV_copy_depth_to_color
#define MYGL_DEPTH_STENCIL_TO_RGBA_NV       0x886E
#define MYGL_DEPTH_STENCIL_TO_BGRA_NV       0x886F
#endif

#ifndef MYGL_ATI_envmap_bumpmap
#define MYGL_BUMP_ROT_MATRIX_ATI            0x8775
#define MYGL_BUMP_ROT_MATRIX_SIZE_ATI       0x8776
#define MYGL_BUMP_NUM_TEX_UNITS_ATI         0x8777
#define MYGL_BUMP_TEX_UNITS_ATI             0x8778
#define MYGL_DUDV_ATI                       0x8779
#define MYGL_DU8DV8_ATI                     0x877A
#define MYGL_BUMP_ENVMAP_ATI                0x877B
#define MYGL_BUMP_TARGET_ATI                0x877C
#endif

#ifndef MYGL_ATI_fragment_shader
#define MYGL_FRAGMENT_SHADER_ATI            0x8920
#define MYGL_REG_0_ATI                      0x8921
#define MYGL_REG_1_ATI                      0x8922
#define MYGL_REG_2_ATI                      0x8923
#define MYGL_REG_3_ATI                      0x8924
#define MYGL_REG_4_ATI                      0x8925
#define MYGL_REG_5_ATI                      0x8926
#define MYGL_REG_6_ATI                      0x8927
#define MYGL_REG_7_ATI                      0x8928
#define MYGL_REG_8_ATI                      0x8929
#define MYGL_REG_9_ATI                      0x892A
#define MYGL_REG_10_ATI                     0x892B
#define MYGL_REG_11_ATI                     0x892C
#define MYGL_REG_12_ATI                     0x892D
#define MYGL_REG_13_ATI                     0x892E
#define MYGL_REG_14_ATI                     0x892F
#define MYGL_REG_15_ATI                     0x8930
#define MYGL_REG_16_ATI                     0x8931
#define MYGL_REG_17_ATI                     0x8932
#define MYGL_REG_18_ATI                     0x8933
#define MYGL_REG_19_ATI                     0x8934
#define MYGL_REG_20_ATI                     0x8935
#define MYGL_REG_21_ATI                     0x8936
#define MYGL_REG_22_ATI                     0x8937
#define MYGL_REG_23_ATI                     0x8938
#define MYGL_REG_24_ATI                     0x8939
#define MYGL_REG_25_ATI                     0x893A
#define MYGL_REG_26_ATI                     0x893B
#define MYGL_REG_27_ATI                     0x893C
#define MYGL_REG_28_ATI                     0x893D
#define MYGL_REG_29_ATI                     0x893E
#define MYGL_REG_30_ATI                     0x893F
#define MYGL_REG_31_ATI                     0x8940
#define MYGL_CON_0_ATI                      0x8941
#define MYGL_CON_1_ATI                      0x8942
#define MYGL_CON_2_ATI                      0x8943
#define MYGL_CON_3_ATI                      0x8944
#define MYGL_CON_4_ATI                      0x8945
#define MYGL_CON_5_ATI                      0x8946
#define MYGL_CON_6_ATI                      0x8947
#define MYGL_CON_7_ATI                      0x8948
#define MYGL_CON_8_ATI                      0x8949
#define MYGL_CON_9_ATI                      0x894A
#define MYGL_CON_10_ATI                     0x894B
#define MYGL_CON_11_ATI                     0x894C
#define MYGL_CON_12_ATI                     0x894D
#define MYGL_CON_13_ATI                     0x894E
#define MYGL_CON_14_ATI                     0x894F
#define MYGL_CON_15_ATI                     0x8950
#define MYGL_CON_16_ATI                     0x8951
#define MYGL_CON_17_ATI                     0x8952
#define MYGL_CON_18_ATI                     0x8953
#define MYGL_CON_19_ATI                     0x8954
#define MYGL_CON_20_ATI                     0x8955
#define MYGL_CON_21_ATI                     0x8956
#define MYGL_CON_22_ATI                     0x8957
#define MYGL_CON_23_ATI                     0x8958
#define MYGL_CON_24_ATI                     0x8959
#define MYGL_CON_25_ATI                     0x895A
#define MYGL_CON_26_ATI                     0x895B
#define MYGL_CON_27_ATI                     0x895C
#define MYGL_CON_28_ATI                     0x895D
#define MYGL_CON_29_ATI                     0x895E
#define MYGL_CON_30_ATI                     0x895F
#define MYGL_CON_31_ATI                     0x8960
#define MYGL_MOV_ATI                        0x8961
#define MYGL_ADD_ATI                        0x8963
#define MYGL_MUL_ATI                        0x8964
#define MYGL_SUB_ATI                        0x8965
#define MYGL_DOT3_ATI                       0x8966
#define MYGL_DOT4_ATI                       0x8967
#define MYGL_MAD_ATI                        0x8968
#define MYGL_LERP_ATI                       0x8969
#define MYGL_CND_ATI                        0x896A
#define MYGL_CND0_ATI                       0x896B
#define MYGL_DOT2_ADD_ATI                   0x896C
#define MYGL_SECONDARY_INTERPOLATOR_ATI     0x896D
#define MYGL_NUM_FRAGMENT_REGISTERS_ATI     0x896E
#define MYGL_NUM_FRAGMENT_CONSTANTS_ATI     0x896F
#define MYGL_NUM_PASSES_ATI                 0x8970
#define MYGL_NUM_INSTRUCTIONS_PER_PASS_ATI  0x8971
#define MYGL_NUM_INSTRUCTIONS_TOTAL_ATI     0x8972
#define MYGL_NUM_INPUT_INTERPOLATOR_COMPONENTS_ATI 0x8973
#define MYGL_NUM_LOOPBACK_COMPONENTS_ATI    0x8974
#define MYGL_COLOR_ALPHA_PAIRING_ATI        0x8975
#define MYGL_SWIZZLE_STR_ATI                0x8976
#define MYGL_SWIZZLE_STQ_ATI                0x8977
#define MYGL_SWIZZLE_STR_DR_ATI             0x8978
#define MYGL_SWIZZLE_STQ_DQ_ATI             0x8979
#define MYGL_SWIZZLE_STRQ_ATI               0x897A
#define MYGL_SWIZZLE_STRQ_DQ_ATI            0x897B
#define MYGL_RED_BIT_ATI                    0x00000001
#define MYGL_GREEN_BIT_ATI                  0x00000002
#define MYGL_BLUE_BIT_ATI                   0x00000004
#define MYGL_2X_BIT_ATI                     0x00000001
#define MYGL_4X_BIT_ATI                     0x00000002
#define MYGL_8X_BIT_ATI                     0x00000004
#define MYGL_HALF_BIT_ATI                   0x00000008
#define MYGL_QUARTER_BIT_ATI                0x00000010
#define MYGL_EIGHTH_BIT_ATI                 0x00000020
#define MYGL_SATURATE_BIT_ATI               0x00000040
#define MYGL_COMP_BIT_ATI                   0x00000002
#define MYGL_NEGATE_BIT_ATI                 0x00000004
#define MYGL_BIAS_BIT_ATI                   0x00000008
#endif

#ifndef MYGL_ATI_pn_triangles
#define MYGL_PN_TRIANGLES_ATI               0x87F0
#define MYGL_MAX_PN_TRIANGLES_TESSELATION_LEVEL_ATI 0x87F1
#define MYGL_PN_TRIANGLES_POINT_MODE_ATI    0x87F2
#define MYGL_PN_TRIANGLES_NORMAL_MODE_ATI   0x87F3
#define MYGL_PN_TRIANGLES_TESSELATION_LEVEL_ATI 0x87F4
#define MYGL_PN_TRIANGLES_POINT_MODE_LINEAR_ATI 0x87F5
#define MYGL_PN_TRIANGLES_POINT_MODE_CUBIC_ATI 0x87F6
#define MYGL_PN_TRIANGLES_NORMAL_MODE_LINEAR_ATI 0x87F7
#define MYGL_PN_TRIANGLES_NORMAL_MODE_QUADRATIC_ATI 0x87F8
#endif

#ifndef MYGL_ATI_vertex_array_object
#define MYGL_STATIC_ATI                     0x8760
#define MYGL_DYNAMIC_ATI                    0x8761
#define MYGL_PRESERVE_ATI                   0x8762
#define MYGL_DISCARD_ATI                    0x8763
#define MYGL_OBJECT_BUFFER_SIZE_ATI         0x8764
#define MYGL_OBJECT_BUFFER_USAGE_ATI        0x8765
#define MYGL_ARRAY_OBJECT_BUFFER_ATI        0x8766
#define MYGL_ARRAY_OBJECT_OFFSET_ATI        0x8767
#endif

#ifndef MYGL_EXT_vertex_shader
#define MYGL_VERTEX_SHADER_EXT              0x8780
#define MYGL_VERTEX_SHADER_BINDING_EXT      0x8781
#define MYGL_OP_INDEX_EXT                   0x8782
#define MYGL_OP_NEGATE_EXT                  0x8783
#define MYGL_OP_DOT3_EXT                    0x8784
#define MYGL_OP_DOT4_EXT                    0x8785
#define MYGL_OP_MUL_EXT                     0x8786
#define MYGL_OP_ADD_EXT                     0x8787
#define MYGL_OP_MADD_EXT                    0x8788
#define MYGL_OP_FRAC_EXT                    0x8789
#define MYGL_OP_MAX_EXT                     0x878A
#define MYGL_OP_MIN_EXT                     0x878B
#define MYGL_OP_SET_GE_EXT                  0x878C
#define MYGL_OP_SET_LT_EXT                  0x878D
#define MYGL_OP_CLAMP_EXT                   0x878E
#define MYGL_OP_FLOOR_EXT                   0x878F
#define MYGL_OP_ROUND_EXT                   0x8790
#define MYGL_OP_EXP_BASE_2_EXT              0x8791
#define MYGL_OP_LOG_BASE_2_EXT              0x8792
#define MYGL_OP_POWER_EXT                   0x8793
#define MYGL_OP_RECIP_EXT                   0x8794
#define MYGL_OP_RECIP_SQRT_EXT              0x8795
#define MYGL_OP_SUB_EXT                     0x8796
#define MYGL_OP_CROSS_PRODUCT_EXT           0x8797
#define MYGL_OP_MULTIPLY_MATRIX_EXT         0x8798
#define MYGL_OP_MOV_EXT                     0x8799
#define MYGL_OUTPUT_VERTEX_EXT              0x879A
#define MYGL_OUTPUT_COLOR0_EXT              0x879B
#define MYGL_OUTPUT_COLOR1_EXT              0x879C
#define MYGL_OUTPUT_TEXTURE_COORD0_EXT      0x879D
#define MYGL_OUTPUT_TEXTURE_COORD1_EXT      0x879E
#define MYGL_OUTPUT_TEXTURE_COORD2_EXT      0x879F
#define MYGL_OUTPUT_TEXTURE_COORD3_EXT      0x87A0
#define MYGL_OUTPUT_TEXTURE_COORD4_EXT      0x87A1
#define MYGL_OUTPUT_TEXTURE_COORD5_EXT      0x87A2
#define MYGL_OUTPUT_TEXTURE_COORD6_EXT      0x87A3
#define MYGL_OUTPUT_TEXTURE_COORD7_EXT      0x87A4
#define MYGL_OUTPUT_TEXTURE_COORD8_EXT      0x87A5
#define MYGL_OUTPUT_TEXTURE_COORD9_EXT      0x87A6
#define MYGL_OUTPUT_TEXTURE_COORD10_EXT     0x87A7
#define MYGL_OUTPUT_TEXTURE_COORD11_EXT     0x87A8
#define MYGL_OUTPUT_TEXTURE_COORD12_EXT     0x87A9
#define MYGL_OUTPUT_TEXTURE_COORD13_EXT     0x87AA
#define MYGL_OUTPUT_TEXTURE_COORD14_EXT     0x87AB
#define MYGL_OUTPUT_TEXTURE_COORD15_EXT     0x87AC
#define MYGL_OUTPUT_TEXTURE_COORD16_EXT     0x87AD
#define MYGL_OUTPUT_TEXTURE_COORD17_EXT     0x87AE
#define MYGL_OUTPUT_TEXTURE_COORD18_EXT     0x87AF
#define MYGL_OUTPUT_TEXTURE_COORD19_EXT     0x87B0
#define MYGL_OUTPUT_TEXTURE_COORD20_EXT     0x87B1
#define MYGL_OUTPUT_TEXTURE_COORD21_EXT     0x87B2
#define MYGL_OUTPUT_TEXTURE_COORD22_EXT     0x87B3
#define MYGL_OUTPUT_TEXTURE_COORD23_EXT     0x87B4
#define MYGL_OUTPUT_TEXTURE_COORD24_EXT     0x87B5
#define MYGL_OUTPUT_TEXTURE_COORD25_EXT     0x87B6
#define MYGL_OUTPUT_TEXTURE_COORD26_EXT     0x87B7
#define MYGL_OUTPUT_TEXTURE_COORD27_EXT     0x87B8
#define MYGL_OUTPUT_TEXTURE_COORD28_EXT     0x87B9
#define MYGL_OUTPUT_TEXTURE_COORD29_EXT     0x87BA
#define MYGL_OUTPUT_TEXTURE_COORD30_EXT     0x87BB
#define MYGL_OUTPUT_TEXTURE_COORD31_EXT     0x87BC
#define MYGL_OUTPUT_FOG_EXT                 0x87BD
#define MYGL_SCALAR_EXT                     0x87BE
#define MYGL_VECTOR_EXT                     0x87BF
#define MYGL_MATRIX_EXT                     0x87C0
#define MYGL_VARIANT_EXT                    0x87C1
#define MYGL_INVARIANT_EXT                  0x87C2
#define MYGL_LOCAL_CONSTANT_EXT             0x87C3
#define MYGL_LOCAL_EXT                      0x87C4
#define MYGL_MAX_VERTEX_SHADER_INSTRUCTIONS_EXT 0x87C5
#define MYGL_MAX_VERTEX_SHADER_VARIANTS_EXT 0x87C6
#define MYGL_MAX_VERTEX_SHADER_INVARIANTS_EXT 0x87C7
#define MYGL_MAX_VERTEX_SHADER_LOCAL_CONSTANTS_EXT 0x87C8
#define MYGL_MAX_VERTEX_SHADER_LOCALS_EXT   0x87C9
#define MYGL_MAX_OPTIMIZED_VERTEX_SHADER_INSTRUCTIONS_EXT 0x87CA
#define MYGL_MAX_OPTIMIZED_VERTEX_SHADER_VARIANTS_EXT 0x87CB
#define MYGL_MAX_OPTIMIZED_VERTEX_SHADER_LOCAL_CONSTANTS_EXT 0x87CC
#define MYGL_MAX_OPTIMIZED_VERTEX_SHADER_INVARIANTS_EXT 0x87CD
#define MYGL_MAX_OPTIMIZED_VERTEX_SHADER_LOCALS_EXT 0x87CE
#define MYGL_VERTEX_SHADER_INSTRUCTIONS_EXT 0x87CF
#define MYGL_VERTEX_SHADER_VARIANTS_EXT     0x87D0
#define MYGL_VERTEX_SHADER_INVARIANTS_EXT   0x87D1
#define MYGL_VERTEX_SHADER_LOCAL_CONSTANTS_EXT 0x87D2
#define MYGL_VERTEX_SHADER_LOCALS_EXT       0x87D3
#define MYGL_VERTEX_SHADER_OPTIMIZED_EXT    0x87D4
#define MYGL_X_EXT                          0x87D5
#define MYGL_Y_EXT                          0x87D6
#define MYGL_Z_EXT                          0x87D7
#define MYGL_W_EXT                          0x87D8
#define MYGL_NEGATIVE_X_EXT                 0x87D9
#define MYGL_NEGATIVE_Y_EXT                 0x87DA
#define MYGL_NEGATIVE_Z_EXT                 0x87DB
#define MYGL_NEGATIVE_W_EXT                 0x87DC
#define MYGL_ZERO_EXT                       0x87DD
#define MYGL_ONE_EXT                        0x87DE
#define MYGL_NEGATIVE_ONE_EXT               0x87DF
#define MYGL_NORMALIZED_RANGE_EXT           0x87E0
#define MYGL_FULL_RANGE_EXT                 0x87E1
#define MYGL_CURRENT_VERTEX_EXT             0x87E2
#define MYGL_MVP_MATRIX_EXT                 0x87E3
#define MYGL_VARIANT_VALUE_EXT              0x87E4
#define MYGL_VARIANT_DATATYPE_EXT           0x87E5
#define MYGL_VARIANT_ARRAY_STRIDE_EXT       0x87E6
#define MYGL_VARIANT_ARRAY_TYPE_EXT         0x87E7
#define MYGL_VARIANT_ARRAY_EXT              0x87E8
#define MYGL_VARIANT_ARRAY_POINTER_EXT      0x87E9
#define MYGL_INVARIANT_VALUE_EXT            0x87EA
#define MYGL_INVARIANT_DATATYPE_EXT         0x87EB
#define MYGL_LOCAL_CONSTANT_VALUE_EXT       0x87EC
#define MYGL_LOCAL_CONSTANT_DATATYPE_EXT    0x87ED
#endif

#ifndef MYGL_ATI_vertex_streams
#define MYGL_MAX_VERTEX_STREAMS_ATI         0x876B
#define MYGL_VERTEX_STREAM0_ATI             0x876C
#define MYGL_VERTEX_STREAM1_ATI             0x876D
#define MYGL_VERTEX_STREAM2_ATI             0x876E
#define MYGL_VERTEX_STREAM3_ATI             0x876F
#define MYGL_VERTEX_STREAM4_ATI             0x8770
#define MYGL_VERTEX_STREAM5_ATI             0x8771
#define MYGL_VERTEX_STREAM6_ATI             0x8772
#define MYGL_VERTEX_STREAM7_ATI             0x8773
#define MYGL_VERTEX_SOURCE_ATI              0x8774
#endif

#ifndef MYGL_ATI_element_array
#define MYGL_ELEMENT_ARRAY_ATI              0x8768
#define MYGL_ELEMENT_ARRAY_TYPE_ATI         0x8769
#define MYGL_ELEMENT_ARRAY_POINTER_ATI      0x876A
#endif

#ifndef MYGL_SUN_mesh_array
#define MYGL_QUAD_MESH_SUN                  0x8614
#define MYGL_TRIANGLE_MESH_SUN              0x8615
#endif

#ifndef MYGL_SUN_slice_accum
#define MYGL_SLICE_ACCUM_SUN                0x85CC
#endif

#ifndef MYGL_NV_multisample_filter_hint
#define MYGL_MULTISAMPLE_FILTER_HINT_NV     0x8534
#endif

#ifndef MYGL_NV_depth_clamp
#define MYGL_DEPTH_CLAMP_NV                 0x864F
#endif

#ifndef MYGL_NV_occlusion_query
#define MYGL_PIXEL_COUNTER_BITS_NV          0x8864
#define MYGL_CURRENT_OCCLUSION_QUERY_ID_NV  0x8865
#define MYGL_PIXEL_COUNT_NV                 0x8866
#define MYGL_PIXEL_COUNT_AVAILABLE_NV       0x8867
#endif

#ifndef MYGL_NV_point_sprite
#define MYGL_POINT_SPRITE_NV                0x8861
#define MYGL_COORD_REPLACE_NV               0x8862
#define MYGL_POINT_SPRITE_R_MODE_NV         0x8863
#endif

#ifndef MYGL_NV_texture_shader3
#define MYGL_OFFSET_PROJECTIVE_TEXTURE_2D_NV 0x8850
#define MYGL_OFFSET_PROJECTIVE_TEXTURE_2D_SCALE_NV 0x8851
#define MYGL_OFFSET_PROJECTIVE_TEXTURE_RECTANGLE_NV 0x8852
#define MYGL_OFFSET_PROJECTIVE_TEXTURE_RECTANGLE_SCALE_NV 0x8853
#define MYGL_OFFSET_HILO_TEXTURE_2D_NV      0x8854
#define MYGL_OFFSET_HILO_TEXTURE_RECTANGLE_NV 0x8855
#define MYGL_OFFSET_HILO_PROJECTIVE_TEXTURE_2D_NV 0x8856
#define MYGL_OFFSET_HILO_PROJECTIVE_TEXTURE_RECTANGLE_NV 0x8857
#define MYGL_DEPENDENT_HILO_TEXTURE_2D_NV   0x8858
#define MYGL_DEPENDENT_RGB_TEXTURE_3D_NV    0x8859
#define MYGL_DEPENDENT_RGB_TEXTURE_CUBE_MAP_NV 0x885A
#define MYGL_DOT_PRODUCT_PASS_THROUGH_NV    0x885B
#define MYGL_DOT_PRODUCT_TEXTURE_1D_NV      0x885C
#define MYGL_DOT_PRODUCT_AFFINE_DEPTH_REPLACE_NV 0x885D
#define MYGL_HILO8_NV                       0x885E
#define MYGL_SIGNED_HILO8_NV                0x885F
#define MYGL_FORCE_BLUE_TO_ONE_NV           0x8860
#endif

#ifndef MYGL_NV_vertex_program1_1
#endif

#ifndef MYGL_EXT_shadow_funcs
#endif

#ifndef MYGL_EXT_stencil_two_side
#define MYGL_STENCIL_TEST_TWO_SIDE_EXT      0x8910
#define MYGL_ACTIVE_STENCIL_FACE_EXT        0x8911
#endif

#ifndef MYGL_ATI_text_fragment_shader
#define MYGL_TEXT_FRAGMENT_SHADER_ATI       0x8200
#endif

#ifndef MYGL_APPLE_client_storage
#define MYGL_UNPACK_CLIENT_STORAGE_APPLE    0x85B2
#endif

#ifndef MYGL_APPLE_element_array
#define MYGL_ELEMENT_ARRAY_APPLE            0x8768
#define MYGL_ELEMENT_ARRAY_TYPE_APPLE       0x8769
#define MYGL_ELEMENT_ARRAY_POINTER_APPLE    0x876A
#endif

#ifndef MYGL_APPLE_fence
#define MYGL_DRAW_PIXELS_APPLE              0x8A0A
#define MYGL_FENCE_APPLE                    0x8A0B
#endif

#ifndef MYGL_APPLE_vertex_array_object
#define MYGL_VERTEX_ARRAY_BINDING_APPLE     0x85B5
#endif

#ifndef MYGL_APPLE_vertex_array_range
#define MYGL_VERTEX_ARRAY_RANGE_APPLE       0x851D
#define MYGL_VERTEX_ARRAY_RANGE_LENGTH_APPLE 0x851E
#define MYGL_VERTEX_ARRAY_STORAGE_HINT_APPLE 0x851F
#define MYGL_VERTEX_ARRAY_RANGE_POINTER_APPLE 0x8521
#define MYGL_STORAGE_CACHED_APPLE           0x85BE
#define MYGL_STORAGE_SHARED_APPLE           0x85BF
#endif

#ifndef MYGL_APPLE_ycbcr_422
#define MYGL_YCBCR_422_APPLE                0x85B9
#define MYGL_UNSIGNED_SHORT_8_8_APPLE       0x85BA
#define MYGL_UNSIGNED_SHORT_8_8_REV_APPLE   0x85BB
#endif

#ifndef MYGL_S3_s3tc
#define MYGL_RGB_S3TC                       0x83A0
#define MYGL_RGB4_S3TC                      0x83A1
#define MYGL_RGBA_S3TC                      0x83A2
#define MYGL_RGBA4_S3TC                     0x83A3
#endif

#ifndef MYGL_ATI_draw_buffers
#define MYGL_MAX_DRAW_BUFFERS_ATI           0x8824
#define MYGL_DRAW_BUFFER0_ATI               0x8825
#define MYGL_DRAW_BUFFER1_ATI               0x8826
#define MYGL_DRAW_BUFFER2_ATI               0x8827
#define MYGL_DRAW_BUFFER3_ATI               0x8828
#define MYGL_DRAW_BUFFER4_ATI               0x8829
#define MYGL_DRAW_BUFFER5_ATI               0x882A
#define MYGL_DRAW_BUFFER6_ATI               0x882B
#define MYGL_DRAW_BUFFER7_ATI               0x882C
#define MYGL_DRAW_BUFFER8_ATI               0x882D
#define MYGL_DRAW_BUFFER9_ATI               0x882E
#define MYGL_DRAW_BUFFER10_ATI              0x882F
#define MYGL_DRAW_BUFFER11_ATI              0x8830
#define MYGL_DRAW_BUFFER12_ATI              0x8831
#define MYGL_DRAW_BUFFER13_ATI              0x8832
#define MYGL_DRAW_BUFFER14_ATI              0x8833
#define MYGL_DRAW_BUFFER15_ATI              0x8834
#endif

#ifndef MYGL_ATI_pixel_format_float
#define MYGL_TYPE_RGBA_FLOAT_ATI            0x8820
#define MYGL_COLOR_CLEAR_UNCLAMPED_VALUE_ATI 0x8835
#endif

#ifndef MYGL_ATI_texture_env_combine3
#define MYGL_MODULATE_ADD_ATI               0x8744
#define MYGL_MODULATE_SIGNED_ADD_ATI        0x8745
#define MYGL_MODULATE_SUBTRACT_ATI          0x8746
#endif

#ifndef MYGL_ATI_texture_float
#define MYGL_RGBA_FLOAT32_ATI               0x8814
#define MYGL_RGB_FLOAT32_ATI                0x8815
#define MYGL_ALPHA_FLOAT32_ATI              0x8816
#define MYGL_INTENSITY_FLOAT32_ATI          0x8817
#define MYGL_LUMINANCE_FLOAT32_ATI          0x8818
#define MYGL_LUMINANCE_ALPHA_FLOAT32_ATI    0x8819
#define MYGL_RGBA_FLOAT16_ATI               0x881A
#define MYGL_RGB_FLOAT16_ATI                0x881B
#define MYGL_ALPHA_FLOAT16_ATI              0x881C
#define MYGL_INTENSITY_FLOAT16_ATI          0x881D
#define MYGL_LUMINANCE_FLOAT16_ATI          0x881E
#define MYGL_LUMINANCE_ALPHA_FLOAT16_ATI    0x881F
#endif

#ifndef MYGL_NV_float_buffer
#define MYGL_FLOAT_R_NV                     0x8880
#define MYGL_FLOAT_RG_NV                    0x8881
#define MYGL_FLOAT_RGB_NV                   0x8882
#define MYGL_FLOAT_RGBA_NV                  0x8883
#define MYGL_FLOAT_R16_NV                   0x8884
#define MYGL_FLOAT_R32_NV                   0x8885
#define MYGL_FLOAT_RG16_NV                  0x8886
#define MYGL_FLOAT_RG32_NV                  0x8887
#define MYGL_FLOAT_RGB16_NV                 0x8888
#define MYGL_FLOAT_RGB32_NV                 0x8889
#define MYGL_FLOAT_RGBA16_NV                0x888A
#define MYGL_FLOAT_RGBA32_NV                0x888B
#define MYGL_TEXTURE_FLOAT_COMPONENTS_NV    0x888C
#define MYGL_FLOAT_CLEAR_COLOR_VALUE_NV     0x888D
#define MYGL_FLOAT_RGBA_MODE_NV             0x888E
#endif

#ifndef MYGL_NV_fragment_program
#define MYGL_MAX_FRAGMENT_PROGRAM_LOCAL_PARAMETERS_NV 0x8868
#define MYGL_FRAGMENT_PROGRAM_NV            0x8870
#define MYGL_MAX_TEXTURE_COORDS_NV          0x8871
#define MYGL_MAX_TEXTURE_IMAGE_UNITS_NV     0x8872
#define MYGL_FRAGMENT_PROGRAM_BINDING_NV    0x8873
#define MYGL_PROGRAM_ERROR_STRING_NV        0x8874
#endif

#ifndef MYGL_NV_half_float
#define MYGL_HALF_FLOAT_NV                  0x140B
#endif

#ifndef MYGL_NV_pixel_data_range
#define MYGL_WRITE_PIXEL_DATA_RANGE_NV      0x8878
#define MYGL_READ_PIXEL_DATA_RANGE_NV       0x8879
#define MYGL_WRITE_PIXEL_DATA_RANGE_LENGTH_NV 0x887A
#define MYGL_READ_PIXEL_DATA_RANGE_LENGTH_NV 0x887B
#define MYGL_WRITE_PIXEL_DATA_RANGE_POINTER_NV 0x887C
#define MYGL_READ_PIXEL_DATA_RANGE_POINTER_NV 0x887D
#endif

#ifndef MYGL_NV_primitive_restart
#define MYGL_PRIMITIVE_RESTART_NV           0x8558
#define MYGL_PRIMITIVE_RESTART_INDEX_NV     0x8559
#endif

#ifndef MYGL_NV_texture_expand_normal
#define MYGL_TEXTURE_UNSIGNED_REMAP_MODE_NV 0x888F
#endif

#ifndef MYGL_NV_vertex_program2
#endif

#ifndef MYGL_ATI_map_object_buffer
#endif

#ifndef MYGL_ATI_separate_stencil
#define MYGL_STENCIL_BACK_FUNC_ATI          0x8800
#define MYGL_STENCIL_BACK_FAIL_ATI          0x8801
#define MYGL_STENCIL_BACK_PASS_DEPTH_FAIL_ATI 0x8802
#define MYGL_STENCIL_BACK_PASS_DEPTH_PASS_ATI 0x8803
#endif

#ifndef MYGL_ATI_vertex_attrib_array_object
#endif

#ifndef MYGL_OES_read_format
#define MYGL_IMPLEMENTATION_COLOR_READ_TYPE_OES 0x8B9A
#define MYGL_IMPLEMENTATION_COLOR_READ_FORMAT_OES 0x8B9B
#endif

#ifndef MYGL_EXT_depth_bounds_test
#define MYGL_DEPTH_BOUNDS_TEST_EXT          0x8890
#define MYGL_DEPTH_BOUNDS_EXT               0x8891
#endif

#ifndef MYGL_EXT_texture_mirror_clamp
#define MYGL_MIRROR_CLAMP_EXT               0x8742
#define MYGL_MIRROR_CLAMP_TO_EDGE_EXT       0x8743
#define MYGL_MIRROR_CLAMP_TO_BORDER_EXT     0x8912
#endif

#ifndef MYGL_EXT_blend_equation_separate
#define MYGL_BLEND_EQUATION_RGB_EXT         MYGL_BLEND_EQUATION
#define MYGL_BLEND_EQUATION_ALPHA_EXT       0x883D
#endif

#ifndef MYGL_MESA_pack_invert
#define MYGL_PACK_INVERT_MESA               0x8758
#endif

#ifndef MYGL_MESA_ycbcr_texture
#define MYGL_UNSIGNED_SHORT_8_8_MESA        0x85BA
#define MYGL_UNSIGNED_SHORT_8_8_REV_MESA    0x85BB
#define MYGL_YCBCR_MESA                     0x8757
#endif

#ifndef MYGL_EXT_pixel_buffer_object
#define MYGL_PIXEL_PACK_BUFFER_EXT          0x88EB
#define MYGL_PIXEL_UNPACK_BUFFER_EXT        0x88EC
#define MYGL_PIXEL_PACK_BUFFER_BINDING_EXT  0x88ED
#define MYGL_PIXEL_UNPACK_BUFFER_BINDING_EXT 0x88EF
#endif

#ifndef MYGL_NV_fragment_program_option
#endif

#ifndef MYGL_NV_fragment_program2
#define MYGL_MAX_PROGRAM_EXEC_INSTRUCTIONS_NV 0x88F4
#define MYGL_MAX_PROGRAM_CALL_DEPTH_NV      0x88F5
#define MYGL_MAX_PROGRAM_IF_DEPTH_NV        0x88F6
#define MYGL_MAX_PROGRAM_LOOP_DEPTH_NV      0x88F7
#define MYGL_MAX_PROGRAM_LOOP_COUNT_NV      0x88F8
#endif

#ifndef MYGL_NV_vertex_program2_option
/* reuse MYGL_MAX_PROGRAM_EXEC_INSTRUCTIONS_NV */
/* reuse MYGL_MAX_PROGRAM_CALL_DEPTH_NV */
#endif

#ifndef MYGL_NV_vertex_program3
/* reuse MYGL_MAX_VERTEX_TEXTURE_IMAGE_UNITS_ARB */
#endif

#ifndef MYGL_EXT_framebuffer_object
#define MYGL_INVALID_FRAMEBUFFER_OPERATION_EXT 0x0506
#define MYGL_MAX_RENDERBUFFER_SIZE_EXT      0x84E8
#define MYGL_FRAMEBUFFER_BINDING_EXT        0x8CA6
#define MYGL_RENDERBUFFER_BINDING_EXT       0x8CA7
#define MYGL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE_EXT 0x8CD0
#define MYGL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME_EXT 0x8CD1
#define MYGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL_EXT 0x8CD2
#define MYGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE_EXT 0x8CD3
#define MYGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_3D_ZOFFSET_EXT 0x8CD4
#define MYGL_FRAMEBUFFER_COMPLETE_EXT       0x8CD5
#define MYGL_FRAMEBUFFER_INCOMPLETE_ATTACHMENTS_EXT 0x8CD6
#define MYGL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT 0x8CD7
#define MYGL_FRAMEBUFFER_INCOMPLETE_DUPLICATE_ATTACHMENT_EXT 0x8CD8
#define MYGL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT 0x8CD9
#define MYGL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT 0x8CDA
#define MYGL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER_EXT 0x8CDB
#define MYGL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER_EXT 0x8CDC
#define MYGL_FRAMEBUFFER_UNSUPPORTED_EXT    0x8CDD
#define MYGL_FRAMEBUFFER_STATUS_ERROR_EXT   0x8CDE
#define MYGL_MAX_COLOR_ATTACHMENTS_EXT      0x8CDF
#define MYGL_COLOR_ATTACHMENT0_EXT          0x8CE0
#define MYGL_COLOR_ATTACHMENT1_EXT          0x8CE1
#define MYGL_COLOR_ATTACHMENT2_EXT          0x8CE2
#define MYGL_COLOR_ATTACHMENT3_EXT          0x8CE3
#define MYGL_COLOR_ATTACHMENT4_EXT          0x8CE4
#define MYGL_COLOR_ATTACHMENT5_EXT          0x8CE5
#define MYGL_COLOR_ATTACHMENT6_EXT          0x8CE6
#define MYGL_COLOR_ATTACHMENT7_EXT          0x8CE7
#define MYGL_COLOR_ATTACHMENT8_EXT          0x8CE8
#define MYGL_COLOR_ATTACHMENT9_EXT          0x8CE9
#define MYGL_COLOR_ATTACHMENT10_EXT         0x8CEA
#define MYGL_COLOR_ATTACHMENT11_EXT         0x8CEB
#define MYGL_COLOR_ATTACHMENT12_EXT         0x8CEC
#define MYGL_COLOR_ATTACHMENT13_EXT         0x8CED
#define MYGL_COLOR_ATTACHMENT14_EXT         0x8CEE
#define MYGL_COLOR_ATTACHMENT15_EXT         0x8CEF
#define MYGL_DEPTH_ATTACHMENT_EXT           0x8D00
#define MYGL_STENCIL_ATTACHMENT_EXT         0x8D20
#define MYGL_FRAMEBUFFER_EXT                0x8D40
#define MYGL_RENDERBUFFER_EXT               0x8D41
#define MYGL_RENDERBUFFER_WIDTH_EXT         0x8D42
#define MYGL_RENDERBUFFER_HEIGHT_EXT        0x8D43
#define MYGL_RENDERBUFFER_INTERNAL_FORMAT_EXT 0x8D44
#define MYGL_STENCIL_INDEX_EXT              0x8D45
#define MYGL_STENCIL_INDEX1_EXT             0x8D46
#define MYGL_STENCIL_INDEX4_EXT             0x8D47
#define MYGL_STENCIL_INDEX8_EXT             0x8D48
#define MYGL_STENCIL_INDEX16_EXT            0x8D49
#endif

#ifndef MYGL_GREMEDY_string_marker
#endif


/*************************************************************/

#include <stddef.h>
#ifndef MYGL_VERSION_2_0
/* GL type for program/shader text */
typedef char GLchar;			/* native character */
#endif

#ifndef MYGL_VERSION_1_5
/* GL types for handling large vertex buffer objects */
typedef ptrdiff_t GLintptr;
typedef ptrdiff_t GLsizeiptr;
#endif

#ifndef MYGL_ARB_vertex_buffer_object
/* GL types for handling large vertex buffer objects */
typedef ptrdiff_t GLintptrARB;
typedef ptrdiff_t GLsizeiptrARB;
#endif

#ifndef MYGL_ARB_shader_objects
/* GL types for handling shader object handles and program/shader text */
typedef char GLcharARB;		/* native character */
typedef unsigned int GLhandleARB;	/* shader object handle */
#endif

/* GL types for "half" precision (s10e5) float data in host memory */
#ifndef MYGL_ARB_half_float_pixel
typedef unsigned short GLhalfARB;
#endif

#ifndef MYGL_NV_half_float
typedef unsigned short GLhalfNV;
#endif

#ifndef MYGL_VERSION_1_2
#define MYGL_VERSION_1_2 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglBlendColor (GLclampf, GLclampf, GLclampf, GLclampf);
GLAPI void APIENTRY myglBlendEquation (GLenum);
GLAPI void APIENTRY myglDrawRangeElements (GLenum, GLuint, GLuint, GLsizei, GLenum, const GLvoid *);
GLAPI void APIENTRY myglColorTable (GLenum, GLenum, GLsizei, GLenum, GLenum, const GLvoid *);
GLAPI void APIENTRY myglColorTableParameterfv (GLenum, GLenum, const GLfloat *);
GLAPI void APIENTRY myglColorTableParameteriv (GLenum, GLenum, const GLint *);
GLAPI void APIENTRY myglCopyColorTable (GLenum, GLenum, GLint, GLint, GLsizei);
GLAPI void APIENTRY myglGetColorTable (GLenum, GLenum, GLenum, GLvoid *);
GLAPI void APIENTRY myglGetColorTableParameterfv (GLenum, GLenum, GLfloat *);
GLAPI void APIENTRY myglGetColorTableParameteriv (GLenum, GLenum, GLint *);
GLAPI void APIENTRY myglColorSubTable (GLenum, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *);
GLAPI void APIENTRY myglCopyColorSubTable (GLenum, GLsizei, GLint, GLint, GLsizei);
GLAPI void APIENTRY myglConvolutionFilter1D (GLenum, GLenum, GLsizei, GLenum, GLenum, const GLvoid *);
GLAPI void APIENTRY myglConvolutionFilter2D (GLenum, GLenum, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *);
GLAPI void APIENTRY myglConvolutionParameterf (GLenum, GLenum, GLfloat);
GLAPI void APIENTRY myglConvolutionParameterfv (GLenum, GLenum, const GLfloat *);
GLAPI void APIENTRY myglConvolutionParameteri (GLenum, GLenum, GLint);
GLAPI void APIENTRY myglConvolutionParameteriv (GLenum, GLenum, const GLint *);
GLAPI void APIENTRY myglCopyConvolutionFilter1D (GLenum, GLenum, GLint, GLint, GLsizei);
GLAPI void APIENTRY myglCopyConvolutionFilter2D (GLenum, GLenum, GLint, GLint, GLsizei, GLsizei);
GLAPI void APIENTRY myglGetConvolutionFilter (GLenum, GLenum, GLenum, GLvoid *);
GLAPI void APIENTRY myglGetConvolutionParameterfv (GLenum, GLenum, GLfloat *);
GLAPI void APIENTRY myglGetConvolutionParameteriv (GLenum, GLenum, GLint *);
GLAPI void APIENTRY myglGetSeparableFilter (GLenum, GLenum, GLenum, GLvoid *, GLvoid *, GLvoid *);
GLAPI void APIENTRY myglSeparableFilter2D (GLenum, GLenum, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *, const GLvoid *);
GLAPI void APIENTRY myglGetHistogram (GLenum, GLboolean, GLenum, GLenum, GLvoid *);
GLAPI void APIENTRY myglGetHistogramParameterfv (GLenum, GLenum, GLfloat *);
GLAPI void APIENTRY myglGetHistogramParameteriv (GLenum, GLenum, GLint *);
GLAPI void APIENTRY myglGetMinmax (GLenum, GLboolean, GLenum, GLenum, GLvoid *);
GLAPI void APIENTRY myglGetMinmaxParameterfv (GLenum, GLenum, GLfloat *);
GLAPI void APIENTRY myglGetMinmaxParameteriv (GLenum, GLenum, GLint *);
GLAPI void APIENTRY myglHistogram (GLenum, GLsizei, GLenum, GLboolean);
GLAPI void APIENTRY myglMinmax (GLenum, GLenum, GLboolean);
GLAPI void APIENTRY myglResetHistogram (GLenum);
GLAPI void APIENTRY myglResetMinmax (GLenum);
GLAPI void APIENTRY myglTexImage3D (GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid *);
GLAPI void APIENTRY myglTexSubImage3D (GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *);
GLAPI void APIENTRY myglCopyTexSubImage3D (GLenum, GLint, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLBLENDCOLORPROC) (GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
typedef void (APIENTRYP PFNMYGLBLENDEQUATIONPROC) (GLenum mode);
typedef void (APIENTRYP PFNMYGLDRAWRANGEELEMENTSPROC) (GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid *indices);
typedef void (APIENTRYP PFNMYGLCOLORTABLEPROC) (GLenum target, GLenum internalformat, GLsizei width, GLenum format, GLenum type, const GLvoid *table);
typedef void (APIENTRYP PFNMYGLCOLORTABLEPARAMETERFVPROC) (GLenum target, GLenum pname, const GLfloat *params);
typedef void (APIENTRYP PFNMYGLCOLORTABLEPARAMETERIVPROC) (GLenum target, GLenum pname, const GLint *params);
typedef void (APIENTRYP PFNMYGLCOPYCOLORTABLEPROC) (GLenum target, GLenum internalformat, GLint x, GLint y, GLsizei width);
typedef void (APIENTRYP PFNMYGLGETCOLORTABLEPROC) (GLenum target, GLenum format, GLenum type, GLvoid *table);
typedef void (APIENTRYP PFNMYGLGETCOLORTABLEPARAMETERFVPROC) (GLenum target, GLenum pname, GLfloat *params);
typedef void (APIENTRYP PFNMYGLGETCOLORTABLEPARAMETERIVPROC) (GLenum target, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLCOLORSUBTABLEPROC) (GLenum target, GLsizei start, GLsizei count, GLenum format, GLenum type, const GLvoid *data);
typedef void (APIENTRYP PFNMYGLCOPYCOLORSUBTABLEPROC) (GLenum target, GLsizei start, GLint x, GLint y, GLsizei width);
typedef void (APIENTRYP PFNMYGLCONVOLUTIONFILTER1DPROC) (GLenum target, GLenum internalformat, GLsizei width, GLenum format, GLenum type, const GLvoid *image);
typedef void (APIENTRYP PFNMYGLCONVOLUTIONFILTER2DPROC) (GLenum target, GLenum internalformat, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *image);
typedef void (APIENTRYP PFNMYGLCONVOLUTIONPARAMETERFPROC) (GLenum target, GLenum pname, GLfloat params);
typedef void (APIENTRYP PFNMYGLCONVOLUTIONPARAMETERFVPROC) (GLenum target, GLenum pname, const GLfloat *params);
typedef void (APIENTRYP PFNMYGLCONVOLUTIONPARAMETERIPROC) (GLenum target, GLenum pname, GLint params);
typedef void (APIENTRYP PFNMYGLCONVOLUTIONPARAMETERIVPROC) (GLenum target, GLenum pname, const GLint *params);
typedef void (APIENTRYP PFNMYGLCOPYCONVOLUTIONFILTER1DPROC) (GLenum target, GLenum internalformat, GLint x, GLint y, GLsizei width);
typedef void (APIENTRYP PFNMYGLCOPYCONVOLUTIONFILTER2DPROC) (GLenum target, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height);
typedef void (APIENTRYP PFNMYGLGETCONVOLUTIONFILTERPROC) (GLenum target, GLenum format, GLenum type, GLvoid *image);
typedef void (APIENTRYP PFNMYGLGETCONVOLUTIONPARAMETERFVPROC) (GLenum target, GLenum pname, GLfloat *params);
typedef void (APIENTRYP PFNMYGLGETCONVOLUTIONPARAMETERIVPROC) (GLenum target, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLGETSEPARABLEFILTERPROC) (GLenum target, GLenum format, GLenum type, GLvoid *row, GLvoid *column, GLvoid *span);
typedef void (APIENTRYP PFNMYGLSEPARABLEFILTER2DPROC) (GLenum target, GLenum internalformat, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *row, const GLvoid *column);
typedef void (APIENTRYP PFNMYGLGETHISTOGRAMPROC) (GLenum target, GLboolean reset, GLenum format, GLenum type, GLvoid *values);
typedef void (APIENTRYP PFNMYGLGETHISTOGRAMPARAMETERFVPROC) (GLenum target, GLenum pname, GLfloat *params);
typedef void (APIENTRYP PFNMYGLGETHISTOGRAMPARAMETERIVPROC) (GLenum target, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLGETMINMAXPROC) (GLenum target, GLboolean reset, GLenum format, GLenum type, GLvoid *values);
typedef void (APIENTRYP PFNMYGLGETMINMAXPARAMETERFVPROC) (GLenum target, GLenum pname, GLfloat *params);
typedef void (APIENTRYP PFNMYGLGETMINMAXPARAMETERIVPROC) (GLenum target, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLHISTOGRAMPROC) (GLenum target, GLsizei width, GLenum internalformat, GLboolean sink);
typedef void (APIENTRYP PFNMYGLMINMAXPROC) (GLenum target, GLenum internalformat, GLboolean sink);
typedef void (APIENTRYP PFNMYGLRESETHISTOGRAMPROC) (GLenum target);
typedef void (APIENTRYP PFNMYGLRESETMINMAXPROC) (GLenum target);
typedef void (APIENTRYP PFNMYGLTEXIMAGE3DPROC) (GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
typedef void (APIENTRYP PFNMYGLTEXSUBIMAGE3DPROC) (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const GLvoid *pixels);
typedef void (APIENTRYP PFNMYGLCOPYTEXSUBIMAGE3DPROC) (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLint x, GLint y, GLsizei width, GLsizei height);
#endif

#ifndef MYGL_VERSION_1_3
#define MYGL_VERSION_1_3 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglActiveTexture (GLenum);
GLAPI void APIENTRY myglClientActiveTexture (GLenum);
GLAPI void APIENTRY myglMultiTexCoord1d (GLenum, GLdouble);
GLAPI void APIENTRY myglMultiTexCoord1dv (GLenum, const GLdouble *);
GLAPI void APIENTRY myglMultiTexCoord1f (GLenum, GLfloat);
GLAPI void APIENTRY myglMultiTexCoord1fv (GLenum, const GLfloat *);
GLAPI void APIENTRY myglMultiTexCoord1i (GLenum, GLint);
GLAPI void APIENTRY myglMultiTexCoord1iv (GLenum, const GLint *);
GLAPI void APIENTRY myglMultiTexCoord1s (GLenum, GLshort);
GLAPI void APIENTRY myglMultiTexCoord1sv (GLenum, const GLshort *);
GLAPI void APIENTRY myglMultiTexCoord2d (GLenum, GLdouble, GLdouble);
GLAPI void APIENTRY myglMultiTexCoord2dv (GLenum, const GLdouble *);
GLAPI void APIENTRY myglMultiTexCoord2f (GLenum, GLfloat, GLfloat);
GLAPI void APIENTRY myglMultiTexCoord2fv (GLenum, const GLfloat *);
GLAPI void APIENTRY myglMultiTexCoord2i (GLenum, GLint, GLint);
GLAPI void APIENTRY myglMultiTexCoord2iv (GLenum, const GLint *);
GLAPI void APIENTRY myglMultiTexCoord2s (GLenum, GLshort, GLshort);
GLAPI void APIENTRY myglMultiTexCoord2sv (GLenum, const GLshort *);
GLAPI void APIENTRY myglMultiTexCoord3d (GLenum, GLdouble, GLdouble, GLdouble);
GLAPI void APIENTRY myglMultiTexCoord3dv (GLenum, const GLdouble *);
GLAPI void APIENTRY myglMultiTexCoord3f (GLenum, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglMultiTexCoord3fv (GLenum, const GLfloat *);
GLAPI void APIENTRY myglMultiTexCoord3i (GLenum, GLint, GLint, GLint);
GLAPI void APIENTRY myglMultiTexCoord3iv (GLenum, const GLint *);
GLAPI void APIENTRY myglMultiTexCoord3s (GLenum, GLshort, GLshort, GLshort);
GLAPI void APIENTRY myglMultiTexCoord3sv (GLenum, const GLshort *);
GLAPI void APIENTRY myglMultiTexCoord4d (GLenum, GLdouble, GLdouble, GLdouble, GLdouble);
GLAPI void APIENTRY myglMultiTexCoord4dv (GLenum, const GLdouble *);
GLAPI void APIENTRY myglMultiTexCoord4f (GLenum, GLfloat, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglMultiTexCoord4fv (GLenum, const GLfloat *);
GLAPI void APIENTRY myglMultiTexCoord4i (GLenum, GLint, GLint, GLint, GLint);
GLAPI void APIENTRY myglMultiTexCoord4iv (GLenum, const GLint *);
GLAPI void APIENTRY myglMultiTexCoord4s (GLenum, GLshort, GLshort, GLshort, GLshort);
GLAPI void APIENTRY myglMultiTexCoord4sv (GLenum, const GLshort *);
GLAPI void APIENTRY myglLoadTransposeMatrixf (const GLfloat *);
GLAPI void APIENTRY myglLoadTransposeMatrixd (const GLdouble *);
GLAPI void APIENTRY myglMultTransposeMatrixf (const GLfloat *);
GLAPI void APIENTRY myglMultTransposeMatrixd (const GLdouble *);
GLAPI void APIENTRY myglSampleCoverage (GLclampf, GLboolean);
GLAPI void APIENTRY myglCompressedTexImage3D (GLenum, GLint, GLenum, GLsizei, GLsizei, GLsizei, GLint, GLsizei, const GLvoid *);
GLAPI void APIENTRY myglCompressedTexImage2D (GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const GLvoid *);
GLAPI void APIENTRY myglCompressedTexImage1D (GLenum, GLint, GLenum, GLsizei, GLint, GLsizei, const GLvoid *);
GLAPI void APIENTRY myglCompressedTexSubImage3D (GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLsizei, const GLvoid *);
GLAPI void APIENTRY myglCompressedTexSubImage2D (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const GLvoid *);
GLAPI void APIENTRY myglCompressedTexSubImage1D (GLenum, GLint, GLint, GLsizei, GLenum, GLsizei, const GLvoid *);
GLAPI void APIENTRY myglGetCompressedTexImage (GLenum, GLint, GLvoid *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLACTIVETEXTUREPROC) (GLenum texture);
typedef void (APIENTRYP PFNMYGLCLIENTACTIVETEXTUREPROC) (GLenum texture);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD1DPROC) (GLenum target, GLdouble s);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD1DVPROC) (GLenum target, const GLdouble *v);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD1FPROC) (GLenum target, GLfloat s);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD1FVPROC) (GLenum target, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD1IPROC) (GLenum target, GLint s);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD1IVPROC) (GLenum target, const GLint *v);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD1SPROC) (GLenum target, GLshort s);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD1SVPROC) (GLenum target, const GLshort *v);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD2DPROC) (GLenum target, GLdouble s, GLdouble t);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD2DVPROC) (GLenum target, const GLdouble *v);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD2FPROC) (GLenum target, GLfloat s, GLfloat t);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD2FVPROC) (GLenum target, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD2IPROC) (GLenum target, GLint s, GLint t);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD2IVPROC) (GLenum target, const GLint *v);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD2SPROC) (GLenum target, GLshort s, GLshort t);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD2SVPROC) (GLenum target, const GLshort *v);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD3DPROC) (GLenum target, GLdouble s, GLdouble t, GLdouble r);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD3DVPROC) (GLenum target, const GLdouble *v);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD3FPROC) (GLenum target, GLfloat s, GLfloat t, GLfloat r);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD3FVPROC) (GLenum target, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD3IPROC) (GLenum target, GLint s, GLint t, GLint r);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD3IVPROC) (GLenum target, const GLint *v);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD3SPROC) (GLenum target, GLshort s, GLshort t, GLshort r);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD3SVPROC) (GLenum target, const GLshort *v);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD4DPROC) (GLenum target, GLdouble s, GLdouble t, GLdouble r, GLdouble q);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD4DVPROC) (GLenum target, const GLdouble *v);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD4FPROC) (GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD4FVPROC) (GLenum target, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD4IPROC) (GLenum target, GLint s, GLint t, GLint r, GLint q);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD4IVPROC) (GLenum target, const GLint *v);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD4SPROC) (GLenum target, GLshort s, GLshort t, GLshort r, GLshort q);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD4SVPROC) (GLenum target, const GLshort *v);
typedef void (APIENTRYP PFNMYGLLOADTRANSPOSEMATRIXFPROC) (const GLfloat *m);
typedef void (APIENTRYP PFNMYGLLOADTRANSPOSEMATRIXDPROC) (const GLdouble *m);
typedef void (APIENTRYP PFNMYGLMULTTRANSPOSEMATRIXFPROC) (const GLfloat *m);
typedef void (APIENTRYP PFNMYGLMULTTRANSPOSEMATRIXDPROC) (const GLdouble *m);
typedef void (APIENTRYP PFNMYGLSAMPLECOVERAGEPROC) (GLclampf value, GLboolean invert);
typedef void (APIENTRYP PFNMYGLCOMPRESSEDTEXIMAGE3DPROC) (GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLsizei imageSize, const GLvoid *data);
typedef void (APIENTRYP PFNMYGLCOMPRESSEDTEXIMAGE2DPROC) (GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const GLvoid *data);
typedef void (APIENTRYP PFNMYGLCOMPRESSEDTEXIMAGE1DPROC) (GLenum target, GLint level, GLenum internalformat, GLsizei width, GLint border, GLsizei imageSize, const GLvoid *data);
typedef void (APIENTRYP PFNMYGLCOMPRESSEDTEXSUBIMAGE3DPROC) (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const GLvoid *data);
typedef void (APIENTRYP PFNMYGLCOMPRESSEDTEXSUBIMAGE2DPROC) (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const GLvoid *data);
typedef void (APIENTRYP PFNMYGLCOMPRESSEDTEXSUBIMAGE1DPROC) (GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLsizei imageSize, const GLvoid *data);
typedef void (APIENTRYP PFNMYGLGETCOMPRESSEDTEXIMAGEPROC) (GLenum target, GLint level, GLvoid *img);
#endif

#ifndef MYGL_VERSION_1_4
#define MYGL_VERSION_1_4 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglBlendFuncSeparate (GLenum, GLenum, GLenum, GLenum);
GLAPI void APIENTRY myglFogCoordf (GLfloat);
GLAPI void APIENTRY myglFogCoordfv (const GLfloat *);
GLAPI void APIENTRY myglFogCoordd (GLdouble);
GLAPI void APIENTRY myglFogCoorddv (const GLdouble *);
GLAPI void APIENTRY myglFogCoordPointer (GLenum, GLsizei, const GLvoid *);
GLAPI void APIENTRY myglMultiDrawArrays (GLenum, GLint *, GLsizei *, GLsizei);
GLAPI void APIENTRY myglMultiDrawElements (GLenum, const GLsizei *, GLenum, const GLvoid* *, GLsizei);
GLAPI void APIENTRY myglPointParameterf (GLenum, GLfloat);
GLAPI void APIENTRY myglPointParameterfv (GLenum, const GLfloat *);
GLAPI void APIENTRY myglPointParameteri (GLenum, GLint);
GLAPI void APIENTRY myglPointParameteriv (GLenum, const GLint *);
GLAPI void APIENTRY myglSecondaryColor3b (GLbyte, GLbyte, GLbyte);
GLAPI void APIENTRY myglSecondaryColor3bv (const GLbyte *);
GLAPI void APIENTRY myglSecondaryColor3d (GLdouble, GLdouble, GLdouble);
GLAPI void APIENTRY myglSecondaryColor3dv (const GLdouble *);
GLAPI void APIENTRY myglSecondaryColor3f (GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglSecondaryColor3fv (const GLfloat *);
GLAPI void APIENTRY myglSecondaryColor3i (GLint, GLint, GLint);
GLAPI void APIENTRY myglSecondaryColor3iv (const GLint *);
GLAPI void APIENTRY myglSecondaryColor3s (GLshort, GLshort, GLshort);
GLAPI void APIENTRY myglSecondaryColor3sv (const GLshort *);
GLAPI void APIENTRY myglSecondaryColor3ub (GLubyte, GLubyte, GLubyte);
GLAPI void APIENTRY myglSecondaryColor3ubv (const GLubyte *);
GLAPI void APIENTRY myglSecondaryColor3ui (GLuint, GLuint, GLuint);
GLAPI void APIENTRY myglSecondaryColor3uiv (const GLuint *);
GLAPI void APIENTRY myglSecondaryColor3us (GLushort, GLushort, GLushort);
GLAPI void APIENTRY myglSecondaryColor3usv (const GLushort *);
GLAPI void APIENTRY myglSecondaryColorPointer (GLint, GLenum, GLsizei, const GLvoid *);
GLAPI void APIENTRY myglWindowPos2d (GLdouble, GLdouble);
GLAPI void APIENTRY myglWindowPos2dv (const GLdouble *);
GLAPI void APIENTRY myglWindowPos2f (GLfloat, GLfloat);
GLAPI void APIENTRY myglWindowPos2fv (const GLfloat *);
GLAPI void APIENTRY myglWindowPos2i (GLint, GLint);
GLAPI void APIENTRY myglWindowPos2iv (const GLint *);
GLAPI void APIENTRY myglWindowPos2s (GLshort, GLshort);
GLAPI void APIENTRY myglWindowPos2sv (const GLshort *);
GLAPI void APIENTRY myglWindowPos3d (GLdouble, GLdouble, GLdouble);
GLAPI void APIENTRY myglWindowPos3dv (const GLdouble *);
GLAPI void APIENTRY myglWindowPos3f (GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglWindowPos3fv (const GLfloat *);
GLAPI void APIENTRY myglWindowPos3i (GLint, GLint, GLint);
GLAPI void APIENTRY myglWindowPos3iv (const GLint *);
GLAPI void APIENTRY myglWindowPos3s (GLshort, GLshort, GLshort);
GLAPI void APIENTRY myglWindowPos3sv (const GLshort *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLBLENDFUNCSEPARATEPROC) (GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha);
typedef void (APIENTRYP PFNMYGLFOGCOORDFPROC) (GLfloat coord);
typedef void (APIENTRYP PFNMYGLFOGCOORDFVPROC) (const GLfloat *coord);
typedef void (APIENTRYP PFNMYGLFOGCOORDDPROC) (GLdouble coord);
typedef void (APIENTRYP PFNMYGLFOGCOORDDVPROC) (const GLdouble *coord);
typedef void (APIENTRYP PFNMYGLFOGCOORDPOINTERPROC) (GLenum type, GLsizei stride, const GLvoid *pointer);
typedef void (APIENTRYP PFNMYGLMULTIDRAWARRAYSPROC) (GLenum mode, GLint *first, GLsizei *count, GLsizei primcount);
typedef void (APIENTRYP PFNMYGLMULTIDRAWELEMENTSPROC) (GLenum mode, const GLsizei *count, GLenum type, const GLvoid* *indices, GLsizei primcount);
typedef void (APIENTRYP PFNMYGLPOINTPARAMETERFPROC) (GLenum pname, GLfloat param);
typedef void (APIENTRYP PFNMYGLPOINTPARAMETERFVPROC) (GLenum pname, const GLfloat *params);
typedef void (APIENTRYP PFNMYGLPOINTPARAMETERIPROC) (GLenum pname, GLint param);
typedef void (APIENTRYP PFNMYGLPOINTPARAMETERIVPROC) (GLenum pname, const GLint *params);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLOR3BPROC) (GLbyte red, GLbyte green, GLbyte blue);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLOR3BVPROC) (const GLbyte *v);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLOR3DPROC) (GLdouble red, GLdouble green, GLdouble blue);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLOR3DVPROC) (const GLdouble *v);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLOR3FPROC) (GLfloat red, GLfloat green, GLfloat blue);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLOR3FVPROC) (const GLfloat *v);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLOR3IPROC) (GLint red, GLint green, GLint blue);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLOR3IVPROC) (const GLint *v);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLOR3SPROC) (GLshort red, GLshort green, GLshort blue);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLOR3SVPROC) (const GLshort *v);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLOR3UBPROC) (GLubyte red, GLubyte green, GLubyte blue);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLOR3UBVPROC) (const GLubyte *v);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLOR3UIPROC) (GLuint red, GLuint green, GLuint blue);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLOR3UIVPROC) (const GLuint *v);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLOR3USPROC) (GLushort red, GLushort green, GLushort blue);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLOR3USVPROC) (const GLushort *v);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLORPOINTERPROC) (GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
typedef void (APIENTRYP PFNMYGLWINDOWPOS2DPROC) (GLdouble x, GLdouble y);
typedef void (APIENTRYP PFNMYGLWINDOWPOS2DVPROC) (const GLdouble *v);
typedef void (APIENTRYP PFNMYGLWINDOWPOS2FPROC) (GLfloat x, GLfloat y);
typedef void (APIENTRYP PFNMYGLWINDOWPOS2FVPROC) (const GLfloat *v);
typedef void (APIENTRYP PFNMYGLWINDOWPOS2IPROC) (GLint x, GLint y);
typedef void (APIENTRYP PFNMYGLWINDOWPOS2IVPROC) (const GLint *v);
typedef void (APIENTRYP PFNMYGLWINDOWPOS2SPROC) (GLshort x, GLshort y);
typedef void (APIENTRYP PFNMYGLWINDOWPOS2SVPROC) (const GLshort *v);
typedef void (APIENTRYP PFNMYGLWINDOWPOS3DPROC) (GLdouble x, GLdouble y, GLdouble z);
typedef void (APIENTRYP PFNMYGLWINDOWPOS3DVPROC) (const GLdouble *v);
typedef void (APIENTRYP PFNMYGLWINDOWPOS3FPROC) (GLfloat x, GLfloat y, GLfloat z);
typedef void (APIENTRYP PFNMYGLWINDOWPOS3FVPROC) (const GLfloat *v);
typedef void (APIENTRYP PFNMYGLWINDOWPOS3IPROC) (GLint x, GLint y, GLint z);
typedef void (APIENTRYP PFNMYGLWINDOWPOS3IVPROC) (const GLint *v);
typedef void (APIENTRYP PFNMYGLWINDOWPOS3SPROC) (GLshort x, GLshort y, GLshort z);
typedef void (APIENTRYP PFNMYGLWINDOWPOS3SVPROC) (const GLshort *v);
#endif

#ifndef MYGL_VERSION_1_5
#define MYGL_VERSION_1_5 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglGenQueries (GLsizei, GLuint *);
GLAPI void APIENTRY myglDeleteQueries (GLsizei, const GLuint *);
GLAPI GLboolean APIENTRY myglIsQuery (GLuint);
GLAPI void APIENTRY myglBeginQuery (GLenum, GLuint);
GLAPI void APIENTRY myglEndQuery (GLenum);
GLAPI void APIENTRY myglGetQueryiv (GLenum, GLenum, GLint *);
GLAPI void APIENTRY myglGetQueryObjectiv (GLuint, GLenum, GLint *);
GLAPI void APIENTRY myglGetQueryObjectuiv (GLuint, GLenum, GLuint *);
GLAPI void APIENTRY myglBindBuffer (GLenum, GLuint);
GLAPI void APIENTRY myglDeleteBuffers (GLsizei, const GLuint *);
GLAPI void APIENTRY myglGenBuffers (GLsizei, GLuint *);
GLAPI GLboolean APIENTRY myglIsBuffer (GLuint);
GLAPI void APIENTRY myglBufferData (GLenum, GLsizeiptr, const GLvoid *, GLenum);
GLAPI void APIENTRY myglBufferSubData (GLenum, GLintptr, GLsizeiptr, const GLvoid *);
GLAPI void APIENTRY myglGetBufferSubData (GLenum, GLintptr, GLsizeiptr, GLvoid *);
GLAPI GLvoid* APIENTRY myglMapBuffer (GLenum, GLenum);
GLAPI GLboolean APIENTRY myglUnmapBuffer (GLenum);
GLAPI void APIENTRY myglGetBufferParameteriv (GLenum, GLenum, GLint *);
GLAPI void APIENTRY myglGetBufferPointerv (GLenum, GLenum, GLvoid* *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLGENQUERIESPROC) (GLsizei n, GLuint *ids);
typedef void (APIENTRYP PFNMYGLDELETEQUERIESPROC) (GLsizei n, const GLuint *ids);
typedef GLboolean (APIENTRYP PFNMYGLISQUERYPROC) (GLuint id);
typedef void (APIENTRYP PFNMYGLBEGINQUERYPROC) (GLenum target, GLuint id);
typedef void (APIENTRYP PFNMYGLENDQUERYPROC) (GLenum target);
typedef void (APIENTRYP PFNMYGLGETQUERYIVPROC) (GLenum target, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLGETQUERYOBJECTIVPROC) (GLuint id, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLGETQUERYOBJECTUIVPROC) (GLuint id, GLenum pname, GLuint *params);
typedef void (APIENTRYP PFNMYGLBINDBUFFERPROC) (GLenum target, GLuint buffer);
typedef void (APIENTRYP PFNMYGLDELETEBUFFERSPROC) (GLsizei n, const GLuint *buffers);
typedef void (APIENTRYP PFNMYGLGENBUFFERSPROC) (GLsizei n, GLuint *buffers);
typedef GLboolean (APIENTRYP PFNMYGLISBUFFERPROC) (GLuint buffer);
typedef void (APIENTRYP PFNMYGLBUFFERDATAPROC) (GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage);
typedef void (APIENTRYP PFNMYGLBUFFERSUBDATAPROC) (GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid *data);
typedef void (APIENTRYP PFNMYGLGETBUFFERSUBDATAPROC) (GLenum target, GLintptr offset, GLsizeiptr size, GLvoid *data);
typedef GLvoid* (APIENTRYP PFNMYGLMAPBUFFERPROC) (GLenum target, GLenum access);
typedef GLboolean (APIENTRYP PFNMYGLUNMAPBUFFERPROC) (GLenum target);
typedef void (APIENTRYP PFNMYGLGETBUFFERPARAMETERIVPROC) (GLenum target, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLGETBUFFERPOINTERVPROC) (GLenum target, GLenum pname, GLvoid* *params);
#endif

#ifndef MYGL_VERSION_2_0
#define MYGL_VERSION_2_0 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglBlendEquationSeparate (GLenum, GLenum);
GLAPI void APIENTRY myglDrawBuffers (GLsizei, const GLenum *);
GLAPI void APIENTRY myglStencilOpSeparate (GLenum, GLenum, GLenum, GLenum);
GLAPI void APIENTRY myglStencilFuncSeparate (GLenum, GLenum, GLint, GLuint);
GLAPI void APIENTRY myglStencilMaskSeparate (GLenum, GLuint);
GLAPI void APIENTRY myglAttachShader (GLuint, GLuint);
GLAPI void APIENTRY myglBindAttribLocation (GLuint, GLuint, const GLchar *);
GLAPI void APIENTRY myglCompileShader (GLuint);
GLAPI GLuint APIENTRY myglCreateProgram (void);
GLAPI GLuint APIENTRY myglCreateShader (GLenum);
GLAPI void APIENTRY myglDeleteProgram (GLuint);
GLAPI void APIENTRY myglDeleteShader (GLuint);
GLAPI void APIENTRY myglDetachShader (GLuint, GLuint);
GLAPI void APIENTRY myglDisableVertexAttribArray (GLuint);
GLAPI void APIENTRY myglEnableVertexAttribArray (GLuint);
GLAPI void APIENTRY myglGetActiveAttrib (GLuint, GLuint, GLsizei, GLsizei *, GLint *, GLenum *, GLchar *);
GLAPI void APIENTRY myglGetActiveUniform (GLuint, GLuint, GLsizei, GLsizei *, GLint *, GLenum *, GLchar *);
GLAPI void APIENTRY myglGetAttachedShaders (GLuint, GLsizei, GLsizei *, GLuint *);
GLAPI GLint APIENTRY myglGetAttribLocation (GLuint, const GLchar *);
GLAPI void APIENTRY myglGetProgramiv (GLuint, GLenum, GLint *);
GLAPI void APIENTRY myglGetProgramInfoLog (GLuint, GLsizei, GLsizei *, GLchar *);
GLAPI void APIENTRY myglGetShaderiv (GLuint, GLenum, GLint *);
GLAPI void APIENTRY myglGetShaderInfoLog (GLuint, GLsizei, GLsizei *, GLchar *);
GLAPI void APIENTRY myglGetShaderSource (GLuint, GLsizei, GLsizei *, GLchar *);
GLAPI GLint APIENTRY myglGetUniformLocation (GLuint, const GLchar *);
GLAPI void APIENTRY myglGetUniformfv (GLuint, GLint, GLfloat *);
GLAPI void APIENTRY myglGetUniformiv (GLuint, GLint, GLint *);
GLAPI void APIENTRY myglGetVertexAttribdv (GLuint, GLenum, GLdouble *);
GLAPI void APIENTRY myglGetVertexAttribfv (GLuint, GLenum, GLfloat *);
GLAPI void APIENTRY myglGetVertexAttribiv (GLuint, GLenum, GLint *);
GLAPI void APIENTRY myglGetVertexAttribPointerv (GLuint, GLenum, GLvoid* *);
GLAPI GLboolean APIENTRY myglIsProgram (GLuint);
GLAPI GLboolean APIENTRY myglIsShader (GLuint);
GLAPI void APIENTRY myglLinkProgram (GLuint);
GLAPI void APIENTRY myglShaderSource (GLuint, GLsizei, const GLchar* *, const GLint *);
GLAPI void APIENTRY myglUseProgram (GLuint);
GLAPI void APIENTRY myglUniform1f (GLint, GLfloat);
GLAPI void APIENTRY myglUniform2f (GLint, GLfloat, GLfloat);
GLAPI void APIENTRY myglUniform3f (GLint, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglUniform4f (GLint, GLfloat, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglUniform1i (GLint, GLint);
GLAPI void APIENTRY myglUniform2i (GLint, GLint, GLint);
GLAPI void APIENTRY myglUniform3i (GLint, GLint, GLint, GLint);
GLAPI void APIENTRY myglUniform4i (GLint, GLint, GLint, GLint, GLint);
GLAPI void APIENTRY myglUniform1fv (GLint, GLsizei, const GLfloat *);
GLAPI void APIENTRY myglUniform2fv (GLint, GLsizei, const GLfloat *);
GLAPI void APIENTRY myglUniform3fv (GLint, GLsizei, const GLfloat *);
GLAPI void APIENTRY myglUniform4fv (GLint, GLsizei, const GLfloat *);
GLAPI void APIENTRY myglUniform1iv (GLint, GLsizei, const GLint *);
GLAPI void APIENTRY myglUniform2iv (GLint, GLsizei, const GLint *);
GLAPI void APIENTRY myglUniform3iv (GLint, GLsizei, const GLint *);
GLAPI void APIENTRY myglUniform4iv (GLint, GLsizei, const GLint *);
GLAPI void APIENTRY myglUniformMatrix2fv (GLint, GLsizei, GLboolean, const GLfloat *);
GLAPI void APIENTRY myglUniformMatrix3fv (GLint, GLsizei, GLboolean, const GLfloat *);
GLAPI void APIENTRY myglUniformMatrix4fv (GLint, GLsizei, GLboolean, const GLfloat *);
GLAPI void APIENTRY myglValidateProgram (GLuint);
GLAPI void APIENTRY myglVertexAttrib1d (GLuint, GLdouble);
GLAPI void APIENTRY myglVertexAttrib1dv (GLuint, const GLdouble *);
GLAPI void APIENTRY myglVertexAttrib1f (GLuint, GLfloat);
GLAPI void APIENTRY myglVertexAttrib1fv (GLuint, const GLfloat *);
GLAPI void APIENTRY myglVertexAttrib1s (GLuint, GLshort);
GLAPI void APIENTRY myglVertexAttrib1sv (GLuint, const GLshort *);
GLAPI void APIENTRY myglVertexAttrib2d (GLuint, GLdouble, GLdouble);
GLAPI void APIENTRY myglVertexAttrib2dv (GLuint, const GLdouble *);
GLAPI void APIENTRY myglVertexAttrib2f (GLuint, GLfloat, GLfloat);
GLAPI void APIENTRY myglVertexAttrib2fv (GLuint, const GLfloat *);
GLAPI void APIENTRY myglVertexAttrib2s (GLuint, GLshort, GLshort);
GLAPI void APIENTRY myglVertexAttrib2sv (GLuint, const GLshort *);
GLAPI void APIENTRY myglVertexAttrib3d (GLuint, GLdouble, GLdouble, GLdouble);
GLAPI void APIENTRY myglVertexAttrib3dv (GLuint, const GLdouble *);
GLAPI void APIENTRY myglVertexAttrib3f (GLuint, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglVertexAttrib3fv (GLuint, const GLfloat *);
GLAPI void APIENTRY myglVertexAttrib3s (GLuint, GLshort, GLshort, GLshort);
GLAPI void APIENTRY myglVertexAttrib3sv (GLuint, const GLshort *);
GLAPI void APIENTRY myglVertexAttrib4Nbv (GLuint, const GLbyte *);
GLAPI void APIENTRY myglVertexAttrib4Niv (GLuint, const GLint *);
GLAPI void APIENTRY myglVertexAttrib4Nsv (GLuint, const GLshort *);
GLAPI void APIENTRY myglVertexAttrib4Nub (GLuint, GLubyte, GLubyte, GLubyte, GLubyte);
GLAPI void APIENTRY myglVertexAttrib4Nubv (GLuint, const GLubyte *);
GLAPI void APIENTRY myglVertexAttrib4Nuiv (GLuint, const GLuint *);
GLAPI void APIENTRY myglVertexAttrib4Nusv (GLuint, const GLushort *);
GLAPI void APIENTRY myglVertexAttrib4bv (GLuint, const GLbyte *);
GLAPI void APIENTRY myglVertexAttrib4d (GLuint, GLdouble, GLdouble, GLdouble, GLdouble);
GLAPI void APIENTRY myglVertexAttrib4dv (GLuint, const GLdouble *);
GLAPI void APIENTRY myglVertexAttrib4f (GLuint, GLfloat, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglVertexAttrib4fv (GLuint, const GLfloat *);
GLAPI void APIENTRY myglVertexAttrib4iv (GLuint, const GLint *);
GLAPI void APIENTRY myglVertexAttrib4s (GLuint, GLshort, GLshort, GLshort, GLshort);
GLAPI void APIENTRY myglVertexAttrib4sv (GLuint, const GLshort *);
GLAPI void APIENTRY myglVertexAttrib4ubv (GLuint, const GLubyte *);
GLAPI void APIENTRY myglVertexAttrib4uiv (GLuint, const GLuint *);
GLAPI void APIENTRY myglVertexAttrib4usv (GLuint, const GLushort *);
GLAPI void APIENTRY myglVertexAttribPointer (GLuint, GLint, GLenum, GLboolean, GLsizei, const GLvoid *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLBLENDEQUATIONSEPARATEPROC) (GLenum modeRGB, GLenum modeAlpha);
typedef void (APIENTRYP PFNMYGLDRAWBUFFERSPROC) (GLsizei n, const GLenum *bufs);
typedef void (APIENTRYP PFNMYGLSTENCILOPSEPARATEPROC) (GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass);
typedef void (APIENTRYP PFNMYGLSTENCILFUNCSEPARATEPROC) (GLenum frontfunc, GLenum backfunc, GLint ref, GLuint mask);
typedef void (APIENTRYP PFNMYGLSTENCILMASKSEPARATEPROC) (GLenum face, GLuint mask);
typedef void (APIENTRYP PFNMYGLATTACHSHADERPROC) (GLuint program, GLuint shader);
typedef void (APIENTRYP PFNMYGLBINDATTRIBLOCATIONPROC) (GLuint program, GLuint index, const GLchar *name);
typedef void (APIENTRYP PFNMYGLCOMPILESHADERPROC) (GLuint shader);
typedef GLuint (APIENTRYP PFNMYGLCREATEPROGRAMPROC) (void);
typedef GLuint (APIENTRYP PFNMYGLCREATESHADERPROC) (GLenum type);
typedef void (APIENTRYP PFNMYGLDELETEPROGRAMPROC) (GLuint program);
typedef void (APIENTRYP PFNMYGLDELETESHADERPROC) (GLuint shader);
typedef void (APIENTRYP PFNMYGLDETACHSHADERPROC) (GLuint program, GLuint shader);
typedef void (APIENTRYP PFNMYGLDISABLEVERTEXATTRIBARRAYPROC) (GLuint index);
typedef void (APIENTRYP PFNMYGLENABLEVERTEXATTRIBARRAYPROC) (GLuint index);
typedef void (APIENTRYP PFNMYGLGETACTIVEATTRIBPROC) (GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name);
typedef void (APIENTRYP PFNMYGLGETACTIVEUNIFORMPROC) (GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name);
typedef void (APIENTRYP PFNMYGLGETATTACHEDSHADERSPROC) (GLuint program, GLsizei maxCount, GLsizei *count, GLuint *obj);
typedef GLint (APIENTRYP PFNMYGLGETATTRIBLOCATIONPROC) (GLuint program, const GLchar *name);
typedef void (APIENTRYP PFNMYGLGETPROGRAMIVPROC) (GLuint program, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLGETPROGRAMINFOLOGPROC) (GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
typedef void (APIENTRYP PFNMYGLGETSHADERIVPROC) (GLuint shader, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLGETSHADERINFOLOGPROC) (GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
typedef void (APIENTRYP PFNMYGLGETSHADERSOURCEPROC) (GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *source);
typedef GLint (APIENTRYP PFNMYGLGETUNIFORMLOCATIONPROC) (GLuint program, const GLchar *name);
typedef void (APIENTRYP PFNMYGLGETUNIFORMFVPROC) (GLuint program, GLint location, GLfloat *params);
typedef void (APIENTRYP PFNMYGLGETUNIFORMIVPROC) (GLuint program, GLint location, GLint *params);
typedef void (APIENTRYP PFNMYGLGETVERTEXATTRIBDVPROC) (GLuint index, GLenum pname, GLdouble *params);
typedef void (APIENTRYP PFNMYGLGETVERTEXATTRIBFVPROC) (GLuint index, GLenum pname, GLfloat *params);
typedef void (APIENTRYP PFNMYGLGETVERTEXATTRIBIVPROC) (GLuint index, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLGETVERTEXATTRIBPOINTERVPROC) (GLuint index, GLenum pname, GLvoid* *pointer);
typedef GLboolean (APIENTRYP PFNMYGLISPROGRAMPROC) (GLuint program);
typedef GLboolean (APIENTRYP PFNMYGLISSHADERPROC) (GLuint shader);
typedef void (APIENTRYP PFNMYGLLINKPROGRAMPROC) (GLuint program);
typedef void (APIENTRYP PFNMYGLSHADERSOURCEPROC) (GLuint shader, GLsizei count, const GLchar* *string, const GLint *length);
typedef void (APIENTRYP PFNMYGLUSEPROGRAMPROC) (GLuint program);
typedef void (APIENTRYP PFNMYGLUNIFORM1FPROC) (GLint location, GLfloat v0);
typedef void (APIENTRYP PFNMYGLUNIFORM2FPROC) (GLint location, GLfloat v0, GLfloat v1);
typedef void (APIENTRYP PFNMYGLUNIFORM3FPROC) (GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
typedef void (APIENTRYP PFNMYGLUNIFORM4FPROC) (GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
typedef void (APIENTRYP PFNMYGLUNIFORM1IPROC) (GLint location, GLint v0);
typedef void (APIENTRYP PFNMYGLUNIFORM2IPROC) (GLint location, GLint v0, GLint v1);
typedef void (APIENTRYP PFNMYGLUNIFORM3IPROC) (GLint location, GLint v0, GLint v1, GLint v2);
typedef void (APIENTRYP PFNMYGLUNIFORM4IPROC) (GLint location, GLint v0, GLint v1, GLint v2, GLint v3);
typedef void (APIENTRYP PFNMYGLUNIFORM1FVPROC) (GLint location, GLsizei count, const GLfloat *value);
typedef void (APIENTRYP PFNMYGLUNIFORM2FVPROC) (GLint location, GLsizei count, const GLfloat *value);
typedef void (APIENTRYP PFNMYGLUNIFORM3FVPROC) (GLint location, GLsizei count, const GLfloat *value);
typedef void (APIENTRYP PFNMYGLUNIFORM4FVPROC) (GLint location, GLsizei count, const GLfloat *value);
typedef void (APIENTRYP PFNMYGLUNIFORM1IVPROC) (GLint location, GLsizei count, const GLint *value);
typedef void (APIENTRYP PFNMYGLUNIFORM2IVPROC) (GLint location, GLsizei count, const GLint *value);
typedef void (APIENTRYP PFNMYGLUNIFORM3IVPROC) (GLint location, GLsizei count, const GLint *value);
typedef void (APIENTRYP PFNMYGLUNIFORM4IVPROC) (GLint location, GLsizei count, const GLint *value);
typedef void (APIENTRYP PFNMYGLUNIFORMMATRIX2FVPROC) (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
typedef void (APIENTRYP PFNMYGLUNIFORMMATRIX3FVPROC) (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
typedef void (APIENTRYP PFNMYGLUNIFORMMATRIX4FVPROC) (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
typedef void (APIENTRYP PFNMYGLVALIDATEPROGRAMPROC) (GLuint program);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB1DPROC) (GLuint index, GLdouble x);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB1DVPROC) (GLuint index, const GLdouble *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB1FPROC) (GLuint index, GLfloat x);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB1FVPROC) (GLuint index, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB1SPROC) (GLuint index, GLshort x);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB1SVPROC) (GLuint index, const GLshort *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB2DPROC) (GLuint index, GLdouble x, GLdouble y);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB2DVPROC) (GLuint index, const GLdouble *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB2FPROC) (GLuint index, GLfloat x, GLfloat y);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB2FVPROC) (GLuint index, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB2SPROC) (GLuint index, GLshort x, GLshort y);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB2SVPROC) (GLuint index, const GLshort *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB3DPROC) (GLuint index, GLdouble x, GLdouble y, GLdouble z);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB3DVPROC) (GLuint index, const GLdouble *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB3FPROC) (GLuint index, GLfloat x, GLfloat y, GLfloat z);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB3FVPROC) (GLuint index, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB3SPROC) (GLuint index, GLshort x, GLshort y, GLshort z);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB3SVPROC) (GLuint index, const GLshort *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4NBVPROC) (GLuint index, const GLbyte *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4NIVPROC) (GLuint index, const GLint *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4NSVPROC) (GLuint index, const GLshort *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4NUBPROC) (GLuint index, GLubyte x, GLubyte y, GLubyte z, GLubyte w);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4NUBVPROC) (GLuint index, const GLubyte *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4NUIVPROC) (GLuint index, const GLuint *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4NUSVPROC) (GLuint index, const GLushort *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4BVPROC) (GLuint index, const GLbyte *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4DPROC) (GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4DVPROC) (GLuint index, const GLdouble *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4FPROC) (GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4FVPROC) (GLuint index, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4IVPROC) (GLuint index, const GLint *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4SPROC) (GLuint index, GLshort x, GLshort y, GLshort z, GLshort w);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4SVPROC) (GLuint index, const GLshort *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4UBVPROC) (GLuint index, const GLubyte *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4UIVPROC) (GLuint index, const GLuint *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4USVPROC) (GLuint index, const GLushort *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIBPOINTERPROC) (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid *pointer);
#endif

#ifndef MYGL_ARB_multitexture
#define MYGL_ARB_multitexture 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglActiveTextureARB (GLenum);
GLAPI void APIENTRY myglClientActiveTextureARB (GLenum);
GLAPI void APIENTRY myglMultiTexCoord1dARB (GLenum, GLdouble);
GLAPI void APIENTRY myglMultiTexCoord1dvARB (GLenum, const GLdouble *);
GLAPI void APIENTRY myglMultiTexCoord1fARB (GLenum, GLfloat);
GLAPI void APIENTRY myglMultiTexCoord1fvARB (GLenum, const GLfloat *);
GLAPI void APIENTRY myglMultiTexCoord1iARB (GLenum, GLint);
GLAPI void APIENTRY myglMultiTexCoord1ivARB (GLenum, const GLint *);
GLAPI void APIENTRY myglMultiTexCoord1sARB (GLenum, GLshort);
GLAPI void APIENTRY myglMultiTexCoord1svARB (GLenum, const GLshort *);
GLAPI void APIENTRY myglMultiTexCoord2dARB (GLenum, GLdouble, GLdouble);
GLAPI void APIENTRY myglMultiTexCoord2dvARB (GLenum, const GLdouble *);
GLAPI void APIENTRY myglMultiTexCoord2fARB (GLenum, GLfloat, GLfloat);
GLAPI void APIENTRY myglMultiTexCoord2fvARB (GLenum, const GLfloat *);
GLAPI void APIENTRY myglMultiTexCoord2iARB (GLenum, GLint, GLint);
GLAPI void APIENTRY myglMultiTexCoord2ivARB (GLenum, const GLint *);
GLAPI void APIENTRY myglMultiTexCoord2sARB (GLenum, GLshort, GLshort);
GLAPI void APIENTRY myglMultiTexCoord2svARB (GLenum, const GLshort *);
GLAPI void APIENTRY myglMultiTexCoord3dARB (GLenum, GLdouble, GLdouble, GLdouble);
GLAPI void APIENTRY myglMultiTexCoord3dvARB (GLenum, const GLdouble *);
GLAPI void APIENTRY myglMultiTexCoord3fARB (GLenum, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglMultiTexCoord3fvARB (GLenum, const GLfloat *);
GLAPI void APIENTRY myglMultiTexCoord3iARB (GLenum, GLint, GLint, GLint);
GLAPI void APIENTRY myglMultiTexCoord3ivARB (GLenum, const GLint *);
GLAPI void APIENTRY myglMultiTexCoord3sARB (GLenum, GLshort, GLshort, GLshort);
GLAPI void APIENTRY myglMultiTexCoord3svARB (GLenum, const GLshort *);
GLAPI void APIENTRY myglMultiTexCoord4dARB (GLenum, GLdouble, GLdouble, GLdouble, GLdouble);
GLAPI void APIENTRY myglMultiTexCoord4dvARB (GLenum, const GLdouble *);
GLAPI void APIENTRY myglMultiTexCoord4fARB (GLenum, GLfloat, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglMultiTexCoord4fvARB (GLenum, const GLfloat *);
GLAPI void APIENTRY myglMultiTexCoord4iARB (GLenum, GLint, GLint, GLint, GLint);
GLAPI void APIENTRY myglMultiTexCoord4ivARB (GLenum, const GLint *);
GLAPI void APIENTRY myglMultiTexCoord4sARB (GLenum, GLshort, GLshort, GLshort, GLshort);
GLAPI void APIENTRY myglMultiTexCoord4svARB (GLenum, const GLshort *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLACTIVETEXTUREARBPROC) (GLenum texture);
typedef void (APIENTRYP PFNMYGLCLIENTACTIVETEXTUREARBPROC) (GLenum texture);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD1DARBPROC) (GLenum target, GLdouble s);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD1DVARBPROC) (GLenum target, const GLdouble *v);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD1FARBPROC) (GLenum target, GLfloat s);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD1FVARBPROC) (GLenum target, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD1IARBPROC) (GLenum target, GLint s);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD1IVARBPROC) (GLenum target, const GLint *v);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD1SARBPROC) (GLenum target, GLshort s);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD1SVARBPROC) (GLenum target, const GLshort *v);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD2DARBPROC) (GLenum target, GLdouble s, GLdouble t);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD2DVARBPROC) (GLenum target, const GLdouble *v);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD2FARBPROC) (GLenum target, GLfloat s, GLfloat t);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD2FVARBPROC) (GLenum target, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD2IARBPROC) (GLenum target, GLint s, GLint t);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD2IVARBPROC) (GLenum target, const GLint *v);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD2SARBPROC) (GLenum target, GLshort s, GLshort t);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD2SVARBPROC) (GLenum target, const GLshort *v);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD3DARBPROC) (GLenum target, GLdouble s, GLdouble t, GLdouble r);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD3DVARBPROC) (GLenum target, const GLdouble *v);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD3FARBPROC) (GLenum target, GLfloat s, GLfloat t, GLfloat r);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD3FVARBPROC) (GLenum target, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD3IARBPROC) (GLenum target, GLint s, GLint t, GLint r);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD3IVARBPROC) (GLenum target, const GLint *v);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD3SARBPROC) (GLenum target, GLshort s, GLshort t, GLshort r);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD3SVARBPROC) (GLenum target, const GLshort *v);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD4DARBPROC) (GLenum target, GLdouble s, GLdouble t, GLdouble r, GLdouble q);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD4DVARBPROC) (GLenum target, const GLdouble *v);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD4FARBPROC) (GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD4FVARBPROC) (GLenum target, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD4IARBPROC) (GLenum target, GLint s, GLint t, GLint r, GLint q);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD4IVARBPROC) (GLenum target, const GLint *v);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD4SARBPROC) (GLenum target, GLshort s, GLshort t, GLshort r, GLshort q);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD4SVARBPROC) (GLenum target, const GLshort *v);
#endif

#ifndef MYGL_ARB_transpose_matrix
#define MYGL_ARB_transpose_matrix 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglLoadTransposeMatrixfARB (const GLfloat *);
GLAPI void APIENTRY myglLoadTransposeMatrixdARB (const GLdouble *);
GLAPI void APIENTRY myglMultTransposeMatrixfARB (const GLfloat *);
GLAPI void APIENTRY myglMultTransposeMatrixdARB (const GLdouble *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLLOADTRANSPOSEMATRIXFARBPROC) (const GLfloat *m);
typedef void (APIENTRYP PFNMYGLLOADTRANSPOSEMATRIXDARBPROC) (const GLdouble *m);
typedef void (APIENTRYP PFNMYGLMULTTRANSPOSEMATRIXFARBPROC) (const GLfloat *m);
typedef void (APIENTRYP PFNMYGLMULTTRANSPOSEMATRIXDARBPROC) (const GLdouble *m);
#endif

#ifndef MYGL_ARB_multisample
#define MYGL_ARB_multisample 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglSampleCoverageARB (GLclampf, GLboolean);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLSAMPLECOVERAGEARBPROC) (GLclampf value, GLboolean invert);
#endif

#ifndef MYGL_ARB_texture_env_add
#define MYGL_ARB_texture_env_add 1
#endif

#ifndef MYGL_ARB_texture_cube_map
#define MYGL_ARB_texture_cube_map 1
#endif

#ifndef MYGL_ARB_texture_compression
#define MYGL_ARB_texture_compression 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglCompressedTexImage3DARB (GLenum, GLint, GLenum, GLsizei, GLsizei, GLsizei, GLint, GLsizei, const GLvoid *);
GLAPI void APIENTRY myglCompressedTexImage2DARB (GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const GLvoid *);
GLAPI void APIENTRY myglCompressedTexImage1DARB (GLenum, GLint, GLenum, GLsizei, GLint, GLsizei, const GLvoid *);
GLAPI void APIENTRY myglCompressedTexSubImage3DARB (GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLsizei, const GLvoid *);
GLAPI void APIENTRY myglCompressedTexSubImage2DARB (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const GLvoid *);
GLAPI void APIENTRY myglCompressedTexSubImage1DARB (GLenum, GLint, GLint, GLsizei, GLenum, GLsizei, const GLvoid *);
GLAPI void APIENTRY myglGetCompressedTexImageARB (GLenum, GLint, GLvoid *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLCOMPRESSEDTEXIMAGE3DARBPROC) (GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLsizei imageSize, const GLvoid *data);
typedef void (APIENTRYP PFNMYGLCOMPRESSEDTEXIMAGE2DARBPROC) (GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const GLvoid *data);
typedef void (APIENTRYP PFNMYGLCOMPRESSEDTEXIMAGE1DARBPROC) (GLenum target, GLint level, GLenum internalformat, GLsizei width, GLint border, GLsizei imageSize, const GLvoid *data);
typedef void (APIENTRYP PFNMYGLCOMPRESSEDTEXSUBIMAGE3DARBPROC) (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const GLvoid *data);
typedef void (APIENTRYP PFNMYGLCOMPRESSEDTEXSUBIMAGE2DARBPROC) (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const GLvoid *data);
typedef void (APIENTRYP PFNMYGLCOMPRESSEDTEXSUBIMAGE1DARBPROC) (GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLsizei imageSize, const GLvoid *data);
typedef void (APIENTRYP PFNMYGLGETCOMPRESSEDTEXIMAGEARBPROC) (GLenum target, GLint level, GLvoid *img);
#endif

#ifndef MYGL_ARB_texture_border_clamp
#define MYGL_ARB_texture_border_clamp 1
#endif

#ifndef MYGL_ARB_point_parameters
#define MYGL_ARB_point_parameters 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglPointParameterfARB (GLenum, GLfloat);
GLAPI void APIENTRY myglPointParameterfvARB (GLenum, const GLfloat *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLPOINTPARAMETERFARBPROC) (GLenum pname, GLfloat param);
typedef void (APIENTRYP PFNMYGLPOINTPARAMETERFVARBPROC) (GLenum pname, const GLfloat *params);
#endif

#ifndef MYGL_ARB_vertex_blend
#define MYGL_ARB_vertex_blend 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglWeightbvARB (GLint, const GLbyte *);
GLAPI void APIENTRY myglWeightsvARB (GLint, const GLshort *);
GLAPI void APIENTRY myglWeightivARB (GLint, const GLint *);
GLAPI void APIENTRY myglWeightfvARB (GLint, const GLfloat *);
GLAPI void APIENTRY myglWeightdvARB (GLint, const GLdouble *);
GLAPI void APIENTRY myglWeightubvARB (GLint, const GLubyte *);
GLAPI void APIENTRY myglWeightusvARB (GLint, const GLushort *);
GLAPI void APIENTRY myglWeightuivARB (GLint, const GLuint *);
GLAPI void APIENTRY myglWeightPointerARB (GLint, GLenum, GLsizei, const GLvoid *);
GLAPI void APIENTRY myglVertexBlendARB (GLint);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLWEIGHTBVARBPROC) (GLint size, const GLbyte *weights);
typedef void (APIENTRYP PFNMYGLWEIGHTSVARBPROC) (GLint size, const GLshort *weights);
typedef void (APIENTRYP PFNMYGLWEIGHTIVARBPROC) (GLint size, const GLint *weights);
typedef void (APIENTRYP PFNMYGLWEIGHTFVARBPROC) (GLint size, const GLfloat *weights);
typedef void (APIENTRYP PFNMYGLWEIGHTDVARBPROC) (GLint size, const GLdouble *weights);
typedef void (APIENTRYP PFNMYGLWEIGHTUBVARBPROC) (GLint size, const GLubyte *weights);
typedef void (APIENTRYP PFNMYGLWEIGHTUSVARBPROC) (GLint size, const GLushort *weights);
typedef void (APIENTRYP PFNMYGLWEIGHTUIVARBPROC) (GLint size, const GLuint *weights);
typedef void (APIENTRYP PFNMYGLWEIGHTPOINTERARBPROC) (GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
typedef void (APIENTRYP PFNMYGLVERTEXBLENDARBPROC) (GLint count);
#endif

#ifndef MYGL_ARB_matrix_palette
#define MYGL_ARB_matrix_palette 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglCurrentPaletteMatrixARB (GLint);
GLAPI void APIENTRY myglMatrixIndexubvARB (GLint, const GLubyte *);
GLAPI void APIENTRY myglMatrixIndexusvARB (GLint, const GLushort *);
GLAPI void APIENTRY myglMatrixIndexuivARB (GLint, const GLuint *);
GLAPI void APIENTRY myglMatrixIndexPointerARB (GLint, GLenum, GLsizei, const GLvoid *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLCURRENTPALETTEMATRIXARBPROC) (GLint index);
typedef void (APIENTRYP PFNMYGLMATRIXINDEXUBVARBPROC) (GLint size, const GLubyte *indices);
typedef void (APIENTRYP PFNMYGLMATRIXINDEXUSVARBPROC) (GLint size, const GLushort *indices);
typedef void (APIENTRYP PFNMYGLMATRIXINDEXUIVARBPROC) (GLint size, const GLuint *indices);
typedef void (APIENTRYP PFNMYGLMATRIXINDEXPOINTERARBPROC) (GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
#endif

#ifndef MYGL_ARB_texture_env_combine
#define MYGL_ARB_texture_env_combine 1
#endif

#ifndef MYGL_ARB_texture_env_crossbar
#define MYGL_ARB_texture_env_crossbar 1
#endif

#ifndef MYGL_ARB_texture_env_dot3
#define MYGL_ARB_texture_env_dot3 1
#endif

#ifndef MYGL_ARB_texture_mirrored_repeat
#define MYGL_ARB_texture_mirrored_repeat 1
#endif

#ifndef MYGL_ARB_depth_texture
#define MYGL_ARB_depth_texture 1
#endif

#ifndef MYGL_ARB_shadow
#define MYGL_ARB_shadow 1
#endif

#ifndef MYGL_ARB_shadow_ambient
#define MYGL_ARB_shadow_ambient 1
#endif

#ifndef MYGL_ARB_window_pos
#define MYGL_ARB_window_pos 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglWindowPos2dARB (GLdouble, GLdouble);
GLAPI void APIENTRY myglWindowPos2dvARB (const GLdouble *);
GLAPI void APIENTRY myglWindowPos2fARB (GLfloat, GLfloat);
GLAPI void APIENTRY myglWindowPos2fvARB (const GLfloat *);
GLAPI void APIENTRY myglWindowPos2iARB (GLint, GLint);
GLAPI void APIENTRY myglWindowPos2ivARB (const GLint *);
GLAPI void APIENTRY myglWindowPos2sARB (GLshort, GLshort);
GLAPI void APIENTRY myglWindowPos2svARB (const GLshort *);
GLAPI void APIENTRY myglWindowPos3dARB (GLdouble, GLdouble, GLdouble);
GLAPI void APIENTRY myglWindowPos3dvARB (const GLdouble *);
GLAPI void APIENTRY myglWindowPos3fARB (GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglWindowPos3fvARB (const GLfloat *);
GLAPI void APIENTRY myglWindowPos3iARB (GLint, GLint, GLint);
GLAPI void APIENTRY myglWindowPos3ivARB (const GLint *);
GLAPI void APIENTRY myglWindowPos3sARB (GLshort, GLshort, GLshort);
GLAPI void APIENTRY myglWindowPos3svARB (const GLshort *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLWINDOWPOS2DARBPROC) (GLdouble x, GLdouble y);
typedef void (APIENTRYP PFNMYGLWINDOWPOS2DVARBPROC) (const GLdouble *v);
typedef void (APIENTRYP PFNMYGLWINDOWPOS2FARBPROC) (GLfloat x, GLfloat y);
typedef void (APIENTRYP PFNMYGLWINDOWPOS2FVARBPROC) (const GLfloat *v);
typedef void (APIENTRYP PFNMYGLWINDOWPOS2IARBPROC) (GLint x, GLint y);
typedef void (APIENTRYP PFNMYGLWINDOWPOS2IVARBPROC) (const GLint *v);
typedef void (APIENTRYP PFNMYGLWINDOWPOS2SARBPROC) (GLshort x, GLshort y);
typedef void (APIENTRYP PFNMYGLWINDOWPOS2SVARBPROC) (const GLshort *v);
typedef void (APIENTRYP PFNMYGLWINDOWPOS3DARBPROC) (GLdouble x, GLdouble y, GLdouble z);
typedef void (APIENTRYP PFNMYGLWINDOWPOS3DVARBPROC) (const GLdouble *v);
typedef void (APIENTRYP PFNMYGLWINDOWPOS3FARBPROC) (GLfloat x, GLfloat y, GLfloat z);
typedef void (APIENTRYP PFNMYGLWINDOWPOS3FVARBPROC) (const GLfloat *v);
typedef void (APIENTRYP PFNMYGLWINDOWPOS3IARBPROC) (GLint x, GLint y, GLint z);
typedef void (APIENTRYP PFNMYGLWINDOWPOS3IVARBPROC) (const GLint *v);
typedef void (APIENTRYP PFNMYGLWINDOWPOS3SARBPROC) (GLshort x, GLshort y, GLshort z);
typedef void (APIENTRYP PFNMYGLWINDOWPOS3SVARBPROC) (const GLshort *v);
#endif

#ifndef MYGL_ARB_vertex_program
#define MYGL_ARB_vertex_program 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglVertexAttrib1dARB (GLuint, GLdouble);
GLAPI void APIENTRY myglVertexAttrib1dvARB (GLuint, const GLdouble *);
GLAPI void APIENTRY myglVertexAttrib1fARB (GLuint, GLfloat);
GLAPI void APIENTRY myglVertexAttrib1fvARB (GLuint, const GLfloat *);
GLAPI void APIENTRY myglVertexAttrib1sARB (GLuint, GLshort);
GLAPI void APIENTRY myglVertexAttrib1svARB (GLuint, const GLshort *);
GLAPI void APIENTRY myglVertexAttrib2dARB (GLuint, GLdouble, GLdouble);
GLAPI void APIENTRY myglVertexAttrib2dvARB (GLuint, const GLdouble *);
GLAPI void APIENTRY myglVertexAttrib2fARB (GLuint, GLfloat, GLfloat);
GLAPI void APIENTRY myglVertexAttrib2fvARB (GLuint, const GLfloat *);
GLAPI void APIENTRY myglVertexAttrib2sARB (GLuint, GLshort, GLshort);
GLAPI void APIENTRY myglVertexAttrib2svARB (GLuint, const GLshort *);
GLAPI void APIENTRY myglVertexAttrib3dARB (GLuint, GLdouble, GLdouble, GLdouble);
GLAPI void APIENTRY myglVertexAttrib3dvARB (GLuint, const GLdouble *);
GLAPI void APIENTRY myglVertexAttrib3fARB (GLuint, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglVertexAttrib3fvARB (GLuint, const GLfloat *);
GLAPI void APIENTRY myglVertexAttrib3sARB (GLuint, GLshort, GLshort, GLshort);
GLAPI void APIENTRY myglVertexAttrib3svARB (GLuint, const GLshort *);
GLAPI void APIENTRY myglVertexAttrib4NbvARB (GLuint, const GLbyte *);
GLAPI void APIENTRY myglVertexAttrib4NivARB (GLuint, const GLint *);
GLAPI void APIENTRY myglVertexAttrib4NsvARB (GLuint, const GLshort *);
GLAPI void APIENTRY myglVertexAttrib4NubARB (GLuint, GLubyte, GLubyte, GLubyte, GLubyte);
GLAPI void APIENTRY myglVertexAttrib4NubvARB (GLuint, const GLubyte *);
GLAPI void APIENTRY myglVertexAttrib4NuivARB (GLuint, const GLuint *);
GLAPI void APIENTRY myglVertexAttrib4NusvARB (GLuint, const GLushort *);
GLAPI void APIENTRY myglVertexAttrib4bvARB (GLuint, const GLbyte *);
GLAPI void APIENTRY myglVertexAttrib4dARB (GLuint, GLdouble, GLdouble, GLdouble, GLdouble);
GLAPI void APIENTRY myglVertexAttrib4dvARB (GLuint, const GLdouble *);
GLAPI void APIENTRY myglVertexAttrib4fARB (GLuint, GLfloat, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglVertexAttrib4fvARB (GLuint, const GLfloat *);
GLAPI void APIENTRY myglVertexAttrib4ivARB (GLuint, const GLint *);
GLAPI void APIENTRY myglVertexAttrib4sARB (GLuint, GLshort, GLshort, GLshort, GLshort);
GLAPI void APIENTRY myglVertexAttrib4svARB (GLuint, const GLshort *);
GLAPI void APIENTRY myglVertexAttrib4ubvARB (GLuint, const GLubyte *);
GLAPI void APIENTRY myglVertexAttrib4uivARB (GLuint, const GLuint *);
GLAPI void APIENTRY myglVertexAttrib4usvARB (GLuint, const GLushort *);
GLAPI void APIENTRY myglVertexAttribPointerARB (GLuint, GLint, GLenum, GLboolean, GLsizei, const GLvoid *);
GLAPI void APIENTRY myglEnableVertexAttribArrayARB (GLuint);
GLAPI void APIENTRY myglDisableVertexAttribArrayARB (GLuint);
GLAPI void APIENTRY myglProgramStringARB (GLenum, GLenum, GLsizei, const GLvoid *);
GLAPI void APIENTRY myglBindProgramARB (GLenum, GLuint);
GLAPI void APIENTRY myglDeleteProgramsARB (GLsizei, const GLuint *);
GLAPI void APIENTRY myglGenProgramsARB (GLsizei, GLuint *);
GLAPI void APIENTRY myglProgramEnvParameter4dARB (GLenum, GLuint, GLdouble, GLdouble, GLdouble, GLdouble);
GLAPI void APIENTRY myglProgramEnvParameter4dvARB (GLenum, GLuint, const GLdouble *);
GLAPI void APIENTRY myglProgramEnvParameter4fARB (GLenum, GLuint, GLfloat, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglProgramEnvParameter4fvARB (GLenum, GLuint, const GLfloat *);
GLAPI void APIENTRY myglProgramLocalParameter4dARB (GLenum, GLuint, GLdouble, GLdouble, GLdouble, GLdouble);
GLAPI void APIENTRY myglProgramLocalParameter4dvARB (GLenum, GLuint, const GLdouble *);
GLAPI void APIENTRY myglProgramLocalParameter4fARB (GLenum, GLuint, GLfloat, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglProgramLocalParameter4fvARB (GLenum, GLuint, const GLfloat *);
GLAPI void APIENTRY myglGetProgramEnvParameterdvARB (GLenum, GLuint, GLdouble *);
GLAPI void APIENTRY myglGetProgramEnvParameterfvARB (GLenum, GLuint, GLfloat *);
GLAPI void APIENTRY myglGetProgramLocalParameterdvARB (GLenum, GLuint, GLdouble *);
GLAPI void APIENTRY myglGetProgramLocalParameterfvARB (GLenum, GLuint, GLfloat *);
GLAPI void APIENTRY myglGetProgramivARB (GLenum, GLenum, GLint *);
GLAPI void APIENTRY myglGetProgramStringARB (GLenum, GLenum, GLvoid *);
GLAPI void APIENTRY myglGetVertexAttribdvARB (GLuint, GLenum, GLdouble *);
GLAPI void APIENTRY myglGetVertexAttribfvARB (GLuint, GLenum, GLfloat *);
GLAPI void APIENTRY myglGetVertexAttribivARB (GLuint, GLenum, GLint *);
GLAPI void APIENTRY myglGetVertexAttribPointervARB (GLuint, GLenum, GLvoid* *);
GLAPI GLboolean APIENTRY myglIsProgramARB (GLuint);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB1DARBPROC) (GLuint index, GLdouble x);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB1DVARBPROC) (GLuint index, const GLdouble *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB1FARBPROC) (GLuint index, GLfloat x);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB1FVARBPROC) (GLuint index, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB1SARBPROC) (GLuint index, GLshort x);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB1SVARBPROC) (GLuint index, const GLshort *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB2DARBPROC) (GLuint index, GLdouble x, GLdouble y);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB2DVARBPROC) (GLuint index, const GLdouble *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB2FARBPROC) (GLuint index, GLfloat x, GLfloat y);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB2FVARBPROC) (GLuint index, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB2SARBPROC) (GLuint index, GLshort x, GLshort y);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB2SVARBPROC) (GLuint index, const GLshort *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB3DARBPROC) (GLuint index, GLdouble x, GLdouble y, GLdouble z);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB3DVARBPROC) (GLuint index, const GLdouble *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB3FARBPROC) (GLuint index, GLfloat x, GLfloat y, GLfloat z);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB3FVARBPROC) (GLuint index, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB3SARBPROC) (GLuint index, GLshort x, GLshort y, GLshort z);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB3SVARBPROC) (GLuint index, const GLshort *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4NBVARBPROC) (GLuint index, const GLbyte *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4NIVARBPROC) (GLuint index, const GLint *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4NSVARBPROC) (GLuint index, const GLshort *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4NUBARBPROC) (GLuint index, GLubyte x, GLubyte y, GLubyte z, GLubyte w);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4NUBVARBPROC) (GLuint index, const GLubyte *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4NUIVARBPROC) (GLuint index, const GLuint *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4NUSVARBPROC) (GLuint index, const GLushort *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4BVARBPROC) (GLuint index, const GLbyte *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4DARBPROC) (GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4DVARBPROC) (GLuint index, const GLdouble *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4FARBPROC) (GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4FVARBPROC) (GLuint index, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4IVARBPROC) (GLuint index, const GLint *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4SARBPROC) (GLuint index, GLshort x, GLshort y, GLshort z, GLshort w);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4SVARBPROC) (GLuint index, const GLshort *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4UBVARBPROC) (GLuint index, const GLubyte *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4UIVARBPROC) (GLuint index, const GLuint *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4USVARBPROC) (GLuint index, const GLushort *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIBPOINTERARBPROC) (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid *pointer);
typedef void (APIENTRYP PFNMYGLENABLEVERTEXATTRIBARRAYARBPROC) (GLuint index);
typedef void (APIENTRYP PFNMYGLDISABLEVERTEXATTRIBARRAYARBPROC) (GLuint index);
typedef void (APIENTRYP PFNMYGLPROGRAMSTRINGARBPROC) (GLenum target, GLenum format, GLsizei len, const GLvoid *string);
typedef void (APIENTRYP PFNMYGLBINDPROGRAMARBPROC) (GLenum target, GLuint program);
typedef void (APIENTRYP PFNMYGLDELETEPROGRAMSARBPROC) (GLsizei n, const GLuint *programs);
typedef void (APIENTRYP PFNMYGLGENPROGRAMSARBPROC) (GLsizei n, GLuint *programs);
typedef void (APIENTRYP PFNMYGLPROGRAMENVPARAMETER4DARBPROC) (GLenum target, GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w);
typedef void (APIENTRYP PFNMYGLPROGRAMENVPARAMETER4DVARBPROC) (GLenum target, GLuint index, const GLdouble *params);
typedef void (APIENTRYP PFNMYGLPROGRAMENVPARAMETER4FARBPROC) (GLenum target, GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
typedef void (APIENTRYP PFNMYGLPROGRAMENVPARAMETER4FVARBPROC) (GLenum target, GLuint index, const GLfloat *params);
typedef void (APIENTRYP PFNMYGLPROGRAMLOCALPARAMETER4DARBPROC) (GLenum target, GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w);
typedef void (APIENTRYP PFNMYGLPROGRAMLOCALPARAMETER4DVARBPROC) (GLenum target, GLuint index, const GLdouble *params);
typedef void (APIENTRYP PFNMYGLPROGRAMLOCALPARAMETER4FARBPROC) (GLenum target, GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
typedef void (APIENTRYP PFNMYGLPROGRAMLOCALPARAMETER4FVARBPROC) (GLenum target, GLuint index, const GLfloat *params);
typedef void (APIENTRYP PFNMYGLGETPROGRAMENVPARAMETERDVARBPROC) (GLenum target, GLuint index, GLdouble *params);
typedef void (APIENTRYP PFNMYGLGETPROGRAMENVPARAMETERFVARBPROC) (GLenum target, GLuint index, GLfloat *params);
typedef void (APIENTRYP PFNMYGLGETPROGRAMLOCALPARAMETERDVARBPROC) (GLenum target, GLuint index, GLdouble *params);
typedef void (APIENTRYP PFNMYGLGETPROGRAMLOCALPARAMETERFVARBPROC) (GLenum target, GLuint index, GLfloat *params);
typedef void (APIENTRYP PFNMYGLGETPROGRAMIVARBPROC) (GLenum target, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLGETPROGRAMSTRINGARBPROC) (GLenum target, GLenum pname, GLvoid *string);
typedef void (APIENTRYP PFNMYGLGETVERTEXATTRIBDVARBPROC) (GLuint index, GLenum pname, GLdouble *params);
typedef void (APIENTRYP PFNMYGLGETVERTEXATTRIBFVARBPROC) (GLuint index, GLenum pname, GLfloat *params);
typedef void (APIENTRYP PFNMYGLGETVERTEXATTRIBIVARBPROC) (GLuint index, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLGETVERTEXATTRIBPOINTERVARBPROC) (GLuint index, GLenum pname, GLvoid* *pointer);
typedef GLboolean (APIENTRYP PFNMYGLISPROGRAMARBPROC) (GLuint program);
#endif

#ifndef MYGL_ARB_fragment_program
#define MYGL_ARB_fragment_program 1
/* All ARB_fragment_program entry points are shared with ARB_vertex_program. */
#endif

#ifndef MYGL_ARB_vertex_buffer_object
#define MYGL_ARB_vertex_buffer_object 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglBindBufferARB (GLenum, GLuint);
GLAPI void APIENTRY myglDeleteBuffersARB (GLsizei, const GLuint *);
GLAPI void APIENTRY myglGenBuffersARB (GLsizei, GLuint *);
GLAPI GLboolean APIENTRY myglIsBufferARB (GLuint);
GLAPI void APIENTRY myglBufferDataARB (GLenum, GLsizeiptrARB, const GLvoid *, GLenum);
GLAPI void APIENTRY myglBufferSubDataARB (GLenum, GLintptrARB, GLsizeiptrARB, const GLvoid *);
GLAPI void APIENTRY myglGetBufferSubDataARB (GLenum, GLintptrARB, GLsizeiptrARB, GLvoid *);
GLAPI GLvoid* APIENTRY myglMapBufferARB (GLenum, GLenum);
GLAPI GLboolean APIENTRY myglUnmapBufferARB (GLenum);
GLAPI void APIENTRY myglGetBufferParameterivARB (GLenum, GLenum, GLint *);
GLAPI void APIENTRY myglGetBufferPointervARB (GLenum, GLenum, GLvoid* *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLBINDBUFFERARBPROC) (GLenum target, GLuint buffer);
typedef void (APIENTRYP PFNMYGLDELETEBUFFERSARBPROC) (GLsizei n, const GLuint *buffers);
typedef void (APIENTRYP PFNMYGLGENBUFFERSARBPROC) (GLsizei n, GLuint *buffers);
typedef GLboolean (APIENTRYP PFNMYGLISBUFFERARBPROC) (GLuint buffer);
typedef void (APIENTRYP PFNMYGLBUFFERDATAARBPROC) (GLenum target, GLsizeiptrARB size, const GLvoid *data, GLenum usage);
typedef void (APIENTRYP PFNMYGLBUFFERSUBDATAARBPROC) (GLenum target, GLintptrARB offset, GLsizeiptrARB size, const GLvoid *data);
typedef void (APIENTRYP PFNMYGLGETBUFFERSUBDATAARBPROC) (GLenum target, GLintptrARB offset, GLsizeiptrARB size, GLvoid *data);
typedef GLvoid* (APIENTRYP PFNMYGLMAPBUFFERARBPROC) (GLenum target, GLenum access);
typedef GLboolean (APIENTRYP PFNMYGLUNMAPBUFFERARBPROC) (GLenum target);
typedef void (APIENTRYP PFNMYGLGETBUFFERPARAMETERIVARBPROC) (GLenum target, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLGETBUFFERPOINTERVARBPROC) (GLenum target, GLenum pname, GLvoid* *params);
#endif

#ifndef MYGL_ARB_occlusion_query
#define MYGL_ARB_occlusion_query 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglGenQueriesARB (GLsizei, GLuint *);
GLAPI void APIENTRY myglDeleteQueriesARB (GLsizei, const GLuint *);
GLAPI GLboolean APIENTRY myglIsQueryARB (GLuint);
GLAPI void APIENTRY myglBeginQueryARB (GLenum, GLuint);
GLAPI void APIENTRY myglEndQueryARB (GLenum);
GLAPI void APIENTRY myglGetQueryivARB (GLenum, GLenum, GLint *);
GLAPI void APIENTRY myglGetQueryObjectivARB (GLuint, GLenum, GLint *);
GLAPI void APIENTRY myglGetQueryObjectuivARB (GLuint, GLenum, GLuint *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLGENQUERIESARBPROC) (GLsizei n, GLuint *ids);
typedef void (APIENTRYP PFNMYGLDELETEQUERIESARBPROC) (GLsizei n, const GLuint *ids);
typedef GLboolean (APIENTRYP PFNMYGLISQUERYARBPROC) (GLuint id);
typedef void (APIENTRYP PFNMYGLBEGINQUERYARBPROC) (GLenum target, GLuint id);
typedef void (APIENTRYP PFNMYGLENDQUERYARBPROC) (GLenum target);
typedef void (APIENTRYP PFNMYGLGETQUERYIVARBPROC) (GLenum target, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLGETQUERYOBJECTIVARBPROC) (GLuint id, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLGETQUERYOBJECTUIVARBPROC) (GLuint id, GLenum pname, GLuint *params);
#endif

#ifndef MYGL_ARB_shader_objects
#define MYGL_ARB_shader_objects 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglDeleteObjectARB (GLhandleARB);
GLAPI GLhandleARB APIENTRY myglGetHandleARB (GLenum);
GLAPI void APIENTRY myglDetachObjectARB (GLhandleARB, GLhandleARB);
GLAPI GLhandleARB APIENTRY myglCreateShaderObjectARB (GLenum);
GLAPI void APIENTRY myglShaderSourceARB (GLhandleARB, GLsizei, const GLcharARB* *, const GLint *);
GLAPI void APIENTRY myglCompileShaderARB (GLhandleARB);
GLAPI GLhandleARB APIENTRY myglCreateProgramObjectARB (void);
GLAPI void APIENTRY myglAttachObjectARB (GLhandleARB, GLhandleARB);
GLAPI void APIENTRY myglLinkProgramARB (GLhandleARB);
GLAPI void APIENTRY myglUseProgramObjectARB (GLhandleARB);
GLAPI void APIENTRY myglValidateProgramARB (GLhandleARB);
GLAPI void APIENTRY myglUniform1fARB (GLint, GLfloat);
GLAPI void APIENTRY myglUniform2fARB (GLint, GLfloat, GLfloat);
GLAPI void APIENTRY myglUniform3fARB (GLint, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglUniform4fARB (GLint, GLfloat, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglUniform1iARB (GLint, GLint);
GLAPI void APIENTRY myglUniform2iARB (GLint, GLint, GLint);
GLAPI void APIENTRY myglUniform3iARB (GLint, GLint, GLint, GLint);
GLAPI void APIENTRY myglUniform4iARB (GLint, GLint, GLint, GLint, GLint);
GLAPI void APIENTRY myglUniform1fvARB (GLint, GLsizei, const GLfloat *);
GLAPI void APIENTRY myglUniform2fvARB (GLint, GLsizei, const GLfloat *);
GLAPI void APIENTRY myglUniform3fvARB (GLint, GLsizei, const GLfloat *);
GLAPI void APIENTRY myglUniform4fvARB (GLint, GLsizei, const GLfloat *);
GLAPI void APIENTRY myglUniform1ivARB (GLint, GLsizei, const GLint *);
GLAPI void APIENTRY myglUniform2ivARB (GLint, GLsizei, const GLint *);
GLAPI void APIENTRY myglUniform3ivARB (GLint, GLsizei, const GLint *);
GLAPI void APIENTRY myglUniform4ivARB (GLint, GLsizei, const GLint *);
GLAPI void APIENTRY myglUniformMatrix2fvARB (GLint, GLsizei, GLboolean, const GLfloat *);
GLAPI void APIENTRY myglUniformMatrix3fvARB (GLint, GLsizei, GLboolean, const GLfloat *);
GLAPI void APIENTRY myglUniformMatrix4fvARB (GLint, GLsizei, GLboolean, const GLfloat *);
GLAPI void APIENTRY myglGetObjectParameterfvARB (GLhandleARB, GLenum, GLfloat *);
GLAPI void APIENTRY myglGetObjectParameterivARB (GLhandleARB, GLenum, GLint *);
GLAPI void APIENTRY myglGetInfoLogARB (GLhandleARB, GLsizei, GLsizei *, GLcharARB *);
GLAPI void APIENTRY myglGetAttachedObjectsARB (GLhandleARB, GLsizei, GLsizei *, GLhandleARB *);
GLAPI GLint APIENTRY myglGetUniformLocationARB (GLhandleARB, const GLcharARB *);
GLAPI void APIENTRY myglGetActiveUniformARB (GLhandleARB, GLuint, GLsizei, GLsizei *, GLint *, GLenum *, GLcharARB *);
GLAPI void APIENTRY myglGetUniformfvARB (GLhandleARB, GLint, GLfloat *);
GLAPI void APIENTRY myglGetUniformivARB (GLhandleARB, GLint, GLint *);
GLAPI void APIENTRY myglGetShaderSourceARB (GLhandleARB, GLsizei, GLsizei *, GLcharARB *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLDELETEOBJECTARBPROC) (GLhandleARB obj);
typedef GLhandleARB (APIENTRYP PFNMYGLGETHANDLEARBPROC) (GLenum pname);
typedef void (APIENTRYP PFNMYGLDETACHOBJECTARBPROC) (GLhandleARB containerObj, GLhandleARB attachedObj);
typedef GLhandleARB (APIENTRYP PFNMYGLCREATESHADEROBJECTARBPROC) (GLenum shaderType);
typedef void (APIENTRYP PFNMYGLSHADERSOURCEARBPROC) (GLhandleARB shaderObj, GLsizei count, const GLcharARB* *string, const GLint *length);
typedef void (APIENTRYP PFNMYGLCOMPILESHADERARBPROC) (GLhandleARB shaderObj);
typedef GLhandleARB (APIENTRYP PFNMYGLCREATEPROGRAMOBJECTARBPROC) (void);
typedef void (APIENTRYP PFNMYGLATTACHOBJECTARBPROC) (GLhandleARB containerObj, GLhandleARB obj);
typedef void (APIENTRYP PFNMYGLLINKPROGRAMARBPROC) (GLhandleARB programObj);
typedef void (APIENTRYP PFNMYGLUSEPROGRAMOBJECTARBPROC) (GLhandleARB programObj);
typedef void (APIENTRYP PFNMYGLVALIDATEPROGRAMARBPROC) (GLhandleARB programObj);
typedef void (APIENTRYP PFNMYGLUNIFORM1FARBPROC) (GLint location, GLfloat v0);
typedef void (APIENTRYP PFNMYGLUNIFORM2FARBPROC) (GLint location, GLfloat v0, GLfloat v1);
typedef void (APIENTRYP PFNMYGLUNIFORM3FARBPROC) (GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
typedef void (APIENTRYP PFNMYGLUNIFORM4FARBPROC) (GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
typedef void (APIENTRYP PFNMYGLUNIFORM1IARBPROC) (GLint location, GLint v0);
typedef void (APIENTRYP PFNMYGLUNIFORM2IARBPROC) (GLint location, GLint v0, GLint v1);
typedef void (APIENTRYP PFNMYGLUNIFORM3IARBPROC) (GLint location, GLint v0, GLint v1, GLint v2);
typedef void (APIENTRYP PFNMYGLUNIFORM4IARBPROC) (GLint location, GLint v0, GLint v1, GLint v2, GLint v3);
typedef void (APIENTRYP PFNMYGLUNIFORM1FVARBPROC) (GLint location, GLsizei count, const GLfloat *value);
typedef void (APIENTRYP PFNMYGLUNIFORM2FVARBPROC) (GLint location, GLsizei count, const GLfloat *value);
typedef void (APIENTRYP PFNMYGLUNIFORM3FVARBPROC) (GLint location, GLsizei count, const GLfloat *value);
typedef void (APIENTRYP PFNMYGLUNIFORM4FVARBPROC) (GLint location, GLsizei count, const GLfloat *value);
typedef void (APIENTRYP PFNMYGLUNIFORM1IVARBPROC) (GLint location, GLsizei count, const GLint *value);
typedef void (APIENTRYP PFNMYGLUNIFORM2IVARBPROC) (GLint location, GLsizei count, const GLint *value);
typedef void (APIENTRYP PFNMYGLUNIFORM3IVARBPROC) (GLint location, GLsizei count, const GLint *value);
typedef void (APIENTRYP PFNMYGLUNIFORM4IVARBPROC) (GLint location, GLsizei count, const GLint *value);
typedef void (APIENTRYP PFNMYGLUNIFORMMATRIX2FVARBPROC) (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
typedef void (APIENTRYP PFNMYGLUNIFORMMATRIX3FVARBPROC) (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
typedef void (APIENTRYP PFNMYGLUNIFORMMATRIX4FVARBPROC) (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
typedef void (APIENTRYP PFNMYGLGETOBJECTPARAMETERFVARBPROC) (GLhandleARB obj, GLenum pname, GLfloat *params);
typedef void (APIENTRYP PFNMYGLGETOBJECTPARAMETERIVARBPROC) (GLhandleARB obj, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLGETINFOLOGARBPROC) (GLhandleARB obj, GLsizei maxLength, GLsizei *length, GLcharARB *infoLog);
typedef void (APIENTRYP PFNMYGLGETATTACHEDOBJECTSARBPROC) (GLhandleARB containerObj, GLsizei maxCount, GLsizei *count, GLhandleARB *obj);
typedef GLint (APIENTRYP PFNMYGLGETUNIFORMLOCATIONARBPROC) (GLhandleARB programObj, const GLcharARB *name);
typedef void (APIENTRYP PFNMYGLGETACTIVEUNIFORMARBPROC) (GLhandleARB programObj, GLuint index, GLsizei maxLength, GLsizei *length, GLint *size, GLenum *type, GLcharARB *name);
typedef void (APIENTRYP PFNMYGLGETUNIFORMFVARBPROC) (GLhandleARB programObj, GLint location, GLfloat *params);
typedef void (APIENTRYP PFNMYGLGETUNIFORMIVARBPROC) (GLhandleARB programObj, GLint location, GLint *params);
typedef void (APIENTRYP PFNMYGLGETSHADERSOURCEARBPROC) (GLhandleARB obj, GLsizei maxLength, GLsizei *length, GLcharARB *source);
#endif

#ifndef MYGL_ARB_vertex_shader
#define MYGL_ARB_vertex_shader 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglBindAttribLocationARB (GLhandleARB, GLuint, const GLcharARB *);
GLAPI void APIENTRY myglGetActiveAttribARB (GLhandleARB, GLuint, GLsizei, GLsizei *, GLint *, GLenum *, GLcharARB *);
GLAPI GLint APIENTRY myglGetAttribLocationARB (GLhandleARB, const GLcharARB *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLBINDATTRIBLOCATIONARBPROC) (GLhandleARB programObj, GLuint index, const GLcharARB *name);
typedef void (APIENTRYP PFNMYGLGETACTIVEATTRIBARBPROC) (GLhandleARB programObj, GLuint index, GLsizei maxLength, GLsizei *length, GLint *size, GLenum *type, GLcharARB *name);
typedef GLint (APIENTRYP PFNMYGLGETATTRIBLOCATIONARBPROC) (GLhandleARB programObj, const GLcharARB *name);
#endif

#ifndef MYGL_ARB_fragment_shader
#define MYGL_ARB_fragment_shader 1
#endif

#ifndef MYGL_ARB_shading_language_100
#define MYGL_ARB_shading_language_100 1
#endif

#ifndef MYGL_ARB_texture_non_power_of_two
#define MYGL_ARB_texture_non_power_of_two 1
#endif

#ifndef MYGL_ARB_point_sprite
#define MYGL_ARB_point_sprite 1
#endif

#ifndef MYGL_ARB_fragment_program_shadow
#define MYGL_ARB_fragment_program_shadow 1
#endif

#ifndef MYGL_ARB_draw_buffers
#define MYGL_ARB_draw_buffers 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglDrawBuffersARB (GLsizei, const GLenum *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLDRAWBUFFERSARBPROC) (GLsizei n, const GLenum *bufs);
#endif

#ifndef MYGL_ARB_texture_rectangle
#define MYGL_ARB_texture_rectangle 1
#endif

#ifndef MYGL_ARB_color_buffer_float
#define MYGL_ARB_color_buffer_float 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglClampColorARB (GLenum, GLenum);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLCLAMPCOLORARBPROC) (GLenum target, GLenum clamp);
#endif

#ifndef MYGL_ARB_half_float_pixel
#define MYGL_ARB_half_float_pixel 1
#endif

#ifndef MYGL_ARB_texture_float
#define MYGL_ARB_texture_float 1
#endif

#ifndef MYGL_ARB_pixel_buffer_object
#define MYGL_ARB_pixel_buffer_object 1
#endif

#ifndef MYGL_EXT_abgr
#define MYGL_EXT_abgr 1
#endif

#ifndef MYGL_EXT_blend_color
#define MYGL_EXT_blend_color 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglBlendColorEXT (GLclampf, GLclampf, GLclampf, GLclampf);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLBLENDCOLOREXTPROC) (GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
#endif

#ifndef MYGL_EXT_polygon_offset
#define MYGL_EXT_polygon_offset 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglPolygonOffsetEXT (GLfloat, GLfloat);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLPOLYGONOFFSETEXTPROC) (GLfloat factor, GLfloat bias);
#endif

#ifndef MYGL_EXT_texture
#define MYGL_EXT_texture 1
#endif

#ifndef MYGL_EXT_texture3D
#define MYGL_EXT_texture3D 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglTexImage3DEXT (GLenum, GLint, GLenum, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid *);
GLAPI void APIENTRY myglTexSubImage3DEXT (GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLTEXIMAGE3DEXTPROC) (GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
typedef void (APIENTRYP PFNMYGLTEXSUBIMAGE3DEXTPROC) (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const GLvoid *pixels);
#endif

#ifndef MYGL_SGIS_texture_filter4
#define MYGL_SGIS_texture_filter4 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglGetTexFilterFuncSGIS (GLenum, GLenum, GLfloat *);
GLAPI void APIENTRY myglTexFilterFuncSGIS (GLenum, GLenum, GLsizei, const GLfloat *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLGETTEXFILTERFUNCSGISPROC) (GLenum target, GLenum filter, GLfloat *weights);
typedef void (APIENTRYP PFNMYGLTEXFILTERFUNCSGISPROC) (GLenum target, GLenum filter, GLsizei n, const GLfloat *weights);
#endif

#ifndef MYGL_EXT_subtexture
#define MYGL_EXT_subtexture 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglTexSubImage1DEXT (GLenum, GLint, GLint, GLsizei, GLenum, GLenum, const GLvoid *);
GLAPI void APIENTRY myglTexSubImage2DEXT (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLTEXSUBIMAGE1DEXTPROC) (GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const GLvoid *pixels);
typedef void (APIENTRYP PFNMYGLTEXSUBIMAGE2DEXTPROC) (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
#endif

#ifndef MYGL_EXT_copy_texture
#define MYGL_EXT_copy_texture 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglCopyTexImage1DEXT (GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLint);
GLAPI void APIENTRY myglCopyTexImage2DEXT (GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLsizei, GLint);
GLAPI void APIENTRY myglCopyTexSubImage1DEXT (GLenum, GLint, GLint, GLint, GLint, GLsizei);
GLAPI void APIENTRY myglCopyTexSubImage2DEXT (GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei);
GLAPI void APIENTRY myglCopyTexSubImage3DEXT (GLenum, GLint, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLCOPYTEXIMAGE1DEXTPROC) (GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLint border);
typedef void (APIENTRYP PFNMYGLCOPYTEXIMAGE2DEXTPROC) (GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border);
typedef void (APIENTRYP PFNMYGLCOPYTEXSUBIMAGE1DEXTPROC) (GLenum target, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width);
typedef void (APIENTRYP PFNMYGLCOPYTEXSUBIMAGE2DEXTPROC) (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height);
typedef void (APIENTRYP PFNMYGLCOPYTEXSUBIMAGE3DEXTPROC) (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLint x, GLint y, GLsizei width, GLsizei height);
#endif

#ifndef MYGL_EXT_histogram
#define MYGL_EXT_histogram 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglGetHistogramEXT (GLenum, GLboolean, GLenum, GLenum, GLvoid *);
GLAPI void APIENTRY myglGetHistogramParameterfvEXT (GLenum, GLenum, GLfloat *);
GLAPI void APIENTRY myglGetHistogramParameterivEXT (GLenum, GLenum, GLint *);
GLAPI void APIENTRY myglGetMinmaxEXT (GLenum, GLboolean, GLenum, GLenum, GLvoid *);
GLAPI void APIENTRY myglGetMinmaxParameterfvEXT (GLenum, GLenum, GLfloat *);
GLAPI void APIENTRY myglGetMinmaxParameterivEXT (GLenum, GLenum, GLint *);
GLAPI void APIENTRY myglHistogramEXT (GLenum, GLsizei, GLenum, GLboolean);
GLAPI void APIENTRY myglMinmaxEXT (GLenum, GLenum, GLboolean);
GLAPI void APIENTRY myglResetHistogramEXT (GLenum);
GLAPI void APIENTRY myglResetMinmaxEXT (GLenum);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLGETHISTOGRAMEXTPROC) (GLenum target, GLboolean reset, GLenum format, GLenum type, GLvoid *values);
typedef void (APIENTRYP PFNMYGLGETHISTOGRAMPARAMETERFVEXTPROC) (GLenum target, GLenum pname, GLfloat *params);
typedef void (APIENTRYP PFNMYGLGETHISTOGRAMPARAMETERIVEXTPROC) (GLenum target, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLGETMINMAXEXTPROC) (GLenum target, GLboolean reset, GLenum format, GLenum type, GLvoid *values);
typedef void (APIENTRYP PFNMYGLGETMINMAXPARAMETERFVEXTPROC) (GLenum target, GLenum pname, GLfloat *params);
typedef void (APIENTRYP PFNMYGLGETMINMAXPARAMETERIVEXTPROC) (GLenum target, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLHISTOGRAMEXTPROC) (GLenum target, GLsizei width, GLenum internalformat, GLboolean sink);
typedef void (APIENTRYP PFNMYGLMINMAXEXTPROC) (GLenum target, GLenum internalformat, GLboolean sink);
typedef void (APIENTRYP PFNMYGLRESETHISTOGRAMEXTPROC) (GLenum target);
typedef void (APIENTRYP PFNMYGLRESETMINMAXEXTPROC) (GLenum target);
#endif

#ifndef MYGL_EXT_convolution
#define MYGL_EXT_convolution 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglConvolutionFilter1DEXT (GLenum, GLenum, GLsizei, GLenum, GLenum, const GLvoid *);
GLAPI void APIENTRY myglConvolutionFilter2DEXT (GLenum, GLenum, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *);
GLAPI void APIENTRY myglConvolutionParameterfEXT (GLenum, GLenum, GLfloat);
GLAPI void APIENTRY myglConvolutionParameterfvEXT (GLenum, GLenum, const GLfloat *);
GLAPI void APIENTRY myglConvolutionParameteriEXT (GLenum, GLenum, GLint);
GLAPI void APIENTRY myglConvolutionParameterivEXT (GLenum, GLenum, const GLint *);
GLAPI void APIENTRY myglCopyConvolutionFilter1DEXT (GLenum, GLenum, GLint, GLint, GLsizei);
GLAPI void APIENTRY myglCopyConvolutionFilter2DEXT (GLenum, GLenum, GLint, GLint, GLsizei, GLsizei);
GLAPI void APIENTRY myglGetConvolutionFilterEXT (GLenum, GLenum, GLenum, GLvoid *);
GLAPI void APIENTRY myglGetConvolutionParameterfvEXT (GLenum, GLenum, GLfloat *);
GLAPI void APIENTRY myglGetConvolutionParameterivEXT (GLenum, GLenum, GLint *);
GLAPI void APIENTRY myglGetSeparableFilterEXT (GLenum, GLenum, GLenum, GLvoid *, GLvoid *, GLvoid *);
GLAPI void APIENTRY myglSeparableFilter2DEXT (GLenum, GLenum, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *, const GLvoid *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLCONVOLUTIONFILTER1DEXTPROC) (GLenum target, GLenum internalformat, GLsizei width, GLenum format, GLenum type, const GLvoid *image);
typedef void (APIENTRYP PFNMYGLCONVOLUTIONFILTER2DEXTPROC) (GLenum target, GLenum internalformat, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *image);
typedef void (APIENTRYP PFNMYGLCONVOLUTIONPARAMETERFEXTPROC) (GLenum target, GLenum pname, GLfloat params);
typedef void (APIENTRYP PFNMYGLCONVOLUTIONPARAMETERFVEXTPROC) (GLenum target, GLenum pname, const GLfloat *params);
typedef void (APIENTRYP PFNMYGLCONVOLUTIONPARAMETERIEXTPROC) (GLenum target, GLenum pname, GLint params);
typedef void (APIENTRYP PFNMYGLCONVOLUTIONPARAMETERIVEXTPROC) (GLenum target, GLenum pname, const GLint *params);
typedef void (APIENTRYP PFNMYGLCOPYCONVOLUTIONFILTER1DEXTPROC) (GLenum target, GLenum internalformat, GLint x, GLint y, GLsizei width);
typedef void (APIENTRYP PFNMYGLCOPYCONVOLUTIONFILTER2DEXTPROC) (GLenum target, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height);
typedef void (APIENTRYP PFNMYGLGETCONVOLUTIONFILTEREXTPROC) (GLenum target, GLenum format, GLenum type, GLvoid *image);
typedef void (APIENTRYP PFNMYGLGETCONVOLUTIONPARAMETERFVEXTPROC) (GLenum target, GLenum pname, GLfloat *params);
typedef void (APIENTRYP PFNMYGLGETCONVOLUTIONPARAMETERIVEXTPROC) (GLenum target, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLGETSEPARABLEFILTEREXTPROC) (GLenum target, GLenum format, GLenum type, GLvoid *row, GLvoid *column, GLvoid *span);
typedef void (APIENTRYP PFNMYGLSEPARABLEFILTER2DEXTPROC) (GLenum target, GLenum internalformat, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *row, const GLvoid *column);
#endif

#ifndef MYGL_EXT_color_matrix
#define MYGL_EXT_color_matrix 1
#endif

#ifndef MYGL_SGI_color_table
#define MYGL_SGI_color_table 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglColorTableSGI (GLenum, GLenum, GLsizei, GLenum, GLenum, const GLvoid *);
GLAPI void APIENTRY myglColorTableParameterfvSGI (GLenum, GLenum, const GLfloat *);
GLAPI void APIENTRY myglColorTableParameterivSGI (GLenum, GLenum, const GLint *);
GLAPI void APIENTRY myglCopyColorTableSGI (GLenum, GLenum, GLint, GLint, GLsizei);
GLAPI void APIENTRY myglGetColorTableSGI (GLenum, GLenum, GLenum, GLvoid *);
GLAPI void APIENTRY myglGetColorTableParameterfvSGI (GLenum, GLenum, GLfloat *);
GLAPI void APIENTRY myglGetColorTableParameterivSGI (GLenum, GLenum, GLint *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLCOLORTABLESGIPROC) (GLenum target, GLenum internalformat, GLsizei width, GLenum format, GLenum type, const GLvoid *table);
typedef void (APIENTRYP PFNMYGLCOLORTABLEPARAMETERFVSGIPROC) (GLenum target, GLenum pname, const GLfloat *params);
typedef void (APIENTRYP PFNMYGLCOLORTABLEPARAMETERIVSGIPROC) (GLenum target, GLenum pname, const GLint *params);
typedef void (APIENTRYP PFNMYGLCOPYCOLORTABLESGIPROC) (GLenum target, GLenum internalformat, GLint x, GLint y, GLsizei width);
typedef void (APIENTRYP PFNMYGLGETCOLORTABLESGIPROC) (GLenum target, GLenum format, GLenum type, GLvoid *table);
typedef void (APIENTRYP PFNMYGLGETCOLORTABLEPARAMETERFVSGIPROC) (GLenum target, GLenum pname, GLfloat *params);
typedef void (APIENTRYP PFNMYGLGETCOLORTABLEPARAMETERIVSGIPROC) (GLenum target, GLenum pname, GLint *params);
#endif

#ifndef MYGL_SGIX_pixel_texture
#define MYGL_SGIX_pixel_texture 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglPixelTexGenSGIX (GLenum);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLPIXELTEXGENSGIXPROC) (GLenum mode);
#endif

#ifndef MYGL_SGIS_pixel_texture
#define MYGL_SGIS_pixel_texture 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglPixelTexGenParameteriSGIS (GLenum, GLint);
GLAPI void APIENTRY myglPixelTexGenParameterivSGIS (GLenum, const GLint *);
GLAPI void APIENTRY myglPixelTexGenParameterfSGIS (GLenum, GLfloat);
GLAPI void APIENTRY myglPixelTexGenParameterfvSGIS (GLenum, const GLfloat *);
GLAPI void APIENTRY myglGetPixelTexGenParameterivSGIS (GLenum, GLint *);
GLAPI void APIENTRY myglGetPixelTexGenParameterfvSGIS (GLenum, GLfloat *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLPIXELTEXGENPARAMETERISGISPROC) (GLenum pname, GLint param);
typedef void (APIENTRYP PFNMYGLPIXELTEXGENPARAMETERIVSGISPROC) (GLenum pname, const GLint *params);
typedef void (APIENTRYP PFNMYGLPIXELTEXGENPARAMETERFSGISPROC) (GLenum pname, GLfloat param);
typedef void (APIENTRYP PFNMYGLPIXELTEXGENPARAMETERFVSGISPROC) (GLenum pname, const GLfloat *params);
typedef void (APIENTRYP PFNMYGLGETPIXELTEXGENPARAMETERIVSGISPROC) (GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLGETPIXELTEXGENPARAMETERFVSGISPROC) (GLenum pname, GLfloat *params);
#endif

#ifndef MYGL_SGIS_texture4D
#define MYGL_SGIS_texture4D 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglTexImage4DSGIS (GLenum, GLint, GLenum, GLsizei, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid *);
GLAPI void APIENTRY myglTexSubImage4DSGIS (GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLTEXIMAGE4DSGISPROC) (GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLsizei size4d, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
typedef void (APIENTRYP PFNMYGLTEXSUBIMAGE4DSGISPROC) (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLint woffset, GLsizei width, GLsizei height, GLsizei depth, GLsizei size4d, GLenum format, GLenum type, const GLvoid *pixels);
#endif

#ifndef MYGL_SGI_texture_color_table
#define MYGL_SGI_texture_color_table 1
#endif

#ifndef MYGL_EXT_cmyka
#define MYGL_EXT_cmyka 1
#endif

#ifndef MYGL_EXT_texture_object
#define MYGL_EXT_texture_object 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI GLboolean APIENTRY myglAreTexturesResidentEXT (GLsizei, const GLuint *, GLboolean *);
GLAPI void APIENTRY myglBindTextureEXT (GLenum, GLuint);
GLAPI void APIENTRY myglDeleteTexturesEXT (GLsizei, const GLuint *);
GLAPI void APIENTRY myglGenTexturesEXT (GLsizei, GLuint *);
GLAPI GLboolean APIENTRY myglIsTextureEXT (GLuint);
GLAPI void APIENTRY myglPrioritizeTexturesEXT (GLsizei, const GLuint *, const GLclampf *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef GLboolean (APIENTRYP PFNMYGLARETEXTURESRESIDENTEXTPROC) (GLsizei n, const GLuint *textures, GLboolean *residences);
typedef void (APIENTRYP PFNMYGLBINDTEXTUREEXTPROC) (GLenum target, GLuint texture);
typedef void (APIENTRYP PFNMYGLDELETETEXTURESEXTPROC) (GLsizei n, const GLuint *textures);
typedef void (APIENTRYP PFNMYGLGENTEXTURESEXTPROC) (GLsizei n, GLuint *textures);
typedef GLboolean (APIENTRYP PFNMYGLISTEXTUREEXTPROC) (GLuint texture);
typedef void (APIENTRYP PFNMYGLPRIORITIZETEXTURESEXTPROC) (GLsizei n, const GLuint *textures, const GLclampf *priorities);
#endif

#ifndef MYGL_SGIS_detail_texture
#define MYGL_SGIS_detail_texture 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglDetailTexFuncSGIS (GLenum, GLsizei, const GLfloat *);
GLAPI void APIENTRY myglGetDetailTexFuncSGIS (GLenum, GLfloat *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLDETAILTEXFUNCSGISPROC) (GLenum target, GLsizei n, const GLfloat *points);
typedef void (APIENTRYP PFNMYGLGETDETAILTEXFUNCSGISPROC) (GLenum target, GLfloat *points);
#endif

#ifndef MYGL_SGIS_sharpen_texture
#define MYGL_SGIS_sharpen_texture 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglSharpenTexFuncSGIS (GLenum, GLsizei, const GLfloat *);
GLAPI void APIENTRY myglGetSharpenTexFuncSGIS (GLenum, GLfloat *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLSHARPENTEXFUNCSGISPROC) (GLenum target, GLsizei n, const GLfloat *points);
typedef void (APIENTRYP PFNMYGLGETSHARPENTEXFUNCSGISPROC) (GLenum target, GLfloat *points);
#endif

#ifndef MYGL_EXT_packed_pixels
#define MYGL_EXT_packed_pixels 1
#endif

#ifndef MYGL_SGIS_texture_lod
#define MYGL_SGIS_texture_lod 1
#endif

#ifndef MYGL_SGIS_multisample
#define MYGL_SGIS_multisample 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglSampleMaskSGIS (GLclampf, GLboolean);
GLAPI void APIENTRY myglSamplePatternSGIS (GLenum);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLSAMPLEMASKSGISPROC) (GLclampf value, GLboolean invert);
typedef void (APIENTRYP PFNMYGLSAMPLEPATTERNSGISPROC) (GLenum pattern);
#endif

#ifndef MYGL_EXT_rescale_normal
#define MYGL_EXT_rescale_normal 1
#endif

#ifndef MYGL_EXT_vertex_array
#define MYGL_EXT_vertex_array 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglArrayElementEXT (GLint);
GLAPI void APIENTRY myglColorPointerEXT (GLint, GLenum, GLsizei, GLsizei, const GLvoid *);
GLAPI void APIENTRY myglDrawArraysEXT (GLenum, GLint, GLsizei);
GLAPI void APIENTRY myglEdgeFlagPointerEXT (GLsizei, GLsizei, const GLboolean *);
GLAPI void APIENTRY myglGetPointervEXT (GLenum, GLvoid* *);
GLAPI void APIENTRY myglIndexPointerEXT (GLenum, GLsizei, GLsizei, const GLvoid *);
GLAPI void APIENTRY myglNormalPointerEXT (GLenum, GLsizei, GLsizei, const GLvoid *);
GLAPI void APIENTRY myglTexCoordPointerEXT (GLint, GLenum, GLsizei, GLsizei, const GLvoid *);
GLAPI void APIENTRY myglVertexPointerEXT (GLint, GLenum, GLsizei, GLsizei, const GLvoid *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLARRAYELEMENTEXTPROC) (GLint i);
typedef void (APIENTRYP PFNMYGLCOLORPOINTEREXTPROC) (GLint size, GLenum type, GLsizei stride, GLsizei count, const GLvoid *pointer);
typedef void (APIENTRYP PFNMYGLDRAWARRAYSEXTPROC) (GLenum mode, GLint first, GLsizei count);
typedef void (APIENTRYP PFNMYGLEDGEFLAGPOINTEREXTPROC) (GLsizei stride, GLsizei count, const GLboolean *pointer);
typedef void (APIENTRYP PFNMYGLGETPOINTERVEXTPROC) (GLenum pname, GLvoid* *params);
typedef void (APIENTRYP PFNMYGLINDEXPOINTEREXTPROC) (GLenum type, GLsizei stride, GLsizei count, const GLvoid *pointer);
typedef void (APIENTRYP PFNMYGLNORMALPOINTEREXTPROC) (GLenum type, GLsizei stride, GLsizei count, const GLvoid *pointer);
typedef void (APIENTRYP PFNMYGLTEXCOORDPOINTEREXTPROC) (GLint size, GLenum type, GLsizei stride, GLsizei count, const GLvoid *pointer);
typedef void (APIENTRYP PFNMYGLVERTEXPOINTEREXTPROC) (GLint size, GLenum type, GLsizei stride, GLsizei count, const GLvoid *pointer);
#endif

#ifndef MYGL_EXT_misc_attribute
#define MYGL_EXT_misc_attribute 1
#endif

#ifndef MYGL_SGIS_generate_mipmap
#define MYGL_SGIS_generate_mipmap 1
#endif

#ifndef MYGL_SGIX_clipmap
#define MYGL_SGIX_clipmap 1
#endif

#ifndef MYGL_SGIX_shadow
#define MYGL_SGIX_shadow 1
#endif

#ifndef MYGL_SGIS_texture_edge_clamp
#define MYGL_SGIS_texture_edge_clamp 1
#endif

#ifndef MYGL_SGIS_texture_border_clamp
#define MYGL_SGIS_texture_border_clamp 1
#endif

#ifndef MYGL_EXT_blend_minmax
#define MYGL_EXT_blend_minmax 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglBlendEquationEXT (GLenum);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLBLENDEQUATIONEXTPROC) (GLenum mode);
#endif

#ifndef MYGL_EXT_blend_subtract
#define MYGL_EXT_blend_subtract 1
#endif

#ifndef MYGL_EXT_blend_logic_op
#define MYGL_EXT_blend_logic_op 1
#endif

#ifndef MYGL_SGIX_interlace
#define MYGL_SGIX_interlace 1
#endif

#ifndef MYGL_SGIX_pixel_tiles
#define MYGL_SGIX_pixel_tiles 1
#endif

#ifndef MYGL_SGIX_texture_select
#define MYGL_SGIX_texture_select 1
#endif

#ifndef MYGL_SGIX_sprite
#define MYGL_SGIX_sprite 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglSpriteParameterfSGIX (GLenum, GLfloat);
GLAPI void APIENTRY myglSpriteParameterfvSGIX (GLenum, const GLfloat *);
GLAPI void APIENTRY myglSpriteParameteriSGIX (GLenum, GLint);
GLAPI void APIENTRY myglSpriteParameterivSGIX (GLenum, const GLint *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLSPRITEPARAMETERFSGIXPROC) (GLenum pname, GLfloat param);
typedef void (APIENTRYP PFNMYGLSPRITEPARAMETERFVSGIXPROC) (GLenum pname, const GLfloat *params);
typedef void (APIENTRYP PFNMYGLSPRITEPARAMETERISGIXPROC) (GLenum pname, GLint param);
typedef void (APIENTRYP PFNMYGLSPRITEPARAMETERIVSGIXPROC) (GLenum pname, const GLint *params);
#endif

#ifndef MYGL_SGIX_texture_multi_buffer
#define MYGL_SGIX_texture_multi_buffer 1
#endif

#ifndef MYGL_EXT_point_parameters
#define MYGL_EXT_point_parameters 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglPointParameterfEXT (GLenum, GLfloat);
GLAPI void APIENTRY myglPointParameterfvEXT (GLenum, const GLfloat *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLPOINTPARAMETERFEXTPROC) (GLenum pname, GLfloat param);
typedef void (APIENTRYP PFNMYGLPOINTPARAMETERFVEXTPROC) (GLenum pname, const GLfloat *params);
#endif

#ifndef MYGL_SGIS_point_parameters
#define MYGL_SGIS_point_parameters 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglPointParameterfSGIS (GLenum, GLfloat);
GLAPI void APIENTRY myglPointParameterfvSGIS (GLenum, const GLfloat *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLPOINTPARAMETERFSGISPROC) (GLenum pname, GLfloat param);
typedef void (APIENTRYP PFNMYGLPOINTPARAMETERFVSGISPROC) (GLenum pname, const GLfloat *params);
#endif

#ifndef MYGL_SGIX_instruments
#define MYGL_SGIX_instruments 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI GLint APIENTRY myglGetInstrumentsSGIX (void);
GLAPI void APIENTRY myglInstrumentsBufferSGIX (GLsizei, GLint *);
GLAPI GLint APIENTRY myglPollInstrumentsSGIX (GLint *);
GLAPI void APIENTRY myglReadInstrumentsSGIX (GLint);
GLAPI void APIENTRY myglStartInstrumentsSGIX (void);
GLAPI void APIENTRY myglStopInstrumentsSGIX (GLint);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef GLint (APIENTRYP PFNMYGLGETINSTRUMENTSSGIXPROC) (void);
typedef void (APIENTRYP PFNMYGLINSTRUMENTSBUFFERSGIXPROC) (GLsizei size, GLint *buffer);
typedef GLint (APIENTRYP PFNMYGLPOLLINSTRUMENTSSGIXPROC) (GLint *marker_p);
typedef void (APIENTRYP PFNMYGLREADINSTRUMENTSSGIXPROC) (GLint marker);
typedef void (APIENTRYP PFNMYGLSTARTINSTRUMENTSSGIXPROC) (void);
typedef void (APIENTRYP PFNMYGLSTOPINSTRUMENTSSGIXPROC) (GLint marker);
#endif

#ifndef MYGL_SGIX_texture_scale_bias
#define MYGL_SGIX_texture_scale_bias 1
#endif

#ifndef MYGL_SGIX_framezoom
#define MYGL_SGIX_framezoom 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglFrameZoomSGIX (GLint);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLFRAMEZOOMSGIXPROC) (GLint factor);
#endif

#ifndef MYGL_SGIX_tag_sample_buffer
#define MYGL_SGIX_tag_sample_buffer 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglTagSampleBufferSGIX (void);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLTAGSAMPLEBUFFERSGIXPROC) (void);
#endif

#ifndef MYGL_SGIX_polynomial_ffd
#define MYGL_SGIX_polynomial_ffd 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglDeformationMap3dSGIX (GLenum, GLdouble, GLdouble, GLint, GLint, GLdouble, GLdouble, GLint, GLint, GLdouble, GLdouble, GLint, GLint, const GLdouble *);
GLAPI void APIENTRY myglDeformationMap3fSGIX (GLenum, GLfloat, GLfloat, GLint, GLint, GLfloat, GLfloat, GLint, GLint, GLfloat, GLfloat, GLint, GLint, const GLfloat *);
GLAPI void APIENTRY myglDeformSGIX (GLbitfield);
GLAPI void APIENTRY myglLoadIdentityDeformationMapSGIX (GLbitfield);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLDEFORMATIONMAP3DSGIXPROC) (GLenum target, GLdouble u1, GLdouble u2, GLint ustride, GLint uorder, GLdouble v1, GLdouble v2, GLint vstride, GLint vorder, GLdouble w1, GLdouble w2, GLint wstride, GLint worder, const GLdouble *points);
typedef void (APIENTRYP PFNMYGLDEFORMATIONMAP3FSGIXPROC) (GLenum target, GLfloat u1, GLfloat u2, GLint ustride, GLint uorder, GLfloat v1, GLfloat v2, GLint vstride, GLint vorder, GLfloat w1, GLfloat w2, GLint wstride, GLint worder, const GLfloat *points);
typedef void (APIENTRYP PFNMYGLDEFORMSGIXPROC) (GLbitfield mask);
typedef void (APIENTRYP PFNMYGLLOADIDENTITYDEFORMATIONMAPSGIXPROC) (GLbitfield mask);
#endif

#ifndef MYGL_SGIX_reference_plane
#define MYGL_SGIX_reference_plane 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglReferencePlaneSGIX (const GLdouble *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLREFERENCEPLANESGIXPROC) (const GLdouble *equation);
#endif

#ifndef MYGL_SGIX_flush_raster
#define MYGL_SGIX_flush_raster 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglFlushRasterSGIX (void);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLFLUSHRASTERSGIXPROC) (void);
#endif

#ifndef MYGL_SGIX_depth_texture
#define MYGL_SGIX_depth_texture 1
#endif

#ifndef MYGL_SGIS_fog_function
#define MYGL_SGIS_fog_function 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglFogFuncSGIS (GLsizei, const GLfloat *);
GLAPI void APIENTRY myglGetFogFuncSGIS (GLfloat *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLFOGFUNCSGISPROC) (GLsizei n, const GLfloat *points);
typedef void (APIENTRYP PFNMYGLGETFOGFUNCSGISPROC) (GLfloat *points);
#endif

#ifndef MYGL_SGIX_fog_offset
#define MYGL_SGIX_fog_offset 1
#endif

#ifndef MYGL_HP_image_transform
#define MYGL_HP_image_transform 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglImageTransformParameteriHP (GLenum, GLenum, GLint);
GLAPI void APIENTRY myglImageTransformParameterfHP (GLenum, GLenum, GLfloat);
GLAPI void APIENTRY myglImageTransformParameterivHP (GLenum, GLenum, const GLint *);
GLAPI void APIENTRY myglImageTransformParameterfvHP (GLenum, GLenum, const GLfloat *);
GLAPI void APIENTRY myglGetImageTransformParameterivHP (GLenum, GLenum, GLint *);
GLAPI void APIENTRY myglGetImageTransformParameterfvHP (GLenum, GLenum, GLfloat *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLIMAGETRANSFORMPARAMETERIHPPROC) (GLenum target, GLenum pname, GLint param);
typedef void (APIENTRYP PFNMYGLIMAGETRANSFORMPARAMETERFHPPROC) (GLenum target, GLenum pname, GLfloat param);
typedef void (APIENTRYP PFNMYGLIMAGETRANSFORMPARAMETERIVHPPROC) (GLenum target, GLenum pname, const GLint *params);
typedef void (APIENTRYP PFNMYGLIMAGETRANSFORMPARAMETERFVHPPROC) (GLenum target, GLenum pname, const GLfloat *params);
typedef void (APIENTRYP PFNMYGLGETIMAGETRANSFORMPARAMETERIVHPPROC) (GLenum target, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLGETIMAGETRANSFORMPARAMETERFVHPPROC) (GLenum target, GLenum pname, GLfloat *params);
#endif

#ifndef MYGL_HP_convolution_border_modes
#define MYGL_HP_convolution_border_modes 1
#endif

#ifndef MYGL_SGIX_texture_add_env
#define MYGL_SGIX_texture_add_env 1
#endif

#ifndef MYGL_EXT_color_subtable
#define MYGL_EXT_color_subtable 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglColorSubTableEXT (GLenum, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *);
GLAPI void APIENTRY myglCopyColorSubTableEXT (GLenum, GLsizei, GLint, GLint, GLsizei);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLCOLORSUBTABLEEXTPROC) (GLenum target, GLsizei start, GLsizei count, GLenum format, GLenum type, const GLvoid *data);
typedef void (APIENTRYP PFNMYGLCOPYCOLORSUBTABLEEXTPROC) (GLenum target, GLsizei start, GLint x, GLint y, GLsizei width);
#endif

#ifndef MYGL_PGI_vertex_hints
#define MYGL_PGI_vertex_hints 1
#endif

#ifndef MYGL_PGI_misc_hints
#define MYGL_PGI_misc_hints 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglHintPGI (GLenum, GLint);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLHINTPGIPROC) (GLenum target, GLint mode);
#endif

#ifndef MYGL_EXT_paletted_texture
#define MYGL_EXT_paletted_texture 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglColorTableEXT (GLenum, GLenum, GLsizei, GLenum, GLenum, const GLvoid *);
GLAPI void APIENTRY myglGetColorTableEXT (GLenum, GLenum, GLenum, GLvoid *);
GLAPI void APIENTRY myglGetColorTableParameterivEXT (GLenum, GLenum, GLint *);
GLAPI void APIENTRY myglGetColorTableParameterfvEXT (GLenum, GLenum, GLfloat *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLCOLORTABLEEXTPROC) (GLenum target, GLenum internalFormat, GLsizei width, GLenum format, GLenum type, const GLvoid *table);
typedef void (APIENTRYP PFNMYGLGETCOLORTABLEEXTPROC) (GLenum target, GLenum format, GLenum type, GLvoid *data);
typedef void (APIENTRYP PFNMYGLGETCOLORTABLEPARAMETERIVEXTPROC) (GLenum target, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLGETCOLORTABLEPARAMETERFVEXTPROC) (GLenum target, GLenum pname, GLfloat *params);
#endif

#ifndef MYGL_EXT_clip_volume_hint
#define MYGL_EXT_clip_volume_hint 1
#endif

#ifndef MYGL_SGIX_list_priority
#define MYGL_SGIX_list_priority 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglGetListParameterfvSGIX (GLuint, GLenum, GLfloat *);
GLAPI void APIENTRY myglGetListParameterivSGIX (GLuint, GLenum, GLint *);
GLAPI void APIENTRY myglListParameterfSGIX (GLuint, GLenum, GLfloat);
GLAPI void APIENTRY myglListParameterfvSGIX (GLuint, GLenum, const GLfloat *);
GLAPI void APIENTRY myglListParameteriSGIX (GLuint, GLenum, GLint);
GLAPI void APIENTRY myglListParameterivSGIX (GLuint, GLenum, const GLint *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLGETLISTPARAMETERFVSGIXPROC) (GLuint list, GLenum pname, GLfloat *params);
typedef void (APIENTRYP PFNMYGLGETLISTPARAMETERIVSGIXPROC) (GLuint list, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLLISTPARAMETERFSGIXPROC) (GLuint list, GLenum pname, GLfloat param);
typedef void (APIENTRYP PFNMYGLLISTPARAMETERFVSGIXPROC) (GLuint list, GLenum pname, const GLfloat *params);
typedef void (APIENTRYP PFNMYGLLISTPARAMETERISGIXPROC) (GLuint list, GLenum pname, GLint param);
typedef void (APIENTRYP PFNMYGLLISTPARAMETERIVSGIXPROC) (GLuint list, GLenum pname, const GLint *params);
#endif

#ifndef MYGL_SGIX_ir_instrument1
#define MYGL_SGIX_ir_instrument1 1
#endif

#ifndef MYGL_SGIX_calligraphic_fragment
#define MYGL_SGIX_calligraphic_fragment 1
#endif

#ifndef MYGL_SGIX_texture_lod_bias
#define MYGL_SGIX_texture_lod_bias 1
#endif

#ifndef MYGL_SGIX_shadow_ambient
#define MYGL_SGIX_shadow_ambient 1
#endif

#ifndef MYGL_EXT_index_texture
#define MYGL_EXT_index_texture 1
#endif

#ifndef MYGL_EXT_index_material
#define MYGL_EXT_index_material 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglIndexMaterialEXT (GLenum, GLenum);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLINDEXMATERIALEXTPROC) (GLenum face, GLenum mode);
#endif

#ifndef MYGL_EXT_index_func
#define MYGL_EXT_index_func 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglIndexFuncEXT (GLenum, GLclampf);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLINDEXFUNCEXTPROC) (GLenum func, GLclampf ref);
#endif

#ifndef MYGL_EXT_index_array_formats
#define MYGL_EXT_index_array_formats 1
#endif

#ifndef MYGL_EXT_compiled_vertex_array
#define MYGL_EXT_compiled_vertex_array 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglLockArraysEXT (GLint, GLsizei);
GLAPI void APIENTRY myglUnlockArraysEXT (void);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLLOCKARRAYSEXTPROC) (GLint first, GLsizei count);
typedef void (APIENTRYP PFNMYGLUNLOCKARRAYSEXTPROC) (void);
#endif

#ifndef MYGL_EXT_cull_vertex
#define MYGL_EXT_cull_vertex 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglCullParameterdvEXT (GLenum, GLdouble *);
GLAPI void APIENTRY myglCullParameterfvEXT (GLenum, GLfloat *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLCULLPARAMETERDVEXTPROC) (GLenum pname, GLdouble *params);
typedef void (APIENTRYP PFNMYGLCULLPARAMETERFVEXTPROC) (GLenum pname, GLfloat *params);
#endif

#ifndef MYGL_SGIX_ycrcb
#define MYGL_SGIX_ycrcb 1
#endif

#ifndef MYGL_SGIX_fragment_lighting
#define MYGL_SGIX_fragment_lighting 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglFragmentColorMaterialSGIX (GLenum, GLenum);
GLAPI void APIENTRY myglFragmentLightfSGIX (GLenum, GLenum, GLfloat);
GLAPI void APIENTRY myglFragmentLightfvSGIX (GLenum, GLenum, const GLfloat *);
GLAPI void APIENTRY myglFragmentLightiSGIX (GLenum, GLenum, GLint);
GLAPI void APIENTRY myglFragmentLightivSGIX (GLenum, GLenum, const GLint *);
GLAPI void APIENTRY myglFragmentLightModelfSGIX (GLenum, GLfloat);
GLAPI void APIENTRY myglFragmentLightModelfvSGIX (GLenum, const GLfloat *);
GLAPI void APIENTRY myglFragmentLightModeliSGIX (GLenum, GLint);
GLAPI void APIENTRY myglFragmentLightModelivSGIX (GLenum, const GLint *);
GLAPI void APIENTRY myglFragmentMaterialfSGIX (GLenum, GLenum, GLfloat);
GLAPI void APIENTRY myglFragmentMaterialfvSGIX (GLenum, GLenum, const GLfloat *);
GLAPI void APIENTRY myglFragmentMaterialiSGIX (GLenum, GLenum, GLint);
GLAPI void APIENTRY myglFragmentMaterialivSGIX (GLenum, GLenum, const GLint *);
GLAPI void APIENTRY myglGetFragmentLightfvSGIX (GLenum, GLenum, GLfloat *);
GLAPI void APIENTRY myglGetFragmentLightivSGIX (GLenum, GLenum, GLint *);
GLAPI void APIENTRY myglGetFragmentMaterialfvSGIX (GLenum, GLenum, GLfloat *);
GLAPI void APIENTRY myglGetFragmentMaterialivSGIX (GLenum, GLenum, GLint *);
GLAPI void APIENTRY myglLightEnviSGIX (GLenum, GLint);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLFRAGMENTCOLORMATERIALSGIXPROC) (GLenum face, GLenum mode);
typedef void (APIENTRYP PFNMYGLFRAGMENTLIGHTFSGIXPROC) (GLenum light, GLenum pname, GLfloat param);
typedef void (APIENTRYP PFNMYGLFRAGMENTLIGHTFVSGIXPROC) (GLenum light, GLenum pname, const GLfloat *params);
typedef void (APIENTRYP PFNMYGLFRAGMENTLIGHTISGIXPROC) (GLenum light, GLenum pname, GLint param);
typedef void (APIENTRYP PFNMYGLFRAGMENTLIGHTIVSGIXPROC) (GLenum light, GLenum pname, const GLint *params);
typedef void (APIENTRYP PFNMYGLFRAGMENTLIGHTMODELFSGIXPROC) (GLenum pname, GLfloat param);
typedef void (APIENTRYP PFNMYGLFRAGMENTLIGHTMODELFVSGIXPROC) (GLenum pname, const GLfloat *params);
typedef void (APIENTRYP PFNMYGLFRAGMENTLIGHTMODELISGIXPROC) (GLenum pname, GLint param);
typedef void (APIENTRYP PFNMYGLFRAGMENTLIGHTMODELIVSGIXPROC) (GLenum pname, const GLint *params);
typedef void (APIENTRYP PFNMYGLFRAGMENTMATERIALFSGIXPROC) (GLenum face, GLenum pname, GLfloat param);
typedef void (APIENTRYP PFNMYGLFRAGMENTMATERIALFVSGIXPROC) (GLenum face, GLenum pname, const GLfloat *params);
typedef void (APIENTRYP PFNMYGLFRAGMENTMATERIALISGIXPROC) (GLenum face, GLenum pname, GLint param);
typedef void (APIENTRYP PFNMYGLFRAGMENTMATERIALIVSGIXPROC) (GLenum face, GLenum pname, const GLint *params);
typedef void (APIENTRYP PFNMYGLGETFRAGMENTLIGHTFVSGIXPROC) (GLenum light, GLenum pname, GLfloat *params);
typedef void (APIENTRYP PFNMYGLGETFRAGMENTLIGHTIVSGIXPROC) (GLenum light, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLGETFRAGMENTMATERIALFVSGIXPROC) (GLenum face, GLenum pname, GLfloat *params);
typedef void (APIENTRYP PFNMYGLGETFRAGMENTMATERIALIVSGIXPROC) (GLenum face, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLLIGHTENVISGIXPROC) (GLenum pname, GLint param);
#endif

#ifndef MYGL_IBM_rasterpos_clip
#define MYGL_IBM_rasterpos_clip 1
#endif

#ifndef MYGL_HP_texture_lighting
#define MYGL_HP_texture_lighting 1
#endif

#ifndef MYGL_EXT_draw_range_elements
#define MYGL_EXT_draw_range_elements 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglDrawRangeElementsEXT (GLenum, GLuint, GLuint, GLsizei, GLenum, const GLvoid *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLDRAWRANGEELEMENTSEXTPROC) (GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid *indices);
#endif

#ifndef MYGL_WIN_phong_shading
#define MYGL_WIN_phong_shading 1
#endif

#ifndef MYGL_WIN_specular_fog
#define MYGL_WIN_specular_fog 1
#endif

#ifndef MYGL_EXT_light_texture
#define MYGL_EXT_light_texture 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglApplyTextureEXT (GLenum);
GLAPI void APIENTRY myglTextureLightEXT (GLenum);
GLAPI void APIENTRY myglTextureMaterialEXT (GLenum, GLenum);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLAPPLYTEXTUREEXTPROC) (GLenum mode);
typedef void (APIENTRYP PFNMYGLTEXTURELIGHTEXTPROC) (GLenum pname);
typedef void (APIENTRYP PFNMYGLTEXTUREMATERIALEXTPROC) (GLenum face, GLenum mode);
#endif

#ifndef MYGL_SGIX_blend_alpha_minmax
#define MYGL_SGIX_blend_alpha_minmax 1
#endif

#ifndef MYGL_EXT_bgra
#define MYGL_EXT_bgra 1
#endif

#ifndef MYGL_SGIX_async
#define MYGL_SGIX_async 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglAsyncMarkerSGIX (GLuint);
GLAPI GLint APIENTRY myglFinishAsyncSGIX (GLuint *);
GLAPI GLint APIENTRY myglPollAsyncSGIX (GLuint *);
GLAPI GLuint APIENTRY myglGenAsyncMarkersSGIX (GLsizei);
GLAPI void APIENTRY myglDeleteAsyncMarkersSGIX (GLuint, GLsizei);
GLAPI GLboolean APIENTRY myglIsAsyncMarkerSGIX (GLuint);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLASYNCMARKERSGIXPROC) (GLuint marker);
typedef GLint (APIENTRYP PFNMYGLFINISHASYNCSGIXPROC) (GLuint *markerp);
typedef GLint (APIENTRYP PFNMYGLPOLLASYNCSGIXPROC) (GLuint *markerp);
typedef GLuint (APIENTRYP PFNMYGLGENASYNCMARKERSSGIXPROC) (GLsizei range);
typedef void (APIENTRYP PFNMYGLDELETEASYNCMARKERSSGIXPROC) (GLuint marker, GLsizei range);
typedef GLboolean (APIENTRYP PFNMYGLISASYNCMARKERSGIXPROC) (GLuint marker);
#endif

#ifndef MYGL_SGIX_async_pixel
#define MYGL_SGIX_async_pixel 1
#endif

#ifndef MYGL_SGIX_async_histogram
#define MYGL_SGIX_async_histogram 1
#endif

#ifndef MYGL_INTEL_parallel_arrays
#define MYGL_INTEL_parallel_arrays 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglVertexPointervINTEL (GLint, GLenum, const GLvoid* *);
GLAPI void APIENTRY myglNormalPointervINTEL (GLenum, const GLvoid* *);
GLAPI void APIENTRY myglColorPointervINTEL (GLint, GLenum, const GLvoid* *);
GLAPI void APIENTRY myglTexCoordPointervINTEL (GLint, GLenum, const GLvoid* *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLVERTEXPOINTERVINTELPROC) (GLint size, GLenum type, const GLvoid* *pointer);
typedef void (APIENTRYP PFNMYGLNORMALPOINTERVINTELPROC) (GLenum type, const GLvoid* *pointer);
typedef void (APIENTRYP PFNMYGLCOLORPOINTERVINTELPROC) (GLint size, GLenum type, const GLvoid* *pointer);
typedef void (APIENTRYP PFNMYGLTEXCOORDPOINTERVINTELPROC) (GLint size, GLenum type, const GLvoid* *pointer);
#endif

#ifndef MYGL_HP_occlusion_test
#define MYGL_HP_occlusion_test 1
#endif

#ifndef MYGL_EXT_pixel_transform
#define MYGL_EXT_pixel_transform 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglPixelTransformParameteriEXT (GLenum, GLenum, GLint);
GLAPI void APIENTRY myglPixelTransformParameterfEXT (GLenum, GLenum, GLfloat);
GLAPI void APIENTRY myglPixelTransformParameterivEXT (GLenum, GLenum, const GLint *);
GLAPI void APIENTRY myglPixelTransformParameterfvEXT (GLenum, GLenum, const GLfloat *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLPIXELTRANSFORMPARAMETERIEXTPROC) (GLenum target, GLenum pname, GLint param);
typedef void (APIENTRYP PFNMYGLPIXELTRANSFORMPARAMETERFEXTPROC) (GLenum target, GLenum pname, GLfloat param);
typedef void (APIENTRYP PFNMYGLPIXELTRANSFORMPARAMETERIVEXTPROC) (GLenum target, GLenum pname, const GLint *params);
typedef void (APIENTRYP PFNMYGLPIXELTRANSFORMPARAMETERFVEXTPROC) (GLenum target, GLenum pname, const GLfloat *params);
#endif

#ifndef MYGL_EXT_pixel_transform_color_table
#define MYGL_EXT_pixel_transform_color_table 1
#endif

#ifndef MYGL_EXT_shared_texture_palette
#define MYGL_EXT_shared_texture_palette 1
#endif

#ifndef MYGL_EXT_separate_specular_color
#define MYGL_EXT_separate_specular_color 1
#endif

#ifndef MYGL_EXT_secondary_color
#define MYGL_EXT_secondary_color 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglSecondaryColor3bEXT (GLbyte, GLbyte, GLbyte);
GLAPI void APIENTRY myglSecondaryColor3bvEXT (const GLbyte *);
GLAPI void APIENTRY myglSecondaryColor3dEXT (GLdouble, GLdouble, GLdouble);
GLAPI void APIENTRY myglSecondaryColor3dvEXT (const GLdouble *);
GLAPI void APIENTRY myglSecondaryColor3fEXT (GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglSecondaryColor3fvEXT (const GLfloat *);
GLAPI void APIENTRY myglSecondaryColor3iEXT (GLint, GLint, GLint);
GLAPI void APIENTRY myglSecondaryColor3ivEXT (const GLint *);
GLAPI void APIENTRY myglSecondaryColor3sEXT (GLshort, GLshort, GLshort);
GLAPI void APIENTRY myglSecondaryColor3svEXT (const GLshort *);
GLAPI void APIENTRY myglSecondaryColor3ubEXT (GLubyte, GLubyte, GLubyte);
GLAPI void APIENTRY myglSecondaryColor3ubvEXT (const GLubyte *);
GLAPI void APIENTRY myglSecondaryColor3uiEXT (GLuint, GLuint, GLuint);
GLAPI void APIENTRY myglSecondaryColor3uivEXT (const GLuint *);
GLAPI void APIENTRY myglSecondaryColor3usEXT (GLushort, GLushort, GLushort);
GLAPI void APIENTRY myglSecondaryColor3usvEXT (const GLushort *);
GLAPI void APIENTRY myglSecondaryColorPointerEXT (GLint, GLenum, GLsizei, const GLvoid *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLSECONDARYCOLOR3BEXTPROC) (GLbyte red, GLbyte green, GLbyte blue);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLOR3BVEXTPROC) (const GLbyte *v);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLOR3DEXTPROC) (GLdouble red, GLdouble green, GLdouble blue);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLOR3DVEXTPROC) (const GLdouble *v);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLOR3FEXTPROC) (GLfloat red, GLfloat green, GLfloat blue);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLOR3FVEXTPROC) (const GLfloat *v);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLOR3IEXTPROC) (GLint red, GLint green, GLint blue);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLOR3IVEXTPROC) (const GLint *v);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLOR3SEXTPROC) (GLshort red, GLshort green, GLshort blue);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLOR3SVEXTPROC) (const GLshort *v);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLOR3UBEXTPROC) (GLubyte red, GLubyte green, GLubyte blue);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLOR3UBVEXTPROC) (const GLubyte *v);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLOR3UIEXTPROC) (GLuint red, GLuint green, GLuint blue);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLOR3UIVEXTPROC) (const GLuint *v);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLOR3USEXTPROC) (GLushort red, GLushort green, GLushort blue);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLOR3USVEXTPROC) (const GLushort *v);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLORPOINTEREXTPROC) (GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
#endif

#ifndef MYGL_EXT_texture_perturb_normal
#define MYGL_EXT_texture_perturb_normal 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglTextureNormalEXT (GLenum);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLTEXTURENORMALEXTPROC) (GLenum mode);
#endif

#ifndef MYGL_EXT_multi_draw_arrays
#define MYGL_EXT_multi_draw_arrays 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglMultiDrawArraysEXT (GLenum, GLint *, GLsizei *, GLsizei);
GLAPI void APIENTRY myglMultiDrawElementsEXT (GLenum, const GLsizei *, GLenum, const GLvoid* *, GLsizei);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLMULTIDRAWARRAYSEXTPROC) (GLenum mode, GLint *first, GLsizei *count, GLsizei primcount);
typedef void (APIENTRYP PFNMYGLMULTIDRAWELEMENTSEXTPROC) (GLenum mode, const GLsizei *count, GLenum type, const GLvoid* *indices, GLsizei primcount);
#endif

#ifndef MYGL_EXT_fog_coord
#define MYGL_EXT_fog_coord 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglFogCoordfEXT (GLfloat);
GLAPI void APIENTRY myglFogCoordfvEXT (const GLfloat *);
GLAPI void APIENTRY myglFogCoorddEXT (GLdouble);
GLAPI void APIENTRY myglFogCoorddvEXT (const GLdouble *);
GLAPI void APIENTRY myglFogCoordPointerEXT (GLenum, GLsizei, const GLvoid *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLFOGCOORDFEXTPROC) (GLfloat coord);
typedef void (APIENTRYP PFNMYGLFOGCOORDFVEXTPROC) (const GLfloat *coord);
typedef void (APIENTRYP PFNMYGLFOGCOORDDEXTPROC) (GLdouble coord);
typedef void (APIENTRYP PFNMYGLFOGCOORDDVEXTPROC) (const GLdouble *coord);
typedef void (APIENTRYP PFNMYGLFOGCOORDPOINTEREXTPROC) (GLenum type, GLsizei stride, const GLvoid *pointer);
#endif

#ifndef MYGL_REND_screen_coordinates
#define MYGL_REND_screen_coordinates 1
#endif

#ifndef MYGL_EXT_coordinate_frame
#define MYGL_EXT_coordinate_frame 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglTangent3bEXT (GLbyte, GLbyte, GLbyte);
GLAPI void APIENTRY myglTangent3bvEXT (const GLbyte *);
GLAPI void APIENTRY myglTangent3dEXT (GLdouble, GLdouble, GLdouble);
GLAPI void APIENTRY myglTangent3dvEXT (const GLdouble *);
GLAPI void APIENTRY myglTangent3fEXT (GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglTangent3fvEXT (const GLfloat *);
GLAPI void APIENTRY myglTangent3iEXT (GLint, GLint, GLint);
GLAPI void APIENTRY myglTangent3ivEXT (const GLint *);
GLAPI void APIENTRY myglTangent3sEXT (GLshort, GLshort, GLshort);
GLAPI void APIENTRY myglTangent3svEXT (const GLshort *);
GLAPI void APIENTRY myglBinormal3bEXT (GLbyte, GLbyte, GLbyte);
GLAPI void APIENTRY myglBinormal3bvEXT (const GLbyte *);
GLAPI void APIENTRY myglBinormal3dEXT (GLdouble, GLdouble, GLdouble);
GLAPI void APIENTRY myglBinormal3dvEXT (const GLdouble *);
GLAPI void APIENTRY myglBinormal3fEXT (GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglBinormal3fvEXT (const GLfloat *);
GLAPI void APIENTRY myglBinormal3iEXT (GLint, GLint, GLint);
GLAPI void APIENTRY myglBinormal3ivEXT (const GLint *);
GLAPI void APIENTRY myglBinormal3sEXT (GLshort, GLshort, GLshort);
GLAPI void APIENTRY myglBinormal3svEXT (const GLshort *);
GLAPI void APIENTRY myglTangentPointerEXT (GLenum, GLsizei, const GLvoid *);
GLAPI void APIENTRY myglBinormalPointerEXT (GLenum, GLsizei, const GLvoid *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLTANGENT3BEXTPROC) (GLbyte tx, GLbyte ty, GLbyte tz);
typedef void (APIENTRYP PFNMYGLTANGENT3BVEXTPROC) (const GLbyte *v);
typedef void (APIENTRYP PFNMYGLTANGENT3DEXTPROC) (GLdouble tx, GLdouble ty, GLdouble tz);
typedef void (APIENTRYP PFNMYGLTANGENT3DVEXTPROC) (const GLdouble *v);
typedef void (APIENTRYP PFNMYGLTANGENT3FEXTPROC) (GLfloat tx, GLfloat ty, GLfloat tz);
typedef void (APIENTRYP PFNMYGLTANGENT3FVEXTPROC) (const GLfloat *v);
typedef void (APIENTRYP PFNMYGLTANGENT3IEXTPROC) (GLint tx, GLint ty, GLint tz);
typedef void (APIENTRYP PFNMYGLTANGENT3IVEXTPROC) (const GLint *v);
typedef void (APIENTRYP PFNMYGLTANGENT3SEXTPROC) (GLshort tx, GLshort ty, GLshort tz);
typedef void (APIENTRYP PFNMYGLTANGENT3SVEXTPROC) (const GLshort *v);
typedef void (APIENTRYP PFNMYGLBINORMAL3BEXTPROC) (GLbyte bx, GLbyte by, GLbyte bz);
typedef void (APIENTRYP PFNMYGLBINORMAL3BVEXTPROC) (const GLbyte *v);
typedef void (APIENTRYP PFNMYGLBINORMAL3DEXTPROC) (GLdouble bx, GLdouble by, GLdouble bz);
typedef void (APIENTRYP PFNMYGLBINORMAL3DVEXTPROC) (const GLdouble *v);
typedef void (APIENTRYP PFNMYGLBINORMAL3FEXTPROC) (GLfloat bx, GLfloat by, GLfloat bz);
typedef void (APIENTRYP PFNMYGLBINORMAL3FVEXTPROC) (const GLfloat *v);
typedef void (APIENTRYP PFNMYGLBINORMAL3IEXTPROC) (GLint bx, GLint by, GLint bz);
typedef void (APIENTRYP PFNMYGLBINORMAL3IVEXTPROC) (const GLint *v);
typedef void (APIENTRYP PFNMYGLBINORMAL3SEXTPROC) (GLshort bx, GLshort by, GLshort bz);
typedef void (APIENTRYP PFNMYGLBINORMAL3SVEXTPROC) (const GLshort *v);
typedef void (APIENTRYP PFNMYGLTANGENTPOINTEREXTPROC) (GLenum type, GLsizei stride, const GLvoid *pointer);
typedef void (APIENTRYP PFNMYGLBINORMALPOINTEREXTPROC) (GLenum type, GLsizei stride, const GLvoid *pointer);
#endif

#ifndef MYGL_EXT_texture_env_combine
#define MYGL_EXT_texture_env_combine 1
#endif

#ifndef MYGL_APPLE_specular_vector
#define MYGL_APPLE_specular_vector 1
#endif

#ifndef MYGL_APPLE_transform_hint
#define MYGL_APPLE_transform_hint 1
#endif

#ifndef MYGL_SGIX_fog_scale
#define MYGL_SGIX_fog_scale 1
#endif

#ifndef MYGL_SUNX_constant_data
#define MYGL_SUNX_constant_data 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglFinishTextureSUNX (void);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLFINISHTEXTURESUNXPROC) (void);
#endif

#ifndef MYGL_SUN_global_alpha
#define MYGL_SUN_global_alpha 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglGlobalAlphaFactorbSUN (GLbyte);
GLAPI void APIENTRY myglGlobalAlphaFactorsSUN (GLshort);
GLAPI void APIENTRY myglGlobalAlphaFactoriSUN (GLint);
GLAPI void APIENTRY myglGlobalAlphaFactorfSUN (GLfloat);
GLAPI void APIENTRY myglGlobalAlphaFactordSUN (GLdouble);
GLAPI void APIENTRY myglGlobalAlphaFactorubSUN (GLubyte);
GLAPI void APIENTRY myglGlobalAlphaFactorusSUN (GLushort);
GLAPI void APIENTRY myglGlobalAlphaFactoruiSUN (GLuint);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLGLOBALALPHAFACTORBSUNPROC) (GLbyte factor);
typedef void (APIENTRYP PFNMYGLGLOBALALPHAFACTORSSUNPROC) (GLshort factor);
typedef void (APIENTRYP PFNMYGLGLOBALALPHAFACTORISUNPROC) (GLint factor);
typedef void (APIENTRYP PFNMYGLGLOBALALPHAFACTORFSUNPROC) (GLfloat factor);
typedef void (APIENTRYP PFNMYGLGLOBALALPHAFACTORDSUNPROC) (GLdouble factor);
typedef void (APIENTRYP PFNMYGLGLOBALALPHAFACTORUBSUNPROC) (GLubyte factor);
typedef void (APIENTRYP PFNMYGLGLOBALALPHAFACTORUSSUNPROC) (GLushort factor);
typedef void (APIENTRYP PFNMYGLGLOBALALPHAFACTORUISUNPROC) (GLuint factor);
#endif

#ifndef MYGL_SUN_triangle_list
#define MYGL_SUN_triangle_list 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglReplacementCodeuiSUN (GLuint);
GLAPI void APIENTRY myglReplacementCodeusSUN (GLushort);
GLAPI void APIENTRY myglReplacementCodeubSUN (GLubyte);
GLAPI void APIENTRY myglReplacementCodeuivSUN (const GLuint *);
GLAPI void APIENTRY myglReplacementCodeusvSUN (const GLushort *);
GLAPI void APIENTRY myglReplacementCodeubvSUN (const GLubyte *);
GLAPI void APIENTRY myglReplacementCodePointerSUN (GLenum, GLsizei, const GLvoid* *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLREPLACEMENTCODEUISUNPROC) (GLuint code);
typedef void (APIENTRYP PFNMYGLREPLACEMENTCODEUSSUNPROC) (GLushort code);
typedef void (APIENTRYP PFNMYGLREPLACEMENTCODEUBSUNPROC) (GLubyte code);
typedef void (APIENTRYP PFNMYGLREPLACEMENTCODEUIVSUNPROC) (const GLuint *code);
typedef void (APIENTRYP PFNMYGLREPLACEMENTCODEUSVSUNPROC) (const GLushort *code);
typedef void (APIENTRYP PFNMYGLREPLACEMENTCODEUBVSUNPROC) (const GLubyte *code);
typedef void (APIENTRYP PFNMYGLREPLACEMENTCODEPOINTERSUNPROC) (GLenum type, GLsizei stride, const GLvoid* *pointer);
#endif

#ifndef MYGL_SUN_vertex
#define MYGL_SUN_vertex 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglColor4ubVertex2fSUN (GLubyte, GLubyte, GLubyte, GLubyte, GLfloat, GLfloat);
GLAPI void APIENTRY myglColor4ubVertex2fvSUN (const GLubyte *, const GLfloat *);
GLAPI void APIENTRY myglColor4ubVertex3fSUN (GLubyte, GLubyte, GLubyte, GLubyte, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglColor4ubVertex3fvSUN (const GLubyte *, const GLfloat *);
GLAPI void APIENTRY myglColor3fVertex3fSUN (GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglColor3fVertex3fvSUN (const GLfloat *, const GLfloat *);
GLAPI void APIENTRY myglNormal3fVertex3fSUN (GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglNormal3fVertex3fvSUN (const GLfloat *, const GLfloat *);
GLAPI void APIENTRY myglColor4fNormal3fVertex3fSUN (GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglColor4fNormal3fVertex3fvSUN (const GLfloat *, const GLfloat *, const GLfloat *);
GLAPI void APIENTRY myglTexCoord2fVertex3fSUN (GLfloat, GLfloat, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglTexCoord2fVertex3fvSUN (const GLfloat *, const GLfloat *);
GLAPI void APIENTRY myglTexCoord4fVertex4fSUN (GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglTexCoord4fVertex4fvSUN (const GLfloat *, const GLfloat *);
GLAPI void APIENTRY myglTexCoord2fColor4ubVertex3fSUN (GLfloat, GLfloat, GLubyte, GLubyte, GLubyte, GLubyte, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglTexCoord2fColor4ubVertex3fvSUN (const GLfloat *, const GLubyte *, const GLfloat *);
GLAPI void APIENTRY myglTexCoord2fColor3fVertex3fSUN (GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglTexCoord2fColor3fVertex3fvSUN (const GLfloat *, const GLfloat *, const GLfloat *);
GLAPI void APIENTRY myglTexCoord2fNormal3fVertex3fSUN (GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglTexCoord2fNormal3fVertex3fvSUN (const GLfloat *, const GLfloat *, const GLfloat *);
GLAPI void APIENTRY myglTexCoord2fColor4fNormal3fVertex3fSUN (GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglTexCoord2fColor4fNormal3fVertex3fvSUN (const GLfloat *, const GLfloat *, const GLfloat *, const GLfloat *);
GLAPI void APIENTRY myglTexCoord4fColor4fNormal3fVertex4fSUN (GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglTexCoord4fColor4fNormal3fVertex4fvSUN (const GLfloat *, const GLfloat *, const GLfloat *, const GLfloat *);
GLAPI void APIENTRY myglReplacementCodeuiVertex3fSUN (GLuint, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglReplacementCodeuiVertex3fvSUN (const GLuint *, const GLfloat *);
GLAPI void APIENTRY myglReplacementCodeuiColor4ubVertex3fSUN (GLuint, GLubyte, GLubyte, GLubyte, GLubyte, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglReplacementCodeuiColor4ubVertex3fvSUN (const GLuint *, const GLubyte *, const GLfloat *);
GLAPI void APIENTRY myglReplacementCodeuiColor3fVertex3fSUN (GLuint, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglReplacementCodeuiColor3fVertex3fvSUN (const GLuint *, const GLfloat *, const GLfloat *);
GLAPI void APIENTRY myglReplacementCodeuiNormal3fVertex3fSUN (GLuint, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglReplacementCodeuiNormal3fVertex3fvSUN (const GLuint *, const GLfloat *, const GLfloat *);
GLAPI void APIENTRY myglReplacementCodeuiColor4fNormal3fVertex3fSUN (GLuint, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglReplacementCodeuiColor4fNormal3fVertex3fvSUN (const GLuint *, const GLfloat *, const GLfloat *, const GLfloat *);
GLAPI void APIENTRY myglReplacementCodeuiTexCoord2fVertex3fSUN (GLuint, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglReplacementCodeuiTexCoord2fVertex3fvSUN (const GLuint *, const GLfloat *, const GLfloat *);
GLAPI void APIENTRY myglReplacementCodeuiTexCoord2fNormal3fVertex3fSUN (GLuint, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglReplacementCodeuiTexCoord2fNormal3fVertex3fvSUN (const GLuint *, const GLfloat *, const GLfloat *, const GLfloat *);
GLAPI void APIENTRY myglReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fSUN (GLuint, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fvSUN (const GLuint *, const GLfloat *, const GLfloat *, const GLfloat *, const GLfloat *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLCOLOR4UBVERTEX2FSUNPROC) (GLubyte r, GLubyte g, GLubyte b, GLubyte a, GLfloat x, GLfloat y);
typedef void (APIENTRYP PFNMYGLCOLOR4UBVERTEX2FVSUNPROC) (const GLubyte *c, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLCOLOR4UBVERTEX3FSUNPROC) (GLubyte r, GLubyte g, GLubyte b, GLubyte a, GLfloat x, GLfloat y, GLfloat z);
typedef void (APIENTRYP PFNMYGLCOLOR4UBVERTEX3FVSUNPROC) (const GLubyte *c, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLCOLOR3FVERTEX3FSUNPROC) (GLfloat r, GLfloat g, GLfloat b, GLfloat x, GLfloat y, GLfloat z);
typedef void (APIENTRYP PFNMYGLCOLOR3FVERTEX3FVSUNPROC) (const GLfloat *c, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLNORMAL3FVERTEX3FSUNPROC) (GLfloat nx, GLfloat ny, GLfloat nz, GLfloat x, GLfloat y, GLfloat z);
typedef void (APIENTRYP PFNMYGLNORMAL3FVERTEX3FVSUNPROC) (const GLfloat *n, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLCOLOR4FNORMAL3FVERTEX3FSUNPROC) (GLfloat r, GLfloat g, GLfloat b, GLfloat a, GLfloat nx, GLfloat ny, GLfloat nz, GLfloat x, GLfloat y, GLfloat z);
typedef void (APIENTRYP PFNMYGLCOLOR4FNORMAL3FVERTEX3FVSUNPROC) (const GLfloat *c, const GLfloat *n, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLTEXCOORD2FVERTEX3FSUNPROC) (GLfloat s, GLfloat t, GLfloat x, GLfloat y, GLfloat z);
typedef void (APIENTRYP PFNMYGLTEXCOORD2FVERTEX3FVSUNPROC) (const GLfloat *tc, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLTEXCOORD4FVERTEX4FSUNPROC) (GLfloat s, GLfloat t, GLfloat p, GLfloat q, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
typedef void (APIENTRYP PFNMYGLTEXCOORD4FVERTEX4FVSUNPROC) (const GLfloat *tc, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLTEXCOORD2FCOLOR4UBVERTEX3FSUNPROC) (GLfloat s, GLfloat t, GLubyte r, GLubyte g, GLubyte b, GLubyte a, GLfloat x, GLfloat y, GLfloat z);
typedef void (APIENTRYP PFNMYGLTEXCOORD2FCOLOR4UBVERTEX3FVSUNPROC) (const GLfloat *tc, const GLubyte *c, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLTEXCOORD2FCOLOR3FVERTEX3FSUNPROC) (GLfloat s, GLfloat t, GLfloat r, GLfloat g, GLfloat b, GLfloat x, GLfloat y, GLfloat z);
typedef void (APIENTRYP PFNMYGLTEXCOORD2FCOLOR3FVERTEX3FVSUNPROC) (const GLfloat *tc, const GLfloat *c, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLTEXCOORD2FNORMAL3FVERTEX3FSUNPROC) (GLfloat s, GLfloat t, GLfloat nx, GLfloat ny, GLfloat nz, GLfloat x, GLfloat y, GLfloat z);
typedef void (APIENTRYP PFNMYGLTEXCOORD2FNORMAL3FVERTEX3FVSUNPROC) (const GLfloat *tc, const GLfloat *n, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLTEXCOORD2FCOLOR4FNORMAL3FVERTEX3FSUNPROC) (GLfloat s, GLfloat t, GLfloat r, GLfloat g, GLfloat b, GLfloat a, GLfloat nx, GLfloat ny, GLfloat nz, GLfloat x, GLfloat y, GLfloat z);
typedef void (APIENTRYP PFNMYGLTEXCOORD2FCOLOR4FNORMAL3FVERTEX3FVSUNPROC) (const GLfloat *tc, const GLfloat *c, const GLfloat *n, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLTEXCOORD4FCOLOR4FNORMAL3FVERTEX4FSUNPROC) (GLfloat s, GLfloat t, GLfloat p, GLfloat q, GLfloat r, GLfloat g, GLfloat b, GLfloat a, GLfloat nx, GLfloat ny, GLfloat nz, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
typedef void (APIENTRYP PFNMYGLTEXCOORD4FCOLOR4FNORMAL3FVERTEX4FVSUNPROC) (const GLfloat *tc, const GLfloat *c, const GLfloat *n, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLREPLACEMENTCODEUIVERTEX3FSUNPROC) (GLuint rc, GLfloat x, GLfloat y, GLfloat z);
typedef void (APIENTRYP PFNMYGLREPLACEMENTCODEUIVERTEX3FVSUNPROC) (const GLuint *rc, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLREPLACEMENTCODEUICOLOR4UBVERTEX3FSUNPROC) (GLuint rc, GLubyte r, GLubyte g, GLubyte b, GLubyte a, GLfloat x, GLfloat y, GLfloat z);
typedef void (APIENTRYP PFNMYGLREPLACEMENTCODEUICOLOR4UBVERTEX3FVSUNPROC) (const GLuint *rc, const GLubyte *c, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLREPLACEMENTCODEUICOLOR3FVERTEX3FSUNPROC) (GLuint rc, GLfloat r, GLfloat g, GLfloat b, GLfloat x, GLfloat y, GLfloat z);
typedef void (APIENTRYP PFNMYGLREPLACEMENTCODEUICOLOR3FVERTEX3FVSUNPROC) (const GLuint *rc, const GLfloat *c, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLREPLACEMENTCODEUINORMAL3FVERTEX3FSUNPROC) (GLuint rc, GLfloat nx, GLfloat ny, GLfloat nz, GLfloat x, GLfloat y, GLfloat z);
typedef void (APIENTRYP PFNMYGLREPLACEMENTCODEUINORMAL3FVERTEX3FVSUNPROC) (const GLuint *rc, const GLfloat *n, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLREPLACEMENTCODEUICOLOR4FNORMAL3FVERTEX3FSUNPROC) (GLuint rc, GLfloat r, GLfloat g, GLfloat b, GLfloat a, GLfloat nx, GLfloat ny, GLfloat nz, GLfloat x, GLfloat y, GLfloat z);
typedef void (APIENTRYP PFNMYGLREPLACEMENTCODEUICOLOR4FNORMAL3FVERTEX3FVSUNPROC) (const GLuint *rc, const GLfloat *c, const GLfloat *n, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLREPLACEMENTCODEUITEXCOORD2FVERTEX3FSUNPROC) (GLuint rc, GLfloat s, GLfloat t, GLfloat x, GLfloat y, GLfloat z);
typedef void (APIENTRYP PFNMYGLREPLACEMENTCODEUITEXCOORD2FVERTEX3FVSUNPROC) (const GLuint *rc, const GLfloat *tc, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLREPLACEMENTCODEUITEXCOORD2FNORMAL3FVERTEX3FSUNPROC) (GLuint rc, GLfloat s, GLfloat t, GLfloat nx, GLfloat ny, GLfloat nz, GLfloat x, GLfloat y, GLfloat z);
typedef void (APIENTRYP PFNMYGLREPLACEMENTCODEUITEXCOORD2FNORMAL3FVERTEX3FVSUNPROC) (const GLuint *rc, const GLfloat *tc, const GLfloat *n, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLREPLACEMENTCODEUITEXCOORD2FCOLOR4FNORMAL3FVERTEX3FSUNPROC) (GLuint rc, GLfloat s, GLfloat t, GLfloat r, GLfloat g, GLfloat b, GLfloat a, GLfloat nx, GLfloat ny, GLfloat nz, GLfloat x, GLfloat y, GLfloat z);
typedef void (APIENTRYP PFNMYGLREPLACEMENTCODEUITEXCOORD2FCOLOR4FNORMAL3FVERTEX3FVSUNPROC) (const GLuint *rc, const GLfloat *tc, const GLfloat *c, const GLfloat *n, const GLfloat *v);
#endif

#ifndef MYGL_EXT_blend_func_separate
#define MYGL_EXT_blend_func_separate 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglBlendFuncSeparateEXT (GLenum, GLenum, GLenum, GLenum);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLBLENDFUNCSEPARATEEXTPROC) (GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha);
#endif

#ifndef MYGL_INGR_blend_func_separate
#define MYGL_INGR_blend_func_separate 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglBlendFuncSeparateINGR (GLenum, GLenum, GLenum, GLenum);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLBLENDFUNCSEPARATEINGRPROC) (GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha);
#endif

#ifndef MYGL_INGR_color_clamp
#define MYGL_INGR_color_clamp 1
#endif

#ifndef MYGL_INGR_interlace_read
#define MYGL_INGR_interlace_read 1
#endif

#ifndef MYGL_EXT_stencil_wrap
#define MYGL_EXT_stencil_wrap 1
#endif

#ifndef MYGL_EXT_422_pixels
#define MYGL_EXT_422_pixels 1
#endif

#ifndef MYGL_NV_texgen_reflection
#define MYGL_NV_texgen_reflection 1
#endif

#ifndef MYGL_SUN_convolution_border_modes
#define MYGL_SUN_convolution_border_modes 1
#endif

#ifndef MYGL_EXT_texture_env_add
#define MYGL_EXT_texture_env_add 1
#endif

#ifndef MYGL_EXT_texture_lod_bias
#define MYGL_EXT_texture_lod_bias 1
#endif

#ifndef MYGL_EXT_texture_filter_anisotropic
#define MYGL_EXT_texture_filter_anisotropic 1
#endif

#ifndef MYGL_EXT_vertex_weighting
#define MYGL_EXT_vertex_weighting 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglVertexWeightfEXT (GLfloat);
GLAPI void APIENTRY myglVertexWeightfvEXT (const GLfloat *);
GLAPI void APIENTRY myglVertexWeightPointerEXT (GLsizei, GLenum, GLsizei, const GLvoid *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLVERTEXWEIGHTFEXTPROC) (GLfloat weight);
typedef void (APIENTRYP PFNMYGLVERTEXWEIGHTFVEXTPROC) (const GLfloat *weight);
typedef void (APIENTRYP PFNMYGLVERTEXWEIGHTPOINTEREXTPROC) (GLsizei size, GLenum type, GLsizei stride, const GLvoid *pointer);
#endif

#ifndef MYGL_NV_light_max_exponent
#define MYGL_NV_light_max_exponent 1
#endif

#ifndef MYGL_NV_vertex_array_range
#define MYGL_NV_vertex_array_range 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglFlushVertexArrayRangeNV (void);
GLAPI void APIENTRY myglVertexArrayRangeNV (GLsizei, const GLvoid *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLFLUSHVERTEXARRAYRANGENVPROC) (void);
typedef void (APIENTRYP PFNMYGLVERTEXARRAYRANGENVPROC) (GLsizei length, const GLvoid *pointer);
#endif

#ifndef MYGL_NV_register_combiners
#define MYGL_NV_register_combiners 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglCombinerParameterfvNV (GLenum, const GLfloat *);
GLAPI void APIENTRY myglCombinerParameterfNV (GLenum, GLfloat);
GLAPI void APIENTRY myglCombinerParameterivNV (GLenum, const GLint *);
GLAPI void APIENTRY myglCombinerParameteriNV (GLenum, GLint);
GLAPI void APIENTRY myglCombinerInputNV (GLenum, GLenum, GLenum, GLenum, GLenum, GLenum);
GLAPI void APIENTRY myglCombinerOutputNV (GLenum, GLenum, GLenum, GLenum, GLenum, GLenum, GLenum, GLboolean, GLboolean, GLboolean);
GLAPI void APIENTRY myglFinalCombinerInputNV (GLenum, GLenum, GLenum, GLenum);
GLAPI void APIENTRY myglGetCombinerInputParameterfvNV (GLenum, GLenum, GLenum, GLenum, GLfloat *);
GLAPI void APIENTRY myglGetCombinerInputParameterivNV (GLenum, GLenum, GLenum, GLenum, GLint *);
GLAPI void APIENTRY myglGetCombinerOutputParameterfvNV (GLenum, GLenum, GLenum, GLfloat *);
GLAPI void APIENTRY myglGetCombinerOutputParameterivNV (GLenum, GLenum, GLenum, GLint *);
GLAPI void APIENTRY myglGetFinalCombinerInputParameterfvNV (GLenum, GLenum, GLfloat *);
GLAPI void APIENTRY myglGetFinalCombinerInputParameterivNV (GLenum, GLenum, GLint *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLCOMBINERPARAMETERFVNVPROC) (GLenum pname, const GLfloat *params);
typedef void (APIENTRYP PFNMYGLCOMBINERPARAMETERFNVPROC) (GLenum pname, GLfloat param);
typedef void (APIENTRYP PFNMYGLCOMBINERPARAMETERIVNVPROC) (GLenum pname, const GLint *params);
typedef void (APIENTRYP PFNMYGLCOMBINERPARAMETERINVPROC) (GLenum pname, GLint param);
typedef void (APIENTRYP PFNMYGLCOMBINERINPUTNVPROC) (GLenum stage, GLenum portion, GLenum variable, GLenum input, GLenum mapping, GLenum componentUsage);
typedef void (APIENTRYP PFNMYGLCOMBINEROUTPUTNVPROC) (GLenum stage, GLenum portion, GLenum abOutput, GLenum cdOutput, GLenum sumOutput, GLenum scale, GLenum bias, GLboolean abDotProduct, GLboolean cdDotProduct, GLboolean muxSum);
typedef void (APIENTRYP PFNMYGLFINALCOMBINERINPUTNVPROC) (GLenum variable, GLenum input, GLenum mapping, GLenum componentUsage);
typedef void (APIENTRYP PFNMYGLGETCOMBINERINPUTPARAMETERFVNVPROC) (GLenum stage, GLenum portion, GLenum variable, GLenum pname, GLfloat *params);
typedef void (APIENTRYP PFNMYGLGETCOMBINERINPUTPARAMETERIVNVPROC) (GLenum stage, GLenum portion, GLenum variable, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLGETCOMBINEROUTPUTPARAMETERFVNVPROC) (GLenum stage, GLenum portion, GLenum pname, GLfloat *params);
typedef void (APIENTRYP PFNMYGLGETCOMBINEROUTPUTPARAMETERIVNVPROC) (GLenum stage, GLenum portion, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLGETFINALCOMBINERINPUTPARAMETERFVNVPROC) (GLenum variable, GLenum pname, GLfloat *params);
typedef void (APIENTRYP PFNMYGLGETFINALCOMBINERINPUTPARAMETERIVNVPROC) (GLenum variable, GLenum pname, GLint *params);
#endif

#ifndef MYGL_NV_fog_distance
#define MYGL_NV_fog_distance 1
#endif

#ifndef MYGL_NV_texgen_emboss
#define MYGL_NV_texgen_emboss 1
#endif

#ifndef MYGL_NV_blend_square
#define MYGL_NV_blend_square 1
#endif

#ifndef MYGL_NV_texture_env_combine4
#define MYGL_NV_texture_env_combine4 1
#endif

#ifndef MYGL_MESA_resize_buffers
#define MYGL_MESA_resize_buffers 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglResizeBuffersMESA (void);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLRESIZEBUFFERSMESAPROC) (void);
#endif

#ifndef MYGL_MESA_window_pos
#define MYGL_MESA_window_pos 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglWindowPos2dMESA (GLdouble, GLdouble);
GLAPI void APIENTRY myglWindowPos2dvMESA (const GLdouble *);
GLAPI void APIENTRY myglWindowPos2fMESA (GLfloat, GLfloat);
GLAPI void APIENTRY myglWindowPos2fvMESA (const GLfloat *);
GLAPI void APIENTRY myglWindowPos2iMESA (GLint, GLint);
GLAPI void APIENTRY myglWindowPos2ivMESA (const GLint *);
GLAPI void APIENTRY myglWindowPos2sMESA (GLshort, GLshort);
GLAPI void APIENTRY myglWindowPos2svMESA (const GLshort *);
GLAPI void APIENTRY myglWindowPos3dMESA (GLdouble, GLdouble, GLdouble);
GLAPI void APIENTRY myglWindowPos3dvMESA (const GLdouble *);
GLAPI void APIENTRY myglWindowPos3fMESA (GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglWindowPos3fvMESA (const GLfloat *);
GLAPI void APIENTRY myglWindowPos3iMESA (GLint, GLint, GLint);
GLAPI void APIENTRY myglWindowPos3ivMESA (const GLint *);
GLAPI void APIENTRY myglWindowPos3sMESA (GLshort, GLshort, GLshort);
GLAPI void APIENTRY myglWindowPos3svMESA (const GLshort *);
GLAPI void APIENTRY myglWindowPos4dMESA (GLdouble, GLdouble, GLdouble, GLdouble);
GLAPI void APIENTRY myglWindowPos4dvMESA (const GLdouble *);
GLAPI void APIENTRY myglWindowPos4fMESA (GLfloat, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglWindowPos4fvMESA (const GLfloat *);
GLAPI void APIENTRY myglWindowPos4iMESA (GLint, GLint, GLint, GLint);
GLAPI void APIENTRY myglWindowPos4ivMESA (const GLint *);
GLAPI void APIENTRY myglWindowPos4sMESA (GLshort, GLshort, GLshort, GLshort);
GLAPI void APIENTRY myglWindowPos4svMESA (const GLshort *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLWINDOWPOS2DMESAPROC) (GLdouble x, GLdouble y);
typedef void (APIENTRYP PFNMYGLWINDOWPOS2DVMESAPROC) (const GLdouble *v);
typedef void (APIENTRYP PFNMYGLWINDOWPOS2FMESAPROC) (GLfloat x, GLfloat y);
typedef void (APIENTRYP PFNMYGLWINDOWPOS2FVMESAPROC) (const GLfloat *v);
typedef void (APIENTRYP PFNMYGLWINDOWPOS2IMESAPROC) (GLint x, GLint y);
typedef void (APIENTRYP PFNMYGLWINDOWPOS2IVMESAPROC) (const GLint *v);
typedef void (APIENTRYP PFNMYGLWINDOWPOS2SMESAPROC) (GLshort x, GLshort y);
typedef void (APIENTRYP PFNMYGLWINDOWPOS2SVMESAPROC) (const GLshort *v);
typedef void (APIENTRYP PFNMYGLWINDOWPOS3DMESAPROC) (GLdouble x, GLdouble y, GLdouble z);
typedef void (APIENTRYP PFNMYGLWINDOWPOS3DVMESAPROC) (const GLdouble *v);
typedef void (APIENTRYP PFNMYGLWINDOWPOS3FMESAPROC) (GLfloat x, GLfloat y, GLfloat z);
typedef void (APIENTRYP PFNMYGLWINDOWPOS3FVMESAPROC) (const GLfloat *v);
typedef void (APIENTRYP PFNMYGLWINDOWPOS3IMESAPROC) (GLint x, GLint y, GLint z);
typedef void (APIENTRYP PFNMYGLWINDOWPOS3IVMESAPROC) (const GLint *v);
typedef void (APIENTRYP PFNMYGLWINDOWPOS3SMESAPROC) (GLshort x, GLshort y, GLshort z);
typedef void (APIENTRYP PFNMYGLWINDOWPOS3SVMESAPROC) (const GLshort *v);
typedef void (APIENTRYP PFNMYGLWINDOWPOS4DMESAPROC) (GLdouble x, GLdouble y, GLdouble z, GLdouble w);
typedef void (APIENTRYP PFNMYGLWINDOWPOS4DVMESAPROC) (const GLdouble *v);
typedef void (APIENTRYP PFNMYGLWINDOWPOS4FMESAPROC) (GLfloat x, GLfloat y, GLfloat z, GLfloat w);
typedef void (APIENTRYP PFNMYGLWINDOWPOS4FVMESAPROC) (const GLfloat *v);
typedef void (APIENTRYP PFNMYGLWINDOWPOS4IMESAPROC) (GLint x, GLint y, GLint z, GLint w);
typedef void (APIENTRYP PFNMYGLWINDOWPOS4IVMESAPROC) (const GLint *v);
typedef void (APIENTRYP PFNMYGLWINDOWPOS4SMESAPROC) (GLshort x, GLshort y, GLshort z, GLshort w);
typedef void (APIENTRYP PFNMYGLWINDOWPOS4SVMESAPROC) (const GLshort *v);
#endif

#ifndef MYGL_IBM_cull_vertex
#define MYGL_IBM_cull_vertex 1
#endif

#ifndef MYGL_IBM_multimode_draw_arrays
#define MYGL_IBM_multimode_draw_arrays 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglMultiModeDrawArraysIBM (const GLenum *, const GLint *, const GLsizei *, GLsizei, GLint);
GLAPI void APIENTRY myglMultiModeDrawElementsIBM (const GLenum *, const GLsizei *, GLenum, const GLvoid* const *, GLsizei, GLint);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLMULTIMODEDRAWARRAYSIBMPROC) (const GLenum *mode, const GLint *first, const GLsizei *count, GLsizei primcount, GLint modestride);
typedef void (APIENTRYP PFNMYGLMULTIMODEDRAWELEMENTSIBMPROC) (const GLenum *mode, const GLsizei *count, GLenum type, const GLvoid* const *indices, GLsizei primcount, GLint modestride);
#endif

#ifndef MYGL_IBM_vertex_array_lists
#define MYGL_IBM_vertex_array_lists 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglColorPointerListIBM (GLint, GLenum, GLint, const GLvoid* *, GLint);
GLAPI void APIENTRY myglSecondaryColorPointerListIBM (GLint, GLenum, GLint, const GLvoid* *, GLint);
GLAPI void APIENTRY myglEdgeFlagPointerListIBM (GLint, const GLboolean* *, GLint);
GLAPI void APIENTRY myglFogCoordPointerListIBM (GLenum, GLint, const GLvoid* *, GLint);
GLAPI void APIENTRY myglIndexPointerListIBM (GLenum, GLint, const GLvoid* *, GLint);
GLAPI void APIENTRY myglNormalPointerListIBM (GLenum, GLint, const GLvoid* *, GLint);
GLAPI void APIENTRY myglTexCoordPointerListIBM (GLint, GLenum, GLint, const GLvoid* *, GLint);
GLAPI void APIENTRY myglVertexPointerListIBM (GLint, GLenum, GLint, const GLvoid* *, GLint);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLCOLORPOINTERLISTIBMPROC) (GLint size, GLenum type, GLint stride, const GLvoid* *pointer, GLint ptrstride);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLORPOINTERLISTIBMPROC) (GLint size, GLenum type, GLint stride, const GLvoid* *pointer, GLint ptrstride);
typedef void (APIENTRYP PFNMYGLEDGEFLAGPOINTERLISTIBMPROC) (GLint stride, const GLboolean* *pointer, GLint ptrstride);
typedef void (APIENTRYP PFNMYGLFOGCOORDPOINTERLISTIBMPROC) (GLenum type, GLint stride, const GLvoid* *pointer, GLint ptrstride);
typedef void (APIENTRYP PFNMYGLINDEXPOINTERLISTIBMPROC) (GLenum type, GLint stride, const GLvoid* *pointer, GLint ptrstride);
typedef void (APIENTRYP PFNMYGLNORMALPOINTERLISTIBMPROC) (GLenum type, GLint stride, const GLvoid* *pointer, GLint ptrstride);
typedef void (APIENTRYP PFNMYGLTEXCOORDPOINTERLISTIBMPROC) (GLint size, GLenum type, GLint stride, const GLvoid* *pointer, GLint ptrstride);
typedef void (APIENTRYP PFNMYGLVERTEXPOINTERLISTIBMPROC) (GLint size, GLenum type, GLint stride, const GLvoid* *pointer, GLint ptrstride);
#endif

#ifndef MYGL_SGIX_subsample
#define MYGL_SGIX_subsample 1
#endif

#ifndef MYGL_SGIX_ycrcba
#define MYGL_SGIX_ycrcba 1
#endif

#ifndef MYGL_SGIX_ycrcb_subsample
#define MYGL_SGIX_ycrcb_subsample 1
#endif

#ifndef MYGL_SGIX_depth_pass_instrument
#define MYGL_SGIX_depth_pass_instrument 1
#endif

#ifndef MYGL_3DFX_texture_compression_FXT1
#define MYGL_3DFX_texture_compression_FXT1 1
#endif

#ifndef MYGL_3DFX_multisample
#define MYGL_3DFX_multisample 1
#endif

#ifndef MYGL_3DFX_tbuffer
#define MYGL_3DFX_tbuffer 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglTbufferMask3DFX (GLuint);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLTBUFFERMASK3DFXPROC) (GLuint mask);
#endif

#ifndef MYGL_EXT_multisample
#define MYGL_EXT_multisample 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglSampleMaskEXT (GLclampf, GLboolean);
GLAPI void APIENTRY myglSamplePatternEXT (GLenum);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLSAMPLEMASKEXTPROC) (GLclampf value, GLboolean invert);
typedef void (APIENTRYP PFNMYGLSAMPLEPATTERNEXTPROC) (GLenum pattern);
#endif

#ifndef MYGL_SGIX_vertex_preclip
#define MYGL_SGIX_vertex_preclip 1
#endif

#ifndef MYGL_SGIX_convolution_accuracy
#define MYGL_SGIX_convolution_accuracy 1
#endif

#ifndef MYGL_SGIX_resample
#define MYGL_SGIX_resample 1
#endif

#ifndef MYGL_SGIS_point_line_texgen
#define MYGL_SGIS_point_line_texgen 1
#endif

#ifndef MYGL_SGIS_texture_color_mask
#define MYGL_SGIS_texture_color_mask 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglTextureColorMaskSGIS (GLboolean, GLboolean, GLboolean, GLboolean);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLTEXTURECOLORMASKSGISPROC) (GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
#endif

#ifndef MYGL_SGIX_igloo_interface
#define MYGL_SGIX_igloo_interface 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglIglooInterfaceSGIX (GLenum, const GLvoid *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLIGLOOINTERFACESGIXPROC) (GLenum pname, const GLvoid *params);
#endif

#ifndef MYGL_EXT_texture_env_dot3
#define MYGL_EXT_texture_env_dot3 1
#endif

#ifndef MYGL_ATI_texture_mirror_once
#define MYGL_ATI_texture_mirror_once 1
#endif

#ifndef MYGL_NV_fence
#define MYGL_NV_fence 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglDeleteFencesNV (GLsizei, const GLuint *);
GLAPI void APIENTRY myglGenFencesNV (GLsizei, GLuint *);
GLAPI GLboolean APIENTRY myglIsFenceNV (GLuint);
GLAPI GLboolean APIENTRY myglTestFenceNV (GLuint);
GLAPI void APIENTRY myglGetFenceivNV (GLuint, GLenum, GLint *);
GLAPI void APIENTRY myglFinishFenceNV (GLuint);
GLAPI void APIENTRY myglSetFenceNV (GLuint, GLenum);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLDELETEFENCESNVPROC) (GLsizei n, const GLuint *fences);
typedef void (APIENTRYP PFNMYGLGENFENCESNVPROC) (GLsizei n, GLuint *fences);
typedef GLboolean (APIENTRYP PFNMYGLISFENCENVPROC) (GLuint fence);
typedef GLboolean (APIENTRYP PFNMYGLTESTFENCENVPROC) (GLuint fence);
typedef void (APIENTRYP PFNMYGLGETFENCEIVNVPROC) (GLuint fence, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLFINISHFENCENVPROC) (GLuint fence);
typedef void (APIENTRYP PFNMYGLSETFENCENVPROC) (GLuint fence, GLenum condition);
#endif

#ifndef MYGL_NV_evaluators
#define MYGL_NV_evaluators 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglMapControlPointsNV (GLenum, GLuint, GLenum, GLsizei, GLsizei, GLint, GLint, GLboolean, const GLvoid *);
GLAPI void APIENTRY myglMapParameterivNV (GLenum, GLenum, const GLint *);
GLAPI void APIENTRY myglMapParameterfvNV (GLenum, GLenum, const GLfloat *);
GLAPI void APIENTRY myglGetMapControlPointsNV (GLenum, GLuint, GLenum, GLsizei, GLsizei, GLboolean, GLvoid *);
GLAPI void APIENTRY myglGetMapParameterivNV (GLenum, GLenum, GLint *);
GLAPI void APIENTRY myglGetMapParameterfvNV (GLenum, GLenum, GLfloat *);
GLAPI void APIENTRY myglGetMapAttribParameterivNV (GLenum, GLuint, GLenum, GLint *);
GLAPI void APIENTRY myglGetMapAttribParameterfvNV (GLenum, GLuint, GLenum, GLfloat *);
GLAPI void APIENTRY myglEvalMapsNV (GLenum, GLenum);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLMAPCONTROLPOINTSNVPROC) (GLenum target, GLuint index, GLenum type, GLsizei ustride, GLsizei vstride, GLint uorder, GLint vorder, GLboolean packed, const GLvoid *points);
typedef void (APIENTRYP PFNMYGLMAPPARAMETERIVNVPROC) (GLenum target, GLenum pname, const GLint *params);
typedef void (APIENTRYP PFNMYGLMAPPARAMETERFVNVPROC) (GLenum target, GLenum pname, const GLfloat *params);
typedef void (APIENTRYP PFNMYGLGETMAPCONTROLPOINTSNVPROC) (GLenum target, GLuint index, GLenum type, GLsizei ustride, GLsizei vstride, GLboolean packed, GLvoid *points);
typedef void (APIENTRYP PFNMYGLGETMAPPARAMETERIVNVPROC) (GLenum target, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLGETMAPPARAMETERFVNVPROC) (GLenum target, GLenum pname, GLfloat *params);
typedef void (APIENTRYP PFNMYGLGETMAPATTRIBPARAMETERIVNVPROC) (GLenum target, GLuint index, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLGETMAPATTRIBPARAMETERFVNVPROC) (GLenum target, GLuint index, GLenum pname, GLfloat *params);
typedef void (APIENTRYP PFNMYGLEVALMAPSNVPROC) (GLenum target, GLenum mode);
#endif

#ifndef MYGL_NV_packed_depth_stencil
#define MYGL_NV_packed_depth_stencil 1
#endif

#ifndef MYGL_NV_register_combiners2
#define MYGL_NV_register_combiners2 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglCombinerStageParameterfvNV (GLenum, GLenum, const GLfloat *);
GLAPI void APIENTRY myglGetCombinerStageParameterfvNV (GLenum, GLenum, GLfloat *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLCOMBINERSTAGEPARAMETERFVNVPROC) (GLenum stage, GLenum pname, const GLfloat *params);
typedef void (APIENTRYP PFNMYGLGETCOMBINERSTAGEPARAMETERFVNVPROC) (GLenum stage, GLenum pname, GLfloat *params);
#endif

#ifndef MYGL_NV_texture_compression_vtc
#define MYGL_NV_texture_compression_vtc 1
#endif

#ifndef MYGL_NV_texture_rectangle
#define MYGL_NV_texture_rectangle 1
#endif

#ifndef MYGL_NV_texture_shader
#define MYGL_NV_texture_shader 1
#endif

#ifndef MYGL_NV_texture_shader2
#define MYGL_NV_texture_shader2 1
#endif

#ifndef MYGL_NV_vertex_array_range2
#define MYGL_NV_vertex_array_range2 1
#endif

#ifndef MYGL_NV_vertex_program
#define MYGL_NV_vertex_program 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI GLboolean APIENTRY myglAreProgramsResidentNV (GLsizei, const GLuint *, GLboolean *);
GLAPI void APIENTRY myglBindProgramNV (GLenum, GLuint);
GLAPI void APIENTRY myglDeleteProgramsNV (GLsizei, const GLuint *);
GLAPI void APIENTRY myglExecuteProgramNV (GLenum, GLuint, const GLfloat *);
GLAPI void APIENTRY myglGenProgramsNV (GLsizei, GLuint *);
GLAPI void APIENTRY myglGetProgramParameterdvNV (GLenum, GLuint, GLenum, GLdouble *);
GLAPI void APIENTRY myglGetProgramParameterfvNV (GLenum, GLuint, GLenum, GLfloat *);
GLAPI void APIENTRY myglGetProgramivNV (GLuint, GLenum, GLint *);
GLAPI void APIENTRY myglGetProgramStringNV (GLuint, GLenum, GLubyte *);
GLAPI void APIENTRY myglGetTrackMatrixivNV (GLenum, GLuint, GLenum, GLint *);
GLAPI void APIENTRY myglGetVertexAttribdvNV (GLuint, GLenum, GLdouble *);
GLAPI void APIENTRY myglGetVertexAttribfvNV (GLuint, GLenum, GLfloat *);
GLAPI void APIENTRY myglGetVertexAttribivNV (GLuint, GLenum, GLint *);
GLAPI void APIENTRY myglGetVertexAttribPointervNV (GLuint, GLenum, GLvoid* *);
GLAPI GLboolean APIENTRY myglIsProgramNV (GLuint);
GLAPI void APIENTRY myglLoadProgramNV (GLenum, GLuint, GLsizei, const GLubyte *);
GLAPI void APIENTRY myglProgramParameter4dNV (GLenum, GLuint, GLdouble, GLdouble, GLdouble, GLdouble);
GLAPI void APIENTRY myglProgramParameter4dvNV (GLenum, GLuint, const GLdouble *);
GLAPI void APIENTRY myglProgramParameter4fNV (GLenum, GLuint, GLfloat, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglProgramParameter4fvNV (GLenum, GLuint, const GLfloat *);
GLAPI void APIENTRY myglProgramParameters4dvNV (GLenum, GLuint, GLuint, const GLdouble *);
GLAPI void APIENTRY myglProgramParameters4fvNV (GLenum, GLuint, GLuint, const GLfloat *);
GLAPI void APIENTRY myglRequestResidentProgramsNV (GLsizei, const GLuint *);
GLAPI void APIENTRY myglTrackMatrixNV (GLenum, GLuint, GLenum, GLenum);
GLAPI void APIENTRY myglVertexAttribPointerNV (GLuint, GLint, GLenum, GLsizei, const GLvoid *);
GLAPI void APIENTRY myglVertexAttrib1dNV (GLuint, GLdouble);
GLAPI void APIENTRY myglVertexAttrib1dvNV (GLuint, const GLdouble *);
GLAPI void APIENTRY myglVertexAttrib1fNV (GLuint, GLfloat);
GLAPI void APIENTRY myglVertexAttrib1fvNV (GLuint, const GLfloat *);
GLAPI void APIENTRY myglVertexAttrib1sNV (GLuint, GLshort);
GLAPI void APIENTRY myglVertexAttrib1svNV (GLuint, const GLshort *);
GLAPI void APIENTRY myglVertexAttrib2dNV (GLuint, GLdouble, GLdouble);
GLAPI void APIENTRY myglVertexAttrib2dvNV (GLuint, const GLdouble *);
GLAPI void APIENTRY myglVertexAttrib2fNV (GLuint, GLfloat, GLfloat);
GLAPI void APIENTRY myglVertexAttrib2fvNV (GLuint, const GLfloat *);
GLAPI void APIENTRY myglVertexAttrib2sNV (GLuint, GLshort, GLshort);
GLAPI void APIENTRY myglVertexAttrib2svNV (GLuint, const GLshort *);
GLAPI void APIENTRY myglVertexAttrib3dNV (GLuint, GLdouble, GLdouble, GLdouble);
GLAPI void APIENTRY myglVertexAttrib3dvNV (GLuint, const GLdouble *);
GLAPI void APIENTRY myglVertexAttrib3fNV (GLuint, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglVertexAttrib3fvNV (GLuint, const GLfloat *);
GLAPI void APIENTRY myglVertexAttrib3sNV (GLuint, GLshort, GLshort, GLshort);
GLAPI void APIENTRY myglVertexAttrib3svNV (GLuint, const GLshort *);
GLAPI void APIENTRY myglVertexAttrib4dNV (GLuint, GLdouble, GLdouble, GLdouble, GLdouble);
GLAPI void APIENTRY myglVertexAttrib4dvNV (GLuint, const GLdouble *);
GLAPI void APIENTRY myglVertexAttrib4fNV (GLuint, GLfloat, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglVertexAttrib4fvNV (GLuint, const GLfloat *);
GLAPI void APIENTRY myglVertexAttrib4sNV (GLuint, GLshort, GLshort, GLshort, GLshort);
GLAPI void APIENTRY myglVertexAttrib4svNV (GLuint, const GLshort *);
GLAPI void APIENTRY myglVertexAttrib4ubNV (GLuint, GLubyte, GLubyte, GLubyte, GLubyte);
GLAPI void APIENTRY myglVertexAttrib4ubvNV (GLuint, const GLubyte *);
GLAPI void APIENTRY myglVertexAttribs1dvNV (GLuint, GLsizei, const GLdouble *);
GLAPI void APIENTRY myglVertexAttribs1fvNV (GLuint, GLsizei, const GLfloat *);
GLAPI void APIENTRY myglVertexAttribs1svNV (GLuint, GLsizei, const GLshort *);
GLAPI void APIENTRY myglVertexAttribs2dvNV (GLuint, GLsizei, const GLdouble *);
GLAPI void APIENTRY myglVertexAttribs2fvNV (GLuint, GLsizei, const GLfloat *);
GLAPI void APIENTRY myglVertexAttribs2svNV (GLuint, GLsizei, const GLshort *);
GLAPI void APIENTRY myglVertexAttribs3dvNV (GLuint, GLsizei, const GLdouble *);
GLAPI void APIENTRY myglVertexAttribs3fvNV (GLuint, GLsizei, const GLfloat *);
GLAPI void APIENTRY myglVertexAttribs3svNV (GLuint, GLsizei, const GLshort *);
GLAPI void APIENTRY myglVertexAttribs4dvNV (GLuint, GLsizei, const GLdouble *);
GLAPI void APIENTRY myglVertexAttribs4fvNV (GLuint, GLsizei, const GLfloat *);
GLAPI void APIENTRY myglVertexAttribs4svNV (GLuint, GLsizei, const GLshort *);
GLAPI void APIENTRY myglVertexAttribs4ubvNV (GLuint, GLsizei, const GLubyte *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef GLboolean (APIENTRYP PFNMYGLAREPROGRAMSRESIDENTNVPROC) (GLsizei n, const GLuint *programs, GLboolean *residences);
typedef void (APIENTRYP PFNMYGLBINDPROGRAMNVPROC) (GLenum target, GLuint id);
typedef void (APIENTRYP PFNMYGLDELETEPROGRAMSNVPROC) (GLsizei n, const GLuint *programs);
typedef void (APIENTRYP PFNMYGLEXECUTEPROGRAMNVPROC) (GLenum target, GLuint id, const GLfloat *params);
typedef void (APIENTRYP PFNMYGLGENPROGRAMSNVPROC) (GLsizei n, GLuint *programs);
typedef void (APIENTRYP PFNMYGLGETPROGRAMPARAMETERDVNVPROC) (GLenum target, GLuint index, GLenum pname, GLdouble *params);
typedef void (APIENTRYP PFNMYGLGETPROGRAMPARAMETERFVNVPROC) (GLenum target, GLuint index, GLenum pname, GLfloat *params);
typedef void (APIENTRYP PFNMYGLGETPROGRAMIVNVPROC) (GLuint id, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLGETPROGRAMSTRINGNVPROC) (GLuint id, GLenum pname, GLubyte *program);
typedef void (APIENTRYP PFNMYGLGETTRACKMATRIXIVNVPROC) (GLenum target, GLuint address, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLGETVERTEXATTRIBDVNVPROC) (GLuint index, GLenum pname, GLdouble *params);
typedef void (APIENTRYP PFNMYGLGETVERTEXATTRIBFVNVPROC) (GLuint index, GLenum pname, GLfloat *params);
typedef void (APIENTRYP PFNMYGLGETVERTEXATTRIBIVNVPROC) (GLuint index, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLGETVERTEXATTRIBPOINTERVNVPROC) (GLuint index, GLenum pname, GLvoid* *pointer);
typedef GLboolean (APIENTRYP PFNMYGLISPROGRAMNVPROC) (GLuint id);
typedef void (APIENTRYP PFNMYGLLOADPROGRAMNVPROC) (GLenum target, GLuint id, GLsizei len, const GLubyte *program);
typedef void (APIENTRYP PFNMYGLPROGRAMPARAMETER4DNVPROC) (GLenum target, GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w);
typedef void (APIENTRYP PFNMYGLPROGRAMPARAMETER4DVNVPROC) (GLenum target, GLuint index, const GLdouble *v);
typedef void (APIENTRYP PFNMYGLPROGRAMPARAMETER4FNVPROC) (GLenum target, GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
typedef void (APIENTRYP PFNMYGLPROGRAMPARAMETER4FVNVPROC) (GLenum target, GLuint index, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLPROGRAMPARAMETERS4DVNVPROC) (GLenum target, GLuint index, GLuint count, const GLdouble *v);
typedef void (APIENTRYP PFNMYGLPROGRAMPARAMETERS4FVNVPROC) (GLenum target, GLuint index, GLuint count, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLREQUESTRESIDENTPROGRAMSNVPROC) (GLsizei n, const GLuint *programs);
typedef void (APIENTRYP PFNMYGLTRACKMATRIXNVPROC) (GLenum target, GLuint address, GLenum matrix, GLenum transform);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIBPOINTERNVPROC) (GLuint index, GLint fsize, GLenum type, GLsizei stride, const GLvoid *pointer);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB1DNVPROC) (GLuint index, GLdouble x);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB1DVNVPROC) (GLuint index, const GLdouble *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB1FNVPROC) (GLuint index, GLfloat x);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB1FVNVPROC) (GLuint index, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB1SNVPROC) (GLuint index, GLshort x);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB1SVNVPROC) (GLuint index, const GLshort *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB2DNVPROC) (GLuint index, GLdouble x, GLdouble y);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB2DVNVPROC) (GLuint index, const GLdouble *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB2FNVPROC) (GLuint index, GLfloat x, GLfloat y);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB2FVNVPROC) (GLuint index, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB2SNVPROC) (GLuint index, GLshort x, GLshort y);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB2SVNVPROC) (GLuint index, const GLshort *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB3DNVPROC) (GLuint index, GLdouble x, GLdouble y, GLdouble z);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB3DVNVPROC) (GLuint index, const GLdouble *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB3FNVPROC) (GLuint index, GLfloat x, GLfloat y, GLfloat z);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB3FVNVPROC) (GLuint index, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB3SNVPROC) (GLuint index, GLshort x, GLshort y, GLshort z);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB3SVNVPROC) (GLuint index, const GLshort *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4DNVPROC) (GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4DVNVPROC) (GLuint index, const GLdouble *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4FNVPROC) (GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4FVNVPROC) (GLuint index, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4SNVPROC) (GLuint index, GLshort x, GLshort y, GLshort z, GLshort w);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4SVNVPROC) (GLuint index, const GLshort *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4UBNVPROC) (GLuint index, GLubyte x, GLubyte y, GLubyte z, GLubyte w);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4UBVNVPROC) (GLuint index, const GLubyte *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIBS1DVNVPROC) (GLuint index, GLsizei count, const GLdouble *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIBS1FVNVPROC) (GLuint index, GLsizei count, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIBS1SVNVPROC) (GLuint index, GLsizei count, const GLshort *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIBS2DVNVPROC) (GLuint index, GLsizei count, const GLdouble *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIBS2FVNVPROC) (GLuint index, GLsizei count, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIBS2SVNVPROC) (GLuint index, GLsizei count, const GLshort *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIBS3DVNVPROC) (GLuint index, GLsizei count, const GLdouble *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIBS3FVNVPROC) (GLuint index, GLsizei count, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIBS3SVNVPROC) (GLuint index, GLsizei count, const GLshort *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIBS4DVNVPROC) (GLuint index, GLsizei count, const GLdouble *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIBS4FVNVPROC) (GLuint index, GLsizei count, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIBS4SVNVPROC) (GLuint index, GLsizei count, const GLshort *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIBS4UBVNVPROC) (GLuint index, GLsizei count, const GLubyte *v);
#endif

#ifndef MYGL_SGIX_texture_coordinate_clamp
#define MYGL_SGIX_texture_coordinate_clamp 1
#endif

#ifndef MYGL_SGIX_scalebias_hint
#define MYGL_SGIX_scalebias_hint 1
#endif

#ifndef MYGL_OML_interlace
#define MYGL_OML_interlace 1
#endif

#ifndef MYGL_OML_subsample
#define MYGL_OML_subsample 1
#endif

#ifndef MYGL_OML_resample
#define MYGL_OML_resample 1
#endif

#ifndef MYGL_NV_copy_depth_to_color
#define MYGL_NV_copy_depth_to_color 1
#endif

#ifndef MYGL_ATI_envmap_bumpmap
#define MYGL_ATI_envmap_bumpmap 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglTexBumpParameterivATI (GLenum, const GLint *);
GLAPI void APIENTRY myglTexBumpParameterfvATI (GLenum, const GLfloat *);
GLAPI void APIENTRY myglGetTexBumpParameterivATI (GLenum, GLint *);
GLAPI void APIENTRY myglGetTexBumpParameterfvATI (GLenum, GLfloat *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLTEXBUMPPARAMETERIVATIPROC) (GLenum pname, const GLint *param);
typedef void (APIENTRYP PFNMYGLTEXBUMPPARAMETERFVATIPROC) (GLenum pname, const GLfloat *param);
typedef void (APIENTRYP PFNMYGLGETTEXBUMPPARAMETERIVATIPROC) (GLenum pname, GLint *param);
typedef void (APIENTRYP PFNMYGLGETTEXBUMPPARAMETERFVATIPROC) (GLenum pname, GLfloat *param);
#endif

#ifndef MYGL_ATI_fragment_shader
#define MYGL_ATI_fragment_shader 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI GLuint APIENTRY myglGenFragmentShadersATI (GLuint);
GLAPI void APIENTRY myglBindFragmentShaderATI (GLuint);
GLAPI void APIENTRY myglDeleteFragmentShaderATI (GLuint);
GLAPI void APIENTRY myglBeginFragmentShaderATI (void);
GLAPI void APIENTRY myglEndFragmentShaderATI (void);
GLAPI void APIENTRY myglPassTexCoordATI (GLuint, GLuint, GLenum);
GLAPI void APIENTRY myglSampleMapATI (GLuint, GLuint, GLenum);
GLAPI void APIENTRY myglColorFragmentOp1ATI (GLenum, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint);
GLAPI void APIENTRY myglColorFragmentOp2ATI (GLenum, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint);
GLAPI void APIENTRY myglColorFragmentOp3ATI (GLenum, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint);
GLAPI void APIENTRY myglAlphaFragmentOp1ATI (GLenum, GLuint, GLuint, GLuint, GLuint, GLuint);
GLAPI void APIENTRY myglAlphaFragmentOp2ATI (GLenum, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint);
GLAPI void APIENTRY myglAlphaFragmentOp3ATI (GLenum, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint, GLuint);
GLAPI void APIENTRY myglSetFragmentShaderConstantATI (GLuint, const GLfloat *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef GLuint (APIENTRYP PFNMYGLGENFRAGMENTSHADERSATIPROC) (GLuint range);
typedef void (APIENTRYP PFNMYGLBINDFRAGMENTSHADERATIPROC) (GLuint id);
typedef void (APIENTRYP PFNMYGLDELETEFRAGMENTSHADERATIPROC) (GLuint id);
typedef void (APIENTRYP PFNMYGLBEGINFRAGMENTSHADERATIPROC) (void);
typedef void (APIENTRYP PFNMYGLENDFRAGMENTSHADERATIPROC) (void);
typedef void (APIENTRYP PFNMYGLPASSTEXCOORDATIPROC) (GLuint dst, GLuint coord, GLenum swizzle);
typedef void (APIENTRYP PFNMYGLSAMPLEMAPATIPROC) (GLuint dst, GLuint interp, GLenum swizzle);
typedef void (APIENTRYP PFNMYGLCOLORFRAGMENTOP1ATIPROC) (GLenum op, GLuint dst, GLuint dstMask, GLuint dstMod, GLuint arg1, GLuint arg1Rep, GLuint arg1Mod);
typedef void (APIENTRYP PFNMYGLCOLORFRAGMENTOP2ATIPROC) (GLenum op, GLuint dst, GLuint dstMask, GLuint dstMod, GLuint arg1, GLuint arg1Rep, GLuint arg1Mod, GLuint arg2, GLuint arg2Rep, GLuint arg2Mod);
typedef void (APIENTRYP PFNMYGLCOLORFRAGMENTOP3ATIPROC) (GLenum op, GLuint dst, GLuint dstMask, GLuint dstMod, GLuint arg1, GLuint arg1Rep, GLuint arg1Mod, GLuint arg2, GLuint arg2Rep, GLuint arg2Mod, GLuint arg3, GLuint arg3Rep, GLuint arg3Mod);
typedef void (APIENTRYP PFNMYGLALPHAFRAGMENTOP1ATIPROC) (GLenum op, GLuint dst, GLuint dstMod, GLuint arg1, GLuint arg1Rep, GLuint arg1Mod);
typedef void (APIENTRYP PFNMYGLALPHAFRAGMENTOP2ATIPROC) (GLenum op, GLuint dst, GLuint dstMod, GLuint arg1, GLuint arg1Rep, GLuint arg1Mod, GLuint arg2, GLuint arg2Rep, GLuint arg2Mod);
typedef void (APIENTRYP PFNMYGLALPHAFRAGMENTOP3ATIPROC) (GLenum op, GLuint dst, GLuint dstMod, GLuint arg1, GLuint arg1Rep, GLuint arg1Mod, GLuint arg2, GLuint arg2Rep, GLuint arg2Mod, GLuint arg3, GLuint arg3Rep, GLuint arg3Mod);
typedef void (APIENTRYP PFNMYGLSETFRAGMENTSHADERCONSTANTATIPROC) (GLuint dst, const GLfloat *value);
#endif

#ifndef MYGL_ATI_pn_triangles
#define MYGL_ATI_pn_triangles 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglPNTrianglesiATI (GLenum, GLint);
GLAPI void APIENTRY myglPNTrianglesfATI (GLenum, GLfloat);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLPNTRIANGLESIATIPROC) (GLenum pname, GLint param);
typedef void (APIENTRYP PFNMYGLPNTRIANGLESFATIPROC) (GLenum pname, GLfloat param);
#endif

#ifndef MYGL_ATI_vertex_array_object
#define MYGL_ATI_vertex_array_object 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI GLuint APIENTRY myglNewObjectBufferATI (GLsizei, const GLvoid *, GLenum);
GLAPI GLboolean APIENTRY myglIsObjectBufferATI (GLuint);
GLAPI void APIENTRY myglUpdateObjectBufferATI (GLuint, GLuint, GLsizei, const GLvoid *, GLenum);
GLAPI void APIENTRY myglGetObjectBufferfvATI (GLuint, GLenum, GLfloat *);
GLAPI void APIENTRY myglGetObjectBufferivATI (GLuint, GLenum, GLint *);
GLAPI void APIENTRY myglFreeObjectBufferATI (GLuint);
GLAPI void APIENTRY myglArrayObjectATI (GLenum, GLint, GLenum, GLsizei, GLuint, GLuint);
GLAPI void APIENTRY myglGetArrayObjectfvATI (GLenum, GLenum, GLfloat *);
GLAPI void APIENTRY myglGetArrayObjectivATI (GLenum, GLenum, GLint *);
GLAPI void APIENTRY myglVariantArrayObjectATI (GLuint, GLenum, GLsizei, GLuint, GLuint);
GLAPI void APIENTRY myglGetVariantArrayObjectfvATI (GLuint, GLenum, GLfloat *);
GLAPI void APIENTRY myglGetVariantArrayObjectivATI (GLuint, GLenum, GLint *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef GLuint (APIENTRYP PFNMYGLNEWOBJECTBUFFERATIPROC) (GLsizei size, const GLvoid *pointer, GLenum usage);
typedef GLboolean (APIENTRYP PFNMYGLISOBJECTBUFFERATIPROC) (GLuint buffer);
typedef void (APIENTRYP PFNMYGLUPDATEOBJECTBUFFERATIPROC) (GLuint buffer, GLuint offset, GLsizei size, const GLvoid *pointer, GLenum preserve);
typedef void (APIENTRYP PFNMYGLGETOBJECTBUFFERFVATIPROC) (GLuint buffer, GLenum pname, GLfloat *params);
typedef void (APIENTRYP PFNMYGLGETOBJECTBUFFERIVATIPROC) (GLuint buffer, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLFREEOBJECTBUFFERATIPROC) (GLuint buffer);
typedef void (APIENTRYP PFNMYGLARRAYOBJECTATIPROC) (GLenum array, GLint size, GLenum type, GLsizei stride, GLuint buffer, GLuint offset);
typedef void (APIENTRYP PFNMYGLGETARRAYOBJECTFVATIPROC) (GLenum array, GLenum pname, GLfloat *params);
typedef void (APIENTRYP PFNMYGLGETARRAYOBJECTIVATIPROC) (GLenum array, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLVARIANTARRAYOBJECTATIPROC) (GLuint id, GLenum type, GLsizei stride, GLuint buffer, GLuint offset);
typedef void (APIENTRYP PFNMYGLGETVARIANTARRAYOBJECTFVATIPROC) (GLuint id, GLenum pname, GLfloat *params);
typedef void (APIENTRYP PFNMYGLGETVARIANTARRAYOBJECTIVATIPROC) (GLuint id, GLenum pname, GLint *params);
#endif

#ifndef MYGL_EXT_vertex_shader
#define MYGL_EXT_vertex_shader 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglBeginVertexShaderEXT (void);
GLAPI void APIENTRY myglEndVertexShaderEXT (void);
GLAPI void APIENTRY myglBindVertexShaderEXT (GLuint);
GLAPI GLuint APIENTRY myglGenVertexShadersEXT (GLuint);
GLAPI void APIENTRY myglDeleteVertexShaderEXT (GLuint);
GLAPI void APIENTRY myglShaderOp1EXT (GLenum, GLuint, GLuint);
GLAPI void APIENTRY myglShaderOp2EXT (GLenum, GLuint, GLuint, GLuint);
GLAPI void APIENTRY myglShaderOp3EXT (GLenum, GLuint, GLuint, GLuint, GLuint);
GLAPI void APIENTRY myglSwizzleEXT (GLuint, GLuint, GLenum, GLenum, GLenum, GLenum);
GLAPI void APIENTRY myglWriteMaskEXT (GLuint, GLuint, GLenum, GLenum, GLenum, GLenum);
GLAPI void APIENTRY myglInsertComponentEXT (GLuint, GLuint, GLuint);
GLAPI void APIENTRY myglExtractComponentEXT (GLuint, GLuint, GLuint);
GLAPI GLuint APIENTRY myglGenSymbolsEXT (GLenum, GLenum, GLenum, GLuint);
GLAPI void APIENTRY myglSetInvariantEXT (GLuint, GLenum, const GLvoid *);
GLAPI void APIENTRY myglSetLocalConstantEXT (GLuint, GLenum, const GLvoid *);
GLAPI void APIENTRY myglVariantbvEXT (GLuint, const GLbyte *);
GLAPI void APIENTRY myglVariantsvEXT (GLuint, const GLshort *);
GLAPI void APIENTRY myglVariantivEXT (GLuint, const GLint *);
GLAPI void APIENTRY myglVariantfvEXT (GLuint, const GLfloat *);
GLAPI void APIENTRY myglVariantdvEXT (GLuint, const GLdouble *);
GLAPI void APIENTRY myglVariantubvEXT (GLuint, const GLubyte *);
GLAPI void APIENTRY myglVariantusvEXT (GLuint, const GLushort *);
GLAPI void APIENTRY myglVariantuivEXT (GLuint, const GLuint *);
GLAPI void APIENTRY myglVariantPointerEXT (GLuint, GLenum, GLuint, const GLvoid *);
GLAPI void APIENTRY myglEnableVariantClientStateEXT (GLuint);
GLAPI void APIENTRY myglDisableVariantClientStateEXT (GLuint);
GLAPI GLuint APIENTRY myglBindLightParameterEXT (GLenum, GLenum);
GLAPI GLuint APIENTRY myglBindMaterialParameterEXT (GLenum, GLenum);
GLAPI GLuint APIENTRY myglBindTexGenParameterEXT (GLenum, GLenum, GLenum);
GLAPI GLuint APIENTRY myglBindTextureUnitParameterEXT (GLenum, GLenum);
GLAPI GLuint APIENTRY myglBindParameterEXT (GLenum);
GLAPI GLboolean APIENTRY myglIsVariantEnabledEXT (GLuint, GLenum);
GLAPI void APIENTRY myglGetVariantBooleanvEXT (GLuint, GLenum, GLboolean *);
GLAPI void APIENTRY myglGetVariantIntegervEXT (GLuint, GLenum, GLint *);
GLAPI void APIENTRY myglGetVariantFloatvEXT (GLuint, GLenum, GLfloat *);
GLAPI void APIENTRY myglGetVariantPointervEXT (GLuint, GLenum, GLvoid* *);
GLAPI void APIENTRY myglGetInvariantBooleanvEXT (GLuint, GLenum, GLboolean *);
GLAPI void APIENTRY myglGetInvariantIntegervEXT (GLuint, GLenum, GLint *);
GLAPI void APIENTRY myglGetInvariantFloatvEXT (GLuint, GLenum, GLfloat *);
GLAPI void APIENTRY myglGetLocalConstantBooleanvEXT (GLuint, GLenum, GLboolean *);
GLAPI void APIENTRY myglGetLocalConstantIntegervEXT (GLuint, GLenum, GLint *);
GLAPI void APIENTRY myglGetLocalConstantFloatvEXT (GLuint, GLenum, GLfloat *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLBEGINVERTEXSHADEREXTPROC) (void);
typedef void (APIENTRYP PFNMYGLENDVERTEXSHADEREXTPROC) (void);
typedef void (APIENTRYP PFNMYGLBINDVERTEXSHADEREXTPROC) (GLuint id);
typedef GLuint (APIENTRYP PFNMYGLGENVERTEXSHADERSEXTPROC) (GLuint range);
typedef void (APIENTRYP PFNMYGLDELETEVERTEXSHADEREXTPROC) (GLuint id);
typedef void (APIENTRYP PFNMYGLSHADEROP1EXTPROC) (GLenum op, GLuint res, GLuint arg1);
typedef void (APIENTRYP PFNMYGLSHADEROP2EXTPROC) (GLenum op, GLuint res, GLuint arg1, GLuint arg2);
typedef void (APIENTRYP PFNMYGLSHADEROP3EXTPROC) (GLenum op, GLuint res, GLuint arg1, GLuint arg2, GLuint arg3);
typedef void (APIENTRYP PFNMYGLSWIZZLEEXTPROC) (GLuint res, GLuint in, GLenum outX, GLenum outY, GLenum outZ, GLenum outW);
typedef void (APIENTRYP PFNMYGLWRITEMASKEXTPROC) (GLuint res, GLuint in, GLenum outX, GLenum outY, GLenum outZ, GLenum outW);
typedef void (APIENTRYP PFNMYGLINSERTCOMPONENTEXTPROC) (GLuint res, GLuint src, GLuint num);
typedef void (APIENTRYP PFNMYGLEXTRACTCOMPONENTEXTPROC) (GLuint res, GLuint src, GLuint num);
typedef GLuint (APIENTRYP PFNMYGLGENSYMBOLSEXTPROC) (GLenum datatype, GLenum storagetype, GLenum range, GLuint components);
typedef void (APIENTRYP PFNMYGLSETINVARIANTEXTPROC) (GLuint id, GLenum type, const GLvoid *addr);
typedef void (APIENTRYP PFNMYGLSETLOCALCONSTANTEXTPROC) (GLuint id, GLenum type, const GLvoid *addr);
typedef void (APIENTRYP PFNMYGLVARIANTBVEXTPROC) (GLuint id, const GLbyte *addr);
typedef void (APIENTRYP PFNMYGLVARIANTSVEXTPROC) (GLuint id, const GLshort *addr);
typedef void (APIENTRYP PFNMYGLVARIANTIVEXTPROC) (GLuint id, const GLint *addr);
typedef void (APIENTRYP PFNMYGLVARIANTFVEXTPROC) (GLuint id, const GLfloat *addr);
typedef void (APIENTRYP PFNMYGLVARIANTDVEXTPROC) (GLuint id, const GLdouble *addr);
typedef void (APIENTRYP PFNMYGLVARIANTUBVEXTPROC) (GLuint id, const GLubyte *addr);
typedef void (APIENTRYP PFNMYGLVARIANTUSVEXTPROC) (GLuint id, const GLushort *addr);
typedef void (APIENTRYP PFNMYGLVARIANTUIVEXTPROC) (GLuint id, const GLuint *addr);
typedef void (APIENTRYP PFNMYGLVARIANTPOINTEREXTPROC) (GLuint id, GLenum type, GLuint stride, const GLvoid *addr);
typedef void (APIENTRYP PFNMYGLENABLEVARIANTCLIENTSTATEEXTPROC) (GLuint id);
typedef void (APIENTRYP PFNMYGLDISABLEVARIANTCLIENTSTATEEXTPROC) (GLuint id);
typedef GLuint (APIENTRYP PFNMYGLBINDLIGHTPARAMETEREXTPROC) (GLenum light, GLenum value);
typedef GLuint (APIENTRYP PFNMYGLBINDMATERIALPARAMETEREXTPROC) (GLenum face, GLenum value);
typedef GLuint (APIENTRYP PFNMYGLBINDTEXGENPARAMETEREXTPROC) (GLenum unit, GLenum coord, GLenum value);
typedef GLuint (APIENTRYP PFNMYGLBINDTEXTUREUNITPARAMETEREXTPROC) (GLenum unit, GLenum value);
typedef GLuint (APIENTRYP PFNMYGLBINDPARAMETEREXTPROC) (GLenum value);
typedef GLboolean (APIENTRYP PFNMYGLISVARIANTENABLEDEXTPROC) (GLuint id, GLenum cap);
typedef void (APIENTRYP PFNMYGLGETVARIANTBOOLEANVEXTPROC) (GLuint id, GLenum value, GLboolean *data);
typedef void (APIENTRYP PFNMYGLGETVARIANTINTEGERVEXTPROC) (GLuint id, GLenum value, GLint *data);
typedef void (APIENTRYP PFNMYGLGETVARIANTFLOATVEXTPROC) (GLuint id, GLenum value, GLfloat *data);
typedef void (APIENTRYP PFNMYGLGETVARIANTPOINTERVEXTPROC) (GLuint id, GLenum value, GLvoid* *data);
typedef void (APIENTRYP PFNMYGLGETINVARIANTBOOLEANVEXTPROC) (GLuint id, GLenum value, GLboolean *data);
typedef void (APIENTRYP PFNMYGLGETINVARIANTINTEGERVEXTPROC) (GLuint id, GLenum value, GLint *data);
typedef void (APIENTRYP PFNMYGLGETINVARIANTFLOATVEXTPROC) (GLuint id, GLenum value, GLfloat *data);
typedef void (APIENTRYP PFNMYGLGETLOCALCONSTANTBOOLEANVEXTPROC) (GLuint id, GLenum value, GLboolean *data);
typedef void (APIENTRYP PFNMYGLGETLOCALCONSTANTINTEGERVEXTPROC) (GLuint id, GLenum value, GLint *data);
typedef void (APIENTRYP PFNMYGLGETLOCALCONSTANTFLOATVEXTPROC) (GLuint id, GLenum value, GLfloat *data);
#endif

#ifndef MYGL_ATI_vertex_streams
#define MYGL_ATI_vertex_streams 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglVertexStream1sATI (GLenum, GLshort);
GLAPI void APIENTRY myglVertexStream1svATI (GLenum, const GLshort *);
GLAPI void APIENTRY myglVertexStream1iATI (GLenum, GLint);
GLAPI void APIENTRY myglVertexStream1ivATI (GLenum, const GLint *);
GLAPI void APIENTRY myglVertexStream1fATI (GLenum, GLfloat);
GLAPI void APIENTRY myglVertexStream1fvATI (GLenum, const GLfloat *);
GLAPI void APIENTRY myglVertexStream1dATI (GLenum, GLdouble);
GLAPI void APIENTRY myglVertexStream1dvATI (GLenum, const GLdouble *);
GLAPI void APIENTRY myglVertexStream2sATI (GLenum, GLshort, GLshort);
GLAPI void APIENTRY myglVertexStream2svATI (GLenum, const GLshort *);
GLAPI void APIENTRY myglVertexStream2iATI (GLenum, GLint, GLint);
GLAPI void APIENTRY myglVertexStream2ivATI (GLenum, const GLint *);
GLAPI void APIENTRY myglVertexStream2fATI (GLenum, GLfloat, GLfloat);
GLAPI void APIENTRY myglVertexStream2fvATI (GLenum, const GLfloat *);
GLAPI void APIENTRY myglVertexStream2dATI (GLenum, GLdouble, GLdouble);
GLAPI void APIENTRY myglVertexStream2dvATI (GLenum, const GLdouble *);
GLAPI void APIENTRY myglVertexStream3sATI (GLenum, GLshort, GLshort, GLshort);
GLAPI void APIENTRY myglVertexStream3svATI (GLenum, const GLshort *);
GLAPI void APIENTRY myglVertexStream3iATI (GLenum, GLint, GLint, GLint);
GLAPI void APIENTRY myglVertexStream3ivATI (GLenum, const GLint *);
GLAPI void APIENTRY myglVertexStream3fATI (GLenum, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglVertexStream3fvATI (GLenum, const GLfloat *);
GLAPI void APIENTRY myglVertexStream3dATI (GLenum, GLdouble, GLdouble, GLdouble);
GLAPI void APIENTRY myglVertexStream3dvATI (GLenum, const GLdouble *);
GLAPI void APIENTRY myglVertexStream4sATI (GLenum, GLshort, GLshort, GLshort, GLshort);
GLAPI void APIENTRY myglVertexStream4svATI (GLenum, const GLshort *);
GLAPI void APIENTRY myglVertexStream4iATI (GLenum, GLint, GLint, GLint, GLint);
GLAPI void APIENTRY myglVertexStream4ivATI (GLenum, const GLint *);
GLAPI void APIENTRY myglVertexStream4fATI (GLenum, GLfloat, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglVertexStream4fvATI (GLenum, const GLfloat *);
GLAPI void APIENTRY myglVertexStream4dATI (GLenum, GLdouble, GLdouble, GLdouble, GLdouble);
GLAPI void APIENTRY myglVertexStream4dvATI (GLenum, const GLdouble *);
GLAPI void APIENTRY myglNormalStream3bATI (GLenum, GLbyte, GLbyte, GLbyte);
GLAPI void APIENTRY myglNormalStream3bvATI (GLenum, const GLbyte *);
GLAPI void APIENTRY myglNormalStream3sATI (GLenum, GLshort, GLshort, GLshort);
GLAPI void APIENTRY myglNormalStream3svATI (GLenum, const GLshort *);
GLAPI void APIENTRY myglNormalStream3iATI (GLenum, GLint, GLint, GLint);
GLAPI void APIENTRY myglNormalStream3ivATI (GLenum, const GLint *);
GLAPI void APIENTRY myglNormalStream3fATI (GLenum, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglNormalStream3fvATI (GLenum, const GLfloat *);
GLAPI void APIENTRY myglNormalStream3dATI (GLenum, GLdouble, GLdouble, GLdouble);
GLAPI void APIENTRY myglNormalStream3dvATI (GLenum, const GLdouble *);
GLAPI void APIENTRY myglClientActiveVertexStreamATI (GLenum);
GLAPI void APIENTRY myglVertexBlendEnviATI (GLenum, GLint);
GLAPI void APIENTRY myglVertexBlendEnvfATI (GLenum, GLfloat);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLVERTEXSTREAM1SATIPROC) (GLenum stream, GLshort x);
typedef void (APIENTRYP PFNMYGLVERTEXSTREAM1SVATIPROC) (GLenum stream, const GLshort *coords);
typedef void (APIENTRYP PFNMYGLVERTEXSTREAM1IATIPROC) (GLenum stream, GLint x);
typedef void (APIENTRYP PFNMYGLVERTEXSTREAM1IVATIPROC) (GLenum stream, const GLint *coords);
typedef void (APIENTRYP PFNMYGLVERTEXSTREAM1FATIPROC) (GLenum stream, GLfloat x);
typedef void (APIENTRYP PFNMYGLVERTEXSTREAM1FVATIPROC) (GLenum stream, const GLfloat *coords);
typedef void (APIENTRYP PFNMYGLVERTEXSTREAM1DATIPROC) (GLenum stream, GLdouble x);
typedef void (APIENTRYP PFNMYGLVERTEXSTREAM1DVATIPROC) (GLenum stream, const GLdouble *coords);
typedef void (APIENTRYP PFNMYGLVERTEXSTREAM2SATIPROC) (GLenum stream, GLshort x, GLshort y);
typedef void (APIENTRYP PFNMYGLVERTEXSTREAM2SVATIPROC) (GLenum stream, const GLshort *coords);
typedef void (APIENTRYP PFNMYGLVERTEXSTREAM2IATIPROC) (GLenum stream, GLint x, GLint y);
typedef void (APIENTRYP PFNMYGLVERTEXSTREAM2IVATIPROC) (GLenum stream, const GLint *coords);
typedef void (APIENTRYP PFNMYGLVERTEXSTREAM2FATIPROC) (GLenum stream, GLfloat x, GLfloat y);
typedef void (APIENTRYP PFNMYGLVERTEXSTREAM2FVATIPROC) (GLenum stream, const GLfloat *coords);
typedef void (APIENTRYP PFNMYGLVERTEXSTREAM2DATIPROC) (GLenum stream, GLdouble x, GLdouble y);
typedef void (APIENTRYP PFNMYGLVERTEXSTREAM2DVATIPROC) (GLenum stream, const GLdouble *coords);
typedef void (APIENTRYP PFNMYGLVERTEXSTREAM3SATIPROC) (GLenum stream, GLshort x, GLshort y, GLshort z);
typedef void (APIENTRYP PFNMYGLVERTEXSTREAM3SVATIPROC) (GLenum stream, const GLshort *coords);
typedef void (APIENTRYP PFNMYGLVERTEXSTREAM3IATIPROC) (GLenum stream, GLint x, GLint y, GLint z);
typedef void (APIENTRYP PFNMYGLVERTEXSTREAM3IVATIPROC) (GLenum stream, const GLint *coords);
typedef void (APIENTRYP PFNMYGLVERTEXSTREAM3FATIPROC) (GLenum stream, GLfloat x, GLfloat y, GLfloat z);
typedef void (APIENTRYP PFNMYGLVERTEXSTREAM3FVATIPROC) (GLenum stream, const GLfloat *coords);
typedef void (APIENTRYP PFNMYGLVERTEXSTREAM3DATIPROC) (GLenum stream, GLdouble x, GLdouble y, GLdouble z);
typedef void (APIENTRYP PFNMYGLVERTEXSTREAM3DVATIPROC) (GLenum stream, const GLdouble *coords);
typedef void (APIENTRYP PFNMYGLVERTEXSTREAM4SATIPROC) (GLenum stream, GLshort x, GLshort y, GLshort z, GLshort w);
typedef void (APIENTRYP PFNMYGLVERTEXSTREAM4SVATIPROC) (GLenum stream, const GLshort *coords);
typedef void (APIENTRYP PFNMYGLVERTEXSTREAM4IATIPROC) (GLenum stream, GLint x, GLint y, GLint z, GLint w);
typedef void (APIENTRYP PFNMYGLVERTEXSTREAM4IVATIPROC) (GLenum stream, const GLint *coords);
typedef void (APIENTRYP PFNMYGLVERTEXSTREAM4FATIPROC) (GLenum stream, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
typedef void (APIENTRYP PFNMYGLVERTEXSTREAM4FVATIPROC) (GLenum stream, const GLfloat *coords);
typedef void (APIENTRYP PFNMYGLVERTEXSTREAM4DATIPROC) (GLenum stream, GLdouble x, GLdouble y, GLdouble z, GLdouble w);
typedef void (APIENTRYP PFNMYGLVERTEXSTREAM4DVATIPROC) (GLenum stream, const GLdouble *coords);
typedef void (APIENTRYP PFNMYGLNORMALSTREAM3BATIPROC) (GLenum stream, GLbyte nx, GLbyte ny, GLbyte nz);
typedef void (APIENTRYP PFNMYGLNORMALSTREAM3BVATIPROC) (GLenum stream, const GLbyte *coords);
typedef void (APIENTRYP PFNMYGLNORMALSTREAM3SATIPROC) (GLenum stream, GLshort nx, GLshort ny, GLshort nz);
typedef void (APIENTRYP PFNMYGLNORMALSTREAM3SVATIPROC) (GLenum stream, const GLshort *coords);
typedef void (APIENTRYP PFNMYGLNORMALSTREAM3IATIPROC) (GLenum stream, GLint nx, GLint ny, GLint nz);
typedef void (APIENTRYP PFNMYGLNORMALSTREAM3IVATIPROC) (GLenum stream, const GLint *coords);
typedef void (APIENTRYP PFNMYGLNORMALSTREAM3FATIPROC) (GLenum stream, GLfloat nx, GLfloat ny, GLfloat nz);
typedef void (APIENTRYP PFNMYGLNORMALSTREAM3FVATIPROC) (GLenum stream, const GLfloat *coords);
typedef void (APIENTRYP PFNMYGLNORMALSTREAM3DATIPROC) (GLenum stream, GLdouble nx, GLdouble ny, GLdouble nz);
typedef void (APIENTRYP PFNMYGLNORMALSTREAM3DVATIPROC) (GLenum stream, const GLdouble *coords);
typedef void (APIENTRYP PFNMYGLCLIENTACTIVEVERTEXSTREAMATIPROC) (GLenum stream);
typedef void (APIENTRYP PFNMYGLVERTEXBLENDENVIATIPROC) (GLenum pname, GLint param);
typedef void (APIENTRYP PFNMYGLVERTEXBLENDENVFATIPROC) (GLenum pname, GLfloat param);
#endif

#ifndef MYGL_ATI_element_array
#define MYGL_ATI_element_array 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglElementPointerATI (GLenum, const GLvoid *);
GLAPI void APIENTRY myglDrawElementArrayATI (GLenum, GLsizei);
GLAPI void APIENTRY myglDrawRangeElementArrayATI (GLenum, GLuint, GLuint, GLsizei);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLELEMENTPOINTERATIPROC) (GLenum type, const GLvoid *pointer);
typedef void (APIENTRYP PFNMYGLDRAWELEMENTARRAYATIPROC) (GLenum mode, GLsizei count);
typedef void (APIENTRYP PFNMYGLDRAWRANGEELEMENTARRAYATIPROC) (GLenum mode, GLuint start, GLuint end, GLsizei count);
#endif

#ifndef MYGL_SUN_mesh_array
#define MYGL_SUN_mesh_array 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglDrawMeshArraysSUN (GLenum, GLint, GLsizei, GLsizei);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLDRAWMESHARRAYSSUNPROC) (GLenum mode, GLint first, GLsizei count, GLsizei width);
#endif

#ifndef MYGL_SUN_slice_accum
#define MYGL_SUN_slice_accum 1
#endif

#ifndef MYGL_NV_multisample_filter_hint
#define MYGL_NV_multisample_filter_hint 1
#endif

#ifndef MYGL_NV_depth_clamp
#define MYGL_NV_depth_clamp 1
#endif

#ifndef MYGL_NV_occlusion_query
#define MYGL_NV_occlusion_query 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglGenOcclusionQueriesNV (GLsizei, GLuint *);
GLAPI void APIENTRY myglDeleteOcclusionQueriesNV (GLsizei, const GLuint *);
GLAPI GLboolean APIENTRY myglIsOcclusionQueryNV (GLuint);
GLAPI void APIENTRY myglBeginOcclusionQueryNV (GLuint);
GLAPI void APIENTRY myglEndOcclusionQueryNV (void);
GLAPI void APIENTRY myglGetOcclusionQueryivNV (GLuint, GLenum, GLint *);
GLAPI void APIENTRY myglGetOcclusionQueryuivNV (GLuint, GLenum, GLuint *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLGENOCCLUSIONQUERIESNVPROC) (GLsizei n, GLuint *ids);
typedef void (APIENTRYP PFNMYGLDELETEOCCLUSIONQUERIESNVPROC) (GLsizei n, const GLuint *ids);
typedef GLboolean (APIENTRYP PFNMYGLISOCCLUSIONQUERYNVPROC) (GLuint id);
typedef void (APIENTRYP PFNMYGLBEGINOCCLUSIONQUERYNVPROC) (GLuint id);
typedef void (APIENTRYP PFNMYGLENDOCCLUSIONQUERYNVPROC) (void);
typedef void (APIENTRYP PFNMYGLGETOCCLUSIONQUERYIVNVPROC) (GLuint id, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLGETOCCLUSIONQUERYUIVNVPROC) (GLuint id, GLenum pname, GLuint *params);
#endif

#ifndef MYGL_NV_point_sprite
#define MYGL_NV_point_sprite 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglPointParameteriNV (GLenum, GLint);
GLAPI void APIENTRY myglPointParameterivNV (GLenum, const GLint *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLPOINTPARAMETERINVPROC) (GLenum pname, GLint param);
typedef void (APIENTRYP PFNMYGLPOINTPARAMETERIVNVPROC) (GLenum pname, const GLint *params);
#endif

#ifndef MYGL_NV_texture_shader3
#define MYGL_NV_texture_shader3 1
#endif

#ifndef MYGL_NV_vertex_program1_1
#define MYGL_NV_vertex_program1_1 1
#endif

#ifndef MYGL_EXT_shadow_funcs
#define MYGL_EXT_shadow_funcs 1
#endif

#ifndef MYGL_EXT_stencil_two_side
#define MYGL_EXT_stencil_two_side 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglActiveStencilFaceEXT (GLenum);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLACTIVESTENCILFACEEXTPROC) (GLenum face);
#endif

#ifndef MYGL_ATI_text_fragment_shader
#define MYGL_ATI_text_fragment_shader 1
#endif

#ifndef MYGL_APPLE_client_storage
#define MYGL_APPLE_client_storage 1
#endif

#ifndef MYGL_APPLE_element_array
#define MYGL_APPLE_element_array 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglElementPointerAPPLE (GLenum, const GLvoid *);
GLAPI void APIENTRY myglDrawElementArrayAPPLE (GLenum, GLint, GLsizei);
GLAPI void APIENTRY myglDrawRangeElementArrayAPPLE (GLenum, GLuint, GLuint, GLint, GLsizei);
GLAPI void APIENTRY myglMultiDrawElementArrayAPPLE (GLenum, const GLint *, const GLsizei *, GLsizei);
GLAPI void APIENTRY myglMultiDrawRangeElementArrayAPPLE (GLenum, GLuint, GLuint, const GLint *, const GLsizei *, GLsizei);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLELEMENTPOINTERAPPLEPROC) (GLenum type, const GLvoid *pointer);
typedef void (APIENTRYP PFNMYGLDRAWELEMENTARRAYAPPLEPROC) (GLenum mode, GLint first, GLsizei count);
typedef void (APIENTRYP PFNMYGLDRAWRANGEELEMENTARRAYAPPLEPROC) (GLenum mode, GLuint start, GLuint end, GLint first, GLsizei count);
typedef void (APIENTRYP PFNMYGLMULTIDRAWELEMENTARRAYAPPLEPROC) (GLenum mode, const GLint *first, const GLsizei *count, GLsizei primcount);
typedef void (APIENTRYP PFNMYGLMULTIDRAWRANGEELEMENTARRAYAPPLEPROC) (GLenum mode, GLuint start, GLuint end, const GLint *first, const GLsizei *count, GLsizei primcount);
#endif

#ifndef MYGL_APPLE_fence
#define MYGL_APPLE_fence 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglGenFencesAPPLE (GLsizei, GLuint *);
GLAPI void APIENTRY myglDeleteFencesAPPLE (GLsizei, const GLuint *);
GLAPI void APIENTRY myglSetFenceAPPLE (GLuint);
GLAPI GLboolean APIENTRY myglIsFenceAPPLE (GLuint);
GLAPI GLboolean APIENTRY myglTestFenceAPPLE (GLuint);
GLAPI void APIENTRY myglFinishFenceAPPLE (GLuint);
GLAPI GLboolean APIENTRY myglTestObjectAPPLE (GLenum, GLuint);
GLAPI void APIENTRY myglFinishObjectAPPLE (GLenum, GLint);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLGENFENCESAPPLEPROC) (GLsizei n, GLuint *fences);
typedef void (APIENTRYP PFNMYGLDELETEFENCESAPPLEPROC) (GLsizei n, const GLuint *fences);
typedef void (APIENTRYP PFNMYGLSETFENCEAPPLEPROC) (GLuint fence);
typedef GLboolean (APIENTRYP PFNMYGLISFENCEAPPLEPROC) (GLuint fence);
typedef GLboolean (APIENTRYP PFNMYGLTESTFENCEAPPLEPROC) (GLuint fence);
typedef void (APIENTRYP PFNMYGLFINISHFENCEAPPLEPROC) (GLuint fence);
typedef GLboolean (APIENTRYP PFNMYGLTESTOBJECTAPPLEPROC) (GLenum object, GLuint name);
typedef void (APIENTRYP PFNMYGLFINISHOBJECTAPPLEPROC) (GLenum object, GLint name);
#endif

#ifndef MYGL_APPLE_vertex_array_object
#define MYGL_APPLE_vertex_array_object 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglBindVertexArrayAPPLE (GLuint);
GLAPI void APIENTRY myglDeleteVertexArraysAPPLE (GLsizei, const GLuint *);
GLAPI void APIENTRY myglGenVertexArraysAPPLE (GLsizei, const GLuint *);
GLAPI GLboolean APIENTRY myglIsVertexArrayAPPLE (GLuint);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLBINDVERTEXARRAYAPPLEPROC) (GLuint array);
typedef void (APIENTRYP PFNMYGLDELETEVERTEXARRAYSAPPLEPROC) (GLsizei n, const GLuint *arrays);
typedef void (APIENTRYP PFNMYGLGENVERTEXARRAYSAPPLEPROC) (GLsizei n, const GLuint *arrays);
typedef GLboolean (APIENTRYP PFNMYGLISVERTEXARRAYAPPLEPROC) (GLuint array);
#endif

#ifndef MYGL_APPLE_vertex_array_range
#define MYGL_APPLE_vertex_array_range 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglVertexArrayRangeAPPLE (GLsizei, GLvoid *);
GLAPI void APIENTRY myglFlushVertexArrayRangeAPPLE (GLsizei, GLvoid *);
GLAPI void APIENTRY myglVertexArrayParameteriAPPLE (GLenum, GLint);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLVERTEXARRAYRANGEAPPLEPROC) (GLsizei length, GLvoid *pointer);
typedef void (APIENTRYP PFNMYGLFLUSHVERTEXARRAYRANGEAPPLEPROC) (GLsizei length, GLvoid *pointer);
typedef void (APIENTRYP PFNMYGLVERTEXARRAYPARAMETERIAPPLEPROC) (GLenum pname, GLint param);
#endif

#ifndef MYGL_APPLE_ycbcr_422
#define MYGL_APPLE_ycbcr_422 1
#endif

#ifndef MYGL_S3_s3tc
#define MYGL_S3_s3tc 1
#endif

#ifndef MYGL_ATI_draw_buffers
#define MYGL_ATI_draw_buffers 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglDrawBuffersATI (GLsizei, const GLenum *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLDRAWBUFFERSATIPROC) (GLsizei n, const GLenum *bufs);
#endif

#ifndef MYGL_ATI_pixel_format_float
#define MYGL_ATI_pixel_format_float 1
/* This is really a WGL extension, but defines some associated GL enums.
 * ATI does not export "MYGL_ATI_pixel_format_float" in the MYGL_EXTENSIONS string.
 */
#endif

#ifndef MYGL_ATI_texture_env_combine3
#define MYGL_ATI_texture_env_combine3 1
#endif

#ifndef MYGL_ATI_texture_float
#define MYGL_ATI_texture_float 1
#endif

#ifndef MYGL_NV_float_buffer
#define MYGL_NV_float_buffer 1
#endif

#ifndef MYGL_NV_fragment_program
#define MYGL_NV_fragment_program 1
/* Some NV_fragment_program entry points are shared with ARB_vertex_program. */
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglProgramNamedParameter4fNV (GLuint, GLsizei, const GLubyte *, GLfloat, GLfloat, GLfloat, GLfloat);
GLAPI void APIENTRY myglProgramNamedParameter4dNV (GLuint, GLsizei, const GLubyte *, GLdouble, GLdouble, GLdouble, GLdouble);
GLAPI void APIENTRY myglProgramNamedParameter4fvNV (GLuint, GLsizei, const GLubyte *, const GLfloat *);
GLAPI void APIENTRY myglProgramNamedParameter4dvNV (GLuint, GLsizei, const GLubyte *, const GLdouble *);
GLAPI void APIENTRY myglGetProgramNamedParameterfvNV (GLuint, GLsizei, const GLubyte *, GLfloat *);
GLAPI void APIENTRY myglGetProgramNamedParameterdvNV (GLuint, GLsizei, const GLubyte *, GLdouble *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLPROGRAMNAMEDPARAMETER4FNVPROC) (GLuint id, GLsizei len, const GLubyte *name, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
typedef void (APIENTRYP PFNMYGLPROGRAMNAMEDPARAMETER4DNVPROC) (GLuint id, GLsizei len, const GLubyte *name, GLdouble x, GLdouble y, GLdouble z, GLdouble w);
typedef void (APIENTRYP PFNMYGLPROGRAMNAMEDPARAMETER4FVNVPROC) (GLuint id, GLsizei len, const GLubyte *name, const GLfloat *v);
typedef void (APIENTRYP PFNMYGLPROGRAMNAMEDPARAMETER4DVNVPROC) (GLuint id, GLsizei len, const GLubyte *name, const GLdouble *v);
typedef void (APIENTRYP PFNMYGLGETPROGRAMNAMEDPARAMETERFVNVPROC) (GLuint id, GLsizei len, const GLubyte *name, GLfloat *params);
typedef void (APIENTRYP PFNMYGLGETPROGRAMNAMEDPARAMETERDVNVPROC) (GLuint id, GLsizei len, const GLubyte *name, GLdouble *params);
#endif

#ifndef MYGL_NV_half_float
#define MYGL_NV_half_float 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglVertex2hNV (GLhalfNV, GLhalfNV);
GLAPI void APIENTRY myglVertex2hvNV (const GLhalfNV *);
GLAPI void APIENTRY myglVertex3hNV (GLhalfNV, GLhalfNV, GLhalfNV);
GLAPI void APIENTRY myglVertex3hvNV (const GLhalfNV *);
GLAPI void APIENTRY myglVertex4hNV (GLhalfNV, GLhalfNV, GLhalfNV, GLhalfNV);
GLAPI void APIENTRY myglVertex4hvNV (const GLhalfNV *);
GLAPI void APIENTRY myglNormal3hNV (GLhalfNV, GLhalfNV, GLhalfNV);
GLAPI void APIENTRY myglNormal3hvNV (const GLhalfNV *);
GLAPI void APIENTRY myglColor3hNV (GLhalfNV, GLhalfNV, GLhalfNV);
GLAPI void APIENTRY myglColor3hvNV (const GLhalfNV *);
GLAPI void APIENTRY myglColor4hNV (GLhalfNV, GLhalfNV, GLhalfNV, GLhalfNV);
GLAPI void APIENTRY myglColor4hvNV (const GLhalfNV *);
GLAPI void APIENTRY myglTexCoord1hNV (GLhalfNV);
GLAPI void APIENTRY myglTexCoord1hvNV (const GLhalfNV *);
GLAPI void APIENTRY myglTexCoord2hNV (GLhalfNV, GLhalfNV);
GLAPI void APIENTRY myglTexCoord2hvNV (const GLhalfNV *);
GLAPI void APIENTRY myglTexCoord3hNV (GLhalfNV, GLhalfNV, GLhalfNV);
GLAPI void APIENTRY myglTexCoord3hvNV (const GLhalfNV *);
GLAPI void APIENTRY myglTexCoord4hNV (GLhalfNV, GLhalfNV, GLhalfNV, GLhalfNV);
GLAPI void APIENTRY myglTexCoord4hvNV (const GLhalfNV *);
GLAPI void APIENTRY myglMultiTexCoord1hNV (GLenum, GLhalfNV);
GLAPI void APIENTRY myglMultiTexCoord1hvNV (GLenum, const GLhalfNV *);
GLAPI void APIENTRY myglMultiTexCoord2hNV (GLenum, GLhalfNV, GLhalfNV);
GLAPI void APIENTRY myglMultiTexCoord2hvNV (GLenum, const GLhalfNV *);
GLAPI void APIENTRY myglMultiTexCoord3hNV (GLenum, GLhalfNV, GLhalfNV, GLhalfNV);
GLAPI void APIENTRY myglMultiTexCoord3hvNV (GLenum, const GLhalfNV *);
GLAPI void APIENTRY myglMultiTexCoord4hNV (GLenum, GLhalfNV, GLhalfNV, GLhalfNV, GLhalfNV);
GLAPI void APIENTRY myglMultiTexCoord4hvNV (GLenum, const GLhalfNV *);
GLAPI void APIENTRY myglFogCoordhNV (GLhalfNV);
GLAPI void APIENTRY myglFogCoordhvNV (const GLhalfNV *);
GLAPI void APIENTRY myglSecondaryColor3hNV (GLhalfNV, GLhalfNV, GLhalfNV);
GLAPI void APIENTRY myglSecondaryColor3hvNV (const GLhalfNV *);
GLAPI void APIENTRY myglVertexWeighthNV (GLhalfNV);
GLAPI void APIENTRY myglVertexWeighthvNV (const GLhalfNV *);
GLAPI void APIENTRY myglVertexAttrib1hNV (GLuint, GLhalfNV);
GLAPI void APIENTRY myglVertexAttrib1hvNV (GLuint, const GLhalfNV *);
GLAPI void APIENTRY myglVertexAttrib2hNV (GLuint, GLhalfNV, GLhalfNV);
GLAPI void APIENTRY myglVertexAttrib2hvNV (GLuint, const GLhalfNV *);
GLAPI void APIENTRY myglVertexAttrib3hNV (GLuint, GLhalfNV, GLhalfNV, GLhalfNV);
GLAPI void APIENTRY myglVertexAttrib3hvNV (GLuint, const GLhalfNV *);
GLAPI void APIENTRY myglVertexAttrib4hNV (GLuint, GLhalfNV, GLhalfNV, GLhalfNV, GLhalfNV);
GLAPI void APIENTRY myglVertexAttrib4hvNV (GLuint, const GLhalfNV *);
GLAPI void APIENTRY myglVertexAttribs1hvNV (GLuint, GLsizei, const GLhalfNV *);
GLAPI void APIENTRY myglVertexAttribs2hvNV (GLuint, GLsizei, const GLhalfNV *);
GLAPI void APIENTRY myglVertexAttribs3hvNV (GLuint, GLsizei, const GLhalfNV *);
GLAPI void APIENTRY myglVertexAttribs4hvNV (GLuint, GLsizei, const GLhalfNV *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLVERTEX2HNVPROC) (GLhalfNV x, GLhalfNV y);
typedef void (APIENTRYP PFNMYGLVERTEX2HVNVPROC) (const GLhalfNV *v);
typedef void (APIENTRYP PFNMYGLVERTEX3HNVPROC) (GLhalfNV x, GLhalfNV y, GLhalfNV z);
typedef void (APIENTRYP PFNMYGLVERTEX3HVNVPROC) (const GLhalfNV *v);
typedef void (APIENTRYP PFNMYGLVERTEX4HNVPROC) (GLhalfNV x, GLhalfNV y, GLhalfNV z, GLhalfNV w);
typedef void (APIENTRYP PFNMYGLVERTEX4HVNVPROC) (const GLhalfNV *v);
typedef void (APIENTRYP PFNMYGLNORMAL3HNVPROC) (GLhalfNV nx, GLhalfNV ny, GLhalfNV nz);
typedef void (APIENTRYP PFNMYGLNORMAL3HVNVPROC) (const GLhalfNV *v);
typedef void (APIENTRYP PFNMYGLCOLOR3HNVPROC) (GLhalfNV red, GLhalfNV green, GLhalfNV blue);
typedef void (APIENTRYP PFNMYGLCOLOR3HVNVPROC) (const GLhalfNV *v);
typedef void (APIENTRYP PFNMYGLCOLOR4HNVPROC) (GLhalfNV red, GLhalfNV green, GLhalfNV blue, GLhalfNV alpha);
typedef void (APIENTRYP PFNMYGLCOLOR4HVNVPROC) (const GLhalfNV *v);
typedef void (APIENTRYP PFNMYGLTEXCOORD1HNVPROC) (GLhalfNV s);
typedef void (APIENTRYP PFNMYGLTEXCOORD1HVNVPROC) (const GLhalfNV *v);
typedef void (APIENTRYP PFNMYGLTEXCOORD2HNVPROC) (GLhalfNV s, GLhalfNV t);
typedef void (APIENTRYP PFNMYGLTEXCOORD2HVNVPROC) (const GLhalfNV *v);
typedef void (APIENTRYP PFNMYGLTEXCOORD3HNVPROC) (GLhalfNV s, GLhalfNV t, GLhalfNV r);
typedef void (APIENTRYP PFNMYGLTEXCOORD3HVNVPROC) (const GLhalfNV *v);
typedef void (APIENTRYP PFNMYGLTEXCOORD4HNVPROC) (GLhalfNV s, GLhalfNV t, GLhalfNV r, GLhalfNV q);
typedef void (APIENTRYP PFNMYGLTEXCOORD4HVNVPROC) (const GLhalfNV *v);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD1HNVPROC) (GLenum target, GLhalfNV s);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD1HVNVPROC) (GLenum target, const GLhalfNV *v);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD2HNVPROC) (GLenum target, GLhalfNV s, GLhalfNV t);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD2HVNVPROC) (GLenum target, const GLhalfNV *v);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD3HNVPROC) (GLenum target, GLhalfNV s, GLhalfNV t, GLhalfNV r);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD3HVNVPROC) (GLenum target, const GLhalfNV *v);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD4HNVPROC) (GLenum target, GLhalfNV s, GLhalfNV t, GLhalfNV r, GLhalfNV q);
typedef void (APIENTRYP PFNMYGLMULTITEXCOORD4HVNVPROC) (GLenum target, const GLhalfNV *v);
typedef void (APIENTRYP PFNMYGLFOGCOORDHNVPROC) (GLhalfNV fog);
typedef void (APIENTRYP PFNMYGLFOGCOORDHVNVPROC) (const GLhalfNV *fog);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLOR3HNVPROC) (GLhalfNV red, GLhalfNV green, GLhalfNV blue);
typedef void (APIENTRYP PFNMYGLSECONDARYCOLOR3HVNVPROC) (const GLhalfNV *v);
typedef void (APIENTRYP PFNMYGLVERTEXWEIGHTHNVPROC) (GLhalfNV weight);
typedef void (APIENTRYP PFNMYGLVERTEXWEIGHTHVNVPROC) (const GLhalfNV *weight);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB1HNVPROC) (GLuint index, GLhalfNV x);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB1HVNVPROC) (GLuint index, const GLhalfNV *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB2HNVPROC) (GLuint index, GLhalfNV x, GLhalfNV y);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB2HVNVPROC) (GLuint index, const GLhalfNV *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB3HNVPROC) (GLuint index, GLhalfNV x, GLhalfNV y, GLhalfNV z);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB3HVNVPROC) (GLuint index, const GLhalfNV *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4HNVPROC) (GLuint index, GLhalfNV x, GLhalfNV y, GLhalfNV z, GLhalfNV w);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIB4HVNVPROC) (GLuint index, const GLhalfNV *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIBS1HVNVPROC) (GLuint index, GLsizei n, const GLhalfNV *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIBS2HVNVPROC) (GLuint index, GLsizei n, const GLhalfNV *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIBS3HVNVPROC) (GLuint index, GLsizei n, const GLhalfNV *v);
typedef void (APIENTRYP PFNMYGLVERTEXATTRIBS4HVNVPROC) (GLuint index, GLsizei n, const GLhalfNV *v);
#endif

#ifndef MYGL_NV_pixel_data_range
#define MYGL_NV_pixel_data_range 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglPixelDataRangeNV (GLenum, GLsizei, GLvoid *);
GLAPI void APIENTRY myglFlushPixelDataRangeNV (GLenum);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLPIXELDATARANGENVPROC) (GLenum target, GLsizei length, GLvoid *pointer);
typedef void (APIENTRYP PFNMYGLFLUSHPIXELDATARANGENVPROC) (GLenum target);
#endif

#ifndef MYGL_NV_primitive_restart
#define MYGL_NV_primitive_restart 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglPrimitiveRestartNV (void);
GLAPI void APIENTRY myglPrimitiveRestartIndexNV (GLuint);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLPRIMITIVERESTARTNVPROC) (void);
typedef void (APIENTRYP PFNMYGLPRIMITIVERESTARTINDEXNVPROC) (GLuint index);
#endif

#ifndef MYGL_NV_texture_expand_normal
#define MYGL_NV_texture_expand_normal 1
#endif

#ifndef MYGL_NV_vertex_program2
#define MYGL_NV_vertex_program2 1
#endif

#ifndef MYGL_ATI_map_object_buffer
#define MYGL_ATI_map_object_buffer 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI GLvoid* APIENTRY myglMapObjectBufferATI (GLuint);
GLAPI void APIENTRY myglUnmapObjectBufferATI (GLuint);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef GLvoid* (APIENTRYP PFNMYGLMAPOBJECTBUFFERATIPROC) (GLuint buffer);
typedef void (APIENTRYP PFNMYGLUNMAPOBJECTBUFFERATIPROC) (GLuint buffer);
#endif

#ifndef MYGL_ATI_separate_stencil
#define MYGL_ATI_separate_stencil 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglStencilOpSeparateATI (GLenum, GLenum, GLenum, GLenum);
GLAPI void APIENTRY myglStencilFuncSeparateATI (GLenum, GLenum, GLint, GLuint);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLSTENCILOPSEPARATEATIPROC) (GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass);
typedef void (APIENTRYP PFNMYGLSTENCILFUNCSEPARATEATIPROC) (GLenum frontfunc, GLenum backfunc, GLint ref, GLuint mask);
#endif

#ifndef MYGL_ATI_vertex_attrib_array_object
#define MYGL_ATI_vertex_attrib_array_object 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglVertexAttribArrayObjectATI (GLuint, GLint, GLenum, GLboolean, GLsizei, GLuint, GLuint);
GLAPI void APIENTRY myglGetVertexAttribArrayObjectfvATI (GLuint, GLenum, GLfloat *);
GLAPI void APIENTRY myglGetVertexAttribArrayObjectivATI (GLuint, GLenum, GLint *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLVERTEXATTRIBARRAYOBJECTATIPROC) (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, GLuint buffer, GLuint offset);
typedef void (APIENTRYP PFNMYGLGETVERTEXATTRIBARRAYOBJECTFVATIPROC) (GLuint index, GLenum pname, GLfloat *params);
typedef void (APIENTRYP PFNMYGLGETVERTEXATTRIBARRAYOBJECTIVATIPROC) (GLuint index, GLenum pname, GLint *params);
#endif

#ifndef MYGL_OES_read_format
#define MYGL_OES_read_format 1
#endif

#ifndef MYGL_EXT_depth_bounds_test
#define MYGL_EXT_depth_bounds_test 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglDepthBoundsEXT (GLclampd, GLclampd);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLDEPTHBOUNDSEXTPROC) (GLclampd zmin, GLclampd zmax);
#endif

#ifndef MYGL_EXT_texture_mirror_clamp
#define MYGL_EXT_texture_mirror_clamp 1
#endif

#ifndef MYGL_EXT_blend_equation_separate
#define MYGL_EXT_blend_equation_separate 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglBlendEquationSeparateEXT (GLenum, GLenum);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLBLENDEQUATIONSEPARATEEXTPROC) (GLenum modeRGB, GLenum modeAlpha);
#endif

#ifndef MYGL_MESA_pack_invert
#define MYGL_MESA_pack_invert 1
#endif

#ifndef MYGL_MESA_ycbcr_texture
#define MYGL_MESA_ycbcr_texture 1
#endif

#ifndef MYGL_EXT_pixel_buffer_object
#define MYGL_EXT_pixel_buffer_object 1
#endif

#ifndef MYGL_NV_fragment_program_option
#define MYGL_NV_fragment_program_option 1
#endif

#ifndef MYGL_NV_fragment_program2
#define MYGL_NV_fragment_program2 1
#endif

#ifndef MYGL_NV_vertex_program2_option
#define MYGL_NV_vertex_program2_option 1
#endif

#ifndef MYGL_NV_vertex_program3
#define MYGL_NV_vertex_program3 1
#endif

#ifndef MYGL_EXT_framebuffer_object
#define MYGL_EXT_framebuffer_object 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI GLboolean APIENTRY myglIsRenderbufferEXT (GLuint);
GLAPI void APIENTRY myglBindRenderbufferEXT (GLenum, GLuint);
GLAPI void APIENTRY myglDeleteRenderbuffersEXT (GLsizei, const GLuint *);
GLAPI void APIENTRY myglGenRenderbuffersEXT (GLsizei, GLuint *);
GLAPI void APIENTRY myglRenderbufferStorageEXT (GLenum, GLenum, GLsizei, GLsizei);
GLAPI void APIENTRY myglGetRenderbufferParameterivEXT (GLenum, GLenum, GLint *);
GLAPI GLboolean APIENTRY myglIsFramebufferEXT (GLuint);
GLAPI void APIENTRY myglBindFramebufferEXT (GLenum, GLuint);
GLAPI void APIENTRY myglDeleteFramebuffersEXT (GLsizei, const GLuint *);
GLAPI void APIENTRY myglGenFramebuffersEXT (GLsizei, GLuint *);
GLAPI GLenum APIENTRY myglCheckFramebufferStatusEXT (GLenum);
GLAPI void APIENTRY myglFramebufferTexture1DEXT (GLenum, GLenum, GLenum, GLuint, GLint);
GLAPI void APIENTRY myglFramebufferTexture2DEXT (GLenum, GLenum, GLenum, GLuint, GLint);
GLAPI void APIENTRY myglFramebufferTexture3DEXT (GLenum, GLenum, GLenum, GLuint, GLint, GLint);
GLAPI void APIENTRY myglFramebufferRenderbufferEXT (GLenum, GLenum, GLenum, GLuint);
GLAPI void APIENTRY myglGetFramebufferAttachmentParameterivEXT (GLenum, GLenum, GLenum, GLint *);
GLAPI void APIENTRY myglGenerateMipmapEXT (GLenum);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef GLboolean (APIENTRYP PFNMYGLISRENDERBUFFEREXTPROC) (GLuint renderbuffer);
typedef void (APIENTRYP PFNMYGLBINDRENDERBUFFEREXTPROC) (GLenum target, GLuint renderbuffer);
typedef void (APIENTRYP PFNMYGLDELETERENDERBUFFERSEXTPROC) (GLsizei n, const GLuint *renderbuffers);
typedef void (APIENTRYP PFNMYGLGENRENDERBUFFERSEXTPROC) (GLsizei n, GLuint *renderbuffers);
typedef void (APIENTRYP PFNMYGLRENDERBUFFERSTORAGEEXTPROC) (GLenum target, GLenum internalformat, GLsizei width, GLsizei height);
typedef void (APIENTRYP PFNMYGLGETRENDERBUFFERPARAMETERIVEXTPROC) (GLenum target, GLenum pname, GLint *params);
typedef GLboolean (APIENTRYP PFNMYGLISFRAMEBUFFEREXTPROC) (GLuint framebuffer);
typedef void (APIENTRYP PFNMYGLBINDFRAMEBUFFEREXTPROC) (GLenum target, GLuint framebuffer);
typedef void (APIENTRYP PFNMYGLDELETEFRAMEBUFFERSEXTPROC) (GLsizei n, const GLuint *framebuffers);
typedef void (APIENTRYP PFNMYGLGENFRAMEBUFFERSEXTPROC) (GLsizei n, GLuint *framebuffers);
typedef GLenum (APIENTRYP PFNMYGLCHECKFRAMEBUFFERSTATUSEXTPROC) (GLenum target);
typedef void (APIENTRYP PFNMYGLFRAMEBUFFERTEXTURE1DEXTPROC) (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
typedef void (APIENTRYP PFNMYGLFRAMEBUFFERTEXTURE2DEXTPROC) (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
typedef void (APIENTRYP PFNMYGLFRAMEBUFFERTEXTURE3DEXTPROC) (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level, GLint zoffset);
typedef void (APIENTRYP PFNMYGLFRAMEBUFFERRENDERBUFFEREXTPROC) (GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);
typedef void (APIENTRYP PFNMYGLGETFRAMEBUFFERATTACHMENTPARAMETERIVEXTPROC) (GLenum target, GLenum attachment, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNMYGLGENERATEMIPMAPEXTPROC) (GLenum target);
#endif

#ifndef MYGL_GREMEDY_string_marker
#define MYGL_GREMEDY_string_marker 1
#ifdef MYGL_GLEXT_PROTOTYPES
GLAPI void APIENTRY myglStringMarkerGREMEDY (GLsizei, const GLvoid *);
#endif /* MYGL_GLEXT_PROTOTYPES */
typedef void (APIENTRYP PFNMYGLSTRINGMARKERGREMEDYPROC) (GLsizei len, const GLvoid *string);
#endif


#ifdef __cplusplus
}
#endif

#endif
