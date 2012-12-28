/*
   mach64_vid - VIDIX based video driver for Mach64 and 3DRage chips
   Copyrights 2002 Nick Kurshev. This file is based on sources from
   GATOS (gatos.sf.net) and X11 (www.xfree86.org)
   Licence: GPL
   WARNING: THIS DRIVER IS IN BETTA STAGE
*/
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/mman.h> /* for m(un)lock */
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#ifdef HAVE_MEMALIGN
#define MACH64_ENABLE_BM 1
#endif
#endif

#include "vidix.h"
#include "fourcc.h"
#include "libdha.h"
#include "pci_ids.h"
#include "pci_names.h"
#include "bswap.h"

#include "mach64.h"

#define UNUSED(x) ((void)(x)) /**< Removes warning about unused arguments */

#define VIDIX_STATIC mach64_

#ifdef MACH64_ENABLE_BM

#define cpu_to_le32(a) (a)
#define VIRT_TO_CARD(a,b,c) bm_virt_to_bus(a,b,c)
#pragma pack(1)
typedef struct
{
	uint32_t framebuf_offset;
	uint32_t sys_addr;
	uint32_t command;
	uint32_t reserved;
} bm_list_descriptor;
#pragma pack()
static void *mach64_dma_desc_base[64];
static unsigned long bus_addr_dma_desc = 0;
static unsigned long *dma_phys_addrs;
#endif

static void *mach64_mmio_base = 0;
static void *mach64_mem_base = 0;
static int32_t mach64_overlay_offset = 0;
static uint32_t mach64_ram_size = 0;
static uint32_t mach64_buffer_base[64][3];
static int num_mach64_buffers=-1;
static int supports_planar=0;
static int supports_colour_adj=0;
static int supports_idct=0;
static int supports_subpic=0;
static int supports_lcd_v_stretch=0;

pciinfo_t pci_info;
static int probed = 0;
static int __verbose = 0;

#define VERBOSE_LEVEL 2

typedef struct bes_registers_s
{
  /* base address of yuv framebuffer */
  uint32_t yuv_base;
  uint32_t fourcc;
  /* YUV BES registers */
  uint32_t reg_load_cntl;
  uint32_t scale_inc;
  uint32_t y_x_start;
  uint32_t y_x_end;
  uint32_t vid_buf_pitch;
  uint32_t height_width;

  uint32_t scale_cntl;
  uint32_t exclusive_horz;
  uint32_t auto_flip_cntl;
  uint32_t filter_cntl;
  uint32_t key_cntl;
  uint32_t test;
  /* Configurable stuff */
  
  int brightness;
  int saturation;
  
  int ckey_on;
  uint32_t graphics_key_clr;
  uint32_t graphics_key_msk;
  
  int deinterlace_on;
  uint32_t deinterlace_pattern;
  
} bes_registers_t;

static bes_registers_t besr;

typedef struct video_registers_s
{
  const char * sname;
  uint32_t name;
  uint32_t value;
}video_registers_t;

static bes_registers_t besr;

/* Graphic keys */
static vidix_grkey_t mach64_grkey;

#define DECLARE_VREG(name) { #name, name, 0 }
static video_registers_t vregs[] = 
{
  DECLARE_VREG(OVERLAY_SCALE_INC),
  DECLARE_VREG(OVERLAY_Y_X_START),
  DECLARE_VREG(OVERLAY_Y_X_END),
  DECLARE_VREG(OVERLAY_SCALE_CNTL),
  DECLARE_VREG(OVERLAY_EXCLUSIVE_HORZ),
  DECLARE_VREG(OVERLAY_EXCLUSIVE_VERT),
  DECLARE_VREG(OVERLAY_TEST),
  DECLARE_VREG(SCALER_BUF_PITCH),
  DECLARE_VREG(SCALER_HEIGHT_WIDTH),
  DECLARE_VREG(SCALER_BUF0_OFFSET),
  DECLARE_VREG(SCALER_BUF0_OFFSET_U),
  DECLARE_VREG(SCALER_BUF0_OFFSET_V),
  DECLARE_VREG(SCALER_BUF1_OFFSET),
  DECLARE_VREG(SCALER_BUF1_OFFSET_U),
  DECLARE_VREG(SCALER_BUF1_OFFSET_V),
  DECLARE_VREG(SCALER_H_COEFF0),
  DECLARE_VREG(SCALER_H_COEFF1),
  DECLARE_VREG(SCALER_H_COEFF2),
  DECLARE_VREG(SCALER_H_COEFF3),
  DECLARE_VREG(SCALER_H_COEFF4),
  DECLARE_VREG(SCALER_COLOUR_CNTL),
  DECLARE_VREG(SCALER_THRESHOLD),
  DECLARE_VREG(VIDEO_FORMAT),
  DECLARE_VREG(VIDEO_CONFIG),
  DECLARE_VREG(VIDEO_SYNC_TEST),
  DECLARE_VREG(VIDEO_SYNC_TEST_B),
  DECLARE_VREG(BUS_CNTL),
  DECLARE_VREG(SRC_CNTL),
  DECLARE_VREG(GUI_STAT),
  DECLARE_VREG(BM_ADDR),
  DECLARE_VREG(BM_DATA),
  DECLARE_VREG(BM_HOSTDATA),
  DECLARE_VREG(BM_GUI_TABLE_CMD),
  DECLARE_VREG(BM_FRAME_BUF_OFFSET),
  DECLARE_VREG(BM_SYSTEM_MEM_ADDR),
  DECLARE_VREG(BM_COMMAND),
  DECLARE_VREG(BM_STATUS),
  DECLARE_VREG(BM_GUI_TABLE),
  DECLARE_VREG(BM_SYSTEM_TABLE),
  DECLARE_VREG(AGP_BASE),
  DECLARE_VREG(AGP_CNTL),
  DECLARE_VREG(CRTC_INT_CNTL)
};

/* VIDIX exports */

/* MMIO space*/
#define GETREG(TYPE,PTR,OFFZ)		(*((volatile TYPE*)((PTR)+(OFFZ))))
#define SETREG(TYPE,PTR,OFFZ,VAL)	(*((volatile TYPE*)((PTR)+(OFFZ))))=VAL

#define INREG8(addr)		GETREG(uint8_t,(uint32_t)mach64_mmio_base,((addr)^0x100)<<2)
#define OUTREG8(addr,val)	SETREG(uint8_t,(uint32_t)mach64_mmio_base,((addr)^0x100)<<2,val)
static inline uint32_t INREG (uint32_t addr) {
    uint32_t tmp = GETREG(uint32_t,(uint32_t)mach64_mmio_base,((addr)^0x100)<<2);
    return le2me_32(tmp);
}
#define OUTREG(addr,val)	SETREG(uint32_t,(uint32_t)mach64_mmio_base,((addr)^0x100)<<2,le2me_32(val))

#define OUTREGP(addr,val,mask)  					\
	do {								\
		unsigned int _tmp = INREG(addr);			\
		_tmp &= (mask);						\
		_tmp |= (val);						\
		OUTREG(addr, _tmp);					\
	} while (0)

static __inline__ int ATIGetMach64LCDReg(int _Index)
{
        OUTREG8(LCD_INDEX, _Index);
        return INREG(LCD_DATA);
}

