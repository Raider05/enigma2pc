#include <fstream>
#include <lib/gdi/xineLib.h>
#include <lib/base/eenv.h>
#include <sstream>

static const std::string getConfigString(const std::string &key, const std::string &defaultValue)
{
        std::string value = defaultValue;

        // get value from enigma2 settings file
        std::ifstream in(eEnv::resolve("${sysconfdir}/enigma2/settings").c_str());
        if (in.good()) {
                do {
                        std::string line;
                        std::getline(in, line);
                        size_t size = key.size();
                        if (!line.compare(0, size, key) && line[size] == '=') {
                                value = line.substr(size + 1);
                                break;
                        }
                } while (in.good());
                in.close();
        }

        return value;
}

static int getConfigInt(const std::string &key)
{
  std::string value = getConfigString(key, "0");
        return atoi(value.c_str());
}

static void config_change_sound(xine_t *xine, int m_sound_card, int m_sound_device, int m_sound_output)
{
		std::ostringstream a_line;
		a_line << "hw:" << m_sound_card << "," << m_sound_device;
		std::string         s = a_line.str();
		const char  *hw_audio = s.c_str();

		printf("Sound card:device - %s\n", hw_audio);

		xine_config_register_string(xine,
		"audio.device.alsa_default_device",
		"default",
		_("device used for mono output"),
		_("xine will use this alsa device "
		"to output mono sound.\n"
		"See the alsa documentation "
		"for information on alsa devices."),
		10, NULL, NULL);

		xine_config_register_string(xine,
		"audio.device.alsa_front_device",
		"plug:front:default",
		_("device used for stereo output"),
		_("xine will use this alsa device "
		"to output stereo sound.\n"
		"See the alsa documentation "
		"for information on alsa devices."),
		10, NULL, NULL);

		xine_config_register_string(xine,
		"audio.device.alsa_surround51_device",
		"plug:surround51:0",
		_("device used for 5.1-channel output"),
		_("xine will use this alsa device to output "
		"5 channel plus LFE (5.1) surround sound.\n"
		"See the alsa documentation for information "
		"on alsa devices."),
		10,  NULL, NULL);

		xine_config_register_string(xine,
		"audio.device.alsa_passthrough_device",
		"iec958:AES0=0x6,AES1=0x82,AES2=0x0,AES3=0x2",
		_("device used for 5.1-channel output"),
		_("xine will use this alsa device to output "
		"undecoded digital surround sound. This can "
		"be used be external surround decoders.\nSee the "
		"alsa documentation for information on alsa "
		"devices."),
		10, NULL, NULL);

		xine_config_register_string(xine,"audio.output.speaker_arrangement",
		"Stereo 2.0",
               _("speaker arrangement"),
               _("Select how your speakers are arranged, "
                 "this determines which speakers xine uses for sound output. "
                 "The individual values are:\n\n"
                 "Mono 1.0: You have only one speaker.\n"
                 "Stereo 2.0: You have two speakers for left and right channel.\n"
                 "Headphones 2.0: You use headphones.\n"
                 "Stereo 2.1: You have two speakers for left and right channel, and one "
                 "subwoofer for the low frequencies.\n"
                 "Surround 3.0: You have three speakers for left, right and rear channel.\n"
                 "Surround 4.0: You have four speakers for front left and right and rear "
                 "left and right channels.\n"
                 "Surround 4.1: You have four speakers for front left and right and rear "
                 "left and right channels, and one subwoofer for the low frequencies.\n"
                 "Surround 5.0: You have five speakers for front left, center and right and "
                 "rear left and right channels.\n"
                 "Surround 5.1: You have five speakers for front left, center and right and "
                 "rear left and right channels, and one subwoofer for the low frequencies.\n"
                 "Surround 6.0: You have six speakers for front left, center and right and "
                 "rear left, center and right channels.\n"
                 "Surround 6.1: You have six speakers for front left, center and right and "
                 "rear left, center and right channels, and one subwoofer for the low frequencies.\n"
                 "Surround 7.1: You have seven speakers for front left, center and right, "
                 "left and right and rear left and right channels, and one subwoofer for the "
                 "low frequencies.\n"
                 "Pass Through: Your sound system will receive undecoded digital sound from xine. "
                 "You need to connect a digital surround decoder capable of decoding the "
                 "formats you want to play to your sound card's digital output."),
		10, NULL, NULL);


		xine->config->update_string(xine->config, "audio.device.alsa_default_device", hw_audio);
		xine->config->update_string(xine->config, "audio.device.alsa_front_device", hw_audio);
		xine->config->update_string(xine->config, "audio.device.alsa_passthrough_device", hw_audio);
		xine->config->update_string(xine->config, "audio.device.alsa_surround51_device", hw_audio);

		if (m_sound_output == 1)
			xine->config->update_string(xine->config, "audio.output.speaker_arrangement", "Stereo 2.0");
		else if (m_sound_output == 8)
			xine->config->update_string(xine->config, "audio.output.speaker_arrangement", "Surround 5.1");
		else if (m_sound_output == 11)
			xine->config->update_string(xine->config, "audio.output.speaker_arrangement", "Surround 7.1");
		else if (m_sound_output == 12)
			xine->config->update_string(xine->config, "audio.output.speaker_arrangement", "Pass Through");
}

