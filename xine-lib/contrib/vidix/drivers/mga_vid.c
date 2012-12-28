/*
 * Matrox MGA driver
 *
 * ported to VIDIX by Alex Beregszaszi
 *
 * YUY2 support (see config.format) added by A'rpi/ESP-team
 * double buffering added by A'rpi/ESP-team
 *
 * Brightness/contrast support by Nick Kurshev/Dariush Pietrzak (eyck) and me
 *
 * Fixed Brightness/Contrast
 * Rewrite or read/write kabi@users.sf.net
 *
 * TODO:
 * * fix memory size detection (current reading pci userconfig isn't
 *   working as requested - returns the max avail. ram on arch?)
 * * translate all non-english comments to english
 */

/*
 * Original copyright:
 *
 * mga_vid.c
 *
 * Copyright (C) 1999 Aaron Holtzman
 *
 * Module skeleton based on gutted agpgart module by Jeff Hartmann
 * <slicer@ionet.net>
 *
 * Matrox MGA G200/G400 YUV Video Interface module Version 0.1.0
 *
 * BES == Back End Scaler
 *
 * This software has been released under the terms of the GNU Public
 * license. See http://www.gnu.org/copyleft/gpl.html for details.
 */

//#define CRTC2

// Set this value, if autodetection fails! (video ram size in megabytes)
//#define MGA_MEMORY_SIZE 16

/* No irq support in userspace implemented yet, do not enable this! */
/* disable irq */
#undef MGA_ALLOW_IRQ

#define MGA_VSYNC_POS 2

#undef MGA_PCICONFIG_MEMDETECT

#define MGA_DEFAULT_FRAMES 64

#define BES

#ifdef MGA_TV
#undef BES
#define CRTC2
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>

#include "vidix.h"
#include "fourcc.h"
#include "libdha.h"
#include "pci_ids.h"
#include "pci_names.h"

#if    !defined(ENOTSUP) && defined(EOPNOTSUPP)
#define ENOTSUP EOPNOTSUPP
#endif

#ifdef CRTC2
#define VIDIX_STATIC mga_crtc2_
#define MGA_MSG "[mga_crtc2]"
#else
#define VIDIX_STATIC mga_
#define MGA_MSG "[mga]"
#endif

/* from radeon_vid */
#define GETREG(TYPE,PTR,OFFZ)		(*((volatile TYPE*)((PTR)+(OFFZ))))
#define SETREG(TYPE,PTR,OFFZ,VAL)	(*((volatile TYPE*)((PTR)+(OFFZ))))=VAL

#define readb(addr)	  GETREG(uint8_t,(uint32_t)(mga_mmio_base + addr),0)
#define writeb(addr, val) SETREG(uint8_t,(uint32_t)(mga_mmio_base + addr),0,val)
#define readl(addr)	  GETREG(uint32_t,(uint32_t)(mga_mmio_base + addr),0)
#define writel(addr, val) SETREG(uint32_t,(uint32_t)(mga_mmio_base + addr),0,val)

static int mga_verbose = 0;

/* for device detection */
static int probed = 0;
static pciinfo_t pci_info;

/* internal booleans */
static int mga_vid_in_use = 0;
static int is_g400 = 0;
static int vid_src_ready = 0;
static int vid_overlay_on = 0;

/* mapped physical addresses */
static uint8_t *mga_mmio_base = 0;
static uint8_t* mga_mem_base = 0;

static int mga_src_base = 0; /* YUV buffer position in video memory */

static uint32_t mga_ram_size = 0; /* how much megabytes videoram we have */

/* Graphic keys */
static vidix_grkey_t mga_grkey;

static int colkey_saved = 0;
static int colkey_on = 0;
static unsigned char colkey_color[4];
static unsigned char colkey_mask[4];

/* for IRQ */
static int mga_irq = -1;

static int mga_next_frame = 0;

static vidix_capability_t mga_cap =
{
#ifdef CRTC2
    "Matrox MGA G200/G4x0/G5x0 YUV Video - with second-head support",
#else    
    "Matrox MGA G200/G4x0/G5x0 YUV Video",
#endif
    "Aaron Holtzman, Arpad Gereoffy, Alex Beregszaszi, Nick Kurshev",
    TYPE_OUTPUT,
    { 0, 0, 0, 0 },
    2048,
    2048,
    4,
    4,
    -1,
    FLAG_UPSCALER | FLAG_DOWNSCALER | FLAG_EQUALIZER,
    VENDOR_MATROX,
    -1, /* will be set in VIDIX_NAME(vixProbe) */
    { 0, 0, 0, 0}
};

/* MATROX BES registers */
typedef struct bes_registers_s
{
    //BES Control
    uint32_t besctl;
    //BES Global control
    uint32_t besglobctl;
    //Luma control (brightness and contrast)
    uint32_t beslumactl;
    //Line pitch
    uint32_t bespitch;

    //Buffer A-1 Chroma 3 plane org
    uint32_t besa1c3org;
    //Buffer A-1 Chroma org
    uint32_t besa1corg;
    //Buffer A-1 Luma org
    uint32_t besa1org;

    //Buffer A-2 Chroma 3 plane org
    uint32_t besa2c3org;
    //Buffer A-2 Chroma org
    uint32_t besa2corg;
    //Buffer A-2 Luma org
    uint32_t besa2org;

    //Buffer B-1 Chroma 3 plane org
    uint32_t besb1c3org;
    //Buffer B-1 Chroma org
    uint32_t besb1corg;
    //Buffer B-1 Luma org
    uint32_t besb1org;

    //Buffer B-2 Chroma 3 plane org
    uint32_t besb2c3org;
    //Buffer B-2 Chroma org
    uint32_t besb2corg;
    //Buffer B-2 Luma org
    uint32_t besb2org;

    //BES Horizontal coord
    uint32_t beshcoord;
    //BES Horizontal inverse scaling [5.14]
    uint32_t beshiscal;
    //BES Horizontal source start [10.14] (for scaling)
    uint32_t beshsrcst;
    //BES Horizontal source ending [10.14] (for scaling)
    uint32_t beshsrcend;
    //BES Horizontal source last
    uint32_t beshsrclst;


    //BES Vertical coord
    uint32_t besvcoord;
    //BES Vertical inverse scaling [5.14]
    uint32_t besviscal;
    //BES Field 1 vertical source last position
    uint32_t besv1srclst;
    //BES Field 1 weight start
    uint32_t besv1wght;
    //BES Field 2 vertical source last position
    uint32_t besv2srclst;
    //BES Field 2 weight start
    uint32_t besv2wght;

} bes_registers_t;
static bes_registers_t regs;

#ifdef CRTC2
typedef struct crtc2_registers_s
{
    uint32_t c2ctl;
    uint32_t c2datactl;
    uint32_t c2misc;
    uint32_t c2hparam;
    uint32_t c2hsync;
    uint32_t c2offset;
    uint32_t c2pl2startadd0;
    uint32_t c2pl2startadd1;
    uint32_t c2pl3startadd0;
    uint32_t c2pl3startadd1;
    uint32_t c2preload;
    uint32_t c2spicstartadd0;
    uint32_t c2spicstartadd1;
    uint32_t c2startadd0;
    uint32_t c2startadd1;
    uint32_t c2subpiclut;
    uint32_t c2vcount;
    uint32_t c2vparam;
    uint32_t c2vsync;
} crtc2_registers_t;
static crtc2_registers_t cregs;
static crtc2_registers_t cregs_save;
#endif

//All register offsets are converted to word aligned offsets (32 bit)
//because we want all our register accesses to be 32 bits
#define VCOUNT      0x1e20

