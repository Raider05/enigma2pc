#include <fstream>
#include <lib/gdi/gxlibdc.h>
#include <lib/actions/action.h>
#include <lib/base/init.h>
#include <lib/base/init_num.h>
#include <lib/base/eenv.h>
#include <lib/driver/input_fake.h>
#include <lib/driver/rcxlib.h>

gXlibDC   *gXlibDC::instance;
Display   *gXlibDC::display;
Window    gXlibDC::window;
int       gXlibDC::width, gXlibDC::height;
double    gXlibDC::pixel_aspect;
int       gXlibDC::xpos, gXlibDC::ypos;

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

static bool getConfigBool(const std::string &key, bool defaultValue)
{
	std::string value = getConfigString(key, defaultValue ? "true" : "false");
	const char *cvalue = value.c_str();

	if (!strcasecmp(cvalue, "true"))
		return true;
	if (!strcasecmp(cvalue, "false"))
		return false;

	return defaultValue;
}

static int getConfigInt(const std::string &key)
{
  std::string value = getConfigString(key, "0");
	return atoi(value.c_str());
}

gXlibDC::gXlibDC() : m_pump(eApp, 1)
{
	double      res_h, res_v;
	
	umask(0);
	mknod("/tmp/ENIGMA_FIFO", S_IFIFO|0666, 0);

	CONNECT(m_pump.recv_msg, gXlibDC::pumpEvent);

	argb_buffer = NULL;
	fullscreen = getConfigBool("config.pc.default_fullscreen", false);
	initialWindowWidth  = getConfigInt("config.pc.initial_window_width");
	initialWindowHeight = getConfigInt("config.pc.initial_window_height");
	windowWidth  = 720;
	windowHeight = 576;
	xpos = 0;
	ypos = 0;

	ASSERT(instance == 0);
	instance = this;
	
	if(!XInitThreads())
	{
		eFatal("XInitThreads() failed\n");
		return;
	}

	if((display = XOpenDisplay(getenv("DISPLAY"))) == NULL) {
		eFatal("XOpenDisplay() failed.\n");
		return;
	}

	screen       = XDefaultScreen(display);

	if (fullscreen)	{
		width  = DisplayWidth( display, screen );
		height = DisplayHeight( display, screen );
	} else {
		width  = windowWidth;
		height = windowHeight;
	}

	XLockDisplay(display);
	if (initialWindowWidth && initialWindowHeight)
 	  window = XCreateSimpleWindow(display, XDefaultRootWindow(display), xpos, ypos, initialWindowWidth, initialWindowHeight, 0, 0, 0);
	else
 	  window = XCreateSimpleWindow(display, XDefaultRootWindow(display), xpos, ypos, windowWidth, windowHeight, 0, 0, 0);
	XSelectInput (display, window, INPUT_MOTION);
	XMapRaised(display, window);
	res_h = (DisplayWidth(display, screen) * 1000 / DisplayWidthMM(display, screen));
	res_v = (DisplayHeight(display, screen) * 1000 / DisplayHeightMM(display, screen));
	XSync(display, False);
	XUnlockDisplay(display);

	wmDelete=XInternAtom(display, "WM_DELETE_WINDOW", True);
	XSetWMProtocols(display, window, &wmDelete, 1);

	printf("Display resolution %d %d\n", DisplayWidth(display, screen), DisplayHeight(display, screen));

	vis.display           = display;
	vis.screen            = screen;
	vis.d                 = window;
	vis.dest_size_cb      = dest_size_cb;
	vis.frame_output_cb   = frame_output_cb;
	vis.user_data         = NULL;
	pixel_aspect          = res_v / res_h;

	if(fabs(pixel_aspect - 1.0) < 0.01)
		pixel_aspect = 1.0;

	xineLib = new cXineLib(&vis);

	setResolution(windowWidth, windowHeight); // default res

	if (fullscreen)
	{
		fullscreen=false;
		fullscreen_switch();
	}

	run();

	/*m_surface.type = 0;
	m_surface.clut.colors = 256;
	m_surface.clut.data = new gRGB[m_surface.clut.colors];

	m_pixmap = new gPixmap(&m_surface);

	memset(m_surface.clut.data, 0, sizeof(*m_surface.clut.data)*m_surface.clut.colors);*/
}

