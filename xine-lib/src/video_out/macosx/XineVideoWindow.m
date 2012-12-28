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
 * Written by Daniel Mack <xine@zonque.org>
 * 
 * Most parts of this code were taken from VLC, http://www.videolan.org
 * Thanks for the good research!
 */


#import "XineOpenGLView.h"
#import "XineVideoWindow.h"


#define DEFAULT_VIDEO_WINDOW_SIZE (NSMakeSize(320, 200))

@implementation XineVideoWindow

- (void) setContentSize: (NSSize) size
{
#ifdef LOG
    NSLog(@"setContent called with new size w:%d h:%d", size.width, size.height);
#endif
    [xineView setViewSizeInMainThread:size];
	
    [super setContentSize: size];
}

- (id) init
{
    return [self initWithContentSize:DEFAULT_VIDEO_WINDOW_SIZE];
}

- (id) initWithContentSize:(NSSize)size
{
    NSScreen *screen = [NSScreen mainScreen];
    NSSize screen_size = [screen frame].size;
    
    /* make a centered window */
    NSRect frame;
    frame.size = size;
    frame.origin.x = (screen_size.width - frame.size.width) / 2;
    frame.origin.y = (screen_size.height - frame.size.height) / 2;
	
    unsigned int style_mask = NSTitledWindowMask | NSMiniaturizableWindowMask |
        NSClosableWindowMask | NSResizableWindowMask;
	
    return ([self initWithContentRect:frame styleMask:style_mask
                              backing:NSBackingStoreBuffered defer:NO
                               screen:screen]);
}

- (id) initWithContentRect: (NSRect)rect 
				 styleMask:(unsigned int)styleMask 
				   backing:(NSBackingStoreType)bufferingType 
					 defer:(BOOL)flag 
					screen:(NSScreen *)aScreen
{
    self = [super initWithContentRect: rect
							styleMask: styleMask
							  backing: bufferingType
								defer: flag
							   screen: aScreen];
	
#ifdef LOG
    NSLog(@"initWithContentRect called with rect x:%d y:%d w:%d h:%d",
          rect.origin.x, rect.origin.y, rect.size.width, rect.size.height);
#endif
	
    xineView = [[XineOpenGLView alloc] initWithFrame:rect];
    [xineView setResizeViewOnVideoSizeChange:YES];
	
    /* receive notifications about window resizing from the xine view */
    NSNotificationCenter *noticeBoard = [NSNotificationCenter defaultCenter];
    [noticeBoard addObserver:self
                    selector:@selector(xineViewDidResize:)
                        name:XineViewDidResizeNotification
                      object:xineView];
	
    [self setContentView: xineView];
    [self setTitle: @"xine video output"];
	
    return self;
}

- (void) dealloc
{
    [xineView release];
    xineView = nil;
	
    [super dealloc];
}


- (id) xineView
{
    return xineView;
}

- (NSRect)windowWillUseStandardFrame:(NSWindow *)sender
                        defaultFrame:(NSRect)defaultFrame
{
    NSSize screen_size, video_size;
    NSRect standard_frame;
	
    if ([xineView isFullScreen])
        return defaultFrame;
	
    screen_size = defaultFrame.size;
    video_size = [xineView videoSize];
	
    if (screen_size.width / screen_size.height >
        video_size.width / video_size.height)
	{
        standard_frame.size.width  = video_size.width *
		(screen_size.height / video_size.height);
        standard_frame.size.height = screen_size.height;
    } 
	else
	{
        standard_frame.size.width  = screen_size.width;
        standard_frame.size.height = video_size.height *
			(screen_size.width / video_size.width);
    }
	
    standard_frame.origin.x =
        (screen_size.width - standard_frame.size.width) / 2;
    standard_frame.origin.y =
        (screen_size.height - standard_frame.size.height) / 2;
	
    return standard_frame;
}


/* Notifications */

- (void) xineViewDidResize:(NSNotification *)note
{
	NSRect frame = [self frame];
	frame.size = [[self contentView] frame].size;
	
	[self setFrame:[self frameRectForContentRect:frame] display:YES];
}

@end /* XineVideoWindow */


@implementation NSWindow (AspectRatioAdditions)

- (void) setKeepsAspectRatio: (BOOL) flag {
    if (flag) {
        NSSize size = [self frame].size;
        [self setAspectRatio:size];
    }
    else {
        [self setResizeIncrements:NSMakeSize(1.0, 1.0)];
    }
}

/* XXX: This is 100% untested ... */
- (BOOL) keepsAspectRatio {
    NSSize size = [self aspectRatio];
    if (size.width == 0 && size.height == 0)
        return false;
    else
        return true;
}

@end /* NSWindow (AspectRatioAdditions) */