/*
 * list available plugins
 */

static void list_plugins_type(xine_t *xine, const char *msg, typeof (xine_list_audio_output_plugins) list_func)
{
  static xine_t *tmp_xine = NULL;
  if(!xine) {
    if(!tmp_xine)
      xine_init(tmp_xine = xine_new());
    xine = tmp_xine;
  }
  const char *const *list = list_func(xine);

  printf("%s", msg);
  while(list && *list)
    printf(" %s", *list++);
  printf("\n");
}

cXineLib   *cXineLib::instance;

DEFINE_REF(cXineLib);

cXineLib::cXineLib(x11_visual_t *vis) : m_pump(eApp, 1) {
	char        configfile[150];
	char        *vo_driver = "auto";
	char        *ao_driver = "alsa";
	const char  *static_post_plugins = "enigma_video;upmix_mono";

	instance = this;
	osd = NULL;
	stream = NULL;
	end_of_stream = false;
	videoPlayed = false;
	post_plugins_t *posts = NULL;

	printf("XINE-LIB version: %s\n", xine_get_version_string() );

	xine = xine_new();
	strcpy(configfile, eEnv::resolve("${datadir}/enigma2/xine.conf").c_str());
	printf("configfile  %s\n", configfile);
	xine_config_load(xine, configfile);
	sound_mode = sound_card = sound_device = 0;
	sound_output = 1;
	sound_mode = getConfigInt("config.av.sound_mode");
	if (sound_mode > 0)
	{
		sound_card   = getConfigInt("config.av.sound_card");
		sound_device = getConfigInt("config.av.sound_device");
		sound_output = getConfigInt("config.av.sound_output");
		config_change_sound(xine, sound_card, sound_device, sound_output);
	}
	xine_init(xine);
	xine_engine_set_param(xine, XINE_ENGINE_PARAM_VERBOSITY, XINE_VERBOSITY_LOG);
	list_plugins_type(xine, "Available post plugins: ", xine_list_post_plugins); 

  cfg_entry_t *entry;
	config_values_t *cfg;
// read Video Driver from config
	cfg = this->xine->config;
	entry = cfg->lookup_entry(cfg, "video.driver");
	vo_driver = strdup(entry->unknown_value);
// read Audio Driver from config
	entry = cfg->lookup_entry(cfg, "audio.driver");
	ao_driver = strdup(entry->unknown_value);
	printf("use vo_driver: %s \n", vo_driver);
	printf("use ao_driver: %s \n", ao_driver);

	
	if((vo_port = xine_open_video_driver(xine, vo_driver , XINE_VISUAL_TYPE_X11, (void *) vis)) == NULL)
	{
		printf("I'm unable to initialize '%s' video driver. Giving up.\n", vo_driver);
		return;
	}

	ao_port     = xine_open_audio_driver(xine , ao_driver, NULL);
	stream      = xine_stream_new(xine, ao_port, vo_port);

	if ( (!xine_open(stream, eEnv::resolve("${sysconfdir}/tuxbox/logo.mvi").c_str()))
			|| (!xine_play(stream, 0, 0)) ) {
		return;
	}

	xine_queue = xine_event_new_queue (stream);
	xine_event_create_listener_thread(xine_queue, xine_event_handler, this);

        posts = this->postplugins = (post_plugins_t*)calloc(1, sizeof(post_plugins_t));
        posts->xine = this->xine;
        posts->audio_port = this->ao_port;
        posts->video_port = this->vo_port;
        posts->video_source = posts->audio_source = this->stream;

//#if 0
//    LOGMSG("Enabling multithreaded post processing");
//    vpplugin_parse_and_store_post(posts, "thread");
//#endif

        if(static_post_plugins && *static_post_plugins) {
                int i;
                printf("static post plugins (from command line): %s\n", static_post_plugins);
                posts->static_post_plugins = strdup(static_post_plugins);
                vpplugin_parse_and_store_post(posts, posts->static_post_plugins);
                applugin_parse_and_store_post(posts, posts->static_post_plugins);

                for(i=0; i<posts->post_audio_elements_num; i++)
                        if(posts->post_audio_elements[i])
                                posts->post_audio_elements[i]->enable = 2;

                for(i=0; i<posts->post_video_elements_num; i++)
                        if(posts->post_video_elements[i])
                                posts->post_video_elements[i]->enable = 2;
                posts->post_video_enable = 1;
                posts->post_audio_enable = 1;
		rewire_posts_load();
        }

	CONNECT(m_pump.recv_msg, cXineLib::pumpEvent);

	m_width     = 0;
	m_height    = 0;
	m_framerate = 0;
	m_aspect    = -1;
	m_windowAspectRatio = 0;
	m_policy43 = 0;
	m_policy169 = 0;
	m_progressive = -1;

	m_sharpness = 0;
	m_noise = 0;
}

