#ifndef __lib_gdi_gxlibdc_h
#define __lib_gdi_gxlibdc_h

#include <lib/base/thread.h>
#include <lib/gdi/gmaindc.h>
#include <lib/dvb/idvb.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <lib/gdi/xineLib.h>

#define INPUT_MOTION (ExposureMask | KeyPressMask | KeyReleaseMask | StructureNotifyMask)

enum
{
	_NET_WM_STATE_REMOVE =0,
	_NET_WM_STATE_ADD = 1,
	_NET_WM_STATE_TOGGLE =2
};

class gXlibDC: public gMainDC, public eThread, public Object
{
private:
	static gXlibDC       *instance;

	int                  screen;
	x11_visual_t         vis;
	int                  fullscreen;
	int                  windowWidth, windowHeight;
	int                  initialWindowWidth, initialWindowHeight;
	cXineLib            *xineLib;
	gSurface             m_surface;
	uint32_t            *argb_buffer;
	bool                 thread_stop;

	Atom                 wmDelete;

	void exec(const gOpcode *opcode);

	eFixedMessagePump<XKeyEvent> m_pump;
	void keyEvent(const XKeyEvent &event);
	void pumpEvent(const XKeyEvent &event);
	virtual void thread();

	enum event {
		EV_SET_VIDEO_MODE,
		EV_FLIP,
		EV_QUIT,
	};

	//void pushEvent(enum event code, void *data1 = 0, void *data2 = 0);
	void evFlip();
	void fullscreen_switch();
	void updateWindowState();

public:
	static int            width, height;
	static double         pixel_aspect;
	static Display       *display;
	static Window         window;
	static int            xpos, ypos;

	gXlibDC();
	virtual ~gXlibDC();

	static gXlibDC *getInstance() { return instance; }
	void setResolution(int xres, int yres);
	int islocked() { return 0; }

	static void frame_output_cb(void *data, int video_width, int video_height, double video_pixel_aspect,
			int *dest_x, int *dest_y, int *dest_width, int *dest_height, double *dest_pixel_aspect,
			int *win_x, int *win_y);
	static void dest_size_cb(void *data, int video_width, int video_height, double video_pixel_aspect,
			 int *dest_width, int *dest_height, double *dest_pixel_aspect);

};

#endif