static __inline__ uint32_t INPLL(uint32_t addr)
{
    uint32_t res;
    uint32_t in;
    
    in= INREG(CLOCK_CNTL);
    in &= ~((PLL_WR_EN | PLL_ADDR)); //clean some stuff
    OUTREG(CLOCK_CNTL, in | (addr<<10));
    
    /* read the register value */
    res = (INREG(CLOCK_CNTL)>>16)&0xFF;
    return res;
}

static __inline__ void OUTPLL(uint32_t addr,uint32_t val)
{
//FIXME buggy but its not used
    /* write addr byte */
    OUTREG8(CLOCK_CNTL + 1, (addr << 2) | PLL_WR_EN);
    /* write the register value */
    OUTREG(CLOCK_CNTL + 2, val);
    OUTREG8(CLOCK_CNTL + 1, (addr << 2) & ~PLL_WR_EN);
}

#define OUTPLLP(addr,val,mask)  					\
	do {								\
		unsigned int _tmp = INPLL(addr);			\
		_tmp &= (mask);						\
		_tmp |= (val);						\
		OUTPLL(addr, _tmp);					\
	} while (0)
	
static void mach64_engine_reset( void )
{
  /* Kill off bus mastering with extreme predjudice... */
  OUTREG(BUS_CNTL, INREG(BUS_CNTL) | BUS_MASTER_DIS);
  OUTREG(CRTC_INT_CNTL,INREG(CRTC_INT_CNTL)&~(CRTC_BUSMASTER_EOL_INT|CRTC_BUSMASTER_EOL_INT_EN));
  /* Reset engine -- This is accomplished by setting bit 8 of the GEN_TEST_CNTL
   register high, then low (per the documentation, it's on high to low transition
   that the GUI engine gets reset...) */
  OUTREG( GEN_TEST_CNTL, INREG( GEN_TEST_CNTL ) | GEN_GUI_EN );
  OUTREG( GEN_TEST_CNTL, INREG( GEN_TEST_CNTL ) & ~GEN_GUI_EN );
}

static void mach64_fifo_wait(unsigned n) 
{
    while ((INREG(FIFO_STAT) & 0xffff) > ((uint32_t)(0x8000 >> n)));
}

static void mach64_wait_for_idle( void ) 
{
    unsigned i;
    mach64_fifo_wait(16);
    for (i=0; i<2000000; i++) if((INREG(GUI_STAT) & GUI_ACTIVE) == 0) break;
    if((INREG(GUI_STAT) & 1) != 0) mach64_engine_reset(); /* due card lookup */
}

static void mach64_wait_vsync( void )
{
    int i;

    for(i=0; i<2000000; i++)
	if( (INREG(CRTC_INT_CNTL)&CRTC_VBLANK)==0 ) break;
    for(i=0; i<2000000; i++)
	if( (INREG(CRTC_INT_CNTL)&CRTC_VBLANK) ) break;

}

static vidix_capability_t mach64_cap =
{
    "BES driver for Mach64/3DRage cards",
    "Nick Kurshev and Michael Niedermayer",
    TYPE_OUTPUT,
    { 0, 0, 0, 0 },
    2048,
    2048,
    4,
    4,
    -1,
    FLAG_UPSCALER|FLAG_DOWNSCALER,
    VENDOR_ATI,
    -1,
    { 0, 0, 0, 0 }
};

static uint32_t mach64_vid_get_dbpp( void )
{
  uint32_t dbpp,retval;
  dbpp = (INREG(CRTC_GEN_CNTL)>>8)& 0x7;
  switch(dbpp)
  {
    case 1: retval = 4; break;
    case 2: retval = 8; break;
    case 3: retval = 15; break;
    case 4: retval = 16; break;
    case 5: retval = 24; break;
    default: retval=32; break;
  }
  return retval;
}

static int mach64_is_dbl_scan( void )
{
  return INREG(CRTC_GEN_CNTL) & CRTC_DBL_SCAN_EN;
}

static int mach64_is_interlace( void )
{
  return INREG(CRTC_GEN_CNTL) & CRTC_INTERLACE_EN;
}

static uint32_t mach64_get_xres( void )
{
  /* FIXME: currently we extract that from CRTC!!!*/
  uint32_t xres,h_total;
  h_total = INREG(CRTC_H_TOTAL_DISP);
  xres = (h_total >> 16) & 0xffff;
  return (xres + 1)*8;
}

static uint32_t mach64_get_yres( void )
{
  /* FIXME: currently we extract that from CRTC!!!*/
  uint32_t yres,v_total;
  v_total = INREG(CRTC_V_TOTAL_DISP);
  yres = (v_total >> 16) & 0xffff;
  return yres + 1;
}

// returns the verical stretch factor in 16.16
static int mach64_get_vert_stretch(void)
{
    int lcd_index;
    int vert_stretching;
    int ext_vert_stretch;
    int ret;
    int yres= mach64_get_yres();

    if(!supports_lcd_v_stretch){
        if(__verbose>0) printf("[mach64] vertical stretching not supported\n");
        return 1<<16;
    }

    lcd_index= INREG(LCD_INDEX);
    
    vert_stretching= ATIGetMach64LCDReg(LCD_VERT_STRETCHING);
    if(!(vert_stretching&VERT_STRETCH_EN)) ret= 1<<16;
    else
    {
    	int panel_size;
        
	ext_vert_stretch= ATIGetMach64LCDReg(LCD_EXT_VERT_STRETCH);
	panel_size= (ext_vert_stretch&VERT_PANEL_SIZE)>>11;
	panel_size++;
	
	ret= ((yres<<16) + (panel_size>>1))/panel_size;
    }
      
//    lcd_gen_ctrl = ATIGetMach64LCDReg(LCD_GEN_CNTL);
    
    OUTREG(LCD_INDEX, lcd_index);
    
    if(__verbose>0) printf("[mach64] vertical stretching factor= %d\n", ret);
    
    return ret;
}

static void mach64_vid_make_default()
{
  mach64_fifo_wait(5);
  OUTREG(SCALER_COLOUR_CNTL,0x00101000);

  besr.ckey_on=0;
  besr.graphics_key_msk=0;
  besr.graphics_key_clr=0;

  OUTREG(OVERLAY_GRAPHICS_KEY_MSK, besr.graphics_key_msk);
  OUTREG(OVERLAY_GRAPHICS_KEY_CLR, besr.graphics_key_clr);
  OUTREG(OVERLAY_KEY_CNTL,VIDEO_KEY_FN_TRUE|GRAPHIC_KEY_FN_EQ|CMP_MIX_AND);

}

static void mach64_vid_dump_regs( void )
{
  size_t i;
  printf("[mach64] *** Begin of DRIVER variables dump ***\n");
  printf("[mach64] mach64_mmio_base=%p\n",mach64_mmio_base);
  printf("[mach64] mach64_mem_base=%p\n",mach64_mem_base);
  printf("[mach64] mach64_overlay_off=%08X\n",mach64_overlay_offset);
  printf("[mach64] mach64_ram_size=%08X\n",mach64_ram_size);
  printf("[mach64] video mode: %ux%u@%u\n",mach64_get_xres(),mach64_get_yres(),mach64_vid_get_dbpp());
  printf("[mach64] *** Begin of OV0 registers dump ***\n");
  for(i=0;i<sizeof(vregs)/sizeof(video_registers_t);i++)
  {
	mach64_wait_for_idle();
	mach64_fifo_wait(2);
	printf("[mach64] %s = %08X\n",vregs[i].sname,INREG(vregs[i].name));
  }
  printf("[mach64] *** End of OV0 registers dump ***\n");
}


