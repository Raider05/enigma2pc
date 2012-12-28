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

/*
#define LOG
*/

#import <Cocoa/Cocoa.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl.h>
#import <OpenGL/glext.h>

#import "XineOpenGLView.h"

#include <xine/xineutils.h>

NSString *XineViewDidResizeNotification EXPORTED = @"XineViewDidResizeNotification";

static uint32_t
NSColorToYUV(NSColor *color)
{
    float           red, green, blue, alpha;
    uint32_t        yuv;
    unsigned char   r, g, b;
    unsigned char   y, u, v;

    NSColor *calibratedColor = [color colorUsingColorSpaceName:NSCalibratedRGBColorSpace];
    [calibratedColor getRed:&red green:&green blue:&blue alpha:&alpha];

    r = red * 255;
    g = green * 255;
    b = blue * 255;

    init_yuv_conversion();

    y = COMPUTE_Y(r, g, b);
    u = COMPUTE_U(r, g, b);
    v = COMPUTE_V(r, g, b);

    yuv = (y << 24) | (u << 16) | (y << 8) | v;
    return yuv;
}

@implementation XineOpenGLView

+ (NSOpenGLPixelFormat *)defaultPixelFormat
{
    NSOpenGLPixelFormatAttribute attributes[] = {
        NSOpenGLPFAAccelerated,
        NSOpenGLPFANoRecovery,
        NSOpenGLPFADoubleBuffer,
        NSOpenGLPFAColorSize,    24,
        NSOpenGLPFAAlphaSize,    8,
        NSOpenGLPFADepthSize,    24,
        NSOpenGLPFAWindow,
        0
    };

    return [[[NSOpenGLPixelFormat alloc] initWithAttributes:attributes] autorelease];
}

+ (NSOpenGLPixelFormat *)fullScreenPixelFormat
{
    NSOpenGLPixelFormatAttribute attributes[] = {
        NSOpenGLPFAAccelerated,
        NSOpenGLPFANoRecovery,
        NSOpenGLPFADoubleBuffer,
        NSOpenGLPFAColorSize,    24,
        NSOpenGLPFAAlphaSize,    8,
        NSOpenGLPFADepthSize,    24,
        NSOpenGLPFAFullScreen,
        NSOpenGLPFAScreenMask,   CGDisplayIDToOpenGLDisplayMask(kCGDirectMainDisplay),
        0
    };
    
    return [[[NSOpenGLPixelFormat alloc] initWithAttributes:attributes] autorelease];
}

- (id)initWithCoder:(NSCoder *)coder
{
    NSColor *color;

    if ((self = [super initWithCoder:coder]) != nil) {
        videoSize     = [self frame].size;
        mutex         = [[NSRecursiveLock alloc] init];
        currentCursor = [[NSCursor arrowCursor] retain];

        if ([coder allowsKeyedCoding]) {
            keepsVideoAspectRatio       = [coder decodeBoolForKey:@"keepsVideoAspectRatio"];
            resizeViewOnVideoSizeChange = [coder decodeBoolForKey:@"resizeViewOnVideoSizeChange"];
            color                       = [coder decodeObjectForKey:@"initialColor"];
        }
        else {  /* Must decode values in the same order as encodeWithCoder: */
            [coder decodeValueOfObjCType:@encode(BOOL) at:&keepsVideoAspectRatio];
            [coder decodeValueOfObjCType:@encode(BOOL) at:&resizeViewOnVideoSizeChange];
            color = [coder decodeObject];
        }
        [self setInitialColor:color];

#ifdef LOG
        NSLog(@"XineOpenGLView: initWithCoder called");
#endif
    }
    return self;
}

- (id)initWithFrame:(NSRect)frame
{
    return [self initWithFrame:frame pixelFormat:[[self class] defaultPixelFormat]];
}

- (id)initWithFrame:(NSRect)frame pixelFormat:(NSOpenGLPixelFormat *)format
{
    format = (format ? : [[self class] defaultPixelFormat]);
    if ((self = [super initWithFrame:frame pixelFormat:format]) != nil) {
        videoSize     = frame.size;
        mutex         = [[NSRecursiveLock alloc] init];
        currentCursor = [[NSCursor arrowCursor] retain];
        [self setInitialColor:nil];

#ifdef LOG
        NSLog(@"XineOpenGLView: initWithFrame called");
#endif
    }
    return self;
}