#define PALWTADD      0x3c00 // Index register for X_DATAREG port
#define X_DATAREG     0x3c0a

#define XMULCTRL      0x19
#define BPP_8         0x00
#define BPP_15        0x01
#define BPP_16        0x02
#define BPP_24        0x03
#define BPP_32_DIR    0x04
#define BPP_32_PAL    0x07

#define XCOLMSK       0x40
#define X_COLKEY      0x42
#define XKEYOPMODE    0x51
#define XCOLMSK0RED   0x52
#define XCOLMSK0GREEN 0x53
#define XCOLMSK0BLUE  0x54
#define XCOLKEY0RED   0x55
#define XCOLKEY0GREEN 0x56
#define XCOLKEY0BLUE  0x57

#ifdef CRTC2
/*CRTC2 registers*/
#define XMISCCTRL  0x1e
#define C2CTL       0x3c10
#define C2DATACTL   0x3c4c
#define C2MISC      0x3c44
#define C2HPARAM    0x3c14
#define C2HSYNC     0x3c18
#define C2OFFSET    0x3c40
#define C2PL2STARTADD0 0x3c30  // like BESA1CORG
#define C2PL2STARTADD1 0x3c34  // like BESA2CORG
#define C2PL3STARTADD0 0x3c38  // like BESA1C3ORG
#define C2PL3STARTADD1 0x3c3c  // like BESA2C3ORG
#define C2PRELOAD   0x3c24
#define C2SPICSTARTADD0 0x3c54
#define C2SPICSTARTADD1 0x3c58
#define C2STARTADD0 0x3c28  // like BESA1ORG
#define C2STARTADD1 0x3c2c  // like BESA2ORG
#define C2SUBPICLUT 0x3c50
#define C2VCOUNT    0x3c48
#define C2VPARAM    0x3c1c
#define C2VSYNC     0x3c20
#endif /* CRTC2 */

// Backend Scaler registers
#define BESCTL      0x3d20
#define BESGLOBCTL  0x3dc0
#define BESLUMACTL  0x3d40
#define BESPITCH    0x3d24

#define BESA1C3ORG  0x3d60
#define BESA1CORG   0x3d10
#define BESA1ORG    0x3d00

#define BESA2C3ORG  0x3d64
#define BESA2CORG   0x3d14
#define BESA2ORG    0x3d04

#define BESB1C3ORG  0x3d68
#define BESB1CORG   0x3d18
#define BESB1ORG    0x3d08

#define BESB2C3ORG  0x3d6C
#define BESB2CORG   0x3d1C
#define BESB2ORG    0x3d0C

#define BESHCOORD   0x3d28
#define BESHISCAL   0x3d30
#define BESHSRCEND  0x3d3C
#define BESHSRCLST  0x3d50
#define BESHSRCST   0x3d38
#define BESV1WGHT   0x3d48
#define BESV2WGHT   0x3d4c
#define BESV1SRCLST 0x3d54
#define BESV2SRCLST 0x3d58
#define BESVISCAL   0x3d34
#define BESVCOORD   0x3d2c
#define BESSTATUS   0x3dc4

#define CRTCX	    0x1fd4
#define CRTCD	    0x1fd5
#define	IEN	    0x1e1c
#define ICLEAR	    0x1e18
#define STATUS      0x1e14
#define CRTCEXTX    0x1fde
#define CRTCEXTD    0x1fdf


#ifdef CRTC2
static void crtc2_frame_sel(int frame)
{
    switch(frame) {
    case 0:
	cregs.c2pl2startadd0=regs.besa1corg;
	cregs.c2pl3startadd0=regs.besa1c3org;
	cregs.c2startadd0=regs.besa1org;
	break;
    case 1:
	cregs.c2pl2startadd0=regs.besa2corg;
	cregs.c2pl3startadd0=regs.besa2c3org;
	cregs.c2startadd0=regs.besa2org;
	break;
    case 2:
	cregs.c2pl2startadd0=regs.besb1corg;
	cregs.c2pl3startadd0=regs.besb1c3org;
	cregs.c2startadd0=regs.besb1org;
	break;
    case 3:
	cregs.c2pl2startadd0=regs.besb2corg;
	cregs.c2pl3startadd0=regs.besb2c3org;
	cregs.c2startadd0=regs.besb2org;
	break;
    }
    writel(C2STARTADD0,    cregs.c2startadd0);
    writel(C2PL2STARTADD0, cregs.c2pl2startadd0);
    writel(C2PL3STARTADD0, cregs.c2pl3startadd0);
}
#endif

int VIDIX_NAME(vixPlaybackFrameSelect)(unsigned int frame)
{
    mga_next_frame = frame;
    if (mga_verbose>1) printf(MGA_MSG" frameselect: %d\n", mga_next_frame);
#if MGA_ALLOW_IRQ
    if (mga_irq == -1)
#endif
    {
#ifdef BES
	//we don't need the vcount protection as we're only hitting
	//one register (and it doesn't seem to be double buffered)
	regs.besctl = (regs.besctl & ~0x07000000) + (mga_next_frame << 25);
	writel(BESCTL, regs.besctl);

	// writel( regs.besglobctl + ((readl(VCOUNT)+2)<<16),
	writel(BESGLOBCTL, regs.besglobctl + (MGA_VSYNC_POS<<16));
#endif
#ifdef CRTC2
	crtc2_frame_sel(mga_next_frame);
#endif
    }

    return(0);
}


