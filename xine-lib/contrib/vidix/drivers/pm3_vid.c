/**
    Driver for 3DLabs GLINT R3 and Permedia3 chips.

    Copyright (C) 2002, 2003  Måns Rullgård

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
**/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/mman.h>

#include "vidix.h"
#include "fourcc.h"
#include "libdha.h"
#include "pci_ids.h"
#include "pci_names.h"

#include "pm3_regs.h"

#define VIDIX_STATIC pm3_

/* MBytes of video memory to use */
#define PM3_VIDMEM 24

#if 0
#define TRACE_ENTER() fprintf(stderr, "%s: enter\n", __FUNCTION__)
#define TRACE_EXIT() fprintf(stderr, "%s: exit\n", __FUNCTION__)
#else
#define TRACE_ENTER()
#define TRACE_EXIT()
#endif

static pciinfo_t pci_info;

void *pm3_reg_base;
static void *pm3_mem;

static int pm3_vidmem = PM3_VIDMEM;
static int pm3_blank = 0;
static int pm3_dma = 0;

static int pm3_ckey_red, pm3_ckey_green, pm3_ckey_blue;

static u_int page_size;

static vidix_capability_t pm3_cap =
{
    "3DLabs GLINT R3/Permedia3 driver",
    "Måns Rullgård <mru@users.sf.net>",
    TYPE_OUTPUT,
    { 0, 0, 0, 0 },
    2048,
    2048,
    4,
    4,
    -1,
    FLAG_UPSCALER | FLAG_DOWNSCALER,
    VENDOR_3DLABS,
    -1,
    { 0, 0, 0, 0 }
};


unsigned int VIDIX_NAME(vixGetVersion)(void)
{
    return(VIDIX_VERSION);
}

static unsigned short pm3_card_ids[] = 
{
    DEVICE_3DLABS_GLINT_R3
};

static int find_chip(unsigned chip_id)
{
  unsigned i;
  for(i = 0;i < sizeof(pm3_card_ids)/sizeof(unsigned short);i++)
  {
    if(chip_id == pm3_card_ids[i]) return i;
  }
  return -1;
}

int VIDIX_NAME(vixProbe)(int verbose, int force)
{
    pciinfo_t lst[MAX_PCI_DEVICES];
    unsigned i,num_pci;
    int err;

    err = pci_scan(lst,&num_pci);
    if(err)
    {
	printf("[pm3] Error occured during pci scan: %s\n",strerror(err));
	return err;
    }
    else
    {
	err = ENXIO;
	for(i=0; i < num_pci; i++)
	{
	    if(lst[i].vendor == VENDOR_3DLABS)
	    {
		int idx;
		const char *dname;
		idx = find_chip(lst[i].device);
		if(idx == -1)
		    continue;
		dname = pci_device_name(VENDOR_3DLABS, lst[i].device);
		dname = dname ? dname : "Unknown chip";
		printf("[pm3] Found chip: %s with IRQ %i\n",
		       dname, lst[i].irq);
		pm3_cap.device_id = lst[i].device;
		err = 0;
		memcpy(&pci_info, &lst[i], sizeof(pciinfo_t));
		break;
	    }
	}
    }
    if(err && verbose) printf("[pm3] Can't find chip\n");
    return err;
}

