/*
 * Copyright (C) 2004 the xine project
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
 */

#ifndef __HAVE_XINE_OPENGL_VIEW_H__
#define __HAVE_XINE_OPENGL_VIEW_H__

#import <Cocoa/Cocoa.h>
#import <OpenGL/gl.h>

#import "XineVideoWindow.h"

@protocol XineOpenGLViewDelegate;

extern NSString *XineViewDidResizeNotification;

@interface XineOpenGLView : NSOpenGLView
{
    @private
    IBOutlet id <NSObject, XineOpenGLViewDelegate>  delegate;
    IBOutlet id <NSObject, XineOpenGLViewDelegate>  controller;

    NSRecursiveLock *                   mutex;
    BOOL                                initDone;

    NSSize                              videoSize;
    char *                              textureBuffer;
    GLuint                              texture;

    BOOL                                keepsVideoAspectRatio;
    BOOL                                resizeViewOnVideoSizeChange;
    NSCursor *                          currentCursor;

    NSColor *                           initialColor;
    unsigned int                        initialColorYUV;
    BOOL                                initialColorYUVIsSet;

    BOOL                                isFullScreen;
    BOOL                                isFullScreenPrepared;
    XineVideoWindowFullScreenMode       fullScreenMode;
    NSOpenGLContext *                   fullScreenContext;
}

+ (NSOpenGLPixelFormat *)defaultPixelFormat;
+ (NSOpenGLPixelFormat *)fullScreenPixelFormat;

- (id)initWithCoder:(NSCoder *)coder;
- (id)initWithFrame:(NSRect)frame;
- (id)initWithFrame:(NSRect)frame pixelFormat:(NSOpenGLPixelFormat *)pixelFormat;

- (void)dealloc;

- (void)encodeWithCoder:(NSCoder *)coder;

- (NSOpenGLContext *)openGLContext;
- (void)prepareOpenGL;
- (void)reshape;
- (void)update;

- (void)initTextures;
- (void)updateTexture;
- (void)drawRect:(NSRect)rect;

- (NSColor *)initialColor;
- (void)setInitialColor:(NSColor *)color;

- (void)setNormalSize;
- (void)setHalfSize;
- (void)setDoubleSize;

- (NSSize)videoSize;

- (BOOL)keepsVideoAspectRatio;
- (void)setKeepsVideoAspectRatio:(BOOL)flag;
- (BOOL)resizeViewOnVideoSizeChange;
- (void)setResizeViewOnVideoSizeChange:(BOOL)flag;

- (void)setViewSize:(NSValue *)sizeWrapper;
- (void)setViewSizeInMainThread:(NSSize)size;

- (NSCursor *)currentCursor;
- (void)setCurrentCursor:(NSCursor *)cursor;

- (BOOL)isFullScreen;
- (void)goFullScreen:(XineVideoWindowFullScreenMode)mode;
- (void)exitFullScreen;

- (id)delegate;
- (void)setDelegate:(id)aDelegate;
- (id)xineController;
- (void)setXineController:(id)aController;

- (BOOL)acceptsFirstResponder;
- (BOOL)mouseDownCanMoveWindow;

// Not intended for public use:
- (char *)textureBuffer;
- (void)setVideoSize:(NSSize)size;
- (void)resetCursorRects;
- (void)resetCursorRectsInMainThread;
- (void)calcFullScreenAspect;
- (void)releaseInMainThread;
- (void)passEventToDelegate:(NSEvent *)theEvent withSelector:(SEL)selector;

- (BOOL)acceptsFirstResponder;
- (BOOL)mouseDownCanMoveWindow;

@end

/* XineOpenGLView delegate methods */
@protocol XineOpenGLViewDelegate

- (void)mouseDown:(NSEvent *)theEvent inXineView:(XineOpenGLView *)theView;
- (void)mouseMoved:(NSEvent *)theEvent inXineView:(XineOpenGLView *)theView;
- (void)otherMouseDown:(NSEvent *)theEvent inXineView:(XineOpenGLView *)theView;
- (void)rightMouseDown:(NSEvent *)theEvent inXineView:(XineOpenGLView *)theView;
- (NSSize)xineViewWillResize:(NSSize)oldSize toSize:(NSSize)proposedSize;
- (void)xineViewDidResize:(NSNotification *)note;

@end

#endif /* __HAVE_XINE_OPENGL_VIEW_H__ */