static void mga_vid_write_regs(int restore)
{
#ifdef BES
    //Make sure internal registers don't get updated until we're done
    writel(BESGLOBCTL, (readl(VCOUNT)-1)<<16);

    // color or coordinate keying

    if (restore && colkey_saved)
    {
	// restore it
	colkey_saved = 0;

	// Set color key registers:
	writeb(PALWTADD,  XKEYOPMODE);
	writeb(X_DATAREG, colkey_on);

	writeb(PALWTADD,  XCOLKEY0RED);
	writeb(X_DATAREG, colkey_color[0]);
	writeb(PALWTADD,  XCOLKEY0GREEN);
	writeb(X_DATAREG, colkey_color[1]);
	writeb(PALWTADD,  XCOLKEY0BLUE);
	writeb(X_DATAREG, colkey_color[2]);
	writeb(PALWTADD,  X_COLKEY);
	writeb(X_DATAREG, colkey_color[3]);

	writeb(PALWTADD,  XCOLMSK0RED);
	writeb(X_DATAREG, colkey_mask[0]);
	writeb(PALWTADD,  XCOLMSK0GREEN);
	writeb(X_DATAREG, colkey_mask[1]);
	writeb(PALWTADD,  XCOLMSK0BLUE);
	writeb(X_DATAREG, colkey_mask[2]);
	writeb(PALWTADD,  XCOLMSK);
	writeb(X_DATAREG, colkey_mask[3]);

	printf(MGA_MSG" Restored colour key (ON: %d  %02X:%02X:%02X)\n",
	       colkey_on,colkey_color[0],colkey_color[1],colkey_color[2]);

    } else if (!colkey_saved) {
	// save it
	colkey_saved=1;
	// Get color key registers:
	writeb(PALWTADD,  XKEYOPMODE);
	colkey_on = readb(X_DATAREG) & 1;

	writeb(PALWTADD,  XCOLKEY0RED);
	colkey_color[0]=(unsigned char)readb(X_DATAREG);
	writeb(PALWTADD,  XCOLKEY0GREEN);
	colkey_color[1]=(unsigned char)readb(X_DATAREG);
	writeb(PALWTADD,  XCOLKEY0BLUE);
	colkey_color[2]=(unsigned char)readb(X_DATAREG);
	writeb(PALWTADD,  X_COLKEY);
	colkey_color[3]=(unsigned char)readb(X_DATAREG);

	writeb(PALWTADD,  XCOLMSK0RED);
	colkey_mask[0]=(unsigned char)readb(X_DATAREG);
	writeb(PALWTADD,  XCOLMSK0GREEN);
	colkey_mask[1]=(unsigned char)readb(X_DATAREG);
	writeb(PALWTADD,  XCOLMSK0BLUE);
	colkey_mask[2]=(unsigned char)readb(X_DATAREG);
	writeb(PALWTADD,  XCOLMSK);
	colkey_mask[3]=(unsigned char)readb(X_DATAREG);

	printf(MGA_MSG" Saved colour key (ON: %d  %02X:%02X:%02X)\n",
	       colkey_on,colkey_color[0],colkey_color[1],colkey_color[2]);
    }

    if (!restore)
    {
	writeb(PALWTADD,  XKEYOPMODE);
	writeb(X_DATAREG, (mga_grkey.ckey.op == CKEY_TRUE));
	if ( mga_grkey.ckey.op == CKEY_TRUE )
	{
	    uint32_t r=0, g=0, b=0;

	    writeb(PALWTADD, XMULCTRL);
	    switch (readb(X_DATAREG))
	    {
	    case BPP_8:
		/* Need to look up the color index, just using
		 color 0 for now. */
		break;
	    case BPP_15:
		r = mga_grkey.ckey.red   >> 3;
		g = mga_grkey.ckey.green >> 3;
		b = mga_grkey.ckey.blue  >> 3;
		break;
	    case BPP_16:
		r = mga_grkey.ckey.red   >> 3;
		g = mga_grkey.ckey.green >> 2;
		b = mga_grkey.ckey.blue  >> 3;
		break;
	    case BPP_24:
	    case BPP_32_DIR:
	    case BPP_32_PAL:
		r = mga_grkey.ckey.red;
		g = mga_grkey.ckey.green;
		b = mga_grkey.ckey.blue;
		break;
	    }

	    // Disable color keying on alpha channel
	    writeb(PALWTADD, XCOLMSK);
	    writeb(X_DATAREG, 0x00);
	    writeb(PALWTADD, X_COLKEY);
	    writeb(X_DATAREG, 0x00);


	    // Set up color key registers
	    writeb(PALWTADD, XCOLKEY0RED);
	    writeb(X_DATAREG, r);
	    writeb(PALWTADD, XCOLKEY0GREEN);
	    writeb(X_DATAREG, g);
	    writeb(PALWTADD, XCOLKEY0BLUE);
	    writeb(X_DATAREG, b);

	    // Set up color key mask registers
	    writeb(PALWTADD, XCOLMSK0RED);
	    writeb(X_DATAREG, 0xff);
	    writeb(PALWTADD, XCOLMSK0GREEN);
	    writeb(X_DATAREG, 0xff);
	    writeb(PALWTADD, XCOLMSK0BLUE);
	    writeb(X_DATAREG, 0xff);
	}
    }

    // Backend Scaler
    writel(BESCTL,      regs.besctl);
    if (is_g400)
	writel(BESLUMACTL, regs.beslumactl);
    writel(BESPITCH,    regs.bespitch);

    writel(BESA1ORG,    regs.besa1org);
    writel(BESA1CORG,   regs.besa1corg);
    writel(BESA2ORG,    regs.besa2org);
    writel(BESA2CORG,   regs.besa2corg);
    writel(BESB1ORG,    regs.besb1org);
    writel(BESB1CORG,   regs.besb1corg);
    writel(BESB2ORG,    regs.besb2org);
    writel(BESB2CORG,   regs.besb2corg);
    if(is_g400)
    {
	writel(BESA1C3ORG, regs.besa1c3org);
	writel(BESA2C3ORG, regs.besa2c3org);
	writel(BESB1C3ORG, regs.besb1c3org);
	writel(BESB2C3ORG, regs.besb2c3org);
    }

    writel(BESHCOORD,   regs.beshcoord);
    writel(BESHISCAL,   regs.beshiscal);
    writel(BESHSRCST,   regs.beshsrcst);
    writel(BESHSRCEND,  regs.beshsrcend);
    writel(BESHSRCLST,  regs.beshsrclst);

    writel(BESVCOORD,   regs.besvcoord);
    writel(BESVISCAL,   regs.besviscal);

    writel(BESV1SRCLST, regs.besv1srclst);
    writel(BESV1WGHT,   regs.besv1wght);
    writel(BESV2SRCLST, regs.besv2srclst);
    writel(BESV2WGHT,   regs.besv2wght);

    //update the registers somewhere between 1 and 2 frames from now.
    writel(BESGLOBCTL,  regs.besglobctl + ((readl(VCOUNT)+2)<<16));

    if (mga_verbose > 1)
    {
	printf(MGA_MSG" wrote BES registers\n");
	printf(MGA_MSG" BESCTL = 0x%08x\n", readl(BESCTL));
	printf(MGA_MSG" BESGLOBCTL = 0x%08x\n", readl(BESGLOBCTL));
	printf(MGA_MSG" BESSTATUS= 0x%08x\n", readl(BESSTATUS));
    }
#endif

#ifdef CRTC2
#if 0
    if (cregs_save.c2ctl == 0)
    {
	//int i;
	cregs_save.c2ctl = readl(C2CTL);
	cregs_save.c2datactl = readl(C2DATACTL);
	cregs_save.c2misc = readl(C2MISC);

	//for (i = 0; i <= 8; i++) { writeb(CRTCEXTX, i); printf("CRTCEXT%d  %x\n", i, readb(CRTCEXTD)); }
	//printf("c2ctl:0x%08x c2datactl:0x%08x\n", cregs_save.c2ctl, cregs_save.c2datactl);
	//printf("c2misc:0x%08x\n", readl(C2MISC));
	//printf("c2ctl:0x%08x c2datactl:0x%08x\n", cregs.c2ctl, cregs.c2datactl);
    }
    if (restore)
    {
	writel(C2CTL,          cregs_save.c2ctl);
        writel(C2DATACTL,      cregs_save.c2datactl);
	writel(C2MISC,         cregs_save.c2misc);
	return;
    }
#endif
    // writel(C2CTL, cregs.c2ctl);

    writel(C2CTL, ((readl(C2CTL) & ~0x03e00000) + (cregs.c2ctl & 0x03e00000)));
    writel(C2DATACTL, ((readl(C2DATACTL) & ~0x000000ff) + (cregs.c2datactl & 0x000000ff)));
    // ctrc2
    // disable CRTC2 acording to specs
    //	writel(C2CTL, cregs.c2ctl & 0xfffffff0);
    // je to treba ???
    //	writeb(XMISCCTRL, (readb(XMISCCTRL) & 0x19) | 0xa2); // MAFC - mfcsel & vdoutsel
    //	writeb(XMISCCTRL, (readb(XMISCCTRL) & 0x19) | 0x92);
    //	writeb(XMISCCTRL, (readb(XMISCCTRL) & ~0xe9) + 0xa2);
    writel(C2DATACTL,   cregs.c2datactl);
//    writel(C2HPARAM,    cregs.c2hparam);
    writel(C2HSYNC,     cregs.c2hsync);
//    writel(C2VPARAM,    cregs.c2vparam);
    writel(C2VSYNC,     cregs.c2vsync);
    //xx
    //writel(C2MISC,      cregs.c2misc);

    if (mga_verbose > 1) printf(MGA_MSG" c2offset = %d\n", cregs.c2offset);

    writel(C2OFFSET,    cregs.c2offset);
    writel(C2STARTADD0, cregs.c2startadd0);
    //	writel(C2STARTADD1, cregs.c2startadd1);
    writel(C2PL2STARTADD0, cregs.c2pl2startadd0);
    //	writel(C2PL2STARTADD1, cregs.c2pl2startadd1);
    writel(C2PL3STARTADD0, cregs.c2pl3startadd0);
    //	writel(C2PL3STARTADD1, cregs.c2pl3startadd1);
    writel(C2SPICSTARTADD0, cregs.c2spicstartadd0);

    //xx
    //writel(C2SPICSTARTADD1, cregs.c2spicstartadd1);

    //set Color Lookup Table for Subpicture Layer
    {
      unsigned char r, g, b, y, cb, cr;
      int i;
      for (i = 0; i < 16; i++) {
           
        r = (i & 0x8) ? 0xff : 0x00;
        g = (i & 0x4) ? ((i & 0x2) ? 0xff : 0xaa) : ((i & 0x2) ? 0x55 : 0x00);
        b = (i & 0x1) ? 0xff : 0x00;

        y  = ((r * 16829 + g *  33039 + b *  6416 + 0x8000) >> 16) + 16; 
        cb = ((r * -9714 + g * -19071 + b * 28784 + 0x8000) >> 16) + 128; 
        cr = ((r * 28784 + g * -24103 + b * -4681 + 0x8000) >> 16) + 128;

        cregs.c2subpiclut = (cr << 24) | (cb << 16) | (y << 8) | i;
        writel(C2SUBPICLUT, cregs.c2subpiclut);
      }
    }

    //writel(C2PRELOAD,   cregs.c2preload);

    // finaly enable everything
//    writel(C2CTL,       cregs.c2ctl);
    //	printf("c2ctl:0x%08x c2datactl:0x%08x\n",readl(C2CTL), readl(C2DATACTL));
    //	printf("c2misc:0x%08x\n", readl(C2MISC));
#endif
}

