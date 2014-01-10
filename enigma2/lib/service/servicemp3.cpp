	/* note: this requires gstreamer 0.10.x and a big list of plugins. */
	/* it's currently hardcoded to use a big-endian alsasink as sink. */
#include <lib/base/ebase.h>
#include <lib/base/eerror.h>
#include <lib/base/init_num.h>
#include <lib/base/init.h>
#include <lib/base/nconfig.h>
#include <lib/base/object.h>
#include <lib/dvb/decoder.h>
#include <lib/components/file_eraser.h>
#include <lib/gui/esubtitle.h>
#include <lib/service/servicemp3.h>
#include <lib/service/service.h>
#include <lib/gdi/gpixmap.h>

#include <string>

#include <gst/gst.h>
#include <gst/pbutils/missing-plugins.h>
#include <sys/stat.h>

#define HTTP_TIMEOUT 10

// eServiceFactoryMP3

/*
 * gstreamer suffers from a bug causing sparse streams to loose sync, after pause/resume / skip
 * see: https://bugzilla.gnome.org/show_bug.cgi?id=619434
 * As a workaround, we run the subsink in sync=false mode
 */
#define GSTREAMER_SUBTITLE_SYNC_MODE_BUG
/**/

eServiceFactoryMP3::eServiceFactoryMP3()
{
	ePtr<eServiceCenter> sc;
	
	eServiceCenter::getPrivInstance(sc);
	if (sc)
	{
		std::list<std::string> extensions;
		extensions.push_back("dts");
		extensions.push_back("mp2");
		extensions.push_back("mp3");
		extensions.push_back("ogg");
		extensions.push_back("mpg");
		extensions.push_back("vob");
		extensions.push_back("wav");
		extensions.push_back("wave");
		extensions.push_back("m4v");
		extensions.push_back("mkv");
		extensions.push_back("avi");
		extensions.push_back("divx");
		extensions.push_back("dat");
		extensions.push_back("flac");
		extensions.push_back("flv");
		extensions.push_back("mp4");
		extensions.push_back("mov");
		extensions.push_back("m4a");
		extensions.push_back("3gp");
		extensions.push_back("3g2");
		extensions.push_back("asf");
		extensions.push_back("wmv");
		extensions.push_back("wma");
		extensions.push_back("m2ts");
		extensions.push_back("webm");
		sc->addServiceFactory(eServiceFactoryMP3::id, this, extensions);
	}

	m_service_info = new eStaticServiceMP3Info();
}

eServiceFactoryMP3::~eServiceFactoryMP3()
{
	ePtr<eServiceCenter> sc;
	
	eServiceCenter::getPrivInstance(sc);
	if (sc)
		sc->removeServiceFactory(eServiceFactoryMP3::id);
}

DEFINE_REF(eServiceFactoryMP3)

	// iServiceHandler
RESULT eServiceFactoryMP3::play(const eServiceReference &ref, ePtr<iPlayableService> &ptr)
{
		// check resources...
	ptr = new eServiceMP3(ref);
	return 0;
}

RESULT eServiceFactoryMP3::record(const eServiceReference &ref, ePtr<iRecordableService> &ptr)
{
	ptr=0;
	return -1;
}

RESULT eServiceFactoryMP3::list(const eServiceReference &, ePtr<iListableService> &ptr)
{
	ptr=0;
	return -1;
}

RESULT eServiceFactoryMP3::info(const eServiceReference &ref, ePtr<iStaticServiceInformation> &ptr)
{
	ptr = m_service_info;
	return 0;
}

class eMP3ServiceOfflineOperations: public iServiceOfflineOperations
{
	DECLARE_REF(eMP3ServiceOfflineOperations);
	eServiceReference m_ref;
public:
	eMP3ServiceOfflineOperations(const eServiceReference &ref);
	
	RESULT deleteFromDisk(int simulate);
	RESULT getListOfFilenames(std::list<std::string> &);
	RESULT reindex();
};

DEFINE_REF(eMP3ServiceOfflineOperations);

