#ifndef __lib_driver_rcxlib_h
#define __lib_driver_rcxlib_h

#include <lib/driver/rc.h>
#include <X11/Xlib.h>

class eXlibInputDevice : public eRCDevice
{
private:
	bool m_escape;
	unsigned int m_unicode;
	int translateKey(int key);

public:
	eXlibInputDevice(eRCDriver *driver);
	~eXlibInputDevice();

	virtual void handleCode(long arg);
	virtual const char *getDescription() const;
};

class eXlibInputDriver : public eRCDriver
{
private:
	static eXlibInputDriver *instance;

public:
	eXlibInputDriver();
	~eXlibInputDriver();

	static eXlibInputDriver *getInstance() { return instance; }
	void keyPressed(const XKeyEvent &keyEvent);
};

#endif