- (void)dealloc
{
    if (isFullScreen) {
        [self exitFullScreen];
    }

    if (texture) {
        [[self openGLContext] makeCurrentContext];
        glDeleteTextures(1, &texture);
        texture = 0;
    }
    free(textureBuffer);

    [currentCursor release], currentCursor = nil;
    [initialColor  release], initialColor  = nil;
    [delegate      release], delegate      = nil;
    [controller    release], controller    = nil;
    [mutex         release], mutex         = nil;

    [super dealloc];
}

- (void)encodeWithCoder:(NSCoder *)coder
{
    [super encodeWithCoder:coder];
    
    if ([coder allowsKeyedCoding]) {
        [coder encodeBool:keepsVideoAspectRatio forKey:@"keepsVideoAspectRatio"];
        [coder encodeBool:resizeViewOnVideoSizeChange forKey:@"resizeViewOnVideoSizeChange"];
        [coder encodeObject:initialColor forKey:@"initialColor"];
    }
    else {
        [coder encodeValueOfObjCType:@encode(BOOL) at:&keepsVideoAspectRatio];
        [coder encodeValueOfObjCType:@encode(BOOL) at:&resizeViewOnVideoSizeChange];
        [coder encodeObject:initialColor];
    }
}

- (NSOpenGLContext *)openGLContext
{
    NSOpenGLContext *context;

    [mutex lock];
    if (!(context = [[fullScreenContext retain] autorelease])) {
        context = [[[super openGLContext] retain] autorelease];
    }
    else if (!isFullScreenPrepared) {
        [self prepareOpenGL];
        isFullScreenPrepared = YES;
    }
    [mutex unlock];

    return context;
}

// NOTE: This does not exist prior to Panther (10.3)
- (void)prepareOpenGL
{
    long    swapInterval = 1;

    [mutex lock];
    [super prepareOpenGL];

    [[self openGLContext] setValues:&swapInterval forParameter:NSOpenGLCPSwapInterval];

    [self initTextures];

    /* Set GL_COLOR_BUFFER_BIT to black */
    glClearColor (0.0, 0.0, 0.0, 0.0);

    [mutex unlock];
}

- (void)reshape
{
    [mutex lock];
    [super reshape];
    if (initDone) {
        [[self openGLContext] makeCurrentContext];
        
        NSRect bounds = [self bounds];
        glViewport(0, 0, bounds.size.width, bounds.size.height);
        
#ifdef LOG
        NSLog(@"XineOpenGLView: Reshape: %x%x%x%x%x%x%x%x",
              textureBuffer[0], textureBuffer[1], textureBuffer[2], textureBuffer[3],
              textureBuffer[4], textureBuffer[5], textureBuffer[6], textureBuffer[7]);
#endif
    }
    [mutex unlock];
}

- (void)update
{
    [mutex lock];
    [super update];
    [mutex unlock];
}