eMP3ServiceOfflineOperations::eMP3ServiceOfflineOperations(const eServiceReference &ref): m_ref((const eServiceReference&)ref)
{
}

RESULT eMP3ServiceOfflineOperations::deleteFromDisk(int simulate)
{
	if (!simulate)
	{
		std::list<std::string> res;
		if (getListOfFilenames(res))
			return -1;
		
		eBackgroundFileEraser *eraser = eBackgroundFileEraser::getInstance();
		if (!eraser)
			eDebug("FATAL !! can't get background file eraser");

		for (std::list<std::string>::iterator i(res.begin()); i != res.end(); ++i)
		{
			eDebug("Removing %s...", i->c_str());
			if (eraser)
				eraser->erase(i->c_str());
			else
				::unlink(i->c_str());
		}
	}
	return 0;
}

RESULT eMP3ServiceOfflineOperations::getListOfFilenames(std::list<std::string> &res)
{
	res.clear();
	res.push_back(m_ref.path);
	return 0;
}

RESULT eMP3ServiceOfflineOperations::reindex()
{
	return -1;
}


RESULT eServiceFactoryMP3::offlineOperations(const eServiceReference &ref, ePtr<iServiceOfflineOperations> &ptr)
{
	ptr = new eMP3ServiceOfflineOperations(ref);
	return 0;
}

// eStaticServiceMP3Info


// eStaticServiceMP3Info is seperated from eServiceMP3 to give information
// about unopened files.

// probably eServiceMP3 should use this class as well, and eStaticServiceMP3Info
// should have a database backend where ID3-files etc. are cached.
// this would allow listing the mp3 database based on certain filters.

DEFINE_REF(eStaticServiceMP3Info)

eStaticServiceMP3Info::eStaticServiceMP3Info()
{
}

RESULT eStaticServiceMP3Info::getName(const eServiceReference &ref, std::string &name)
{
	if ( ref.name.length() )
		name = ref.name;
	else
	{
		size_t last = ref.path.rfind('/');
		if (last != std::string::npos)
			name = ref.path.substr(last+1);
		else
			name = ref.path;
	}
	return 0;
}

int eStaticServiceMP3Info::getLength(const eServiceReference &ref)
{
	return -1;
}

int eStaticServiceMP3Info::getInfo(const eServiceReference &ref, int w)
{
	switch (w)
	{
	case iServiceInformation::sTimeCreate:
		{
			struct stat s;
			if (stat(ref.path.c_str(), &s) == 0)
			{
				return s.st_mtime;
			}
		}
		break;
	case iServiceInformation::sFileSize:
		{
			struct stat s;
			if (stat(ref.path.c_str(), &s) == 0)
			{
				return s.st_size;
			}
		}
		break;
	}
	return iServiceInformation::resNA;
}

long long eStaticServiceMP3Info::getFileSize(const eServiceReference &ref)
{
	struct stat s;
	if (stat(ref.path.c_str(), &s) == 0)
	{
		return s.st_size;
	}
	return 0;
}

// eServiceMP3
int eServiceMP3::ac3_delay,
    eServiceMP3::pcm_delay;