unsigned int VIDIX_NAME(vixGetVersion)(void)
{
    return(VIDIX_VERSION);
}

typedef struct ati_chip_id_s
{
    unsigned short	id;
    unsigned short	is_agp;
}ati_chip_id_t;

static ati_chip_id_t ati_card_ids[] = 
{
 { DEVICE_ATI_215CT_MACH64_CT, 0 },
 { DEVICE_ATI_210888CX_MACH64_CX, 0 },
 { DEVICE_ATI_210888ET_MACH64_ET, 0 },
 { DEVICE_ATI_MACH64_VT, 0 },
 { DEVICE_ATI_210888GX_MACH64_GX, 0 },
 { DEVICE_ATI_264LT_MACH64_LT, 0 },
 { DEVICE_ATI_264VT_MACH64_VT, 0 },
 { DEVICE_ATI_264VT3_MACH64_VT3, 0 },
 { DEVICE_ATI_264VT4_MACH64_VT4, 0 },
 /**/
 { DEVICE_ATI_3D_RAGE_PRO, 1 },
 { DEVICE_ATI_3D_RAGE_PRO2, 1 },
 { DEVICE_ATI_3D_RAGE_PRO3, 0 },
 { DEVICE_ATI_3D_RAGE_PRO4, 0 },
 { DEVICE_ATI_RAGE_XC, 0 },
 { DEVICE_ATI_RAGE_XL_AGP, 1 },
 { DEVICE_ATI_RAGE_XC_AGP, 1 },
 { DEVICE_ATI_RAGE_XL, 0 },
 { DEVICE_ATI_3D_RAGE_PRO5, 0 },
 { DEVICE_ATI_3D_RAGE_PRO6, 0 },
 { DEVICE_ATI_RAGE_XL2, 0 },
 { DEVICE_ATI_RAGE_XC2, 0 },
 { DEVICE_ATI_3D_RAGE_I_II, 0 },
 { DEVICE_ATI_3D_RAGE_II, 0 },
 { DEVICE_ATI_3D_RAGE_IIC, 1 },
 { DEVICE_ATI_3D_RAGE_IIC2, 0 },
 { DEVICE_ATI_3D_RAGE_IIC3, 0 },
 { DEVICE_ATI_3D_RAGE_IIC4, 1 },
 { DEVICE_ATI_3D_RAGE_LT, 1 },
 { DEVICE_ATI_3D_RAGE_LT2, 1 },
 { DEVICE_ATI_3D_RAGE_LT_G, 0 },
 { DEVICE_ATI_3D_RAGE_LT3, 0 },
 { DEVICE_ATI_RAGE_MOBILITY_P_M, 1 },
 { DEVICE_ATI_RAGE_MOBILITY_L, 1 },
 { DEVICE_ATI_3D_RAGE_LT4, 0 },
 { DEVICE_ATI_3D_RAGE_LT5, 0 },
 { DEVICE_ATI_RAGE_MOBILITY_P_M2, 0 },
 { DEVICE_ATI_RAGE_MOBILITY_L2, 0 }
};

static int is_agp;

static int find_chip(unsigned chip_id)
{
  unsigned i;
  for(i = 0;i < sizeof(ati_card_ids)/sizeof(ati_chip_id_t);i++)
  {
    if(chip_id == ati_card_ids[i].id) return i;
  }
  return -1;
}

int VIDIX_NAME(vixProbe)(int verbose,int force)
{
  pciinfo_t lst[MAX_PCI_DEVICES];
  unsigned i,num_pci;
  int err;
  __verbose = verbose;
  err = pci_scan(lst,&num_pci);
  if(err)
  {
    printf("[mach64] Error occured during pci scan: %s\n",strerror(err));
    return err;
  }
  else
  {
    err = ENXIO;
    for(i=0;i<num_pci;i++)
    {
      if(lst[i].vendor == VENDOR_ATI)
      {
        int idx;
	const char *dname;
	idx = find_chip(lst[i].device);
	if(idx == -1 && force == PROBE_NORMAL) continue;
	dname = pci_device_name(VENDOR_ATI,lst[i].device);
	dname = dname ? dname : "Unknown chip";
	printf("[mach64] Found chip: %s\n",dname);
	if(force > PROBE_NORMAL)
	{
	    printf("[mach64] Driver was forced. Was found %sknown chip\n",idx == -1 ? "un" : "");
	    if(idx == -1)
		printf("[mach64] Assuming it as Mach64\n");
	}
	if(idx != -1) is_agp = ati_card_ids[idx].is_agp;
	mach64_cap.device_id = lst[i].device;
	err = 0;
	memcpy(&pci_info,&lst[i],sizeof(pciinfo_t));
	probed=1;
	break;
      }
    }
  }
  if(err && verbose) printf("[mach64] Can't find chip\n");
  return err;
}

static void reset_regs( void )
{
  size_t i;
  for(i=0;i<sizeof(vregs)/sizeof(video_registers_t);i++)
  {
	mach64_fifo_wait(2);
	OUTREG(vregs[i].name,0);
  }
}

typedef struct saved_regs_s
{
    uint32_t overlay_video_key_clr;
    uint32_t overlay_video_key_msk;
    uint32_t overlay_graphics_key_clr;
    uint32_t overlay_graphics_key_msk;
    uint32_t overlay_key_cntl;
    uint32_t bus_cntl;
}saved_regs_t;
static saved_regs_t savreg;

static void save_regs( void )
{
    mach64_fifo_wait(6);
    savreg.overlay_video_key_clr	= INREG(OVERLAY_VIDEO_KEY_CLR);
    savreg.overlay_video_key_msk	= INREG(OVERLAY_VIDEO_KEY_MSK);
    savreg.overlay_graphics_key_clr	= INREG(OVERLAY_GRAPHICS_KEY_CLR);
    savreg.overlay_graphics_key_msk	= INREG(OVERLAY_GRAPHICS_KEY_MSK);
    savreg.overlay_key_cntl		= INREG(OVERLAY_KEY_CNTL);
    savreg.bus_cntl			= INREG(BUS_CNTL);
}

static void restore_regs( void )
{
    mach64_fifo_wait(6);
    OUTREG(OVERLAY_VIDEO_KEY_CLR,savreg.overlay_video_key_clr);
    OUTREG(OVERLAY_VIDEO_KEY_MSK,savreg.overlay_video_key_msk);
    OUTREG(OVERLAY_GRAPHICS_KEY_CLR,savreg.overlay_graphics_key_clr);
    OUTREG(OVERLAY_GRAPHICS_KEY_MSK,savreg.overlay_graphics_key_msk);
    OUTREG(OVERLAY_KEY_CNTL,savreg.overlay_key_cntl);
    OUTREG(BUS_CNTL,savreg.bus_cntl|BUS_MASTER_DIS);
}

static int forced_irq=INT_MAX;