gXlibDC::~gXlibDC()
{
	instance = 0;

	thread_stop = true;
	kill();

	if (xineLib) {
		delete xineLib;
		xineLib = NULL;
	}

	XLockDisplay(display);
	XUnmapWindow(display, window);
	XDestroyWindow(display, window);
	XUnlockDisplay(display);

	XCloseDisplay (display);
}

void gXlibDC::keyEvent(const XKeyEvent &event)
{
	eXlibInputDriver *driver = eXlibInputDriver::getInstance();
	xineLib->getVideoFrameRate();
	eDebug("SDL Key %s: key=%d", (event.type == KeyPress) ? "Down" : "Up", event.keycode);

	if (driver)
		driver->keyPressed(event);
}

void gXlibDC::pumpEvent(const XKeyEvent &event)
{
	switch (event.type) {
	case KeyPress:
	case KeyRelease:
		switch (event.keycode) {
		case 95: // F11
			if (event.type==KeyPress) {
				fullscreen_switch();
			}
			break;
		case 53: // X
			if (event.type==KeyPress) {
				eDebug("Enigma2 Quit");
				extern void quitMainloop(int exit_code);
				quitMainloop(0);
			}
			break;
		default:
			keyEvent(event);
			break;
		}
		break;
	}
}

/*void gSDLDC::pushEvent(enum event code, void *data1, void *data2)
{
	SDL_Event event;

	event.type = SDL_USEREVENT;
	event.user.code = code;
	event.user.data1 = data1;
	event.user.data2 = data2;

	SDL_PushEvent(&event);
}*/

void gXlibDC::exec(const gOpcode *o)
{
	switch (o->opcode)
	{
	case gOpcode::flush:
		eDebug("FLUSH");
		xineLib->showOsd();
		break;
	default:
		gDC::exec(o);
		break;
	}
}

void gXlibDC::setResolution(int xres, int yres)
{
	printf("setResolution %d %d\n", xres, yres);
	windowWidth  = xres;
	windowHeight = yres;

	if (!fullscreen) {
		width = xres;
		height = yres;
	}

	if (argb_buffer)
		delete [] argb_buffer;
	argb_buffer = new uint32_t[windowWidth*windowHeight];
	memset(argb_buffer, 0, windowWidth * windowHeight * sizeof(uint32_t));

	xineLib->newOsd(windowWidth, windowHeight, argb_buffer);

//	m_surface.type = 0;
	m_surface.x = windowWidth;
	m_surface.y = windowHeight;
	m_surface.bpp = 32;
	m_surface.bypp = 4;
	m_surface.stride = windowWidth*4;
	m_surface.data = argb_buffer;
//	m_surface.offset = 0;

	m_pixmap = new gPixmap(&m_surface);

	if (initialWindowWidth == 0 || initialWindowHeight == 0)
		XResizeWindow(display, window, windowWidth, windowHeight);
	updateWindowState();
}

void gXlibDC::updateWindowState() {
	if (fullscreen)	{
		width  = DisplayWidth( display, screen );
		height = DisplayHeight( display, screen );
	} else if (initialWindowWidth && initialWindowHeight)
	{
		width  = initialWindowWidth;
		height = initialWindowHeight;
	} else
	{
		width  = windowWidth;
		height = windowHeight;
	}

	XFlush(display);
	xineLib->updateWindowSize(width, height);
	xineLib->showOsd();
}