#define PRINT_REG(reg)							\
{									\
    long _foo = READ_REG(reg);						\
    printf("[pm3] " #reg " (%x) = %#lx (%li)\n", reg, _foo, _foo);	\
}

int VIDIX_NAME(vixInit)(const char *args)
{
    if(args != NULL){
	char *ac = strdup(args), *s, *opt;

	opt = strtok_r(ac, ",", &s);
	while(opt){
	    char *a = strchr(opt, '=');

	    if(a)
		*a++ = 0;
	    if(!strcmp(opt, "mem")){
		if(a)
		    pm3_vidmem = strtol(a, NULL, 0);
	    } else if(!strcmp(opt, "blank")){
		pm3_blank = a? strtol(a, NULL, 0): 1;
	    }

	    opt = strtok_r(NULL, ",", &s);
	}

	free(ac);
    }

    pm3_reg_base = map_phys_mem(pci_info.base0, 0x20000);
    pm3_mem = map_phys_mem(pci_info.base1, 0x2000000);

    if(bm_open() == 0){
	fprintf(stderr, "[pm3] DMA available.\n");
	pm3_cap.flags |= FLAG_DMA | FLAG_SYNC_DMA;
	page_size = sysconf(_SC_PAGESIZE);
	hwirq_install(pci_info.bus, pci_info.card, pci_info.func,
		      0, PM3IntFlags, -1);
	WRITE_REG(PM3IntEnable, (1 << 7));
	pm3_dma = 1;
    }

    RAMDAC_GET_REG(PM3RD_VideoOverlayKeyR, pm3_ckey_red);
    RAMDAC_GET_REG(PM3RD_VideoOverlayKeyG, pm3_ckey_green);
    RAMDAC_GET_REG(PM3RD_VideoOverlayKeyB, pm3_ckey_blue);

    return 0;
}

void VIDIX_NAME(vixDestroy)(void)
{
    if(pm3_dma)
	WRITE_REG(PM3IntEnable, 0);

    RAMDAC_SET_REG(PM3RD_VideoOverlayKeyR, pm3_ckey_red);
    RAMDAC_SET_REG(PM3RD_VideoOverlayKeyG, pm3_ckey_green);
    RAMDAC_SET_REG(PM3RD_VideoOverlayKeyB, pm3_ckey_blue);

    unmap_phys_mem(pm3_reg_base, 0x20000);
    unmap_phys_mem(pm3_mem, 0x2000000);
    hwirq_uninstall(pci_info.bus, pci_info.card, pci_info.func);
    bm_close();
}

int VIDIX_NAME(vixGetCapability)(vidix_capability_t *to)
{
    memcpy(to, &pm3_cap, sizeof(vidix_capability_t));
    return 0;
}

static int is_supported_fourcc(uint32_t fourcc)
{
    switch(fourcc){
    case IMGFMT_YUY2:
    case IMGFMT_UYVY:
	return 1;
    default:
	return 0;
    }
}

int VIDIX_NAME(vixQueryFourcc)(vidix_fourcc_t *to)
{
    if(is_supported_fourcc(to->fourcc))
    {
	to->depth = VID_DEPTH_1BPP | VID_DEPTH_2BPP |
		    VID_DEPTH_4BPP | VID_DEPTH_8BPP |
		    VID_DEPTH_12BPP| VID_DEPTH_15BPP|
		    VID_DEPTH_16BPP| VID_DEPTH_24BPP|
		    VID_DEPTH_32BPP;
	to->flags = VID_CAP_EXPAND | VID_CAP_SHRINK | VID_CAP_COLORKEY;
	return 0;
    }
    else  to->depth = to->flags = 0;
    return ENOSYS;
}

static int frames[VID_PLAY_MAXFRAMES], vid_base;
static int overlay_mode, overlay_control, video_control, int_enable;
static int rdoverlay_mode;
static int src_w, drw_w;
static int src_h, drw_h;
static int drw_x, drw_y;

#define FORMAT_RGB8888	PM3VideoOverlayMode_COLORFORMAT_RGB8888 
#define FORMAT_RGB4444	PM3VideoOverlayMode_COLORFORMAT_RGB4444
#define FORMAT_RGB5551	PM3VideoOverlayMode_COLORFORMAT_RGB5551
#define FORMAT_RGB565	PM3VideoOverlayMode_COLORFORMAT_RGB565
#define FORMAT_RGB332	PM3VideoOverlayMode_COLORFORMAT_RGB332
#define FORMAT_BGR8888	PM3VideoOverlayMode_COLORFORMAT_BGR8888
#define FORMAT_BGR4444	PM3VideoOverlayMode_COLORFORMAT_BGR4444
#define FORMAT_BGR5551	PM3VideoOverlayMode_COLORFORMAT_BGR5551
#define FORMAT_BGR565	PM3VideoOverlayMode_COLORFORMAT_BGR565
#define FORMAT_BGR332	PM3VideoOverlayMode_COLORFORMAT_BGR332
#define FORMAT_CI8	PM3VideoOverlayMode_COLORFORMAT_CI8
#define FORMAT_VUY444	PM3VideoOverlayMode_COLORFORMAT_VUY444
#define FORMAT_YUV444	PM3VideoOverlayMode_COLORFORMAT_YUV444
#define FORMAT_VUY422	PM3VideoOverlayMode_COLORFORMAT_VUY422
#define FORMAT_YUV422	PM3VideoOverlayMode_COLORFORMAT_YUV422

/* Notice, have to check that we dont overflow the deltas here ... */
static void
compute_scale_factor(int* src_w, int* dst_w,
		     u_int* shrink_delta, u_int* zoom_delta)
{
    /* NOTE: If we don't return reasonable values here then the video
     * unit can potential shut off and won't display an image until re-enabled.
     * Seems as though the zoom_delta is o.k, and I've not had the problem.
     * The 'shrink_delta' is prone to this the most - FIXME ! */

    if (*src_w >= *dst_w) {
	*src_w &= ~0x3;
	*dst_w &= ~0x3;
	*shrink_delta = (((*src_w << 16) / *dst_w) + 0x0f) & 0x0ffffff0;
	*zoom_delta = 1<<16;
	if ( ((*shrink_delta * *dst_w) >> 16) & 0x03 )
	    *shrink_delta += 0x10;
    } else {
	*src_w &= ~0x3;
	*dst_w &= ~0x3;
	*zoom_delta = (((*src_w << 16) / *dst_w) + 0x0f) & 0x0001fff0;
	*shrink_delta = 1<<16;
	if ( ((*zoom_delta * *dst_w) >> 16) & 0x03 )
	    *zoom_delta += 0x10;
    }
}

static void
pm3_setup_overlay(vidix_playback_t *info)
{
    u_int shrink, zoom;
    int format = 0;
    int filter = 0;
    int sw = src_w;

    switch(info->fourcc){
    case IMGFMT_YUY2:
	format = FORMAT_YUV422;
	break;
    case IMGFMT_UYVY:
	format = FORMAT_VUY422;
	break;
    }

    compute_scale_factor(&sw, &drw_w, &shrink, &zoom);

    WAIT_FIFO(9);
    WRITE_REG(PM3VideoOverlayBase0, vid_base >> 1);
    WRITE_REG(PM3VideoOverlayStride, PM3VideoOverlayStride_STRIDE(src_w));
    WRITE_REG(PM3VideoOverlayWidth, PM3VideoOverlayWidth_WIDTH(sw));
    WRITE_REG(PM3VideoOverlayHeight, PM3VideoOverlayHeight_HEIGHT(src_h));
    WRITE_REG(PM3VideoOverlayOrigin, 0);

    /* Scale the source to the destinationsize */
    if (src_w == drw_w) {
    	WRITE_REG(PM3VideoOverlayShrinkXDelta, 1<<16);
    	WRITE_REG(PM3VideoOverlayZoomXDelta, 1<<16);
    } else {
    	WRITE_REG(PM3VideoOverlayShrinkXDelta, shrink);
    	WRITE_REG(PM3VideoOverlayZoomXDelta, zoom);
	filter = PM3VideoOverlayMode_FILTER_PARTIAL;
    }
    if (src_h == drw_h) {
	WRITE_REG(PM3VideoOverlayYDelta, PM3VideoOverlayYDelta_NONE);
    } else {
	WRITE_REG(PM3VideoOverlayYDelta,
		  PM3VideoOverlayYDelta_DELTA(src_h, drw_h));
	filter = PM3VideoOverlayMode_FILTER_FULL;
    }

    WRITE_REG(PM3VideoOverlayIndex, 0);

    /* Now set the ramdac video overlay region and mode */
    RAMDAC_SET_REG(PM3RD_VideoOverlayXStartLow, (drw_x & 0xff));
    RAMDAC_SET_REG(PM3RD_VideoOverlayXStartHigh, (drw_x & 0xf00)>>8);
    RAMDAC_SET_REG(PM3RD_VideoOverlayXEndLow, (drw_x+drw_w) & 0xff);
    RAMDAC_SET_REG(PM3RD_VideoOverlayXEndHigh,
		   ((drw_x+drw_w) & 0xf00)>>8);
    RAMDAC_SET_REG(PM3RD_VideoOverlayYStartLow, (drw_y & 0xff)); 
    RAMDAC_SET_REG(PM3RD_VideoOverlayYStartHigh, (drw_y & 0xf00)>>8);
    RAMDAC_SET_REG(PM3RD_VideoOverlayYEndLow, (drw_y+drw_h) & 0xff); 
    RAMDAC_SET_REG(PM3RD_VideoOverlayYEndHigh,
		   ((drw_y+drw_h) & 0xf00)>>8);

    overlay_mode =
	1 << 5 |
	format |
	filter |
	PM3VideoOverlayMode_BUFFERSYNC_MANUAL |
	PM3VideoOverlayMode_FLIP_VIDEO;

    overlay_control = 
	PM3RD_VideoOverlayControl_KEY_COLOR |
	PM3RD_VideoOverlayControl_DIRECTCOLOR_ENABLED;
}

extern int
VIDIX_NAME(vixSetGrKeys)(const vidix_grkey_t *key)
{
    if(key->ckey.op == CKEY_TRUE){
	RAMDAC_SET_REG(PM3RD_VideoOverlayKeyR, key->ckey.red);
	RAMDAC_SET_REG(PM3RD_VideoOverlayKeyG, key->ckey.green);
	RAMDAC_SET_REG(PM3RD_VideoOverlayKeyB, key->ckey.blue);
	rdoverlay_mode = PM3RD_VideoOverlayControl_MODE_MAINKEY;
    } else {
	rdoverlay_mode = PM3RD_VideoOverlayControl_MODE_ALWAYS;
    }
    RAMDAC_SET_REG(PM3RD_VideoOverlayControl,
		   overlay_control | rdoverlay_mode);

    return 0;
}

extern int
VIDIX_NAME(vixGetGrKeys)(vidix_grkey_t *key)
{
    RAMDAC_GET_REG(PM3RD_VideoOverlayKeyR, key->ckey.red);
    RAMDAC_GET_REG(PM3RD_VideoOverlayKeyG, key->ckey.green);
    RAMDAC_GET_REG(PM3RD_VideoOverlayKeyB, key->ckey.blue);
    return 0;
}

extern int
VIDIX_NAME(vixConfigPlayback)(vidix_playback_t *info)
{
    unsigned int i;
    u_int frame_size;
    u_int vidmem_size;
    u_int max_frames;

    TRACE_ENTER();

    src_w = info->src.w;
    src_h = info->src.h;
    drw_w = info->dest.w;
    drw_h = info->dest.h;
    drw_x = info->dest.x;
    drw_y = info->dest.y;

    frame_size = src_w * src_h * 2;
    vidmem_size = pm3_vidmem*1024*1024;
    max_frames = vidmem_size / frame_size;
    if(max_frames > VID_PLAY_MAXFRAMES)
	max_frames = VID_PLAY_MAXFRAMES;

    src_h--; /* ugh */

    if(info->num_frames > max_frames)
	info->num_frames = max_frames;
    vidmem_size = info->num_frames * frame_size;

    /* Use end of video memory. Assume the card has 32 MB */
    vid_base = 32*1024*1024 - vidmem_size;
    info->dga_addr = pm3_mem + vid_base;

    info->dest.pitch.y = 2;
    info->dest.pitch.u = 0;
    info->dest.pitch.v = 0;
    info->offset.y = 0;
    info->offset.v = 0;
    info->offset.u = 0;
    info->frame_size = frame_size;

    for(i = 0; i < info->num_frames; i++){
	info->offsets[i] = frame_size * i;
	frames[i] = (vid_base + info->offsets[i]) >> 1;
    }

    pm3_setup_overlay(info);

    video_control = READ_REG(PM3VideoControl);
    int_enable = READ_REG(PM3IntEnable);

    TRACE_EXIT();
    return 0;
}

int VIDIX_NAME(vixPlaybackOn)(void)
{
    TRACE_ENTER();

    WRITE_REG(PM3VideoOverlayMode,
	      overlay_mode | PM3VideoOverlayMode_ENABLE);
    overlay_control |= PM3RD_VideoOverlayControl_ENABLE;
    RAMDAC_SET_REG(PM3RD_VideoOverlayControl,
		   overlay_control | rdoverlay_mode);
    WRITE_REG(PM3VideoOverlayUpdate, PM3VideoOverlayUpdate_ENABLE);

    if(pm3_blank)
	WRITE_REG(PM3VideoControl,
		  video_control | PM3VideoControl_DISPLAY_ENABLE);

    TRACE_EXIT();
    return 0;
}

int VIDIX_NAME(vixPlaybackOff)(void)
{
    overlay_control &= ~PM3RD_VideoOverlayControl_ENABLE;
    RAMDAC_SET_REG(PM3RD_VideoOverlayControl,
		   PM3RD_VideoOverlayControl_DISABLE);
    WRITE_REG(PM3VideoOverlayMode,
	      PM3VideoOverlayMode_DISABLE);

    if(video_control)
	WRITE_REG(PM3VideoControl,
		  video_control & ~PM3VideoControl_DISPLAY_ENABLE);

    return 0;
}

int VIDIX_NAME(vixPlaybackFrameSelect)(unsigned int frame)
{
    WRITE_REG(PM3VideoOverlayBase0, frames[frame]);

    return 0;
}

struct pm3_bydma_cmd {
    uint32_t bus_addr;
    uint32_t fb_addr;
    uint32_t mask;
    uint32_t count;
};

struct pm3_bydma_frame {
    struct pm3_bydma_cmd *cmds;
    u_long bus_addr;
    uint32_t count;
};

static struct pm3_bydma_frame *
pm3_setup_bydma(vidix_dma_t *dma, struct pm3_bydma_frame *bdf)
{
    u_int size = dma->size;
    u_int pages = (size + page_size-1) / page_size;
    unsigned long baddr[pages];
    u_int i;
    uint32_t dest;

    if(bm_virt_to_bus(dma->src, dma->size, baddr))
	return NULL;

    if(!bdf){
	bdf = malloc(sizeof(*bdf));
	bdf->cmds = valloc(pages * sizeof(struct pm3_bydma_cmd));
	if(dma->flags & BM_DMA_FIXED_BUFFS){
	    mlock(bdf->cmds, page_size);
	}
    }

    dest = vid_base + dma->dest_offset;
    for(i = 0; i < pages; i++, dest += page_size, size -= page_size){
	bdf->cmds[i].bus_addr = baddr[i];
	bdf->cmds[i].fb_addr = dest;
	bdf->cmds[i].mask = ~0;
	bdf->cmds[i].count = ((size > page_size)? page_size: size) / 16;
    }

    bdf->count = pages;

    if(bm_virt_to_bus(bdf->cmds, page_size, &bdf->bus_addr) != 0){
	free(bdf->cmds);
	free(bdf);
	return NULL;
    }

    return bdf;
}

extern int
VIDIX_NAME(vixPlaybackCopyFrame)(vidix_dma_t *dma)
{
    u_int frame = dma->idx;
    struct pm3_bydma_frame *bdf;

    bdf = dma->internal[frame];
    if(!bdf || !(dma->flags & BM_DMA_FIXED_BUFFS))
	bdf = pm3_setup_bydma(dma, bdf);
    if(!bdf)
	return -1;

    if(!dma->internal[frame])
	dma->internal[frame] = bdf;

    if(dma->flags & BM_DMA_SYNC){
	hwirq_wait(pci_info.irq);
    }

    WAIT_FIFO(3);
    WRITE_REG(PM3ByDMAReadCommandBase, bdf->bus_addr);
    WRITE_REG(PM3ByDMAReadCommandCount, bdf->count);
    WRITE_REG(PM3ByDMAReadMode,
	      PM3ByDMAReadMode_ByteSwap_NONE |
	      PM3ByDMAReadMode_Format_RAW |
	      PM3ByDMAReadMode_PixelSize(16) |
	      PM3ByDMAReadMode_Active |
	      PM3ByDMAReadMode_Burst(7) |
	      PM3ByDMAReadMode_Align);

    if(dma->flags & BM_DMA_BLOCK){
	hwirq_wait(pci_info.irq);
    }

    return 0;
}

extern int
VIDIX_NAME(vixQueryDMAStatus)(void)
{
    uint32_t bdm = READ_REG(PM3ByDMAReadMode);
    return (bdm & PM3ByDMAReadMode_Active)? 1: 0;
}