#ifdef MACH64_ENABLE_BM
static int can_use_irq=0;
static int irq_installed=0;

static void init_irq(void)
{
	irq_installed=1;
	if(forced_irq != INT_MAX) pci_info.irq=forced_irq;
	if(hwirq_install(pci_info.bus,pci_info.card,pci_info.func,
			 2,CRTC_INT_CNTL,CRTC_BUSMASTER_EOL_INT) == 0)
	{
	    can_use_irq=1;
	    if(__verbose) printf("[mach64] Will use %u irq line\n",pci_info.irq);
	}
	else 
	    if(__verbose) printf("[mach64] Can't initialize irq handling: %s\n"
				 "[mach64]irq_param: line=%u pin=%u gnt=%u lat=%u\n"
				 ,strerror(errno)
				 ,pci_info.irq,pci_info.ipin,pci_info.gnt,pci_info.lat);
}
#endif

int VIDIX_NAME(vixInit)(const char *args)
{
  int err;
#ifdef MACH64_ENABLE_BM
  unsigned i;
#endif
  if(!probed)
  {
    printf("[mach64] Driver was not probed but is being initializing\n");
    return EINTR;
  }
  if(__verbose>0) printf("[mach64] version %d args='%s'\n", VIDIX_VERSION,args);
  if(args)
  if(strncmp(args,"irq=",4) == 0) 
  {
    forced_irq=atoi(&args[4]);
    if(__verbose>0) printf("[mach64] forcing IRQ to %u\n",forced_irq);     
  }

  if((mach64_mmio_base = map_phys_mem(pci_info.base2,0x4000))==(void *)-1) return ENOMEM;
  mach64_wait_for_idle();
  mach64_ram_size = INREG(MEM_CNTL) & CTL_MEM_SIZEB;
  if (mach64_ram_size < 8) mach64_ram_size = (mach64_ram_size + 1) * 512;
  else if (mach64_ram_size < 12) mach64_ram_size = (mach64_ram_size - 3) * 1024;
  else mach64_ram_size = (mach64_ram_size - 7) * 2048;
  mach64_ram_size *= 0x400; /* KB -> bytes */
  if((mach64_mem_base = map_phys_mem(pci_info.base0,mach64_ram_size))==(void *)-1) return ENOMEM;
  memset(&besr,0,sizeof(bes_registers_t));
  printf("[mach64] Video memory = %uMb\n",mach64_ram_size/0x100000);
  err = mtrr_set_type(pci_info.base0,mach64_ram_size,MTRR_TYPE_WRCOMB);
  if(!err) printf("[mach64] Set write-combining type of video memory\n");
  
  save_regs();
  /* check if planar formats are supported */
  supports_planar=0;
  mach64_wait_for_idle();
  mach64_fifo_wait(2);
  if(INREG(SCALER_BUF0_OFFSET_U)) supports_planar=1;
  else
  {
	OUTREG(SCALER_BUF0_OFFSET_U,	-1);

	mach64_wait_vsync();
	mach64_wait_for_idle();
	mach64_fifo_wait(2);

	if(INREG(SCALER_BUF0_OFFSET_U)) 	supports_planar=1;
  }
  printf("[mach64] Planar YUV formats are %s supported\n",supports_planar?"":"not");
  supports_colour_adj=0;
  OUTREG(SCALER_COLOUR_CNTL,-1);
  if(INREG(SCALER_COLOUR_CNTL)) supports_colour_adj=1;
  supports_idct=0;
  OUTREG(IDCT_CONTROL,-1);
  if(INREG(IDCT_CONTROL)) supports_idct=1;
  OUTREG(IDCT_CONTROL,0);
  printf("[mach64] IDCT is %s supported\n",supports_idct?"":"not");
  supports_subpic=0;
  OUTREG(SUBPIC_CNTL,-1);
  if(INREG(SUBPIC_CNTL)) supports_subpic=1;
  OUTREG(SUBPIC_CNTL,0);
  printf("[mach64] subpictures are %s supported\n",supports_subpic?"":"not");
  if(   mach64_cap.device_id==DEVICE_ATI_RAGE_MOBILITY_P_M
     || mach64_cap.device_id==DEVICE_ATI_RAGE_MOBILITY_P_M2
     || mach64_cap.device_id==DEVICE_ATI_RAGE_MOBILITY_L
     || mach64_cap.device_id==DEVICE_ATI_RAGE_MOBILITY_L2)
         supports_lcd_v_stretch=1;
  else
         supports_lcd_v_stretch=0;
  
  reset_regs();
  mach64_vid_make_default();
  if(__verbose > VERBOSE_LEVEL) mach64_vid_dump_regs();
#ifdef MACH64_ENABLE_BM
  if(!(INREG(BUS_CNTL) & BUS_MASTER_DIS))
		OUTREG(BUS_CNTL,INREG(BUS_CNTL)|BUS_MSTR_RESET);
  if(bm_open() == 0)
  {
	mach64_cap.flags |= FLAG_DMA | FLAG_EQ_DMA;
	if((dma_phys_addrs = malloc(mach64_ram_size*sizeof(unsigned long)/4096)) == 0)
	{
	    out_mem:
	    printf("[mach64] Can't allocate temporary buffer for DMA\n");
	    mach64_cap.flags &= ~FLAG_DMA & ~FLAG_EQ_DMA;
	    return 0;
	}
	/*
	    WARNING: We MUST have continigous descriptors!!!
	    But: (720*720*2(YUV422)*16(sizeof(bm_descriptor)))/4096=4050
	    Thus one 4K page is far enough to describe max movie size.
	*/
	for(i=0;i<64;i++)
	    if((mach64_dma_desc_base[i] = memalign(4096,mach64_ram_size*sizeof(bm_list_descriptor)/4096)) == 0)
		goto out_mem;
#if 0
	if(!is_agp)
	{
	    long tst;
	    if(pci_config_read(pci_info.bus,pci_info.card,pci_info.func,4,4,&pci_command) == 0)
		pci_config_write(pci_info.bus,pci_info.card,pci_info.func,4,4,pci_command|0x14);
	    pci_config_read(pci_info.bus,pci_info.card,pci_info.func,4,4,&tst);
	}
#endif
  }
  else
    if(__verbose) printf("[mach64] Can't initialize busmastering: %s\n",strerror(errno));
#endif
  return 0;
}

void VIDIX_NAME(vixDestroy)(void)
{
#ifdef MACH64_ENABLE_BM
  unsigned i;
#endif
  restore_regs();
#ifdef MACH64_ENABLE_BM
  mach64_engine_reset();
#endif
  unmap_phys_mem(mach64_mem_base,mach64_ram_size);
  unmap_phys_mem(mach64_mmio_base,0x4000);
#ifdef MACH64_ENABLE_BM
  bm_close();
  if(can_use_irq && irq_installed) hwirq_uninstall(pci_info.bus,pci_info.card,pci_info.func);
  if(dma_phys_addrs) free(dma_phys_addrs);
  for(i=0;i<64;i++) 
  {
    if(mach64_dma_desc_base[i]) free(mach64_dma_desc_base[i]);
  }
#endif
}

int VIDIX_NAME(vixGetCapability)(vidix_capability_t *to)
{
    memcpy(to, &mach64_cap, sizeof(vidix_capability_t));
    return 0;
}

