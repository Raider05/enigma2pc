#include <lib/gui/evideo.h>
#include <lib/gui/ewidgetdesktop.h>
#include <lib/gdi/xineLib.h>

ePtr<eTimer> fullsizeTimer;
//int eVideoWidget::pendingFullsize = 0;
static int pendingFullsize;

void setFullsize()
{
	for (int decoder=0; decoder < 1; ++decoder)
	{
		if (pendingFullsize & (1 << decoder))
		{
			cXineLib* xineLib = cXineLib::getInstance();
			xineLib->setVideoWindow(0, 0, 0, 0);
			pendingFullsize &= ~(1 << decoder);
		}
	}
}


eVideoWidget::eVideoWidget(eWidget *parent)
	:eLabel(parent), m_fb_size(720, 576), m_state(0), m_decoder(1)
{
	if (!fullsizeTimer)
	{
		fullsizeTimer = eTimer::create(eApp);
		fullsizeTimer->timeout.connect(slot(setFullsize));
	}
	parent->setPositionNotifyChild(1);
}

int eVideoWidget::event(int event, void *data, void *data2)
{
	switch (event)
	{
	case evtChangedPosition:
	case evtParentChangedPosition:
		m_state &= ~1;
		updatePosition(!isVisible());
		break;
	case evtChangedSize:
		m_state |= 2;
		updatePosition(!isVisible());
		break;
	case evtParentVisibilityChanged:
		updatePosition(!isVisible());
		break;
	}
	return eLabel::event(event, data, data2);
}

eVideoWidget::~eVideoWidget()
{
	updatePosition(1);
}

void eVideoWidget::setFBSize(eSize size)
{
	m_fb_size = size;
}

void eVideoWidget::writeProc(const std::string &filename, int value)
{
	FILE *f = fopen(filename.c_str(), "w");
	if (f)
	{
		fprintf(f, "%08x\n", value);
		fclose(f);
	}
}

void eVideoWidget::setPosition(int index, int left, int top, int width, int height)
{
	char filenamebase[128];
	snprintf(filenamebase, sizeof(filenamebase), "/proc/stb/vmpeg/%d/dst_", index);
	std::string filename = filenamebase;
	writeProc(filename + "left", left);
	writeProc(filename + "top", top);
	writeProc(filename + "width", width);
	writeProc(filename + "height", height);
	writeProc(filename + "apply", 1);
}


void eVideoWidget::updatePosition(int disable)
{
	if (!disable)
		m_state |= 4;

	if (disable && !(m_state & 4))
	{
//		eDebug("was not visible!");
		return;
	}

	if ((m_state & 2) != 2)
	{
//		eDebug("no size!");
		return;
	}

//	eDebug("position %d %d -> %d %d", position().x(), position().y(), size().width(), size().height());

	eRect pos(0,0,0,0);
	if (!disable)
		pos = eRect(getAbsolutePosition(), size());
	else
		m_state &= ~4;

//	eDebug("abs position %d %d -> %d %d", pos.left(), pos.top(), pos.width(), pos.height());

	if (!disable && m_state & 8 && pos == m_user_rect)
	{
//		eDebug("matched");
		return;
	}

	if (!(m_state & 1))
	{
		m_user_rect = pos;
		m_state |= 1;
//		eDebug("set user rect pos!");
	}

//	eDebug("m_user_rect %d %d -> %d %d", m_user_rect.left(), m_user_rect.top(), m_user_rect.width(), m_user_rect.height());

	if (!disable)
	{
		cXineLib* xineLib = cXineLib::getInstance();
		xineLib->setVideoWindow(pos.left(), pos.top(), pos.width(), pos.height());
		pendingFullsize &= ~(1 << m_decoder);
		m_state |= 8;
	}
	else
	{
		m_state &= ~8;
		pendingFullsize |= (1 << m_decoder);
		fullsizeTimer->start(100, true);
	}

}


void eVideoWidget::setDecoder(int decoder)
{
	m_decoder = decoder;
}