#ifdef MGA_ALLOW_IRQ
static void enable_irq()
{
    long int cc;

    cc = readl(IEN);
    //	printf("*** !!! IRQREG = %d\n", (int)(cc&0xff));

    writeb(CRTCX, 0x11);

    writeb(CRTCD, 0x20);  /* clear 0, enable off */
    writeb(CRTCD, 0x00);  /* enable on */
    writeb(CRTCD, 0x10);  /* clear = 1 */

    writel(BESGLOBCTL, regs.besglobctl);

    return;
}

static void disable_irq()
{
    writeb(CRTCX, 0x11);
    writeb(CRTCD, 0x20);  /* clear 0, enable off */

    return;
}

void mga_handle_irq(int irq, void *dev_id/*, struct pt_regs *pregs*/) {
    //	static int frame=0;
    //	static int counter=0;
    long int cc;
    //	if ( ! mga_enabled_flag ) return;

    //	printf("vcount = %d\n",readl(VCOUNT));

    //printf("mga_interrupt #%d\n", irq);

    if ( irq != -1 ) {

	cc = readl(STATUS);
	if ( ! (cc & 0x10) ) return;  /* vsyncpen */
	// 		debug_irqcnt++;
    }

    //    if ( debug_irqignore ) {
    //	debug_irqignore = 0;

    /*
     if ( mga_conf_deinterlace ) {
     if ( mga_first_field ) {
     // printf("mga_interrupt first field\n");
     if ( syncfb_interrupt() )
     mga_first_field = 0;
     } else {
     // printf("mga_interrupt second field\n");
     mga_select_buffer( mga_current_field | 2 );
     mga_first_field = 1;
     }
     } else {
     syncfb_interrupt();
     }
     */

    //	frame=(frame+1)&1;
    regs.besctl = (regs.besctl & ~0x07000000) + (mga_next_frame << 25);
    writel(BESCTL, regs.besctl);

#ifdef CRTC2
    crtc2_frame_sel(mga_next_frame);
#endif

#if 0
    ++counter;
    if(!(counter&63)){
	printf("mga irq counter = %d\n",counter);
    }
#endif

    //    } else {
    //	debug_irqignore = 1;
    //    }

    if ( irq != -1 ) {
	writeb(CRTCX, 0x11);
	writeb(CRTCD, 0);
	writeb(CRTCD, 0x10);
    }

    //writel(BESGLOBCTL, regs.besglobctl);

}
#endif /* MGA_ALLOW_IRQ */