eServiceMP3::eServiceMP3(eServiceReference ref)
	:m_ref(ref)//, m_pump(eApp, 1) openpliPC
{
	m_subtitle_sync_timer = eTimer::create(eApp);
	m_streamingsrc_timeout = 0;
	//m_stream_tags = 0; openpliPC
	m_currentAudioStream = -1;
	m_currentSubtitleStream = -1;
	m_cachedSubtitleStream = 0; /* report the first subtitle stream to be 'cached'. TODO: use an actual cache. */
	m_subtitle_widget = 0;
	m_currentTrickRatio = 1.0;
	m_buffer_size = 5*1024*1024;
	m_buffer_duration = 5 * GST_SECOND;
	m_use_prefillbuffer = FALSE;
	m_prev_decoder_time = -1;
	m_decoder_time_valid_state = 0;
	m_errorInfo.missing_codec = "";

	CONNECT(m_subtitle_sync_timer->timeout, eServiceMP3::pushSubtitles);
	//CONNECT(m_pump.recv_msg, eServiceMP3::gstPoll); openpliPC
	m_aspect = m_width = m_height = m_framerate = m_progressive = -1;

	m_state = stIdle;
	eDebug("eServiceMP3::construct!");

	const char *filename = m_ref.path.c_str();
	const char *ext = strrchr(filename, '.');
	if (!ext)
		ext = filename;

	m_sourceinfo.is_video = FALSE;
	m_sourceinfo.audiotype = atUnknown;
	if ( (strcasecmp(ext, ".mpeg") && strcasecmp(ext, ".mpg") && strcasecmp(ext, ".vob") && strcasecmp(ext, ".bin") && strcasecmp(ext, ".dat") ) == 0 )
	{
		m_sourceinfo.containertype = ctMPEGPS;
		m_sourceinfo.is_video = TRUE;
	}
	else if ( strcasecmp(ext, ".ts") == 0 )
	{
		m_sourceinfo.containertype = ctMPEGTS;
		m_sourceinfo.is_video = TRUE;
	}
	else if ( strcasecmp(ext, ".mkv") == 0 )
	{
		m_sourceinfo.containertype = ctMKV;
		m_sourceinfo.is_video = TRUE;
	}
	else if ( strcasecmp(ext, ".avi") == 0 || strcasecmp(ext, ".divx") == 0)
	{
		m_sourceinfo.containertype = ctAVI;
		m_sourceinfo.is_video = TRUE;
	}
	else if ( strcasecmp(ext, ".mp4") == 0 || strcasecmp(ext, ".mov") == 0 || strcasecmp(ext, ".m4v") == 0 || strcasecmp(ext, ".3gp") == 0 || strcasecmp(ext, ".3g2") == 0)
	{
		m_sourceinfo.containertype = ctMP4;
		m_sourceinfo.is_video = TRUE;
	}
	else if ( strcasecmp(ext, ".asf") == 0 || strcasecmp(ext, ".wmv") == 0)
	{
		m_sourceinfo.containertype = ctASF;
		m_sourceinfo.is_video = TRUE;
	}
	else if ( strcasecmp(ext, ".m4a") == 0 )
	{
		m_sourceinfo.containertype = ctMP4;
		m_sourceinfo.audiotype = atAAC;
	}
	else if ( strcasecmp(ext, ".mp3") == 0 )
		m_sourceinfo.audiotype = atMP3;
	else if ( strcasecmp(ext, ".wma") == 0 )
		m_sourceinfo.audiotype = atWMA;
	else if ( (strncmp(filename, "/autofs/", 8) || strncmp(filename+strlen(filename)-13, "/track-", 7) || strcasecmp(ext, ".wav")) == 0 )
		m_sourceinfo.containertype = ctCDA;
	if ( strcasecmp(ext, ".dat") == 0 )
	{
		m_sourceinfo.containertype = ctVCD;
		m_sourceinfo.is_video = TRUE;
	}
	if ( strstr(filename, "://") )
		m_sourceinfo.is_streaming = TRUE;
	if ( strstr(filename, " buffer=1") )
		m_use_prefillbuffer = TRUE;

	/*gchar *uri; openpliPC

	if ( m_sourceinfo.is_streaming )
	{
		uri = g_strdup_printf ("%s", filename);
		m_streamingsrc_timeout = eTimer::create(eApp);;
		CONNECT(m_streamingsrc_timeout->timeout, eServiceMP3::sourceTimeout);

		std::string config_str;
		if (eConfigManager::getConfigBoolValue("config.mediaplayer.useAlternateUserAgent"))
		{
			m_useragent = eConfigManager::getConfigValue("config.mediaplayer.alternateUserAgent");
		}
		if (m_useragent.empty())
			m_useragent = "Enigma2 Mediaplayer";
	}
	else if ( m_sourceinfo.containertype == ctCDA )
	{
		int i_track = atoi(filename+18);
		uri = g_strdup_printf ("cdda://%i", i_track);
	}
	else if ( m_sourceinfo.containertype == ctVCD )
	{
		int ret = -1;
		int fd = open(filename,O_RDONLY);
		if (fd >= 0)
		{
			char tmp[128*1024];
			ret = read(fd, tmp, 128*1024);
			close(fd);
		}
		if ( ret == -1 ) // this is a "REAL" VCD
			uri = g_strdup_printf ("vcd://");
		else
			uri = g_filename_to_uri(filename, NULL, NULL);
	}
	else
		uri = g_filename_to_uri(filename, NULL, NULL);

	eDebug("eServiceMP3::playbin2 uri=%s", uri);

	m_gst_playbin = gst_element_factory_make("playbin2", "playbin");
	if ( m_gst_playbin )
	{
		g_object_set (G_OBJECT (m_gst_playbin), "uri", uri, NULL);
		int flags = 0x47; // ( GST_PLAY_FLAG_VIDEO | GST_PLAY_FLAG_AUDIO | GST_PLAY_FLAG_NATIVE_VIDEO | GST_PLAY_FLAG_TEXT );
		if ( m_sourceinfo.is_streaming )
		{
			g_signal_connect (G_OBJECT (m_gst_playbin), "notify::source", G_CALLBACK (gstHTTPSourceSetAgent), this);
			if (m_use_prefillbuffer)
			{
				g_object_set (G_OBJECT (m_gst_playbin), "buffer_duration", m_buffer_duration, NULL);
				flags |= 0x100; // USE_BUFFERING
			}
		}
		g_object_set (G_OBJECT (m_gst_playbin), "flags", flags, NULL);
		GstElement *subsink = gst_element_factory_make("subsink", "subtitle_sink");
		if (!subsink)
			eDebug("eServiceMP3::sorry, can't play: missing gst-plugin-subsink");
		else
		{
			m_subs_to_pull_handler_id = g_signal_connect (subsink, "new-buffer", G_CALLBACK (gstCBsubtitleAvail), this);
			g_object_set (G_OBJECT (subsink), "caps", gst_caps_from_string("text/plain; text/x-plain; text/x-pango-markup; video/x-dvd-subpicture; subpicture/x-pgs"), NULL);
			g_object_set (G_OBJECT (m_gst_playbin), "text-sink", subsink, NULL);
			g_object_set (G_OBJECT (m_gst_playbin), "current-text", m_currentSubtitleStream, NULL);
		}
		gst_bus_set_sync_handler(gst_pipeline_get_bus (GST_PIPELINE (m_gst_playbin)), gstBusSyncHandler, this);
		char srt_filename[strlen(filename)+1];
		strncpy(srt_filename,filename,strlen(filename)-3);
		srt_filename[strlen(filename)-3]='\0';
		strcat(srt_filename, "srt");
		if (::access(srt_filename, R_OK) >= 0)
		{
			eDebug("eServiceMP3::subtitle uri: %s", g_filename_to_uri(srt_filename, NULL, NULL));
			g_object_set (G_OBJECT (m_gst_playbin), "suburi", g_filename_to_uri(srt_filename, NULL, NULL), NULL);
		}
	} else
	{
		m_event((iPlayableService*)this, evUser+12);
		m_gst_playbin = 0;
		m_errorInfo.error_message = "failed to create GStreamer pipeline!\n";

		eDebug("eServiceMP3::sorry, can't play: %s",m_errorInfo.error_message.c_str());
	}
	g_free(uri);

	setBufferSize(m_buffer_size);*/
  
  cXineLib *xineLib = cXineLib::getInstance();
	int uzunluk;
	uzunluk=strlen(filename);
	char myfilesrt[1000];
	sprintf(myfilesrt,"%s",filename);
	myfilesrt[uzunluk-4]='\0';
	char myfile[1000];
	sprintf(myfile,"%s#subtitle:%s.srt",filename,myfilesrt);
	xineLib->FilmVideo(myfile); 
}

