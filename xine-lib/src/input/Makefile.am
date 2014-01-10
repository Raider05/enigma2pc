include $(top_srcdir)/misc/Makefile.quiet
include $(top_builddir)/misc/Makefile.plugins
include $(top_srcdir)/misc/Makefile.common

# REVISIT: This second line here bothers me more than just a little bit
AM_CFLAGS  = $(DEFAULT_OCFLAGS) $(VISIBILITY_FLAG)
AM_LDFLAGS = $(xineplug_ldflags)

##
# IMPORTANT:
# ---------
# All of xine input plugins should be named like the scheme "xineplug_inp_"
#

SUBDIRS = libreal librtsp
if ENABLE_VCD
SUBDIRS += vcd
endif
if !WITH_EXTERNAL_DVDNAV
SUBDIRS += libdvdnav
endif


noinst_HEADERS = net_buf_ctrl.h mms.h mmsh.h pnm.h media_helper.h http_helper.h


if ENABLE_DVB
in_dvb = xineplug_inp_dvb.la
endif

if ENABLE_VCDO
in_vcd = xineplug_inp_vcdo.la
endif

if ENABLE_V4L
in_v4l = xineplug_inp_v4l.la
endif

if ENABLE_V4L2
in_v4l2 = xineplug_inp_v4l2.la
in_pvr = xineplug_inp_pvr.la
endif

if ENABLE_GNOME_VFS
in_gnome_vfs = xineplug_inp_gnome_vfs.la
endif

if ENABLE_LIBSMBCLIENT
in_smb = xineplug_inp_smb.la
endif

if WITH_EXTERNAL_DVDNAV
DVD_CFLAGS  = $(DVDNAV_CFLAGS)
link_dvdnav = $(DVDNAV_LIBS)
else
DVD_CFLAGS  = -I$(top_srcdir)/src/input/libdvdnav
link_dvdnav = libdvdnav/libdvdnav.la
endif

# not ported to native Windows
if !WIN32
in_rtp = xineplug_inp_rtp.la
endif

if ENABLE_BLURAY
in_bluray = xineplug_inp_bluray.la
endif

xineplug_LTLIBRARIES = \
	xineplug_inp_file.la \
	xineplug_inp_test.la \
	xineplug_inp_http.la \
	xineplug_inp_dvd.la \
	$(in_vcd) \
	$(in_v4l) \
	$(in_v4l2) \
	$(in_gnome_vfs) \
	$(in_smb) \
	xineplug_inp_mms.la \
	xineplug_inp_stdin_fifo.la \
	xineplug_inp_pnm.la \
	$(in_rtp) \
	xineplug_inp_rtsp.la \
	xineplug_inp_net.la \
	$(in_pvr) \
	$(in_dvb) \
	$(in_bluray) \
	xineplug_inp_cdda.la \
	xineplug_inp_enigma.la


xineplug_inp_file_la_SOURCES = input_file.c
xineplug_inp_file_la_LIBADD = $(XINE_LIB) $(LTLIBINTL)

xineplug_inp_test_la_SOURCES = input_test.c
xineplug_inp_test_la_LIBADD = $(XINE_LIB) $(LTLIBINTL)
xineplug_inp_test_la_CFLAGS = $(VISIBILITY_FLAG) $(AM_CFLAGS)
xineplug_inp_test_la_LDFLAGS = $(xineplug_ldflags)

xineplug_inp_dvd_la_SOURCES = input_dvd.c media_helper.c
xineplug_inp_dvd_la_LIBADD = $(XINE_LIB) $(link_dvdnav) $(PTHREAD_LIBS) $(DYNAMIC_LD_LIBS)
xineplug_inp_dvd_la_CFLAGS = $(AM_CFLAGS) $(DVD_CFLAGS)

xineplug_inp_net_la_SOURCES = input_net.c net_buf_ctrl.c
xineplug_inp_net_la_LIBADD = $(XINE_LIB) $(NET_LIBS) $(PTHREAD_LIBS) $(LTLIBINTL)

# note: compiling ../demuxers/asfheader.c here is safe given same compiler options
xineplug_inp_mms_la_SOURCES = input_mms.c net_buf_ctrl.c mms.c mmsh.c http_helper.c ../demuxers/asfheader.c
xineplug_inp_mms_la_LIBADD = $(XINE_LIB) $(LTLIBICONV) $(PTHREAD_LIBS) $(LTLIBINTL)

xineplug_inp_vcdo_la_SOURCES = input_vcd.c media_helper.c
xineplug_inp_vcdo_la_LIBADD = $(XINE_LIB) $(LTLIBINTL)

xineplug_inp_stdin_fifo_la_SOURCES = input_stdin_fifo.c net_buf_ctrl.c
xineplug_inp_stdin_fifo_la_LIBADD = $(XINE_LIB) $(PTHREAD_LIBS) $(LTLIBINTL)

