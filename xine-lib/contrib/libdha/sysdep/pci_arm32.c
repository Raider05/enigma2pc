/*
   This file is based on:
   $XFree86: xc/programs/Xserver/hw/xfree86/etc/scanpci.c,v 3.34.2.17 1998/11/10 11:55:40 dawes Exp $
   Modified for readability by Nick Kurshev
*/

static int pci_config_type( void )
{
  unsigned long tmplong1, tmplong2;
  unsigned char tmp1, tmp2;
  int retval;
    retval = 0;
 
    OUTPORT8(PCI_MODE2_ENABLE_REG, 0x00);
    OUTPORT8(PCI_MODE2_FORWARD_REG, 0x00);
    tmp1 = INPORT8(PCI_MODE2_ENABLE_REG);
    tmp2 = INPORT8(PCI_MODE2_FORWARD_REG);
    if ((tmp1 == 0x00) && (tmp2 == 0x00)) {
	retval = 2;
        /*printf("PCI says configuration type 2\n");*/
    } else {
        tmplong1 = INPORT32(PCI_MODE1_ADDRESS_REG);
        OUTPORT32(PCI_MODE1_ADDRESS_REG, PCI_EN);
        tmplong2 = INPORT32(PCI_MODE1_ADDRESS_REG);
        OUTPORT32(PCI_MODE1_ADDRESS_REG, tmplong1);
        if (tmplong2 == PCI_EN) {
	    retval = 1;
            /*printf("PCI says configuration type 1\n");*/
	} else {
            /*printf("No PCI !\n");*/
	    disable_app_io();
	    /*exit(1);*/
	    retval = 0xFFFF;
	}
    }
  return retval;
}

static int pci_get_vendor(
          unsigned char bus,
          unsigned char dev,
          int func)
{
    unsigned long config_cmd;
    config_cmd = PCI_EN | (bus<<16) | (dev<<11) | (func<<8);
    OUTPORT32(PCI_MODE1_ADDRESS_REG, config_cmd);
    return INPORT32(PCI_MODE1_DATA_REG);
}

static long pci_config_read_long(
          unsigned char bus,
          unsigned char dev,
          int func, 
          unsigned cmd)
{
    unsigned long config_cmd;
    config_cmd = PCI_EN | (bus<<16) | (dev<<11) | (func<<8);
    OUTPORT32(PCI_MODE1_ADDRESS_REG, config_cmd | cmd);
    return INPORT32(PCI_MODE1_DATA_REG);
}

static long pci_config_read_word(
          unsigned char bus,
          unsigned char dev,
          int func, 
          unsigned cmd)
{
    unsigned long config_cmd;
    config_cmd = PCI_EN | (bus<<16) | (dev<<11) | (func<<8);
    OUTPORT32(PCI_MODE1_ADDRESS_REG, config_cmd | cmd);
    return INPORT16(PCI_MODE1_DATA_REG);
}

static long pci_config_read_byte(
          unsigned char bus,
          unsigned char dev,
          int func, 
          unsigned cmd)
{
    unsigned long config_cmd;
    config_cmd = PCI_EN | (bus<<16) | (dev<<11) | (func<<8);
    OUTPORT32(PCI_MODE1_ADDRESS_REG, config_cmd | cmd);
    return INPORT8(PCI_MODE1_DATA_REG);
}

static void pci_config_write_long(
          unsigned char bus,
          unsigned char dev,
          int func, 
          unsigned cmd,
	  long val)
{
    unsigned long config_cmd;
    config_cmd = PCI_EN | (bus<<16) | (dev<<11) | (func<<8);
    OUTPORT32(PCI_MODE1_ADDRESS_REG, config_cmd | cmd);
    OUTPORT32(PCI_MODE1_DATA_REG,val);
}

static void pci_config_write_word(
          unsigned char bus,
          unsigned char dev,
          int func, 
          unsigned cmd,
	  unsigned val)
{
    unsigned long config_cmd;
    config_cmd = PCI_EN | (bus<<16) | (dev<<11) | (func<<8);
    OUTPORT32(PCI_MODE1_ADDRESS_REG, config_cmd | cmd);
    OUTPORT16(PCI_MODE1_DATA_REG,val);
}

static void pci_config_write_byte(
          unsigned char bus,
          unsigned char dev,
          int func, 
          unsigned cmd,
	  long val)
{
    unsigned long config_cmd;
    config_cmd = PCI_EN | (bus<<16) | (dev<<11) | (func<<8);
    OUTPORT32(PCI_MODE1_ADDRESS_REG, config_cmd | cmd);
    OUTPORT8(PCI_MODE1_DATA_REG,val);
}
