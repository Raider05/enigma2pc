#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>
#include <fcntl.h>

#include "../vidix.h"
#include "../fourcc.h"
#include "../../libdha/libdha.h"
#include "../../libdha/pci_ids.h"
#include "../../libdha/pci_names.h"

#define DEMO_DRIVER 1
#define VIDIX_STATIC genfb_

#define GENFB_MSG "[genfb-demo-driver] "

#if 0 /* these are unused. remove? */
static int fd;

static void *mmio_base = 0;
static void *mem_base = 0;
static int32_t overlay_offset = 0;
static uint32_t ram_size = 0;
#endif

static int probed = 0;

/* VIDIX exports */

static vidix_capability_t genfb_cap =
{
    "General Framebuffer",
    "alex",
    TYPE_OUTPUT,
    { 0, 0, 0, 0 },
    2048,
    2048,
    4,
    4,
    -1,
    FLAG_UPSCALER|FLAG_DOWNSCALER,
    -1,
    -1,
    { 0, 0, 0, 0 }
};

unsigned int VIDIX_NAME(vixGetVersion)(void)
{
    return(VIDIX_VERSION);
}

int VIDIX_NAME(vixProbe)(int verbose,int force)
{
#if 0
    int err = 0;
#ifdef DEMO_DRIVER
    err = ENOSYS;
#endif
    
    printf(GENFB_MSG"probe\n");

    fd = open("/dev/fb0", O_RDWR);
    if (fd < 0)
    {
	printf(GENFB_MSG"Error occured durint open: %s\n", strerror(errno));
	err = errno;
    }
    
    probed = 1;

    return(err);
#else
  pciinfo_t lst[MAX_PCI_DEVICES];
  unsigned i,num_pci;
  int err;
  err = pci_scan(lst,&num_pci);
  if(err)
  {
    printf(GENFB_MSG"Error occured during pci scan: %s\n",strerror(err));
    return err;
  }
  else
  {
    err = ENXIO;
    for(i=0;i<num_pci;i++)
    {
	if(verbose)
	    printf(GENFB_MSG" Found chip [%04X:%04X] '%s' '%s'\n"
	    ,lst[i].vendor
	    ,lst[i].device
	    ,pci_vendor_name(lst[i].vendor)
	    ,pci_device_name(lst[i].vendor,lst[i].device));
    }
  }
  return ENOSYS;
#endif
}

int VIDIX_NAME(vixInit)(const char *args)
{
    printf(GENFB_MSG"init\n");
    
    if (!probed)
    {
	printf(GENFB_MSG"Driver was not probed but is being initialized\n");
	return(EINTR);
    }

    return(0);
}

void VIDIX_NAME(vixDestroy)(void)
{
    printf(GENFB_MSG"destory\n");
    return;
}

int VIDIX_NAME(vixGetCapability)(vidix_capability_t *to)
{
    memcpy(to, &genfb_cap, sizeof(vidix_capability_t));
    return(0);
}

int VIDIX_NAME(vixQueryFourcc)(vidix_fourcc_t *to)
{
    printf(GENFB_MSG"query fourcc (%x)\n", to->fourcc);

    to->depth = VID_DEPTH_1BPP | VID_DEPTH_2BPP |
		VID_DEPTH_4BPP | VID_DEPTH_8BPP |
		VID_DEPTH_12BPP | VID_DEPTH_15BPP |
		VID_DEPTH_16BPP | VID_DEPTH_24BPP |
		VID_DEPTH_32BPP;

    to->flags = 0;
    return(0);
}

int VIDIX_NAME(vixConfigPlayback)(vidix_playback_t *info)
{
    printf(GENFB_MSG"config playback\n");

    info->num_frames = 2;
    info->frame_size = info->src.w*info->src.h+(info->src.w*info->src.h)/2;
    info->dest.pitch.y = 32;
    info->dest.pitch.u = info->dest.pitch.v = 16;
    info->offsets[0] = 0;
    info->offsets[1] = info->frame_size;
    info->offset.y = 0;
    info->offset.v = ((info->src.w+31) & ~31) * info->src.h;
    info->offset.u = info->offset.v+((info->src.w+31) & ~31) * info->src.h/4;    
    info->dga_addr = malloc(info->num_frames*info->frame_size);   
    printf(GENFB_MSG"frame_size: %d, dga_addr: %p\n",
	info->frame_size, info->dga_addr);

    return(0);
}

int VIDIX_NAME(vixPlaybackOn)(void)
{
    printf(GENFB_MSG"playback on\n");
    return(0);
}

int VIDIX_NAME(vixPlaybackOff)(void)
{
    printf(GENFB_MSG"playback off\n");
    return(0);
}

int VIDIX_NAME(vixPlaybackFrameSelect)(unsigned int frame)
{
    printf(GENFB_MSG"frameselect: %d\n", frame);
    return(0);
}
