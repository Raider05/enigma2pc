include $(top_srcdir)/misc/Makefile.quiet
include $(top_builddir)/misc/Makefile.plugins
include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS  = $(DEFAULT_OCFLAGS) $(VISIBILITY_FLAG)
AM_LDFLAGS = $(xineplug_ldflags)

##
# IMPORTANT:
# ---------
# all xine audio out plugins should be named like the
# scheme "xineplug_ao_out_"
#

EXTRA_DIST = audio_irixal_out.c

if ENABLE_OSS
oss_module = xineplug_ao_out_oss.la
endif

if ENABLE_ALSA
alsa_module = xineplug_ao_out_alsa.la
endif

if ENABLE_ESD
esd_module = xineplug_ao_out_esd.la
endif

if ENABLE_SUNAUDIO
sun_module = xineplug_ao_out_sun.la
endif

#if ENABLE_IRIXAL
#irixal_module = xineplug_ao_out_irixal.la
#endif

if ENABLE_DIRECTX
directx_module = xineplug_ao_out_directx.la
directx2_module = xineplug_ao_out_directx2.la
endif

if ENABLE_COREAUDIO
coreaudio_module = xineplug_ao_out_coreaudio.la
endif

if ENABLE_PULSEAUDIO
pulseaudio_module = xineplug_ao_out_pulseaudio.la
endif

if ENABLE_FUSIONSOUND
fusionsound_module = xineplug_ao_out_fusionsound.la
endif

if ENABLE_JACK
jack_module = xineplug_ao_out_jack.la
endif

if ENABLE_SNDIO
sndio_module = xineplug_ao_out_sndio.la
endif

xineplug_LTLIBRARIES =		\
	xineplug_ao_out_none.la	\
	xineplug_ao_out_file.la	\
	$(oss_module)		\
	$(alsa_module)		\
	$(sun_module)		\
	$(esd_module)		\
	$(directx_module)	\
	$(coreaudio_module)	\
	$(pulseaudio_module)	\
	$(directx2_module)	\
	$(fusionsound_module)	\
	$(jack_module)		\
	$(sndio_module)

xineplug_ao_out_none_la_SOURCES = audio_none_out.c
xineplug_ao_out_none_la_LIBADD = $(XINE_LIB) $(LTLIBINTL)

xineplug_ao_out_file_la_SOURCES = audio_file_out.c
xineplug_ao_out_file_la_LIBADD = $(XINE_LIB) $(LTLIBINTL)

xineplug_ao_out_oss_la_SOURCES = audio_oss_out.c
xineplug_ao_out_oss_la_LIBADD = $(XINE_LIB) $(LTLIBINTL)

xineplug_ao_out_alsa_la_SOURCES = audio_alsa_out.c
xineplug_ao_out_alsa_la_LIBADD = $(XINE_LIB) $(ALSA_LIBS) $(PTHREAD_LIBS) $(LTLIBINTL)
xineplug_ao_out_alsa_la_CFLAGS = $(AM_CFLAGS) $(ALSA_CFLAGS)

xineplug_ao_out_esd_la_SOURCES = audio_esd_out.c
xineplug_ao_out_esd_la_LIBADD = $(XINE_LIB) $(ESD_LIBS) $(LTLIBINTL)
xineplug_ao_out_esd_la_CFLAGS = $(AM_CFLAGS) $(ESD_CFLAGS)

xineplug_ao_out_sun_la_SOURCES = audio_sun_out.c
xineplug_ao_out_sun_la_LIBADD = $(XINE_LIB)

#xineplug_ao_out_irixal_la_SOURCES = audio_irixal_out.c
#xineplug_ao_out_irixal_la_LIBADD = $(IRIXAL_LIBS)
#xineplug_ao_out_irixal_la_CFLAGS = $(AM_CFLAGS) $(IRIXAL_CFLAGS)

xineplug_ao_out_directx_la_SOURCES = audio_directx_out.c
xineplug_ao_out_directx_la_LIBADD = $(XINE_LIB) $(DIRECTX_AUDIO_LIBS) $(LTLIBINTL)
xineplug_ao_out_directx_la_CPPFLAGS = $(AM_CPPFLAGS) $(DIRECTX_CPPFLAGS)

xineplug_ao_out_coreaudio_la_SOURCES = audio_coreaudio_out.c
xineplug_ao_out_coreaudio_la_LIBADD = $(XINE_LIB)
xineplug_ao_out_coreaudio_la_LDFLAGS = $(AM_LDFLAGS) -framework CoreAudio -framework AudioUnit

xineplug_ao_out_pulseaudio_la_SOURCES = audio_pulse_out.c
xineplug_ao_out_pulseaudio_la_LIBADD = $(XINE_LIB) $(PULSEAUDIO_LIBS) $(LTLIBINTL)
xineplug_ao_out_pulseaudio_la_CFLAGS = $(AM_CFLAGS) $(PULSEAUDIO_CFLAGS)

xineplug_ao_out_directx2_la_SOURCES = audio_directx2_out.c
xineplug_ao_out_directx2_la_CPPFLAGS = $(AM_CPPFLAGS) $(DIRECTX_CPPFLAGS) $(LTLIBINTL)
xineplug_ao_out_directx2_la_LIBADD = $(XINE_LIB) $(DIRECTX_AUDIO_LIBS) $(PTHREAD_LIBS)

xineplug_ao_out_fusionsound_la_SOURCES = audio_fusionsound_out.c
xineplug_ao_out_fusionsound_la_LIBADD = $(XINE_LIB) $(FUSIONSOUND_LIBS)
xineplug_ao_out_fusionsound_la_CFLAGS = $(AM_CFLAGS) $(FUSIONSOUND_CFLAGS)

xineplug_ao_out_jack_la_SOURCES = audio_jack_out.c
xineplug_ao_out_jack_la_LIBADD = $(XINE_LIB) $(JACK_LIBS) $(LTLIBINTL)
xineplug_ao_out_jack_la_CFLAGS = $(AM_FLAGS) $(JACK_CFLAGS)

xineplug_ao_out_sndio_la_SOURCES = audio_sndio_out.c
xineplug_ao_out_sndio_la_LIBADD = $(XINE_LIB) $(SNDIO_LIBS)
xineplug_ao_out_sndio_la_CFLAGS = $(AM_CFLAGS) $(SNDIO_CFLAGS)