xineplug_inp_rtp_la_SOURCES = input_rtp.c net_buf_ctrl.c
xineplug_inp_rtp_la_CFLAGS = $(AM_CFLAGS) -fno-strict-aliasing
xineplug_inp_rtp_la_LIBADD = $(XINE_LIB) $(NET_LIBS) $(PTHREAD_LIBS) $(LTLIBINTL)

xineplug_inp_http_la_SOURCES = input_http.c net_buf_ctrl.c http_helper.c
xineplug_inp_http_la_LIBADD = $(XINE_LIB) $(NET_LIBS) $(PTHREAD_LIBS) $(LTLIBINTL) $(AVUTIL_LIBS)
xineplug_inp_http_la_CPPFLAGS = $(AM_CPPFLAGS) $(AVUTIL_CFLAGS)

xineplug_inp_pnm_la_SOURCES = input_pnm.c net_buf_ctrl.c pnm.c
xineplug_inp_pnm_la_LIBADD = $(XINE_LIB) $(PTHREAD_LIBS) $(LTLIBINTL)

xineplug_inp_dvb_la_SOURCES = input_dvb.c net_buf_ctrl.c
xineplug_inp_dvb_la_DEPS = $(XDG_BASEDIR_DEPS)
xineplug_inp_dvb_la_LIBADD = $(XINE_LIB) $(PTHREAD_LIBS) $(LTLIBINTL) $(XDG_BASEDIR_LIBS) $(AVUTIL_LIBS)
xineplug_inp_dvb_la_CPPFLAGS = $(AM_CPPFLAGS) $(XDG_BASEDIR_CPPFLAGS) $(AVUTIL_CFLAGS)

xineplug_inp_rtsp_la_SOURCES = input_rtsp.c net_buf_ctrl.c
xineplug_inp_rtsp_la_LIBADD = $(XINE_LIB) $(PTHREAD_LIBS) $(LTLIBINTL) libreal/libreal.la librtsp/librtsp.la

xineplug_inp_cdda_la_SOURCES = input_cdda.c media_helper.c
xineplug_inp_cdda_la_DEPS = $(XDG_BASEDIR_DEPS)
xineplug_inp_cdda_la_LIBADD = $(XINE_LIB) $(LTLIBINTL) $(XDG_BASEDIR_LIBS) $(AVUTIL_LIBS)
xineplug_inp_cdda_la_CPPFLAGS = $(AM_CPPFLAGS) $(XDG_BASEDIR_CPPFLAGS) $(AVUTIL_CFLAGS)

xineplug_inp_v4l_la_SOURCES = input_v4l.c
xineplug_inp_v4l_la_LIBADD = $(XINE_LIB) $(ALSA_LIBS) $(LTLIBINTL)
xineplug_inp_v4l_la_CFLAGS = $(AM_CFLAGS) $(ALSA_CFLAGS)

xineplug_inp_v4l2_la_SOURCES = input_v4l2.c
xineplug_inp_v4l2_la_LIBADD = $(XINE_LIB) $(ALSA_LIBS) $(LTLIBINTL) $(V4L2_LIBS)
xineplug_inp_v4l2_la_CFLAGS = $(AM_CFLAGS) $(ALSA_CFLAGS) $(V4L2_CFLAGS)
xineplug_inp_v4l2_la_LDFLAGS = -avoid-version -module

xineplug_inp_gnome_vfs_la_SOURCES = input_gnome_vfs.c net_buf_ctrl.c
xineplug_inp_gnome_vfs_la_LIBADD = $(XINE_LIB) $(GNOME_VFS_LIBS) $(PTHREAD_LIBS) $(LTLIBINTL)
xineplug_inp_gnome_vfs_la_CFLAGS = $(AM_CFLAGS) $(GNOME_VFS_CFLAGS)

xineplug_inp_smb_la_SOURCES = input_smb.c
xineplug_inp_smb_la_LIBADD = $(XINE_LIB) $(LIBSMBCLIENT_LIBS) $(LTLIBINTL)
xineplug_inp_smb_la_CFLAGS = $(AM_CFLAGS) $(LIBSMBCLIENT_CFLAGS)

xineplug_inp_pvr_la_SOURCES = input_pvr.c
xineplug_inp_pvr_la_LIBADD = $(XINE_LIB) $(PTHREAD_LIBS) $(LTLIBINTL)

xineplug_inp_bluray_la_SOURCES = input_bluray.c media_helper.c
xineplug_inp_bluray_la_LIBADD = $(XINE_LIB) $(LIBBLURAY_LIBS) $(PTHREAD_LIBS) $(LTLIBINTL)
xineplug_inp_bluray_la_CFLAGS = $(AM_CFLAGS) $(LIBBLURAY_CFLAGS)

xineplug_inp_enigma_la_SOURCES = combined_enigma.c combined_enigma.h input_enigma.c net_buf_ctrl.c post_enigma_video.c
xineplug_vdr_la_CFLAGS = $(AM_CFLAGS) -fno-strict-aliasing
xineplug_inp_enigma_la_LIBADD = $(XINE_LIB) $(PTHREAD_LIBS) $(LTLIBINTL)