- (void)initTextures
{
    uint32_t    *p, *q, yuv;

    [mutex lock];
    
    /* Free previous texture if any */
    if (texture) {
        glDeleteTextures(1, &texture);
        texture = 0;
    }
    
    if (!initialColorYUVIsSet && initialColor) {
        initialColorYUV = NSColorToYUV(initialColor);
        initialColorYUVIsSet = YES;
    }

    if (textureBuffer) {
        textureBuffer = (char *)realloc(textureBuffer, videoSize.width * videoSize.height * 4);
    }
    else {
        textureBuffer = (char *)malloc(videoSize.width * videoSize.height * 4);

        // There _has_ to be a better way of doing this ...

        yuv = OSSwapHostToBigInt32(initialColorYUV);
        q = (uint32_t *)(char *)(textureBuffer + (int)(videoSize.width * videoSize.height * 4));
        for (p = (uint32_t *)textureBuffer;  p < q;  *p++ = yuv);
    }

    /* Create textures */
    glGenTextures(1, &texture);
    
    glEnable(GL_TEXTURE_RECTANGLE_EXT);
    glEnable(GL_UNPACK_CLIENT_STORAGE_APPLE);
    
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, videoSize.width);
    
    glBindTexture(GL_TEXTURE_RECTANGLE_EXT, texture);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    
    /* Use VRAM texturing */
    glTexParameteri(GL_TEXTURE_RECTANGLE_EXT,
                    GL_TEXTURE_STORAGE_HINT_APPLE, GL_STORAGE_CACHED_APPLE);
    
    /* Tell the driver not to make a copy of the texture but to use
        our buffer */
    glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_TRUE);
    
    /* Linear interpolation */
    glTexParameteri(GL_TEXTURE_RECTANGLE_EXT,
                    GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_RECTANGLE_EXT,
                    GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    /* I have no idea what this exactly does, but it seems to be
        necessary for scaling */
    glTexParameteri(GL_TEXTURE_RECTANGLE_EXT,
                    GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_RECTANGLE_EXT,
                    GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glTexImage2D(GL_TEXTURE_RECTANGLE_EXT, 0, GL_RGBA,
                 videoSize.width, videoSize.height, 0, GL_YCBCR_422_APPLE,
#if WORDS_BIGENDIAN
                 GL_UNSIGNED_SHORT_8_8_APPLE,
#else
                 GL_UNSIGNED_SHORT_8_8_REV_APPLE,
#endif
                 textureBuffer);
    
    initDone = YES;
    [mutex unlock];

#ifdef LOG
    NSLog(@"XineOpenGLView: initTextures called: %hhx %hhx %hhx %hhx %hhx %hhx %hhx %hhx",
          textureBuffer[0], textureBuffer[1], textureBuffer[2], textureBuffer[3],
          textureBuffer[4], textureBuffer[5], textureBuffer[6], textureBuffer[7]);
#endif
}

- (void)updateTexture
{
    [mutex lock];
    [[self openGLContext] makeCurrentContext];

    glBindTexture(GL_TEXTURE_RECTANGLE_EXT, texture);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, videoSize.width);

    // glTexSubImage2D is faster than glTexImage2D
    // http://developer.apple.com/samplecode/Sample_Code/Graphics_3D/TextureRange/MainOpenGLView.m.htm
    glTexSubImage2D(GL_TEXTURE_RECTANGLE_EXT, 0, 0, 0,
                    videoSize.width, videoSize.height, GL_YCBCR_422_APPLE,
#if WORDS_BIGENDIAN
                    GL_UNSIGNED_SHORT_8_8_APPLE,
#else
                    GL_UNSIGNED_SHORT_8_8_REV_APPLE,
#endif
                    textureBuffer);

    [self setNeedsDisplay:YES];
    [mutex unlock];
}

- (void)drawRect:(NSRect)rect
{
    [mutex lock];
    if (initDone && texture) {
        glBindTexture(GL_TEXTURE_RECTANGLE_EXT, texture);
        glBegin(GL_QUADS);
            glTexCoord2f(0.0,             0.0);               glVertex2f(-1.0,  1.0);   // top left
            glTexCoord2f(0.0,             videoSize.height);  glVertex2f(-1.0, -1.0);   // bottom left
            glTexCoord2f(videoSize.width, videoSize.height);  glVertex2f( 1.0, -1.0);   // bottom right
            glTexCoord2f(videoSize.width, 0.0);               glVertex2f( 1.0,  1.0);   // top right
        glEnd();
        [[self openGLContext] flushBuffer];
    }
    [mutex unlock];
}

- (NSColor *)initialColor
{
    return initialColor;
}

- (void) setInitialColor:(NSColor *)color
{
    [initialColor autorelease];
    initialColor = (color ? [color copy] : [[NSColor blackColor] retain]);
}

- (void)setNormalSize
{
    [mutex lock];
    if (!isFullScreen) {
        [self setViewSizeInMainThread:videoSize];
    }
    [mutex unlock];
}

- (void)setHalfSize
{
    NSSize size;

    [mutex lock];
    if (!isFullScreen) {
        size.width  = trunc(videoSize.width  / 2);
        size.height = trunc(videoSize.height / 2);
        [self setViewSizeInMainThread:size];
    }
    [mutex unlock];
}

- (void)setDoubleSize
{
    NSSize size;

    [mutex lock];
    if (!isFullScreen) {
        size.width  = videoSize.width  * 2;
        size.height = videoSize.height * 2;
        [self setViewSizeInMainThread:size];
    }
    [mutex unlock];
}

- (NSSize)videoSize
{
    return videoSize;
}

- (BOOL)keepsVideoAspectRatio
{
    return keepsVideoAspectRatio;
}

- (void)setKeepsVideoAspectRatio:(BOOL)flag
{
    keepsVideoAspectRatio = flag;
}

- (BOOL)resizeViewOnVideoSizeChange
{
    return resizeViewOnVideoSizeChange;
}

- (void)setResizeViewOnVideoSizeChange:(BOOL)flag
{
    resizeViewOnVideoSizeChange = flag;
}