static unsigned mach64_query_pitch(unsigned fourcc,const vidix_yuv_t *spitch)
{
  unsigned pitch,spy,spv,spu;
  spy = spv = spu = 0;
  switch(spitch->y)
  {
    case 16:
    case 32:
    case 64:
    case 128:
    case 256: spy = spitch->y; break;
    default: break;
  }
  switch(spitch->u)
  {
    case 16:
    case 32:
    case 64:
    case 128:
    case 256: spu = spitch->u; break;
    default: break;
  }
  switch(spitch->v)
  {
    case 16:
    case 32:
    case 64:
    case 128:
    case 256: spv = spitch->v; break;
    default: break;
  }
  switch(fourcc)
  {
	/* 4:2:0 */
	case IMGFMT_IYUV:
	case IMGFMT_YV12:
	case IMGFMT_I420:
		if(spy > 16 && spu == spy/2 && spv == spy/2)	pitch = spy;
		else						pitch = 32;
		break;
	case IMGFMT_YVU9:
		if(spy > 32 && spu == spy/4 && spv == spy/4)	pitch = spy;
		else						pitch = 64;
		break;
	default:
		if(spy >= 16)	pitch = spy;
		else		pitch = 16;
		break;
  }
  return pitch;
}

static void mach64_compute_framesize(vidix_playback_t *info)
{
  unsigned pitch,awidth;
  pitch = mach64_query_pitch(info->fourcc,&info->src.pitch);
  switch(info->fourcc)
  {
    case IMGFMT_I420:
    case IMGFMT_YV12:
    case IMGFMT_IYUV:
		awidth = (info->src.w + (pitch-1)) & ~(pitch-1);
		info->frame_size = awidth*(info->src.h+info->src.h/2);
		break;
    case IMGFMT_YVU9:
		awidth = (info->src.w + (pitch-1)) & ~(pitch-1);
		info->frame_size = awidth*(info->src.h+info->src.h/8);
		break;
//    case IMGFMT_RGB32:
    case IMGFMT_BGR32:
		awidth = (info->src.w*4 + (pitch-1)) & ~(pitch-1);
		info->frame_size = (awidth*info->src.h);
		break;
    /* YUY2 YVYU, RGB15, RGB16 */
    default:	
		awidth = (info->src.w*2 + (pitch-1)) & ~(pitch-1);
		info->frame_size = (awidth*info->src.h);
		break;
  }
  info->frame_size+=256; // so we have some space for alignment & such
  info->frame_size&=~16;
}

static void mach64_vid_stop_video( void )
{
    mach64_fifo_wait(14);
    OUTREG(OVERLAY_SCALE_CNTL, 0x80000000);
    OUTREG(OVERLAY_EXCLUSIVE_HORZ, 0);
    OUTREG(OVERLAY_EXCLUSIVE_VERT, 0);
    OUTREG(SCALER_H_COEFF0, 0x00002000);
    OUTREG(SCALER_H_COEFF1, 0x0D06200D);
    OUTREG(SCALER_H_COEFF2, 0x0D0A1C0D);
    OUTREG(SCALER_H_COEFF3, 0x0C0E1A0C);
    OUTREG(SCALER_H_COEFF4, 0x0C14140C);
    OUTREG(VIDEO_FORMAT, 0xB000B);
    OUTREG(OVERLAY_TEST, 0x0);
}

static void mach64_vid_display_video( void )
{
    uint32_t vf,sc,width;
    mach64_fifo_wait(14);

    OUTREG(OVERLAY_Y_X_START,			besr.y_x_start);
    OUTREG(OVERLAY_Y_X_END,			besr.y_x_end);
    OUTREG(OVERLAY_SCALE_INC,			besr.scale_inc);
    OUTREG(SCALER_BUF_PITCH,			besr.vid_buf_pitch);
    OUTREG(SCALER_HEIGHT_WIDTH,			besr.height_width);
    OUTREG(SCALER_BUF0_OFFSET,			mach64_buffer_base[0][0]);
    OUTREG(SCALER_BUF0_OFFSET_U,		mach64_buffer_base[0][1]);
    OUTREG(SCALER_BUF0_OFFSET_V,		mach64_buffer_base[0][2]);
    OUTREG(SCALER_BUF1_OFFSET,			mach64_buffer_base[0][0]);
    OUTREG(SCALER_BUF1_OFFSET_U,		mach64_buffer_base[0][1]);
    OUTREG(SCALER_BUF1_OFFSET_V,		mach64_buffer_base[0][2]);
    mach64_wait_vsync();
    width = (besr.height_width >> 16 & 0x03FF);
    sc = 	SCALE_EN | OVERLAY_EN | 
		SCALE_BANDWIDTH | /* reset bandwidth status */
		SCALE_PIX_EXPAND | /* dynamic range correct */
		SCALE_Y2R_TEMP; /* use the equal temparature for every component of RGB */
    /* Force clocks of scaler. */
    if(width > 360 && !supports_planar && !mach64_is_interlace())
	     sc |= SCALE_CLK_FORCE_ON;
    /* Do we need that? And how we can improve the quality of 3dRageII scaler ?
       3dRageII+ (non pro) is really crapped HW :(
       ^^^^^^^^^^^^^^^^^^^
	!!SCALER_WIDTH <= 360 provides full scaling functionality !!!!!!!!!!!!!
	!!360 < SCALER_WIDTH <= 720 provides scaling with vertical replication (crap)
	!!SCALER_WIDTH > 720 is illegal. (no comments)
	
       As for me - I would prefer to limit movie's width with 360 but it provides only
       half of picture but with perfect quality. (NK) */
    mach64_fifo_wait(10);
    OUTREG(OVERLAY_SCALE_CNTL, sc);
    mach64_wait_for_idle();

    switch(besr.fourcc)
    {
	/* BGR formats */
	case IMGFMT_BGR15: vf = SCALER_IN_RGB15;  break;
	case IMGFMT_BGR16: vf = SCALER_IN_RGB16;  break;
	case IMGFMT_BGR32: vf = SCALER_IN_RGB32;  break;
        /* 4:2:0 */
	case IMGFMT_IYUV:
	case IMGFMT_I420:
	case IMGFMT_YV12:  vf = SCALER_IN_YUV12;  break;
	/* 4:1:0 */
	case IMGFMT_YVU9:  vf = SCALER_IN_YUV9;  break;
        /* 4:2:2 */
        case IMGFMT_YVYU:
	case IMGFMT_UYVY:  vf = SCALER_IN_YVYU422; break;
	case IMGFMT_YUY2:
	default:           vf = SCALER_IN_VYUY422; break;
    }
    OUTREG(VIDEO_FORMAT,vf);
    if(__verbose > VERBOSE_LEVEL) mach64_vid_dump_regs();
}

/* Goal of this function: hide RGB background and provide black screen around movie.
   Useful in '-vo fbdev:vidix -fs -zoom' mode.
   Reverse effect to colorkey */
static void mach64_vid_exclusive( void )
{
    unsigned screenw,screenh;
    screenw = mach64_get_xres();
    screenh = mach64_get_yres();
    OUTREG(OVERLAY_EXCLUSIVE_VERT,(((screenh-1)<<16)&EXCLUSIVE_VERT_END));
    OUTREG(OVERLAY_EXCLUSIVE_HORZ,(((screenw/8+1)<<8)&EXCLUSIVE_HORZ_END)|EXCLUSIVE_EN);
}