eServiceMP3::~eServiceMP3()
{
	// disconnect subtitle callback
	/*GstElement *subsink = gst_bin_get_by_name(GST_BIN(m_gst_playbin), "subtitle_sink"); openpliPC

	if (subsink)
	{
		g_signal_handler_disconnect (subsink, m_subs_to_pull_handler_id);
		gst_object_unref(subsink);
	}

	if (m_subtitle_widget) m_subtitle_widget->destroy();
	m_subtitle_widget = 0;

	// disconnect sync handler callback
	gst_bus_set_sync_handler(gst_pipeline_get_bus (GST_PIPELINE (m_gst_playbin)), NULL, NULL);*/
  
	if (m_state == stRunning)
		stop();

	/*if (m_stream_tags) openpliPC
		gst_tag_list_free(m_stream_tags);
	
	if (m_gst_playbin)
	{
		gst_object_unref (GST_OBJECT (m_gst_playbin));
		eDebug("eServiceMP3::destruct!");
	}*/
}

DEFINE_REF(eServiceMP3);

//DEFINE_REF(eServiceMP3::GstMessageContainer); openpliPC

RESULT eServiceMP3::connectEvent(const Slot2<void,iPlayableService*,int> &event, ePtr<eConnection> &connection)
{
	connection = new eConnection((iPlayableService*)this, m_event.connect(event));
	return 0;
}