int VIDIX_NAME(vixConfigPlayback)(vidix_playback_t *config)
{
    unsigned int i;
    int x, y, sw, sh, dw, dh;
    int besleft, bestop, ifactor, ofsleft, ofstop, baseadrofs, weight, weights;
#ifdef CRTC2
#define right_margin 0
#define left_margin 18
#define hsync_len 46
#define lower_margin 10
#define vsync_len 4
#define upper_margin 39

    unsigned int hdispend = (config->src.w + 31) & ~31;
    unsigned int hsyncstart = hdispend + (right_margin & ~7);
    unsigned int hsyncend = hsyncstart + (hsync_len & ~7);
    unsigned int htotal = hsyncend + (left_margin & ~7);
    unsigned int vdispend = config->src.h;
    unsigned int vsyncstart = vdispend + lower_margin;
    unsigned int vsyncend = vsyncstart + vsync_len;
    unsigned int vtotal = vsyncend + upper_margin;
#endif

    if ((config->num_frames < 1) || (config->num_frames > MGA_DEFAULT_FRAMES))
    {
	printf(MGA_MSG" illegal num_frames: %d, setting to %d\n",
	       config->num_frames, MGA_DEFAULT_FRAMES);
	config->num_frames = MGA_DEFAULT_FRAMES;
    }
    for(;config->num_frames>0;config->num_frames--)
    {
	/*FIXME: this driver can use more frames but we need to apply
	 some tricks to avoid RGB-memory hits*/
	mga_src_base = ((mga_ram_size/2)*0x100000-(config->num_frames+1)*config->frame_size);
	mga_src_base &= (~0xFFFF); /* 64k boundary */
	if(mga_src_base>=0) break;
    }
    if (mga_verbose > 1) printf(MGA_MSG" YUV buffer base: 0x%x\n", mga_src_base);

    config->dga_addr = mga_mem_base + mga_src_base;

    x = config->dest.x;
    y = config->dest.y;
    sw = config->src.w;
    sh = config->src.h;
    dw = config->dest.w;
    dh = config->dest.h;

    if (mga_verbose) printf(MGA_MSG" Setting up a %dx%d-%dx%d video window (src %dx%d) format %X\n",
			    dw, dh, x, y, sw, sh, config->fourcc);

    if ((sw < 4) || (sh < 4) || (dw < 4) || (dh < 4))
    {
	printf(MGA_MSG" Invalid src/dest dimensions\n");
	return(EINVAL);
    }

    //FIXME check that window is valid and inside desktop

    //    printf(MGA_MSG" vcount = %d\n", readl(VCOUNT));

    sw += sw & 1;
    switch(config->fourcc)
    {
    case IMGFMT_I420:
    case IMGFMT_IYUV:
    case IMGFMT_YV12:
	sh+=sh&1;
	config->dest.pitch.y=config->dest.pitch.u=config->dest.pitch.v=32;
	config->frame_size = ((sw + 31) & ~31) * sh + (((sw + 31) & ~31) * sh) / 2;
	break;
    case IMGFMT_YUY2:
    case IMGFMT_UYVY:
	config->dest.pitch.y=16;
	config->dest.pitch.u=config->dest.pitch.v=0;
	config->frame_size = ((sw + 8) & ~8) * sh * 2;
	break;
    default:
	printf(MGA_MSG" Unsupported pixel format: %x\n", config->fourcc);
	return(ENOTSUP);
    }

    config->offsets[0] = 0;
    //    config->offsets[1] = config->frame_size;
    //    config->offsets[2] = 2*config->frame_size;
    //    config->offsets[3] = 3*config->frame_size;
    for (i = 1; i < config->num_frames+2; i++)
	config->offsets[i] = i*config->frame_size;

    config->offset.y=0;
    config->offset.v=((sw + 31) & ~31) * sh;
    config->offset.u=config->offset.v+((sw + 31) & ~31) * sh /4;

    //FIXME figure out a better way to allocate memory on card
    //allocate 2 megs
    //mga_src_base = mga_mem_base + (MGA_VIDMEM_SIZE-2) * 0x100000;
    //mga_src_base = (MGA_VIDMEM_SIZE-3) * 0x100000;


    /* for G200 set Interleaved UV planes */
    if (!is_g400)
	config->flags = VID_PLAY_INTERLEAVED_UV | INTERLEAVING_UV;

    //Setup the BES registers for a three plane 4:2:0 video source

    regs.besglobctl = 0;

    switch(config->fourcc)
    {
    case IMGFMT_YV12:
    case IMGFMT_I420:
    case IMGFMT_IYUV:
	regs.besctl = 1 // BES enabled
	    + (0<<6)    // even start polarity
	    + (1<<10)   // x filtering enabled
	    + (1<<11)   // y filtering enabled
	    + (1<<16)   // chroma upsampling
	    + (1<<17)   // 4:2:0 mode
	    + (1<<18);  // dither enabled
#if 0
	if(is_g400)
	{
	    //zoom disabled, zoom filter disabled, 420 3 plane format, proc amp
	    //disabled, rgb mode disabled
	    regs.besglobctl = (1<<5);
	}
	else
	{
	    //zoom disabled, zoom filter disabled, Cb samples in 0246, Cr
	    //in 1357, BES register update on besvcnt
	    regs.besglobctl = 0;
	}
#endif
	break;

    case IMGFMT_YUY2:
	regs.besctl = 1 // BES enabled
	    + (0<<6)    // even start polarity
	    + (1<<10)   // x filtering enabled
	    + (1<<11)   // y filtering enabled
	    + (1<<16)   // chroma upsampling
	    + (0<<17)   // 4:2:2 mode
	    + (1<<18);  // dither enabled

	regs.besglobctl = 0;        // YUY2 format selected
	break;

    case IMGFMT_UYVY:
	regs.besctl = 1         // BES enabled
	    + (0<<6)    // even start polarity
	    + (1<<10)   // x filtering enabled
	    + (1<<11)   // y filtering enabled
	    + (1<<16)   // chroma upsampling
	    + (0<<17)   // 4:2:2 mode
	    + (1<<18);  // dither enabled

	regs.besglobctl = 1<<6;        // UYVY format selected
	break;

    }

    //Disable contrast and brightness control
    regs.besglobctl |= (1<<5) + (1<<7);
    // we want to preserver these across restarts
    //regs.beslumactl = (0x0 << 16) + 0x80;

    //Setup destination window boundaries
    besleft = x > 0 ? x : 0;
    bestop = y > 0 ? y : 0;
    regs.beshcoord = (besleft<<16) + (x + dw-1);
    regs.besvcoord = (bestop<<16) + (y + dh-1);

    //Setup source dimensions
    regs.beshsrclst = (sw - 1) << 16;
    switch(config->fourcc)
    {
    case IMGFMT_YV12:
    case IMGFMT_I420:
    case IMGFMT_IYUV:
	regs.bespitch = (sw + 31) & ~31;
	break;
    case IMGFMT_YUY2:
    case IMGFMT_UYVY:
	regs.bespitch = (sw + 8) & ~8;
	break;
    }

    //Setup horizontal scaling
    ifactor = ((sw-1)<<14)/(dw-1);
    ofsleft = besleft - x;

    regs.beshiscal = ifactor<<2;
    regs.beshsrcst = (ofsleft*ifactor)<<2;
    regs.beshsrcend = regs.beshsrcst + (((dw - ofsleft - 1) * ifactor) << 2);

    //Setup vertical scaling
    ifactor = ((sh-1)<<14)/(dh-1);
    ofstop = bestop - y;

    regs.besviscal = ifactor<<2;

    baseadrofs = ((ofstop*regs.besviscal)>>16)*regs.bespitch;
    //frame_size = ((sw + 31) & ~31) * sh + (((sw + 31) & ~31) * sh) / 2;
    regs.besa1org = (uint32_t) mga_src_base + baseadrofs;
    regs.besa2org = (uint32_t) mga_src_base + baseadrofs + 1*config->frame_size;
    regs.besb1org = (uint32_t) mga_src_base + baseadrofs + 2*config->frame_size;
    regs.besb2org = (uint32_t) mga_src_base + baseadrofs + 3*config->frame_size;

    if (config->fourcc == IMGFMT_YV12
	|| config->fourcc == IMGFMT_IYUV
	|| config->fourcc == IMGFMT_I420)
    {
	// planar YUV frames:
	if (is_g400)
	    baseadrofs = (((ofstop*regs.besviscal)/4)>>16)*regs.bespitch;
	else
	    baseadrofs = (((ofstop*regs.besviscal)/2)>>16)*regs.bespitch;

	if (config->fourcc == IMGFMT_YV12){
	    regs.besa1corg = (uint32_t) mga_src_base + baseadrofs + regs.bespitch * sh ;
	    regs.besa2corg = (uint32_t) mga_src_base + baseadrofs + 1*config->frame_size + regs.bespitch * sh;
	    regs.besb1corg = (uint32_t) mga_src_base + baseadrofs + 2*config->frame_size + regs.bespitch * sh;
	    regs.besb2corg = (uint32_t) mga_src_base + baseadrofs + 3*config->frame_size + regs.bespitch * sh;
	    regs.besa1c3org = regs.besa1corg + ((regs.bespitch * sh) / 4);
	    regs.besa2c3org = regs.besa2corg + ((regs.bespitch * sh) / 4);
	    regs.besb1c3org = regs.besb1corg + ((regs.bespitch * sh) / 4);
	    regs.besb2c3org = regs.besb2corg + ((regs.bespitch * sh) / 4);
	} else {
	    regs.besa1c3org = (uint32_t) mga_src_base + baseadrofs + regs.bespitch * sh ;
	    regs.besa2c3org = (uint32_t) mga_src_base + baseadrofs + 1*config->frame_size + regs.bespitch * sh;
	    regs.besb1c3org = (uint32_t) mga_src_base + baseadrofs + 2*config->frame_size + regs.bespitch * sh;
	    regs.besb2c3org = (uint32_t) mga_src_base + baseadrofs + 3*config->frame_size + regs.bespitch * sh;
	    regs.besa1corg = regs.besa1c3org + ((regs.bespitch * sh) / 4);
	    regs.besa2corg = regs.besa2c3org + ((regs.bespitch * sh) / 4);
	    regs.besb1corg = regs.besb1c3org + ((regs.bespitch * sh) / 4);
	    regs.besb2corg = regs.besb2c3org + ((regs.bespitch * sh) / 4);
	}
    }

    weight = ofstop * (regs.besviscal >> 2);
    weights = weight < 0 ? 1 : 0;
    regs.besv2wght = regs.besv1wght = (weights << 16) + ((weight & 0x3FFF) << 2);
    regs.besv2srclst = regs.besv1srclst = sh - 1 - (((ofstop * regs.besviscal) >> 16) & 0x03FF);

#ifdef CRTC2
    // pridat hlavni registry - tj. casovani ...


    switch(config->fourcc){
    case IMGFMT_YV12:
    case IMGFMT_I420:
    case IMGFMT_IYUV:
	cregs.c2ctl = 1         // CRTC2 enabled
	    + (1<<1)	// external clock
	    + (0<<2)	// external clock
	    + (1<<3)	// pixel clock enable - not needed ???
	    + (0<<4)	// high priority req
	    + (1<<5)	// high priority req
	    + (0<<6)	// high priority req
	    + (1<<8)	// high priority req max
	    + (0<<9)	// high priority req max
	    + (0<<10)	// high priority req max
	    + (0<<20)   // CRTC1 to DAC
	    + (1<<21)   // 420 mode
	    + (1<<22)   // 420 mode
	    + (1<<23)   // 420 mode
	    + (0<<24)   // single chroma line for 420 mode - need to be corrected
	    + (0<<25)   /*/ interlace mode - need to be corrected*/
	    + (0<<26)   // field legth polariry
	    + (0<<27)   // field identification polariry
	    + (1<<28)   // VIDRST detection mode
	    + (0<<29)   // VIDRST detection mode
	    + (1<<30)   // Horizontal counter preload
	    + (1<<31)   // Vertical counter preload
	    ;
	cregs.c2datactl = 1         // disable dither - propably not needed, we are already in YUV mode
	    + (1<<1)	// Y filter enable
	    + (1<<2)	// CbCr filter enable
	    + (1<<3)	// subpicture enable (enabled)
	    + (0<<4)	// NTSC enable (disabled - PAL)
	    + (0<<5)	// C2 static subpicture enable (disabled)
	    + (0<<6)	// C2 subpicture offset division (disabled)
	    + (0<<7)	// 422 subformat selection !
	    /*		    + (0<<8)	// 15 bpp high alpha
	     + (0<<9)	// 15 bpp high alpha
	     + (0<<10)	// 15 bpp high alpha
	     + (0<<11)	// 15 bpp high alpha
	     + (0<<12)	// 15 bpp high alpha
	     + (0<<13)	// 15 bpp high alpha
	     + (0<<14)	// 15 bpp high alpha
	     + (0<<15)	// 15 bpp high alpha
	     + (0<<16)	// 15 bpp low alpha
	     + (0<<17)	// 15 bpp low alpha
	     + (0<<18)	// 15 bpp low alpha
	     + (0<<19)	// 15 bpp low alpha
	     + (0<<20)	// 15 bpp low alpha
	     + (0<<21)	// 15 bpp low alpha
	     + (0<<22)	// 15 bpp low alpha
	     + (0<<23)	// 15 bpp low alpha
	     + (0<<24)	// static subpicture key
	     + (0<<25)	// static subpicture key
	     + (0<<26)	// static subpicture key
	     + (0<<27)	// static subpicture key
	     + (0<<28)	// static subpicture key
	     */		    ;
	break;

    case IMGFMT_YUY2:
	cregs.c2ctl = 1         // CRTC2 enabled
	    + (1<<1)	// external clock
	    + (0<<2)	// external clock
	    + (1<<3)	// pixel clock enable - not needed ???
	    + (0<<4)	// high priority req - acc to spec
	    + (1<<5)	// high priority req
	    + (0<<6)	// high priority req
	    // 7 reserved
	    + (1<<8)	// high priority req max
	    + (0<<9)	// high priority req max
	    + (0<<10)	// high priority req max
	    // 11-19 reserved
	    + (0<<20)   // CRTC1 to DAC
	    + (1<<21)   // 422 mode
	    + (0<<22)   // 422 mode
	    + (1<<23)   // 422 mode
	    + (0<<24)   // single chroma line for 420 mode - need to be corrected
	    + (0<<25)   /*/ interlace mode - need to be corrected*/
	    + (0<<26)   // field legth polariry
	    + (0<<27)   // field identification polariry
	    + (1<<28)   // VIDRST detection mode
	    + (0<<29)   // VIDRST detection mode
	    + (1<<30)   // Horizontal counter preload
	    + (1<<31)   // Vertical counter preload
	    ;
	cregs.c2datactl = 1         // disable dither - propably not needed, we are already in YUV mode
	    + (1<<1)	// Y filter enable
	    + (1<<2)	// CbCr filter enable
	    + (1<<3)	// subpicture enable (enabled)
	    + (0<<4)	// NTSC enable (disabled - PAL)
	    + (0<<5)	// C2 static subpicture enable (disabled)
	    + (0<<6)	// C2 subpicture offset division (disabled)
	    + (0<<7)	// 422 subformat selection !
	    /*		    + (0<<8)	// 15 bpp high alpha
	     + (0<<9)	// 15 bpp high alpha
	     + (0<<10)	// 15 bpp high alpha
	     + (0<<11)	// 15 bpp high alpha
	     + (0<<12)	// 15 bpp high alpha
	     + (0<<13)	// 15 bpp high alpha
	     + (0<<14)	// 15 bpp high alpha
	     + (0<<15)	// 15 bpp high alpha
	     + (0<<16)	// 15 bpp low alpha
	     + (0<<17)	// 15 bpp low alpha
	     + (0<<18)	// 15 bpp low alpha
	     + (0<<19)	// 15 bpp low alpha
	     + (0<<20)	// 15 bpp low alpha
	     + (0<<21)	// 15 bpp low alpha
	     + (0<<22)	// 15 bpp low alpha
	     + (0<<23)	// 15 bpp low alpha
	     + (0<<24)	// static subpicture key
	     + (0<<25)	// static subpicture key
	     + (0<<26)	// static subpicture key
	     + (0<<27)	// static subpicture key
	     + (0<<28)	// static subpicture key
	     */			;
	break;

    case IMGFMT_UYVY:
	cregs.c2ctl = 1         // CRTC2 enabled
	    + (1<<1)	// external clock
	    + (0<<2)	// external clock
	    + (1<<3)	// pixel clock enable - not needed ???
	    + (0<<4)	// high priority req
	    + (1<<5)	// high priority req
	    + (0<<6)	// high priority req
	    + (1<<8)	// high priority req max
	    + (0<<9)	// high priority req max
	    + (0<<10)	// high priority req max
	    + (0<<20)   // CRTC1 to DAC
	    + (1<<21)   // 422 mode
	    + (0<<22)   // 422 mode
	    + (1<<23)   // 422 mode
	    + (1<<24)   // single chroma line for 420 mode - need to be corrected
	    + (1<<25)   /*/ interlace mode - need to be corrected*/
	    + (0<<26)   // field legth polariry
	    + (0<<27)   // field identification polariry
	    + (1<<28)   // VIDRST detection mode
	    + (0<<29)   // VIDRST detection mode
	    + (1<<30)   // Horizontal counter preload
	    + (1<<31)   // Vertical counter preload
	    ;
	cregs.c2datactl = 0         // enable dither - propably not needed, we are already in YUV mode
	    + (1<<1)	// Y filter enable
	    + (1<<2)	// CbCr filter enable
	    + (1<<3)	// subpicture enable (enabled)
	    + (0<<4)	// NTSC enable (disabled - PAL)
	    + (0<<5)	// C2 static subpicture enable (disabled)
	    + (0<<6)	// C2 subpicture offset division (disabled)
	    + (1<<7)	// 422 subformat selection !
	    /*		    + (0<<8)	// 15 bpp high alpha
	     + (0<<9)	// 15 bpp high alpha
	     + (0<<10)	// 15 bpp high alpha
	     + (0<<11)	// 15 bpp high alpha
	     + (0<<12)	// 15 bpp high alpha
	     + (0<<13)	// 15 bpp high alpha
	     + (0<<14)	// 15 bpp high alpha
	     + (0<<15)	// 15 bpp high alpha
	     + (0<<16)	// 15 bpp low alpha
	     + (0<<17)	// 15 bpp low alpha
	     + (0<<18)	// 15 bpp low alpha
	     + (0<<19)	// 15 bpp low alpha
	     + (0<<20)	// 15 bpp low alpha
	     + (0<<21)	// 15 bpp low alpha
	     + (0<<22)	// 15 bpp low alpha
	     + (0<<23)	// 15 bpp low alpha
	     + (0<<24)	// static subpicture key
	     + (0<<25)	// static subpicture key
	     + (0<<26)	// static subpicture key
	     + (0<<27)	// static subpicture key
	     + (0<<28)	// static subpicture key
	     */		    ;
	break;
    }

    cregs.c2hparam=((hdispend - 8) << 16) | (htotal - 8);
    cregs.c2hsync=((hsyncend - 8) << 16) | (hsyncstart - 8);

    cregs.c2misc=0	// CRTCV2 656 togg f0
	+(0<<1) // CRTCV2 656 togg f0
	+(0<<2) // CRTCV2 656 togg f0
	+(0<<4) // CRTCV2 656 togg f1
	+(0<<5) // CRTCV2 656 togg f1
	+(0<<6) // CRTCV2 656 togg f1
	+(0<<8) // Hsync active high
	+(0<<9) // Vsync active high
	// 16-27 c2vlinecomp - nevim co tam dat
	;
    cregs.c2offset=(regs.bespitch << 1);

    cregs.c2pl2startadd0=regs.besa1corg;
    //cregs.c2pl2startadd1=regs.besa2corg;
    cregs.c2pl3startadd0=regs.besa1c3org;
    //cregs.c2pl3startadd1=regs.besa2c3org;

    cregs.c2preload=(vsyncstart << 16) | (hsyncstart); // from

    memset(config->dga_addr + config->offsets[config->num_frames], 0, config->frame_size); // clean spic area
    cregs.c2spicstartadd0=(uint32_t) mga_src_base + baseadrofs + config->num_frames*config->frame_size;
    //cregs.c2spicstartadd1=0; // not used

    cregs.c2startadd0=regs.besa1org;
    //cregs.c2startadd1=regs.besa2org;

    cregs.c2subpiclut=0; //not used

    cregs.c2vparam=((vdispend - 1) << 16) | (vtotal - 1);
    cregs.c2vsync=((vsyncend - 1) << 16) | (vsyncstart - 1);
#endif /* CRTC2 */

    mga_vid_write_regs(0);
    return(0);
}