cXineLib::~cXineLib() {
	instance = 0;

	if (stream)
	{
		xine_stop(stream);

                if (this->postplugins) {
			rewire_posts_unload();
		}

		xine_close(stream);

		if (xine_queue)
		{
			xine_event_dispose_queue(xine_queue);
			xine_queue = 0;
		}

		_x_demux_flush_engine(stream);

		xine_dispose(stream);
		stream = NULL;
	}

	if (ao_port)
		xine_close_audio_driver(xine, ao_port);
	if (vo_port)
		xine_close_video_driver(xine, vo_port);
}

void cXineLib::rewire_posts_load() {
        printf("Enable re-wiring post plugins\n");
        if (this->postplugins) {
                vpplugin_rewire_posts(this->postplugins);
                applugin_rewire_posts(this->postplugins);
        }
}

void cXineLib::rewire_posts_unload() {
        if (this->postplugins) {
                printf("Unloading post plugins\n");
                vpplugin_unload_post(this->postplugins, NULL);
                applugin_unload_post(this->postplugins, NULL);
        }
}

void cXineLib::setVolume(int value) {
//	xine_set_param (stream, XINE_PARAM_AUDIO_VOLUME, value);
	xine_set_param (stream, XINE_PARAM_AUDIO_AMP_LEVEL , value);
}

void cXineLib::setVolumeMute(int value) {
//	xine_set_param (stream, XINE_PARAM_AUDIO_MUTE, value==0?0:1);
	xine_set_param(stream, XINE_PARAM_AUDIO_AMP_MUTE, value==0?0:1);
}