RESULT eServiceMP3::start()
{
//	ASSERT(m_state == stIdle); openpliPC

	m_state = stRunning;
	/*if (m_gst_playbin) openpliPC
	{
		eDebug("eServiceMP3::starting pipeline");
		gst_element_set_state (m_gst_playbin, GST_STATE_PLAYING);
	}*/

	m_event(this, evStart);

	return 0;
}

void eServiceMP3::sourceTimeout()
{
	eDebug("eServiceMP3::http source timeout! issuing eof...");
	m_event((iPlayableService*)this, evEOF);
}

RESULT eServiceMP3::stop()
{
	ASSERT(m_state != stIdle);

	if (m_state == stStopped)
		return -1;

	eDebug("eServiceMP3::stop %s", m_ref.path.c_str());
	//gst_element_set_state(m_gst_playbin, GST_STATE_NULL); openpliPC
	m_state = stStopped;
	cXineLib *xineLib = cXineLib::getInstance();
	xineLib->stopVideo();

	return 0;
}

RESULT eServiceMP3::setTarget(int target)
{
	return -1;
}

RESULT eServiceMP3::pause(ePtr<iPauseableService> &ptr)
{
	ptr=this;
	return 0;
}

RESULT eServiceMP3::setSlowMotion(int ratio)
{
	if (!ratio)
		return 0;
	eDebug("eServiceMP3::setSlowMotion ratio=%f",1.0/(gdouble)ratio);
	return trickSeek(1.0/(gdouble)ratio);
}

RESULT eServiceMP3::setFastForward(int ratio)
{
	eDebug("eServiceMP3::setFastForward ratio=%i",ratio);
	return trickSeek(ratio);
}

		// iPausableService
RESULT eServiceMP3::pause()
{
	//if (!m_gst_playbin || m_state != stRunning) openpliPC
	if (m_state != stRunning)
		return -1;

	//trickSeek(0.0); openpliPC

	cXineLib *xineLib = cXineLib::getInstance();
	xineLib->VideoPause();

	return 0;
}

RESULT eServiceMP3::unpause()
{
	//if (!m_gst_playbin || m_state != stRunning) openpliPC
	if (m_state != stRunning)
		return -1;

	//trickSeek(1.0); openpliPC

	cXineLib *xineLib = cXineLib::getInstance();
	xineLib->VideoResume();

	return 0;
}

	/* iSeekableService */
RESULT eServiceMP3::seek(ePtr<iSeekableService> &ptr)
{
	ptr = this;
	return 0;
}

RESULT eServiceMP3::getLength(pts_t &pts)
{
	if (m_state != stRunning)
		return -1;

	cXineLib *xineLib = cXineLib::getInstance();
	xineLib->VideoPosisyon();
	pts=xineLib->Vlength*90;
	return 0;
}

RESULT eServiceMP3::seekToImpl(pts_t to)
{
	return 0;
}