int VIDIX_NAME(vixPlaybackOn)(void)
{
    if (mga_verbose) printf(MGA_MSG" playback on\n");

    vid_src_ready = 1;
    if(vid_overlay_on)
    {
	regs.besctl |= 1;
	mga_vid_write_regs(0);
    }
#ifdef MGA_ALLOW_IRQ
    if (mga_irq != -1)
	enable_irq();
#endif
    mga_next_frame=0;

    return(0);
}

int VIDIX_NAME(vixPlaybackOff)(void)
{
    if (mga_verbose) printf(MGA_MSG" playback off\n");

    vid_src_ready = 0;
#ifdef MGA_ALLOW_IRQ
    if (mga_irq != -1)
	disable_irq();
#endif
    regs.besctl &= ~1;
    regs.besglobctl &= ~(1<<6); /* UYVY format selected */
    mga_vid_write_regs(0);

    return(0);
}

int VIDIX_NAME(vixProbe)(int verbose,int force)
{
    pciinfo_t lst[MAX_PCI_DEVICES];
    unsigned int i, num_pci;
    int err;

    if (verbose) printf(MGA_MSG" probe\n");

    mga_verbose = verbose;

    is_g400 = -1;

    err = pci_scan(lst, &num_pci);
    if (err)
    {
	printf(MGA_MSG" Error occured during pci scan: %s\n", strerror(err));
	return(err);
    }

    if (mga_verbose)
	printf(MGA_MSG" found %d pci devices\n", num_pci);

    for (i = 0; i < num_pci; i++)
    {
	if (mga_verbose > 1)
	    printf(MGA_MSG" pci[%d] vendor: %d device: %d\n",
		   i, lst[i].vendor, lst[i].device);
	if (lst[i].vendor == VENDOR_MATROX)
	{
	    switch(lst[i].device)
	    {
	    case DEVICE_MATROX_MGA_G550_AGP:
		printf(MGA_MSG" Found MGA G550\n");
		is_g400 = 1;
		goto card_found;
	    case DEVICE_MATROX_MGA_G400_AGP:
		printf(MGA_MSG" Found MGA G400/G450\n");
		is_g400 = 1;
		goto card_found;
#ifndef CRTC2
	    case DEVICE_MATROX_MGA_G200_AGP:
		printf(MGA_MSG" Found MGA G200 AGP\n");
		is_g400 = 0;
		goto card_found;
	    case DEVICE_MATROX_MGA_G200:
		printf(MGA_MSG" Found MGA G200 PCI\n");
		is_g400 = 0;
		goto card_found;
#endif
	    }
	}
    }

    if (is_g400 == -1)
    {
	if(verbose)
	  printf(MGA_MSG" Can't find chip\n\n");
	return(ENXIO);
    }

card_found:
    probed = 1;
    memcpy(&pci_info, &lst[i], sizeof(pciinfo_t));

    mga_cap.device_id = pci_info.device; /* set device id in capabilites */

    return(0);
}