- (void)setViewSize:(NSValue *)sizeWrapper
{
    NSSize  currentSize, newSize, proposedSize;
    
    [sizeWrapper getValue:&proposedSize];
    newSize = proposedSize;
    
    currentSize = [self frame].size;
    if (proposedSize.width == currentSize.width &&
        proposedSize.height == currentSize.height)
    {
        return;
    }
    
    // If our controller handles xineViewWillResize:toSize:, send the
    // message to him first.  Note that the delegate still has a chance
    // to override the controller's resize preference ...
    if ([controller respondsToSelector:@selector(xineViewWillResize:toSize:)]) {
        NSSize oldSize = [self frame].size;
        newSize = [controller xineViewWillResize:oldSize toSize:proposedSize];
    }
    
    // If our delegate handles xineViewWillResize:toSize:, send the
    // message to him; otherwise, just resize ourselves
    if ([delegate respondsToSelector:@selector(xineViewWillResize:toSize:)]) {
        NSSize oldSize = [self frame].size;
        newSize = [delegate xineViewWillResize:oldSize toSize:proposedSize];
    }
    
    [self setFrameSize:newSize];
    [self setBoundsSize:newSize];
    
    /* Post a notification that we resized and also notify our controller */
    /* and delegate */
    NSNotification *note =
        [NSNotification notificationWithName:XineViewDidResizeNotification
                                      object:self];
    [[NSNotificationCenter defaultCenter] postNotification:note];
    
    if ([controller respondsToSelector:@selector(xineViewDidResize:)]) {
        [controller xineViewDidResize:note];
    }
    
    if ([delegate respondsToSelector:@selector(xineViewDidResize:)]) {
        [delegate xineViewDidResize:note];
    }

    [mutex lock];
    [[self openGLContext] makeCurrentContext];
    if (isFullScreen) {
        [self calcFullScreenAspect];
    }
    [self initTextures];
    [mutex unlock];
}

- (void)setViewSizeInMainThread:(NSSize)size
{
    // Create an autorelease pool, since we're running in a xine thread that
    // may not have a pool of its own
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    NSValue *sizeWrapper = [NSValue valueWithBytes:&size
                                          objCType:@encode(NSSize)];
    
    [self performSelectorOnMainThread:@selector(setViewSize:)
                           withObject:sizeWrapper
                        waitUntilDone:NO];
    
#ifdef LOG
    NSLog(@"setViewSizeInMainThread called");
#endif
    
    [pool release];
}

- (NSCursor *)currentCursor
{
    return currentCursor;
}

- (void)setCurrentCursor:(NSCursor *)cursor
{
    [currentCursor autorelease];
    currentCursor = [cursor retain];
    [self resetCursorRectsInMainThread];
}

- (BOOL)isFullScreen
{
    return isFullScreen;
}

- (void)goFullScreen:(XineVideoWindowFullScreenMode)mode
{
    NSOpenGLPixelFormat *pixelFormat;

    if (!(pixelFormat = [[self class] fullScreenPixelFormat])) {
        NSLog(@"Cannot create NSOpenGLPixelFormat for full screen mode");
        return;
    }

    if (!(fullScreenContext = [[NSOpenGLContext alloc] initWithFormat:pixelFormat shareContext:nil])) {
        NSLog(@"Cannot create NSOpenGLContext for full screen mode");
        return;
    }

    if (CGCaptureAllDisplays() != CGDisplayNoErr) {
        [fullScreenContext release], fullScreenContext = nil;
        NSLog(@"CGCaptureAllDisplays() failed");
        return;
    }

    [mutex lock];
    fullScreenMode = mode;
    isFullScreenPrepared = NO;

    [fullScreenContext setFullScreen];
    [[self openGLContext] makeCurrentContext];

    // Redraw the last picture
    [self setNeedsDisplay:YES];

    isFullScreen = YES;
    [mutex unlock];
}

- (void)exitFullScreen
{
    NSOpenGLContext *context;

    [mutex lock];
    if (isFullScreen) {
        context = fullScreenContext;
        fullScreenContext = nil;
        [[self openGLContext] makeCurrentContext];

        [context clearDrawable];
        [context release];

        [self reshape];
        [self initTextures];

        CGReleaseAllDisplays();
        [self setNeedsDisplay:YES];
        isFullScreen = NO;
    }
    [mutex unlock];
}

- (id)delegate
{
    return [[delegate retain] autorelease];
}