static void mach64_vid_non_exclusive( void )
{
    OUTREG(OVERLAY_EXCLUSIVE_HORZ,0);
}

static int mach64_vid_init_video( vidix_playback_t *config )
{
    uint32_t src_w,src_h,dest_w,dest_h,pitch,h_inc,v_inc,left,leftUV,top,ecp,y_pos;
    int is_420,best_pitch,mpitch;
    int src_offset_y, src_offset_u, src_offset_v;
    unsigned int i;

    mach64_vid_stop_video();
/* warning, if left or top are != 0 this will fail, as the framesize is too small then */
    left = config->src.x;
    top =  config->src.y;
    src_h = config->src.h;
    src_w = config->src.w;
    is_420 = 0;
    if(config->fourcc == IMGFMT_YV12 ||
       config->fourcc == IMGFMT_I420 ||
       config->fourcc == IMGFMT_IYUV) is_420 = 1;
    best_pitch = mach64_query_pitch(config->fourcc,&config->src.pitch);
    mpitch = best_pitch-1;
    switch(config->fourcc)
    {
	case IMGFMT_YVU9:
	/* 4:2:0 */
	case IMGFMT_IYUV:
	case IMGFMT_YV12:
	case IMGFMT_I420: pitch = (src_w + mpitch) & ~mpitch;
			  config->dest.pitch.y = 
			  config->dest.pitch.u = 
			  config->dest.pitch.v = best_pitch;
			  besr.vid_buf_pitch= pitch;
			  break;
	/* RGB 4:4:4:4 */
	case IMGFMT_RGB32:
	case IMGFMT_BGR32: pitch = (src_w*4 + mpitch) & ~mpitch;
			  config->dest.pitch.y = 
			  config->dest.pitch.u = 
			  config->dest.pitch.v = best_pitch;
			  besr.vid_buf_pitch= pitch>>2;
			  break;
	/* 4:2:2 */
        default: /* RGB15, RGB16, YVYU, UYVY, YUY2 */
			  pitch = ((src_w*2) + mpitch) & ~mpitch;
			  config->dest.pitch.y =
			  config->dest.pitch.u =
			  config->dest.pitch.v = best_pitch;
			  besr.vid_buf_pitch= pitch>>1;
			  break;
    }
    dest_w = config->dest.w;
    dest_h = config->dest.h;
    besr.fourcc = config->fourcc;
    ecp = (INPLL(PLL_VCLK_CNTL) & PLL_ECP_DIV) >> 4;
#if 0
{
int i;
for(i=0; i<32; i++){
    printf("%X ", INPLL(i));
}
}
#endif
    if(__verbose>0) printf("[mach64] ecp: %d\n", ecp);
    v_inc = src_h * mach64_get_vert_stretch();
    
    if(mach64_is_interlace()) v_inc<<=1;
    if(mach64_is_dbl_scan() ) v_inc>>=1;
    v_inc/= dest_h;
    v_inc>>=4; // convert 16.16 -> 4.12

    h_inc = (src_w << (12+ecp)) / dest_w;
    /* keep everything in 4.12 */
    config->offsets[0] = 0;
    for(i=1; i<config->num_frames; i++)
        config->offsets[i] = config->offsets[i-1] + config->frame_size;
    
	/*FIXME the left / top stuff is broken (= zoom a src rectangle from a larger one)
		1. the framesize isnt known as the outer src rectangle dimensions arent known
		2. the mach64 needs aligned addresses so it cant work anyway
		   -> so we could shift the outer buffer to compensate that but that would mean
		      alignment problems for the code which writes into it
	*/
    
    if(is_420)
    {
	config->offset.y= 0;
	config->offset.u= (pitch*src_h + 15)&~15; 
	config->offset.v= (config->offset.u + (pitch*src_h>>2) + 15)&~15;
	
	src_offset_y= config->offset.y + top*pitch + left;
	src_offset_u= config->offset.u + (top*pitch>>2) + (left>>1);
	src_offset_v= config->offset.v + (top*pitch>>2) + (left>>1);

	if(besr.fourcc == IMGFMT_I420 || besr.fourcc == IMGFMT_IYUV)
	{
	  uint32_t tmp;
	  tmp = config->offset.u;
	  config->offset.u = config->offset.v;
	  config->offset.v = tmp;
	  src_offset_u=config->offset.u;
	  src_offset_v=config->offset.v;
	}
    }
    else if(besr.fourcc == IMGFMT_YVU9)
    {
	config->offset.y= 0;
	config->offset.u= (pitch*src_h + 15)&~15; 
	config->offset.v= (config->offset.u + (pitch*src_h>>4) + 15)&~15;
	
	src_offset_y= config->offset.y + top*pitch + left;
	src_offset_u= config->offset.u + (top*pitch>>4) + (left>>1);
	src_offset_v= config->offset.v + (top*pitch>>4) + (left>>1);
    }
    else if(besr.fourcc == IMGFMT_BGR32)
    {
      config->offset.y = config->offset.u = config->offset.v = 0;
      src_offset_y= src_offset_u= src_offset_v= top*pitch + (left << 2);
    }
    else
    {
      config->offset.y = config->offset.u = config->offset.v = 0;
      src_offset_y= src_offset_u= src_offset_v= top*pitch + (left << 1);
    }

    num_mach64_buffers= config->num_frames;
    for(i=0; i<config->num_frames; i++)
    {
	mach64_buffer_base[i][0]= (mach64_overlay_offset + config->offsets[i] + src_offset_y)&~15;
	mach64_buffer_base[i][1]= (mach64_overlay_offset + config->offsets[i] + src_offset_u)&~15;
	mach64_buffer_base[i][2]= (mach64_overlay_offset + config->offsets[i] + src_offset_v)&~15;
    }

    leftUV = (left >> 17) & 15;
    left = (left >> 16) & 15;
    besr.scale_inc = ( h_inc << 16 ) | v_inc;
    y_pos = config->dest.y;
    if(mach64_is_dbl_scan()) y_pos*=2;
    else
    if(mach64_is_interlace()) y_pos/=2;
    besr.y_x_start = y_pos | (config->dest.x << 16);
    y_pos =config->dest.y + dest_h;
    if(mach64_is_dbl_scan()) y_pos*=2;
    else
    if(mach64_is_interlace()) y_pos/=2;
    besr.y_x_end = y_pos | ((config->dest.x + dest_w) << 16);
    besr.height_width = ((src_w - left)<<16) | (src_h - top);
    return 0;
}