int VIDIX_NAME(vixInit)(const char *args)
{
    unsigned int card_option = 0;
    int err;

    /* reset Brightness & Constrast here */
    regs.beslumactl = (0x0 << 16) + 0x80;

    if (mga_verbose) printf(MGA_MSG" init\n");

    mga_vid_in_use = 0;

    if (!probed)
    {
	printf(MGA_MSG" driver was not probed but is being initializing\n");
	return(EINTR);
    }

#ifdef MGA_PCICONFIG_MEMDETECT
    pci_config_read(pci_info.bus, pci_info.card, pci_info.func,
		    0x40, 4, &card_option);
    if (mga_verbose > 1) printf(MGA_MSG" OPTION word: 0x%08X  mem: 0x%02X  %s\n", card_option,
				(card_option>>10)&0x17, ((card_option>>14)&1)?"SGRAM":"SDRAM");
#endif

    if (mga_ram_size)
    {
	printf(MGA_MSG" RAMSIZE forced to %d MB\n", mga_ram_size);
    }
    else
    {
#ifdef MGA_MEMORY_SIZE
	mga_ram_size = MGA_MEMORY_SIZE;
	printf(MGA_MSG" hard-coded RAMSIZE is %d MB\n", (unsigned int) mga_ram_size);
#else
	if (is_g400)
	{
	    switch((card_option>>10)&0x17)
	    {
	    // SDRAM:
	    case 0x00:
	    case 0x04:  mga_ram_size = 16; break;
	    case 0x03:  mga_ram_size = 32; break;
	    // SGRAM:
	    case 0x10:
	    case 0x14:  mga_ram_size = 32; break;
	    case 0x11:
	    case 0x12:  mga_ram_size = 16; break;
	    default:
		mga_ram_size = 16;
		printf(MGA_MSG" Couldn't detect RAMSIZE, assuming 16MB!\n");
	    }
	}
	else
	{
	    switch((card_option>>10)&0x17)
	    {
		// case 0x10:
		// case 0x13:  mga_ram_size = 8; break;
	    default: mga_ram_size = 8;
	    }
	}

#if 0
	//	    printf("List resources -----------\n");
	for(temp=0;temp<DEVICE_COUNT_RESOURCE;temp++){
	    struct resource *res=&pci_dev->resource[temp];
	    if(res->flags){
		int size=(1+res->end-res->start)>>20;
		printf("res %d:  start: 0x%X   end: 0x%X  (%d MB) flags=0x%X\n",temp,res->start,res->end,size,res->flags);
		if(res->flags&(IORESOURCE_MEM|IORESOURCE_PREFETCH)){
		    if(size>mga_ram_size && size<=64) mga_ram_size=size;
		}
	    }
	}
#endif

	printf(MGA_MSG" detected RAMSIZE is %d MB\n", (unsigned int) mga_ram_size);
#endif
    }

    if (mga_ram_size)
    {
	if ((mga_ram_size < 4) || (mga_ram_size > 64))
	{
	    printf(MGA_MSG" invalid RAMSIZE: %d MB\n", mga_ram_size);
	    return(EINVAL);
	}
    }

    if (mga_verbose > 1) printf(MGA_MSG" hardware addresses: mmio: 0x%lx, framebuffer: 0x%lx\n",
				pci_info.base1, pci_info.base0);

    mga_mmio_base = map_phys_mem(pci_info.base1,0x4000);
    mga_mem_base = map_phys_mem(pci_info.base0,mga_ram_size*1024*1024);

    if (mga_verbose > 1) printf(MGA_MSG" MMIO at %p, IRQ: %d, framebuffer: %p\n",
				mga_mmio_base, mga_irq, mga_mem_base);
    err = mtrr_set_type(pci_info.base0,mga_ram_size*1024*1024,MTRR_TYPE_WRCOMB);
    if(!err) printf(MGA_MSG" Set write-combining type of video memory\n");
#ifdef MGA_ALLOW_IRQ
    if (mga_irq != -1)
    {
	int tmp = request_irq(mga_irq, mga_handle_irq, SA_INTERRUPT | SA_SHIRQ, "Syncfb Time Base", &mga_irq);
	if (tmp)
	{
	    printf("syncfb (mga): cannot register irq %d (Err: %d)\n", mga_irq, tmp);
	    mga_irq=-1;
	}
	else
	{
	    printf("syncfb (mga): registered irq %d\n", mga_irq);
	}
    }
    else
    {
	printf("syncfb (mga): No valid irq was found\n");
	mga_irq=-1;
    }
#else
    printf(MGA_MSG" IRQ support disabled\n");
    mga_irq=-1;
#endif
#ifdef CRTC2
    memset(&cregs_save, 0, sizeof(cregs_save));
#endif
    return(0);
}