- (void)setDelegate:(id)aDelegate
{
    [delegate autorelease];
    delegate = [aDelegate retain];
}

- (id)xineController
{
    return controller;
}

- (void)setXineController:(id)aController
{
    [controller autorelease];
    controller = [aController retain];
}

- (char *)textureBuffer
{
    return textureBuffer;
}

- (void)setVideoSize:(NSSize)size
{
    [mutex lock];
    videoSize = size;
    if (resizeViewOnVideoSizeChange) {
        [self setViewSizeInMainThread:size];
    }

    if (initDone) {
        [[self openGLContext] makeCurrentContext];
        [self initTextures];
    }
    [mutex unlock];
}

- (void)resetCursorRects
{
    [mutex lock];
    [self discardCursorRects];
    [self addCursorRect:[self visibleRect] cursor:currentCursor];
    [currentCursor set];
    [mutex unlock];
}

- (void)resetCursorRectsInMainThread
{
    [self performSelectorOnMainThread:@selector(resetCursorRects)
                           withObject:nil
                        waitUntilDone:NO];
}

- (void)releaseInMainThread
{
    [self performSelectorOnMainThread:@selector(release)
                           withObject:nil
                        waitUntilDone:NO];
}

- (void)calcFullScreenAspect
{
    float   fs_height, fs_width, h, w, x, y;

    // Feh, should go to main or should go to current display of window?
    fs_width  = CGDisplayPixelsWide(kCGDirectMainDisplay);
    fs_height = CGDisplayPixelsHigh(kCGDirectMainDisplay);

    switch (fullScreenMode) {
        case XINE_FULLSCREEN_OVERSCAN:
            if ((fs_width / fs_height) > (videoSize.width / videoSize.height)) {
                w = videoSize.width * (fs_height / videoSize.height);
                h = fs_height;
                x = (fs_width - w) / 2;
                y = 0;
            }
            else {
                w = fs_width;
                h = videoSize.height * (fs_width / videoSize.width);
                x = 0;
                y = (fs_height - h) / 2;
            }
            break;
            
        case XINE_FULLSCREEN_CROP:
            if ((fs_width / fs_height) > (videoSize.width / videoSize.height)) {
                w = fs_width;
                h = videoSize.height * (fs_width / videoSize.width);
                x = 0;
                y = (fs_height - h) / 2;
            }
            else {
                w = videoSize.width * (fs_height / videoSize.height);
                h = fs_height;
                x = (fs_width - w) / 2;
                y = 0;
            }
            break;

        default:
            NSLog(@"Mac OS X fullscreen mode unrecognized: %d", fullScreenMode);
            return;
    }
    
#ifdef LOG
    NSLog(@"Mac OS X fullscreen mode: %fx%f => %fx%f @ %f,%f\n",
          videoSize.width, videoSize.height, w, h, x, y);
#endif

    // Assumes locked and current context set
    glViewport(x, y, w, h);
}

- (void)passEventToDelegate:(NSEvent *)theEvent withSelector:(SEL)selector
{
    NSPoint point = [self convertPoint:[theEvent locationInWindow]
                              fromView:nil];
    
    if (NSMouseInRect(point, [self bounds], [self isFlipped])) {
        if ([delegate respondsToSelector:selector]) {
            [delegate performSelector:selector
                           withObject:theEvent
                           withObject:self];
        }
        else if ([controller respondsToSelector:selector]) {
            [controller performSelector:selector
                                  withObject:theEvent
                                  withObject:self];
        }
    }
}

- (void)mouseMoved:(NSEvent *)theEvent
{
    [self passEventToDelegate:theEvent
                 withSelector:@selector(mouseMoved:inXineView:)];
    
    [super mouseMoved:theEvent];
}

- (void)mouseDown:(NSEvent *)theEvent
{
    [self passEventToDelegate:theEvent
                 withSelector:@selector(mouseDown:inXineView:)];
    
    [super mouseDown:theEvent];
}

- (void)rightMouseDown:(NSEvent *)theEvent
{
    [self passEventToDelegate:theEvent
                 withSelector:@selector(rightMouseDown:inXineView:)];
    
    [super rightMouseDown:theEvent];
}

- (void)otherMouseDown:(NSEvent *)theEvent
{
    [self passEventToDelegate:theEvent
                 withSelector:@selector(otherMouseDown:inXineView:)];
    
    [super otherMouseDown:theEvent];
}

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (BOOL)mouseDownCanMoveWindow
{
    return YES;
}

@end