void cXineLib::showOsd() {
	xine_osd_show_scaled(osd, 0);
	//stream->osd_renderer->draw_bitmap(osd, (uint8_t*)m_surface.data, 0, 0, 720, 576, temp_bitmap_mapping);
}

void cXineLib::newOsd(int width, int height, uint32_t *argb_buffer) {
	osdWidth  = width;
	osdHeight = height;

	if (osd)
		xine_osd_free(osd);

	osd = xine_osd_new(stream, 0, 0, osdWidth, osdHeight);
	xine_osd_set_extent(osd, osdWidth, osdHeight);
	xine_osd_set_argb_buffer(osd, argb_buffer, 0, 0, osdWidth, osdHeight);
}

void cXineLib::playVideo(void) {
	end_of_stream = false;
	videoPlayed = false;

	printf("XINE try START !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
	if ( !xine_open(stream, "enigma:/") ) {
		printf("Unable to open stream !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
	}

	//	setStreamType(1);
	//	setStreamType(0);
	xine_pids_data_t data;
	xine_event_t event;
	event.type = XINE_EVENT_PIDS_CHANGE;
	data.vpid = videoData.pid;
	data.apid = audioData.pid;
	event.data = &data;
	event.data_length = sizeof (xine_pids_data_t);

	printf ("input_dvb: sending event\n");

	xine_event_send (stream, &event);

	xine_set_param(this->stream, XINE_PARAM_AUDIO_CHANNEL_LOGICAL, -1);

	setStreamType(1);
	setStreamType(0);

        //_x_demux_control_start(stream);
        //_x_demux_seek(stream, 0, 0, 0);

//	rewire_posts_load();

	if( !xine_play(stream, 0, 0) ) {
		printf("Unable to play stream !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
	}
	else {
		printf("XINE STARTED !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
		videoPlayed = true;
	}
}

void cXineLib::stopVideo(void) {

	if (videoPlayed) {
		xine_stop(stream);
		end_of_stream = true;
		videoPlayed = false;
	}
}

void cXineLib::setStreamType(int video) {
	xine_event_t event;

	if (video==1) {
		event.type = XINE_EVENT_SET_VIDEO_STREAMTYPE;
		event.data = &videoData;
	} else {
		event.type = XINE_EVENT_SET_AUDIO_STREAMTYPE;
		event.data = &audioData;
	}

	event.data_length = sizeof (xine_streamtype_data_t);

	xine_event_send (stream, &event);
}

void cXineLib::setVideoType(int pid, int type) {
	videoData.pid = pid;
	videoData.streamtype = type;
}

//////////////////////7
void cXineLib::FilmVideo(char *mrl) {
ASSERT(stream);

	if (!xine_open(stream, mrl))
	{
		eWarning("xine_open failed!");
		return ;
	}

	if (!xine_play(stream, 0, 0))
	{
		eWarning("xine_play failed!");
		return ;
	}

/*	if (xine_queue==0)
	{
		xine_queue = xine_event_new_queue (stream);
		xine_event_create_listener_thread(xine_queue, xine_event_handler, this);
	}
*/
	xine_set_param(this->stream, XINE_PARAM_AUDIO_CHANNEL_LOGICAL, 0);
	videoPlayed = true;
}

int
cXineLib::VideoPause()
{
xine_set_param(stream, XINE_PARAM_SPEED, XINE_SPEED_PAUSE);
return 1;
}


int
cXineLib::VideoResume()
{
//	int ret;
	/* Resume the playback. */
//	ret = xine_get_param(stream, XINE_PARAM_SPEED);
//	if( ret != XINE_SPEED_NORMAL ){
		xine_set_param(stream, XINE_PARAM_SPEED, XINE_SPEED_NORMAL);
//	}
	return 1;
}

int
cXineLib::VideoGeriT(pts_t Sar)
{// 10 saniye Geri Sarma 
pts_t geriSar;
xine_set_param(stream, XINE_PARAM_SPEED, XINE_SPEED_PAUSE);
VideoPosisyon();
geriSar=Vpos+Sar;
printf("%d---Vpos=%d---Sar=%d",geriSar,Vpos,Sar);
if (geriSar<0) geriSar=0;
xine_play(stream, 0, geriSar);
xine_set_param(stream, XINE_PARAM_SPEED, XINE_SPEED_NORMAL);
return 1;
}

int
cXineLib::VideoIleriF()
{
	int ret;
	/* Slow the playback. */
	ret = xine_get_param(stream, XINE_PARAM_SPEED);
	if( ret != XINE_EVENT_INPUT_RIGHT){
		xine_set_param(stream, XINE_PARAM_SPEED, XINE_EVENT_INPUT_RIGHT);
	}
	return 1;
}

int
cXineLib::VideoPosisyon()
{
xine_get_pos_length (stream, &VposStream, &Vpos, &Vlength);
return 1;
}
/*
XINE_SPEED_SLOW_4
XINE_SPEED_SLOW_2
XINE_SPEED_NORMAL
XINE_SPEED_FAST_2
XINE_SPEED_FAST_4
XINE_FINE_SPEED_NORMAL
*/
/////////////////////

void cXineLib::SeekTo(long long value) {
	xine_play(stream, 0, value);
}

void cXineLib::setAudioType(int pid, int type) {
	audioData.pid = pid;
	audioData.streamtype = type;
	
	if (videoPlayed) {
	    setStreamType(0);
	}
}

int cXineLib::getNumberOfTracksAudio() {
	int ret = xine_get_stream_info(this->stream, XINE_STREAM_INFO_MAX_AUDIO_CHANNEL);
	return ret;
}

void cXineLib::selectAudioStream(int value) {
	xine_set_param(this->stream, XINE_PARAM_AUDIO_CHANNEL_LOGICAL, value);
}

int cXineLib::getCurrentTrackAudio() {
	if (getNumberOfTracksAudio()) {
		int ret=xine_get_param(this->stream, XINE_PARAM_AUDIO_CHANNEL_LOGICAL);
		return ret;
	}
	return 0;
}

std::string cXineLib::getAudioLang(int value) {
	char lang_buffer[XINE_LANG_MAX];
	char *lang = NULL;

	if (!xine_get_audio_lang(this->stream, value, &lang_buffer[0])) {
		snprintf(lang_buffer, sizeof(lang_buffer), "%3d", value);
	}
	lang = lang_buffer;

	return lang;
}

void cXineLib::setPrebuffer(int prebuffer) {
	xine_set_param(stream, XINE_PARAM_METRONOM_PREBUFFER, prebuffer);
}

/* void cXineLib::detect_aspect_from_frame(bool b_aspect)
{
        int old_height, old_width;
        xine_current_frame_data_t frame_data;
        memset(&frame_data, 0, sizeof (frame_data));
	old_height=old_width=-1;

        while (b_aspect)
        {
                if (xine_get_current_frame_data(this->stream, &frame_data, XINE_FRAME_DATA_ALLOCATE_IMG))
                {
                   /*     cur_aspect=frame_data.ratio_code;
                        if ((cur_aspect != old_aspect) && (old_aspect<0))
                        {
                                printf("Aspect changed from %d to %d !!!\n", old_aspect, cur_aspect);
                        }
			old_aspect=cur_aspect;
		* /
			printf("Current height - %d, current width - %d, old height - %d, old  width - %d !!!\n", frame_data.crop_left, frame_data.crop_right, frame_data.crop_top, frame_data.crop_bottom);
			old_height = frame_data.height;
			old_width = frame_data.width;
                }
		sleep(1);

        }


}
*/
void cXineLib::xine_event_handler(void *user_data, const xine_event_t *event)
{
	cXineLib *xineLib = (cXineLib*)user_data;
	//if (event->type!=15)
	printf("I have XINE event ---  %d\n", event->type);

	switch (event->type)
	{
	case XINE_EVENT_UI_PLAYBACK_FINISHED:
		printf("XINE_EVENT_UI_PLAYBACK_FINISHED\n");
		break;
	case XINE_EVENT_NBC_STATS:
		return;
	case XINE_EVENT_FRAME_FORMAT_CHANGE:
		printf("XINE_EVENT_FRAME_FORMAT_CHANGE\n");
		{
			xine_format_change_data_t* data = (xine_format_change_data_t*)event->data;
			printf("width %d  height %d  aspect %d\n", data->width, data->height, data->aspect);

			struct iTSMPEGDecoder::videoEvent evt;
			evt.type = iTSMPEGDecoder::videoEvent::eventSizeChanged;
			xineLib->m_aspect = evt.aspect = data->aspect;
			xineLib->m_height = evt.height = data->height;
			xineLib->m_width  = evt.width  = data->width;
			xineLib->m_pump.send(evt);

			xineLib->adjust_policy();
		}
		return;
	case XINE_EVENT_FRAMERATE_CHANGE:
		printf("XINE_EVENT_FRAMERATE_CHANGE\n");
		{
			xine_framerate_data_t* data = (xine_framerate_data_t*)event->data;
			printf("framerate %d  \n", data->framerate);

			struct iTSMPEGDecoder::videoEvent evt;
			evt.type = iTSMPEGDecoder::videoEvent::eventFrameRateChanged;
			xineLib->m_framerate = evt.framerate = data->framerate;
			xineLib->m_pump.send(evt);
		}
		return;
	case XINE_EVENT_PROGRESS:
		{
			xine_progress_data_t* data = (xine_progress_data_t*) event->data;
			printf("XINE_EVENT_PROGRESS  %s  %d\n", data->description, data->percent);
			if (xineLib->videoPlayed && data->percent==0)
				xineLib->end_of_stream = true;
		}
		break;

	default:
		printf("xine_event_handler(): event->type: %d\n", event->type);
		return;
	}
}

void cXineLib::pumpEvent(const iTSMPEGDecoder::videoEvent &event)
{
	m_event(event);
}

int cXineLib::getVideoWidth()
{
	return m_width;
}

int cXineLib::getVideoHeight()
{
	return m_height;
}

int cXineLib::getVideoFrameRate()
{
	if (stream) {
		int	d;
		d = xine_get_stream_info(this->stream, XINE_STREAM_INFO_FRAME_DURATION);
		if (d != 0) {
			m_framerate = int(90000000/d) ;
		}
		printf("framerate : %d\n", m_framerate);
	}
	return m_framerate;
}

int cXineLib::getProgressive()
{
	if (stream) {
		xine_current_frame_data_t  data;
		memset(&data, 0, sizeof (data));
		if (xine_get_current_frame_data(this->stream, &data, XINE_FRAME_DATA_ALLOCATE_IMG)) {
			if (data.interlaced == 1)
				m_progressive = 0;
			else
				m_progressive = 1;
		}
		printf("progressive : %d\n", m_progressive);
	}
	return m_progressive;
}

int cXineLib::getVideoAspect()
{
	return m_aspect;
}

RESULT cXineLib::getPTS(pts_t &pts)
{
	pts_t* last_pts_l = (pts_t*)vo_port->get_property(vo_port, VO_PROP_LAST_PTS);

	pts = *last_pts_l;

	if (pts != 0)
		return 0;
	
	return -1;
}

void cXineLib::setVideoWindow(int window_x, int window_y, int window_width, int window_height)
{
	int left = window_x * windowWidth / osdWidth;
	int top = window_y * windowHeight / osdHeight;
	int width = window_width * windowWidth / osdWidth;
	int height = window_height * windowHeight / osdHeight;

	xine_osd_set_video_window(osd, left, top, width, height);
	showOsd();
}

void cXineLib::updateWindowSize(int width, int height)
{
	windowWidth  = width;
	windowHeight = height;
}

void cXineLib::setDeinterlace(int global, int sd, int hd)
{
	vo_port->set_property(vo_port, VO_PROP_DEINTERLACE_SD, sd);
	vo_port->set_property(vo_port, VO_PROP_DEINTERLACE_HD, hd);
	vo_port->set_property(vo_port, VO_PROP_INTERLACED, global);
}

void cXineLib::setSDfeatures(int sharpness, int noise)
{
	m_sharpness = sharpness;
	m_noise = noise;
}

void cXineLib::setAspectRatio(int ratio)
{
	m_windowAspectRatio = ratio;
}

void cXineLib::setPolicy43(int mode)
{
	m_policy43 = mode;
}

void cXineLib::setPolicy169(int mode)
{
	m_policy169 = mode;
}

void cXineLib::setZoom(int zoom43_x, int zoom43_y, int zoom169_x, int zoom169_y)
{
	m_zoom43_x = zoom43_x;
	m_zoom43_y = zoom43_y;
	m_zoom169_x = zoom169_x;
	m_zoom169_y = zoom169_y;
}

void cXineLib::set_zoom_settings(int x, int y)
{
	xine_set_param(stream, XINE_PARAM_VO_ZOOM_X, x);
	xine_set_param(stream, XINE_PARAM_VO_ZOOM_Y, y);
}

void cXineLib::set_crop_settings(int left, int right, int top, int bottom)
{
	xine_set_param(stream, XINE_PARAM_VO_CROP_LEFT, left);
	xine_set_param(stream, XINE_PARAM_VO_CROP_RIGHT, right);
	xine_set_param(stream, XINE_PARAM_VO_CROP_TOP, top);
	xine_set_param(stream, XINE_PARAM_VO_CROP_BOTTOM, bottom);
}

void cXineLib::adjust_policy()
{
	switch (m_windowAspectRatio) {
	case XINE_VO_ASPECT_AUTO:
		printf("XINE_VO_ASPECT_AUTO !!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
		xine_set_param(stream, XINE_PARAM_VO_ASPECT_RATIO, 0);
		set_zoom_settings(100, 100);
	 	set_crop_settings(0, 0, 0, 0);
		break;
	case XINE_VO_ASPECT_4_3:
		printf("XINE_VO_ASPECT_4_3 !!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
		switch (m_aspect) {
		case 2: // 4:3
			printf("m_policy43 %d\n", m_policy43);
			switch (m_policy43) {
			case 0: // scale
			case 1: // nonlinear
			case 2: // panscan
			case 3: // pillarbox
				printf("4:3 SCALE/NONLINEAR/PANSCAN/PILLARBOX\n");
				xine_set_param(stream, XINE_PARAM_VO_ASPECT_RATIO, XINE_VO_ASPECT_4_3);
				set_zoom_settings(100, 100);
			 	set_crop_settings(0, 0, 0, 0);
				break;
			case 4: // zoom
				printf("4:3 ZOOM\n");
				xine_set_param(stream, XINE_PARAM_VO_ASPECT_RATIO, XINE_VO_ASPECT_4_3);
				set_zoom_settings(m_zoom43_x, m_zoom43_y);
	 			set_crop_settings(0, 0, 0, 0);
				break;
			}
			break;
		case 3: // 16:9
			printf("m_policy169 %d\n", m_policy169);
			switch (m_policy169) {
			case 0: // scale
				printf("16:9 SCALE\n");
				xine_set_param(stream, XINE_PARAM_VO_ASPECT_RATIO, XINE_VO_ASPECT_4_3);
				set_zoom_settings(100, 100);
			 	set_crop_settings(0, 0, 0, 0);
				break;
			case 1: // panscan
				printf("16:9 PANSCAN\n");
				xine_set_param(stream, XINE_PARAM_VO_ASPECT_RATIO, XINE_VO_ASPECT_4_3);
				set_zoom_settings(100, 100);
			 	set_crop_settings(m_width/8, m_width/8, 0, 0);
				break;
			case 2: // letterbox
				printf("16:9 LETTERBOX\n");
				xine_set_param(stream, XINE_PARAM_VO_ASPECT_RATIO, XINE_VO_ASPECT_ANAMORPHIC);
				set_zoom_settings(100, 100);
			 	set_crop_settings(0, 0, 0, 0);
				break;
			case 3: // zoom
				printf("16:9 ZOOM\n");
				xine_set_param(stream, XINE_PARAM_VO_ASPECT_RATIO, XINE_VO_ASPECT_ANAMORPHIC);
				set_zoom_settings(m_zoom169_x, m_zoom169_y);
	 			set_crop_settings(0, 0, 0, 0);
				break;
			}
			break;
		}
		break;
	case XINE_VO_ASPECT_ANAMORPHIC: //16:9
		printf("XINE_VO_ASPECT_ANAMORPHIC (16:9) !!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
		switch (m_aspect) {
		case 2: // 4:3
			switch (m_policy43) {
			case 0: // scale
				printf("4:3 SCALE\n");
				xine_set_param(stream, XINE_PARAM_VO_ASPECT_RATIO, XINE_VO_ASPECT_ANAMORPHIC);
				set_zoom_settings(100, 100);
			 	set_crop_settings(0, 0, 0, 0);
				break;
			case 1: // nonlinear
				printf("4:3 NONLINEAR\n");
				break;
			case 2: // panscan
				printf("4:3 PANSCAN\n");
				xine_set_param(stream, XINE_PARAM_VO_ASPECT_RATIO, XINE_VO_ASPECT_ANAMORPHIC);
				set_zoom_settings(100, 100);
			 	set_crop_settings(0, 0, m_height/8, m_height/8);
				break;
			case 3: // pillarbox
				xine_set_param(stream, XINE_PARAM_VO_ASPECT_RATIO, XINE_VO_ASPECT_4_3);
				set_zoom_settings(100, 100);
				printf("4:3 PILLARBOX\n");
				break;
			case 4: // zoom
				printf("4:3 ZOOM\n");
				xine_set_param(stream, XINE_PARAM_VO_ASPECT_RATIO, XINE_VO_ASPECT_4_3);
				set_zoom_settings(m_zoom43_x, m_zoom43_y);
	 			set_crop_settings(0, 0, 0, 0);
//				detect_aspect_from_frame(true);
				break;
			}
			break;
		case 3: // 16:9
			printf("m_policy169 %d\n", m_policy169);
			switch (m_policy169) {
			case 0: // scale
			case 1: // panscan
			case 2: // letterbox
				printf("16:9 SCALE/PANSCAN/LETTERBOX\n");
				xine_set_param(stream, XINE_PARAM_VO_ASPECT_RATIO, XINE_VO_ASPECT_ANAMORPHIC);
				set_zoom_settings(100, 100);
			 	set_crop_settings(0, 0, 0, 0);
				break;
			case 3: // zoom
				printf("16:9 ZOOM\n");
				xine_set_param(stream, XINE_PARAM_VO_ASPECT_RATIO, XINE_VO_ASPECT_ANAMORPHIC);
				set_zoom_settings(m_zoom169_x, m_zoom169_y);
	 			set_crop_settings(0, 0, 0, 0);
				break;
			}
			break;
		}
		break;
	}

	if (m_width<=720) // SD channels
	{
		vo_port->set_property(vo_port, VO_PROP_SHARPNESS, m_sharpness);
		vo_port->set_property(vo_port, VO_PROP_NOISE_REDUCTION, m_noise);
	}
	else // HD channels
	{
		vo_port->set_property(vo_port, VO_PROP_SHARPNESS, 0);
		vo_port->set_property(vo_port, VO_PROP_NOISE_REDUCTION, 0);
	}
}

