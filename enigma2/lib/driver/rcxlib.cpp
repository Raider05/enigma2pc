#include <lib/driver/rcxlib.h>
#include <lib/base/init.h>
#include <lib/base/init_num.h>
#include <lib/driver/input_fake.h>

/*
 * eXlibInputDevice
 */

eXlibInputDevice::eXlibInputDevice(eRCDriver *driver) : eRCDevice("Xlib", driver), m_escape(false), m_unicode(0)
{
}

eXlibInputDevice::~eXlibInputDevice()
{
}

void eXlibInputDevice::handleCode(long arg)
{
	const XKeyEvent* event = (const XKeyEvent*)arg;
	int km = input->getKeyboardMode();
	int code, flags;

	if (event->type == KeyPress) {
		//m_unicode = key->unicode;
		flags = eRCKey::flagMake;
	} else {
		flags = eRCKey::flagBreak;
	}

	if (1/*km == eRCInput::kmNone*/) {
		//eDebug("eRCInput::kmNone\n");
		code = translateKey(event->keycode);
	} else {
		eDebug("eRCInput::kmNone NO\n");
		// ASCII keys should only generate key press events
		if (flags == eRCKey::flagBreak)
			return;

		/*eDebug("unicode=%04x scancode=%02x", m_unicode, key->scancode);
		if (m_unicode & 0xff80) {
			eDebug("SDL: skipping unicode character");
			return;
		}
		code = m_unicode & ~0xff80;
		// unicode not set...!? use key symbol
		if (code == 0) {
			// keysym is ascii
			if (key >= 128) {
				eDebug("SDL: cannot emulate ASCII");
				return;
			}
			eDebug("SDL: emulate ASCII");
			code = key;
		}
		if (km == eRCInput::kmAscii) {
			// skip ESC c or ESC '[' c
			if (m_escape) {
				if (code != '[')
					m_escape = false;
				return;
			}

			if (code == SDLK_ESCAPE)
				m_escape = true;

			if ((code < SDLK_SPACE) ||
			    (code == 0x7e) ||	// really?
			    (code == SDLK_DELETE))
				return;
		}*/
		flags |= eRCKey::flagAscii;
	}

	//eDebug("SDL code=%d flags=%d", code, flags);
	input->keyPressed(eRCKey(this, code, flags));
}

const char *eXlibInputDevice::getDescription() const
{
	return "Xlib";
}

int eXlibInputDevice::translateKey(int key)
{

	switch (key) {
	case 9:
		return KEY_ESC;


	case 10:
		return KEY_1;
	case 11:
		return KEY_2;
	case 12:
		return KEY_3;
	case 13:
		return KEY_4;
	case 14:
		return KEY_5;
	case 15:
		return KEY_6;
	case 16:
		return KEY_7;
	case 17:
		return KEY_8;
	case 18:
		return KEY_9;
	case 19:
		return KEY_0;

	case 26: // E
		return KEY_EPG;

	case 27: // R
		return KEY_RECORD;

	case 28: // T
		return KEY_TV;

	case 31: // I
		return KEY_INFO;

	case 33: // P
		return KEY_PLAYPAUSE;

	case 36:
		return KEY_OK;

	case 38: // A
		return KEY_AUDIO;

	case 40: // D
		return KEY_RADIO;

	case 55: // V
		return KEY_VIDEO;

	case 111:
		return KEY_UP;
	case 113:
		return KEY_LEFT;
	case 114:
		return KEY_RIGHT;
	case 116:
		return KEY_DOWN;

	case 58: // M
		return KEY_MUTE;

	case 65: // SPACE
		return KEY_MENU;


	case 67:
		return KEY_RED;
	case 68:
		return KEY_GREEN;
	case 69:
		return KEY_YELLOW;
	case 70:
		return KEY_BLUE;

	case	71: //F5
		return KEY_INFO;
	
	case	72: //F6
		return KEY_EPG;

	case 82:
	case 61: // -
		return KEY_VOLUMEDOWN;

	case 86:
	case 35: // +
		return KEY_VOLUMEUP;

	case 112: // Page Up
		return KEY_CHANNELUP;
	case 117: // Page Down
		return KEY_CHANNELDOWN;
	
	case	76: //F10
		return KEY_POWER;
	
	default:
		eDebug("unhandled KEYBOARD keycode: %d", key);
		return KEY_RESERVED;
	}

}

/*
 * eXlibInputDriver
 */

eXlibInputDriver *eXlibInputDriver::instance;

eXlibInputDriver::eXlibInputDriver() : eRCDriver(eRCInput::getInstance())
{
	ASSERT(instance == 0);
	instance = this;
}

eXlibInputDriver::~eXlibInputDriver()
{
	instance = 0;
}

void eXlibInputDriver::keyPressed(const XKeyEvent &keyEvent)
{
	/*eDebug("km=%d enabled=%d locked=%d",
		input->getKeyboardMode(), enabled, input->islocked());*/

	if (!enabled || input->islocked())
		return;

	std::list<eRCDevice*>::iterator i(listeners.begin());
	while (i != listeners.end()) {
		(*i)->handleCode((long)&keyEvent);
		++i;
	}
}

class eRCXlibInit
{
private:
	eXlibInputDriver driver;
	eXlibInputDevice device;

public:
	eRCXlibInit(): driver(), device(&driver)
	{
	}
};

eAutoInitP0<eRCXlibInit> init_rcXlib(eAutoInitNumbers::rc+1, "Xlib RC Driver");