void VIDIX_NAME(vixDestroy)(void)
{
    if (mga_verbose) printf(MGA_MSG" destroy\n");

    /* FIXME turn off BES */
    vid_src_ready = 0;
    regs.besctl &= ~1;
    regs.besglobctl &= ~(1<<6);  // UYVY format selected
    //    mga_config.colkey_on=0; //!!!
    mga_vid_write_regs(1);
    mga_vid_in_use = 0;

#ifdef MGA_ALLOW_IRQ
    if (mga_irq != -1)
	free_irq(mga_irq, &mga_irq);
#endif

    if (mga_mmio_base)
	unmap_phys_mem(mga_mmio_base, 0x4000);
    if (mga_mem_base)
	unmap_phys_mem(mga_mem_base, mga_ram_size);
    return;
}

int VIDIX_NAME(vixQueryFourcc)(vidix_fourcc_t *to)
{
    int supports=0;
    if (mga_verbose) printf(MGA_MSG" query fourcc (%x)\n", to->fourcc);

    switch(to->fourcc)
    {
    case IMGFMT_YV12:
    case IMGFMT_IYUV:
    case IMGFMT_I420:
	supports = is_g400 ? 1 : 0;
    case IMGFMT_NV12:
	supports = is_g400 ? 0 : 1;
    case IMGFMT_YUY2:
    case IMGFMT_UYVY:
	supports = 1;
	break;
    default:
	supports = 0;
    }

    if(!supports)
    {
	to->depth = to->flags = 0;
	return(ENOTSUP);
    }
    to->depth = VID_DEPTH_12BPP |
	VID_DEPTH_15BPP | VID_DEPTH_16BPP |
	VID_DEPTH_24BPP | VID_DEPTH_32BPP;
    to->flags = VID_CAP_EXPAND | VID_CAP_SHRINK | VID_CAP_COLORKEY;
    return(0);
}

unsigned int VIDIX_NAME(vixGetVersion)(void)
{
    return(VIDIX_VERSION);
}

int VIDIX_NAME(vixGetCapability)(vidix_capability_t *to)
{
    memcpy(to, &mga_cap, sizeof(vidix_capability_t));
    return(0);
}

int VIDIX_NAME(vixGetGrKeys)(vidix_grkey_t *grkey)
{
    memcpy(grkey, &mga_grkey, sizeof(vidix_grkey_t));
    return(0);
}

int VIDIX_NAME(vixSetGrKeys)(const vidix_grkey_t *grkey)
{
    memcpy(&mga_grkey, grkey, sizeof(vidix_grkey_t));
    return(0);
}

int VIDIX_NAME(vixPlaybackSetEq)( const vidix_video_eq_t * eq)
{
    uint32_t luma;
    float factor = 255.0 / 2000;

    /* contrast and brightness control isn't supported on G200 - alex */
    if (!is_g400)
    {
	if (mga_verbose) printf(MGA_MSG" equalizer isn't supported with G200\n");
	return(ENOTSUP);
    }

    luma = regs.beslumactl;

    if (eq->cap & VEQ_CAP_BRIGHTNESS)
    {
	luma &= 0xffff;
	luma |= (((int)(eq->brightness * factor) & 0xff) << 16);
    }
    if (eq->cap & VEQ_CAP_CONTRAST)
    {
	luma &= 0xffff << 16;
	luma |= ((int)((eq->contrast + 1000) * factor) & 0xff);
    }

    regs.beslumactl = luma;
#ifdef BES
    writel(BESLUMACTL, regs.beslumactl);
#endif
    return(0);
}

int VIDIX_NAME(vixPlaybackGetEq)( vidix_video_eq_t * eq)
{
    float factor = 2000.0 / 255;

    /* contrast and brightness control isn't supported on G200 - alex */
    if (!is_g400)
    {
	if (mga_verbose) printf(MGA_MSG" equalizer isn't supported with G200\n");
	return(ENOTSUP);
    }

    // BESLUMACTL is WO only registr!
    // this will not work: regs.beslumactl = readl(BESLUMACTL);
    eq->brightness = ((signed char)((regs.beslumactl >> 16) & 0xff)) * factor;
    eq->contrast = (regs.beslumactl & 0xFF) * factor - 1000;
    eq->cap = VEQ_CAP_BRIGHTNESS | VEQ_CAP_CONTRAST;

    return(0);
}