RESULT eServiceMP3::seekTo(pts_t to)
{
	cXineLib *xineLib = cXineLib::getInstance();
	xineLib->SeekTo(to/90);

	RESULT ret = 0;
	return ret;
}


RESULT eServiceMP3::trickSeek(gdouble ratio)
{
//	m_currentTrickRatio = ratio;
	printf("----Ratio=%d\n",ratio);
	cXineLib *xineLib = cXineLib::getInstance();
	xineLib->VideoIleriF();
	//if (!ratio) return seekRelative(0, 0);	
	m_subtitle_pages.clear();
	m_prev_decoder_time = -1;
	m_decoder_time_valid_state = 0;
	return 0;
}


RESULT eServiceMP3::seekRelative(int direction, pts_t to)
{
	eDebug("eDVBServicePlay::seekRelative: jump %d, %lld", direction, to);
	cXineLib *xineLib = cXineLib::getInstance();
	xineLib->VideoGeriT(to/90*direction);
	return 0;
}

RESULT eServiceMP3::getPlayPosition(pts_t &pts)
{
	if (m_state != stRunning)
		return -1;

	cXineLib *xineLib = cXineLib::getInstance();
	xineLib->VideoPosisyon();
	pts=xineLib->Vpos*90;
	return 0;
}

RESULT eServiceMP3::setTrickmode(int trick)
{
		/* trickmode is not yet supported by our dvbmediasinks. */
	return -1;
}

RESULT eServiceMP3::isCurrentlySeekable()
{
	if (m_state != stRunning)
		return 0;

	int ret = 3; // seeking and fast/slow winding possible
	return ret;
}

RESULT eServiceMP3::info(ePtr<iServiceInformation>&i)
{
	i = this;
	return 0;
}

RESULT eServiceMP3::getName(std::string &name)
{
	std::string title = m_ref.getName();
	if (title.empty())
	{
		name = m_ref.path;
		size_t n = name.rfind('/');
		if (n != std::string::npos)
			name = name.substr(n + 1);
	}
	else
		name = title;
	return 0;
}

int eServiceMP3::getInfo(int w)
{
	cXineLib *xineLib = cXineLib::getInstance();
 
 	switch (w)
 	{
 	case sServiceref: return m_ref;
	case sVideoHeight:
		return xineLib->getVideoHeight();
		break;
	case sVideoWidth:
		return xineLib->getVideoWidth();
		break;
	case sFrameRate:
		return xineLib->getVideoFrameRate();
		break;
 	case sTagTitle:
 	case sTagArtist:
 	case sTagAlbum:
	case sTagTitleSortname:
	case sTagArtistSortname:
	case sTagAlbumSortname:
	case sTagDate:
	case sTagComposer:
	case sTagGenre:
	case sTagComment:
	case sTagExtendedComment:
	case sTagLocation:
	case sTagHomepage:
	case sTagDescription:
	case sTagVersion:
	case sTagISRC:
	case sTagOrganization:
	case sTagCopyright:
	case sTagCopyrightURI:
	case sTagContact:
	case sTagLicense:
	case sTagLicenseURI:
	case sTagCodec:
	case sTagAudioCodec:
	case sTagVideoCodec:
	case sTagEncoder:
	case sTagLanguageCode:
	case sTagKeywords:
	case sTagChannelMode:
	case sUser+12:
		return resIsString;
	case sTagTrackGain:
	case sTagTrackPeak:
	case sTagAlbumGain:
	case sTagAlbumPeak:
	case sTagReferenceLevel:
	case sTagBeatsPerMinute:
	case sTagImage:
 	case sTagPreviewImage:
 	case sTagAttachment:
		return resIsPyObject;
		break;
	default:
		return resNA;
	}

	return 0;
}

std::string eServiceMP3::getInfoString(int w)
{
	return "";
}

PyObject *eServiceMP3::getInfoObject(int w)
{
	Py_RETURN_NONE;
}

RESULT eServiceMP3::audioChannel(ePtr<iAudioChannelSelection> &ptr)
{
	ptr = this;
	return 0;
}