static int is_supported_fourcc(uint32_t fourcc)
{
    switch(fourcc)
    {
    case IMGFMT_YV12:
    case IMGFMT_I420:
    case IMGFMT_YVU9:
    case IMGFMT_IYUV:
	return supports_planar;
    case IMGFMT_YUY2:
    case IMGFMT_UYVY:
    case IMGFMT_BGR15:
    case IMGFMT_BGR16:
    case IMGFMT_BGR32:
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

int VIDIX_NAME(vixConfigPlayback)(vidix_playback_t *info)
{
  unsigned rgb_size,nfr;
  uint32_t mach64_video_size;
  if(!is_supported_fourcc(info->fourcc)) return ENOSYS;
  if(info->src.h > 720 || info->src.w > 720)
  {
    printf("[mach64] Can't apply width or height > 720\n");
    return EINVAL;
  }
  if(info->num_frames>VID_PLAY_MAXFRAMES) info->num_frames=VID_PLAY_MAXFRAMES;

  mach64_compute_framesize(info);
  rgb_size = mach64_get_xres()*mach64_get_yres()*((mach64_vid_get_dbpp()+7)/8);
  nfr = info->num_frames;
  mach64_video_size = mach64_ram_size;
  for(;nfr>0;nfr--)
  {
      mach64_overlay_offset = mach64_video_size - info->frame_size*nfr;
      mach64_overlay_offset &= 0xffff0000;
      if(mach64_overlay_offset >= (int)rgb_size ) break;
  }
  if(nfr <= 3)
  {
   nfr = info->num_frames;
   for(;nfr>0;nfr--)
   {
      mach64_overlay_offset = mach64_video_size - info->frame_size*nfr;
      mach64_overlay_offset &= 0xffff0000;
      if(mach64_overlay_offset>=0) break;
   }
  }
  if(nfr <= 0) return EINVAL;
  info->num_frames=nfr;
  num_mach64_buffers = info->num_frames;
  info->dga_addr = (char *)mach64_mem_base + mach64_overlay_offset;
  mach64_vid_init_video(info);
  return 0;
}

int VIDIX_NAME(vixPlaybackOn)(void)
{
  int err;
  unsigned dw,dh;
  dw = (besr.y_x_end >> 16) - (besr.y_x_start >> 16);
  dh = (besr.y_x_end & 0xFFFF) - (besr.y_x_start & 0xFFFF);
  if(dw == mach64_get_xres() || dh == mach64_get_yres()) mach64_vid_exclusive();
  else mach64_vid_non_exclusive();
  mach64_vid_display_video();
  err = INREG(SCALER_BUF_PITCH) == besr.vid_buf_pitch ? 0 : EINTR;
  if(err)
  {
    printf("[mach64] *** Internal fatal error ***: Detected pitch corruption\n"
	   "[mach64] Try decrease number of buffers\n");
  }
  return err;
}

int VIDIX_NAME(vixPlaybackOff)(void)
{
  mach64_vid_stop_video();
  return 0;
}

int VIDIX_NAME(vixPlaybackFrameSelect)(unsigned int frame)
{
    uint32_t off[6];
    int i;
    int last_frame= (frame-1+num_mach64_buffers) % num_mach64_buffers;
    /*
    buf3-5 always should point onto second buffer for better
    deinterlacing and TV-in
    */
    if(num_mach64_buffers==1) return 0;
    for(i=0; i<3; i++)
    {
    	off[i]  = mach64_buffer_base[frame][i];
    	off[i+3]= mach64_buffer_base[last_frame][i];
    }
    if(__verbose > VERBOSE_LEVEL) printf("mach64_vid: flip_page = %u\n",frame);

#if 0 // delay routine so the individual frames can be ssen better
{
volatile int i=0;
for(i=0; i<10000000; i++);
}
#endif

    mach64_wait_for_idle();
    mach64_fifo_wait(7);

    OUTREG(SCALER_BUF0_OFFSET,		off[0]);
    OUTREG(SCALER_BUF0_OFFSET_U,	off[1]);
    OUTREG(SCALER_BUF0_OFFSET_V,	off[2]);
    OUTREG(SCALER_BUF1_OFFSET,		off[3]);
    OUTREG(SCALER_BUF1_OFFSET_U,	off[4]);
    OUTREG(SCALER_BUF1_OFFSET_V,	off[5]);
    if(num_mach64_buffers==2) mach64_wait_vsync(); //only wait for vsync if we do double buffering
       
    if(__verbose > VERBOSE_LEVEL) mach64_vid_dump_regs();
    return 0;
}

vidix_video_eq_t equal =
{
 VEQ_CAP_BRIGHTNESS | VEQ_CAP_SATURATION
 ,
 0, 0, 0, 0, 0, 0, 0, 0 };

int 	VIDIX_NAME(vixPlaybackGetEq)( vidix_video_eq_t * eq)
{
  memcpy(eq,&equal,sizeof(vidix_video_eq_t));
  if(!supports_colour_adj) eq->cap = VEQ_CAP_BRIGHTNESS;
  return 0;
}

int 	VIDIX_NAME(vixPlaybackSetEq)( const vidix_video_eq_t * eq)
{
  int br,sat;
    if(eq->cap & VEQ_CAP_BRIGHTNESS) equal.brightness = eq->brightness;
    if(eq->cap & VEQ_CAP_CONTRAST)   equal.contrast   = eq->contrast;
    if(eq->cap & VEQ_CAP_SATURATION) equal.saturation = eq->saturation;
    if(eq->cap & VEQ_CAP_HUE)        equal.hue        = eq->hue;
    if(eq->cap & VEQ_CAP_RGB_INTENSITY)
    {
      equal.red_intensity   = eq->red_intensity;
      equal.green_intensity = eq->green_intensity;
      equal.blue_intensity  = eq->blue_intensity;
    }
    if(supports_colour_adj)
    {
	equal.flags = eq->flags;
	br = equal.brightness * 64 / 1000;
	if(br < -64) br = -64; if(br > 63) br = 63;
	sat = (equal.saturation + 1000) * 16 / 1000;
	if(sat < 0) sat = 0; if(sat > 31) sat = 31;
	OUTREG(SCALER_COLOUR_CNTL, (br & 0x7f) | (sat << 8) | (sat << 16));
    }
    else
    {
	unsigned gamma;
	br = equal.brightness * 3 / 1000;
	if(br < 0) br = 0;
	switch(br)
	{
	    default:gamma = SCALE_GAMMA_SEL_BRIGHT; break;
	    case 1: gamma = SCALE_GAMMA_SEL_G14; break;
	    case 2: gamma = SCALE_GAMMA_SEL_G18; break;
	    case 3: gamma = SCALE_GAMMA_SEL_G22; break;
	}
	OUTREG(OVERLAY_SCALE_CNTL,(INREG(OVERLAY_SCALE_CNTL) & ~SCALE_GAMMA_SEL_MSK) | gamma);
    }
  return 0;
}

int VIDIX_NAME(vixGetGrKeys)(vidix_grkey_t *grkey)
{
    memcpy(grkey, &mach64_grkey, sizeof(vidix_grkey_t));
    return(0);
}

int VIDIX_NAME(vixSetGrKeys)(const vidix_grkey_t *grkey)
{
    memcpy(&mach64_grkey, grkey, sizeof(vidix_grkey_t));

    if(mach64_grkey.ckey.op == CKEY_TRUE)
    {
	besr.ckey_on=1;

	switch(mach64_vid_get_dbpp())
	{
	case 15:
		besr.graphics_key_msk=0x7FFF;
		besr.graphics_key_clr=
			  ((mach64_grkey.ckey.blue &0xF8)>>3)
			| ((mach64_grkey.ckey.green&0xF8)<<2)
			| ((mach64_grkey.ckey.red  &0xF8)<<7);
		break;
	case 16:
		besr.graphics_key_msk=0xFFFF;
		besr.graphics_key_clr=
			  ((mach64_grkey.ckey.blue &0xF8)>>3)
			| ((mach64_grkey.ckey.green&0xFC)<<3)
			| ((mach64_grkey.ckey.red  &0xF8)<<8);
		break;
	case 24:
		besr.graphics_key_msk=0xFFFFFF;
		besr.graphics_key_clr=
			  ((mach64_grkey.ckey.blue &0xFF))
			| ((mach64_grkey.ckey.green&0xFF)<<8)
			| ((mach64_grkey.ckey.red  &0xFF)<<16);
		break;
	case 32:
		besr.graphics_key_msk=0xFFFFFF;
		besr.graphics_key_clr=
			  ((mach64_grkey.ckey.blue &0xFF))
			| ((mach64_grkey.ckey.green&0xFF)<<8)
			| ((mach64_grkey.ckey.red  &0xFF)<<16);
		break;
	default:
		besr.ckey_on=0;
		besr.graphics_key_msk=0;
		besr.graphics_key_clr=0;
	}
    }
    else
    {
	besr.ckey_on=0;
	besr.graphics_key_msk=0;
	besr.graphics_key_clr=0;
    }

    mach64_fifo_wait(4);
    OUTREG(OVERLAY_GRAPHICS_KEY_MSK, besr.graphics_key_msk);
    OUTREG(OVERLAY_GRAPHICS_KEY_CLR, besr.graphics_key_clr);
//    OUTREG(OVERLAY_VIDEO_KEY_MSK, 0);
//    OUTREG(OVERLAY_VIDEO_KEY_CLR, 0);
    if(besr.ckey_on)
    	OUTREG(OVERLAY_KEY_CNTL,VIDEO_KEY_FN_TRUE|GRAPHIC_KEY_FN_EQ|CMP_MIX_AND);
    else
    	OUTREG(OVERLAY_KEY_CNTL,VIDEO_KEY_FN_TRUE|GRAPHIC_KEY_FN_TRUE|CMP_MIX_AND);

    return(0);
}

#ifdef MACH64_ENABLE_BM
static int mach64_setup_frame( vidix_dma_t * dmai )
{
    if(mach64_overlay_offset + dmai->dest_offset + dmai->size > mach64_ram_size) return E2BIG;
    if(dmai->idx > VID_PLAY_MAXFRAMES-1) dmai->idx=0;
    if(!(dmai->internal[dmai->idx] && (dmai->flags & BM_DMA_FIXED_BUFFS)))
    {
	bm_list_descriptor * list = (bm_list_descriptor *)mach64_dma_desc_base[dmai->idx];
	unsigned long dest_ptr;
	unsigned i,n,count;
	int retval;
	n = dmai->size / 4096;
	if(dmai->size % 4096) n++;
	if((retval = VIRT_TO_CARD(dmai->src,dmai->size,dma_phys_addrs)) != 0) return retval;
	dmai->internal[dmai->idx] = mach64_dma_desc_base[dmai->idx];
	dest_ptr = dmai->dest_offset;
	count = dmai->size;
#if 0
printf("MACH64_DMA_REQUEST va=%X size=%X\n",dmai->src,dmai->size);
#endif
	for(i=0;i<n;i++)
	{
	    list[i].framebuf_offset = mach64_overlay_offset + dest_ptr; /* offset within of video memory */
	    list[i].sys_addr = dma_phys_addrs[i];
	    list[i].command = (count > 4096 ? 4096 : (count | DMA_GUI_COMMAND__EOL));
	    list[i].reserved = 0;
#if 0
printf("MACH64_DMA_TABLE[%i] fboff=%X pa=%X cmd=%X rsrvd=%X\n",i,list[i].framebuf_offset,list[i].sys_addr,list[i].command,list[i].reserved);
#endif
	    dest_ptr += 4096;
	    count -= 4096;
	}
	cpu_flush(list,4096);
    }
    return 0;
}

static int mach64_transfer_frame( unsigned long ba_dma_desc,int sync_mode )
{
    uint32_t crtc_int;
    mach64_wait_for_idle();
    mach64_fifo_wait(4);
    OUTREG(BUS_CNTL,(INREG(BUS_CNTL)|BUS_EXT_REG_EN)&(~BUS_MASTER_DIS));
    crtc_int = INREG(CRTC_INT_CNTL);
    if(sync_mode && can_use_irq) OUTREG(CRTC_INT_CNTL,crtc_int|CRTC_BUSMASTER_EOL_INT|CRTC_BUSMASTER_EOL_INT_EN);
    else			 OUTREG(CRTC_INT_CNTL,crtc_int|CRTC_BUSMASTER_EOL_INT);
    OUTREG(BM_SYSTEM_TABLE,ba_dma_desc|SYSTEM_TRIGGER_SYSTEM_TO_VIDEO);
    if(__verbose > VERBOSE_LEVEL) mach64_vid_dump_regs();    
#if 0
    mach64_fifo_wait(4);
    mach64_fifo_wait(16);
    printf("MACH64_DMA_DBG: bm_fb_off=%08X bm_sysmem_addr=%08X bm_cmd=%08X bm_status=%08X bm_agp_base=%08X bm_agp_cntl=%08X\n",
	    INREG(BM_FRAME_BUF_OFFSET),
	    INREG(BM_SYSTEM_MEM_ADDR),
	    INREG(BM_COMMAND),
	    INREG(BM_STATUS),
	    INREG(AGP_BASE),
	    INREG(AGP_CNTL));
#endif
    return 0;
}

int VIDIX_NAME(vixQueryDMAStatus)( void )
{
    int bm_off;
    unsigned crtc_int_cntl;
    mach64_wait_for_idle();
    mach64_fifo_wait(2);
    crtc_int_cntl = INREG(CRTC_INT_CNTL);
    bm_off = crtc_int_cntl & CRTC_BUSMASTER_EOL_INT;
//    if(bm_off) OUTREG(CRTC_INT_CNTL,crtc_int_cntl | CRTC_BUSMASTER_EOL_INT);
    return bm_off?0:1;
}

int VIDIX_NAME(vixPlaybackCopyFrame)( vidix_dma_t * dmai )
{
    int retval,sync_mode;
    if(!(dmai->flags & BM_DMA_FIXED_BUFFS)) if(bm_lock_mem(dmai->src,dmai->size) != 0) return errno;
    sync_mode = (dmai->flags & BM_DMA_SYNC) == BM_DMA_SYNC;
    if(sync_mode)
    {
	if(!irq_installed) init_irq();
	/* burn CPU instead of PCI bus here */
	while(vixQueryDMAStatus()!=0){
	    if(can_use_irq)	hwirq_wait(pci_info.irq);
	    else		usleep(0); /* ugly but may help */
	}
    }
    mach64_engine_reset();
    retval = mach64_setup_frame(dmai);
    VIRT_TO_CARD(mach64_dma_desc_base[dmai->idx],1,&bus_addr_dma_desc);
    if(retval == 0) retval = mach64_transfer_frame(bus_addr_dma_desc,sync_mode);
    if(!(dmai->flags & BM_DMA_FIXED_BUFFS)) bm_unlock_mem(dmai->src,dmai->size);
    return retval;
}
#endif