void gXlibDC::fullscreen_switch() {
	printf("FULLSCREEN EVENT\n");
	fullscreen ^= 1;
	
	XEvent xev;
	XLockDisplay(display);
	Atom wm_state = XInternAtom(display, "_NET_WM_STATE", False);
	Atom fullscreenAtom = XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", False);
	XUnlockDisplay(display);
	memset(&xev, 0, sizeof(xev));
	xev.type = ClientMessage;
	xev.xclient.window = window;
	xev.xclient.message_type = wm_state;
	xev.xclient.format = 32;
	xev.xclient.data.l[0] = fullscreen ? _NET_WM_STATE_ADD : _NET_WM_STATE_REMOVE;
	xev.xclient.data.l[1] = fullscreenAtom;
	xev.xclient.data.l[2] = 0;
	XSendEvent(display, XDefaultRootWindow(display), False, SubstructureNotifyMask, &xev);
	
	updateWindowState();
}

void gXlibDC::evFlip()
{
	//SDL_Flip(m_screen);
}

void gXlibDC::thread()
{
	hasStarted();

	int x11_fd = ConnectionNumber(display);
	thread_stop = false;
	fd_set in_fds;
	struct timeval tv;
	XEvent event;

	while (!thread_stop) {
		FD_ZERO(&in_fds);
		FD_SET(x11_fd, &in_fds);

		tv.tv_usec = 100000;
		tv.tv_sec = 0;

		if (select(x11_fd+1, &in_fds, 0, 0, &tv))
			printf("Event Received!\n");

		while(XPending(display))
		{
			XNextEvent(display, &event);
			printf("XNextEvent %d\n", event.type);
			switch(event.type)
			{
			case KeyPress:
			case KeyRelease:
				{
					XKeyEvent& xKeyEvent = (XKeyEvent&)event;
					m_pump.send(xKeyEvent);
				}
				break;
			case ClientMessage:
				if (event.xclient.data.l[0] == wmDelete) {
					thread_stop = true;
					XKeyEvent xKeyEvent;
					xKeyEvent.type = KeyPress;
					xKeyEvent.keycode = 53; // X
					m_pump.send(xKeyEvent);
				}
				break;
			case Expose:
			    if(event.xexpose.count != 0)
				break;
			    xineLib->showOsd();
			    break;
			case ConfigureNotify:
			    {
				   XConfigureEvent& cne = (XConfigureEvent&)event;
				   Window           tmp_win;
				   
				   if((cne.x == 0) && (cne.y == 0)) {
			                 XLockDisplay(display);
	        		         XTranslateCoordinates(display, cne.window, DefaultRootWindow(cne.display),
						                           0, 0, &xpos, &ypos, &tmp_win);
			                 XUnlockDisplay(display);
	        	           }
				   else {
					xpos = cne.x;
					ypos = cne.y;
				   }
		           	   if (!fullscreen){
					 if (cne.width != windowWidth || cne.height != windowHeight)
					 {
					   windowWidth  = cne.width;
					   windowHeight = cne.height;
					   updateWindowState();
					 }  
				   }
			     }
			     break;				
			}				
		}
	}
}

void gXlibDC::frame_output_cb(void *data, int video_width, int video_height, double video_pixel_aspect,
		int *dest_x, int *dest_y, int *dest_width, int *dest_height, double *dest_pixel_aspect,
		int *win_x, int *win_y)
{
	*dest_x            = 0;
	*dest_y            = 0;
	*win_x             = xpos;
	*win_y             = ypos;
	*dest_width        = gXlibDC::width;
	*dest_height       = gXlibDC::height;
	*dest_pixel_aspect = gXlibDC::pixel_aspect;
}

void gXlibDC::dest_size_cb(void *data, int video_width, int video_height, double video_pixel_aspect,
			 int *dest_width, int *dest_height, double *dest_pixel_aspect)
{
	*dest_width        = gXlibDC::width;
	*dest_height       = gXlibDC::height;
	*dest_pixel_aspect = gXlibDC::pixel_aspect;
}

eAutoInitPtr<gXlibDC> init_gXlibDC(eAutoInitNumbers::graphic-1, "gXlibDC");