RESULT eServiceMP3::audioTracks(ePtr<iAudioTrackSelection> &ptr)
{
	ptr = this;
	return 0;
}

RESULT eServiceMP3::subtitle(ePtr<iSubtitleOutput> &ptr)
{
	ptr = this;
	return 0;
}

RESULT eServiceMP3::audioDelay(ePtr<iAudioDelay> &ptr)
{
	ptr = this;
	return 0;
}

int eServiceMP3::getNumberOfTracks()
{
	cXineLib *xineLib = cXineLib::getInstance();
	int ret=xineLib->getNumberOfTracksAudio();
	//printf("Number of tracks - %d\n", ret);
	if (ret) 
	{
		return ret;
	}
	return 0;
}

int eServiceMP3::getCurrentTrack()
{
	cXineLib *xineLib = cXineLib::getInstance();
	int ret = xineLib->getCurrentTrackAudio();
	//printf("Current  track audio - %d\n", ret);
	return ret;
}

RESULT eServiceMP3::selectTrack(unsigned int i)
{
	pts_t ppos;
	getPlayPosition(ppos);
	ppos -= 90000;
	if (ppos < 0)
		ppos = 0;

	int ret = selectAudioStream(i);
	if (!ret) {
		/* flush */
		//seekTo(ppos);
	}

	return ret;
}

int eServiceMP3::selectAudioStream(int i)
{
	cXineLib *xineLib = cXineLib::getInstance();
	xineLib->selectAudioStream(i);

	//return 0;
	return i;
}

int eServiceMP3::getCurrentChannel()
{
	return STEREO;
}

RESULT eServiceMP3::selectChannel(int i)
{
	eDebug("eServiceMP3::selectChannel(%i)",i);
	return 0;
}

RESULT eServiceMP3::getTrackInfo(struct iAudioTrackInfo &info, unsigned int i)
{
	cXineLib *xineLib = cXineLib::getInstance();
	info.m_description = "???";

	if (info.m_language.empty())
		info.m_language = xineLib->getAudioLang(i);

	return 0;
}

eAutoInitPtr<eServiceFactoryMP3> init_eServiceFactoryMP3(eAutoInitNumbers::service+1, "eServiceFactoryMP3");

void eServiceMP3::pushSubtitles()
{
}

RESULT eServiceMP3::enableSubtitles(iSubtitleUser *user, struct SubtitleTrack &track)
{
  return 0;
}

RESULT eServiceMP3::disableSubtitles()
{
	return 0;
}

RESULT eServiceMP3::getCachedSubtitle(struct SubtitleTrack &track)
{
// 	eDebug("eServiceMP3::getCachedSubtitle");
	return -1;
}

RESULT eServiceMP3::getSubtitleList(std::vector<struct SubtitleTrack> &subtitlelist)
{
// 	eDebug("eServiceMP3::getSubtitleList");
	int stream_idx = 0;
	eDebug("eServiceMP3::getSubtitleList finished");
	return 0;
}

RESULT eServiceMP3::streamed(ePtr<iStreamedService> &ptr)
{
	ptr = this;
	return 0;
}

PyObject *eServiceMP3::getBufferCharge()
{
	ePyObject tuple = PyTuple_New(5);
	PyTuple_SET_ITEM(tuple, 0, PyInt_FromLong(0));
	PyTuple_SET_ITEM(tuple, 1, PyInt_FromLong(0));
	PyTuple_SET_ITEM(tuple, 2, PyInt_FromLong(0));
	PyTuple_SET_ITEM(tuple, 3, PyInt_FromLong(0));
	PyTuple_SET_ITEM(tuple, 4, PyInt_FromLong(0));
	return tuple;
}

int eServiceMP3::setBufferSize(int size)
{
	m_buffer_size = size;
	return 0;
}

int eServiceMP3::getAC3Delay()
{
	return ac3_delay;
}

int eServiceMP3::getPCMDelay()
{
	return pcm_delay;
}

void eServiceMP3::setAC3Delay(int delay)
{
}

void eServiceMP3::setPCMDelay(int delay)
{
}

