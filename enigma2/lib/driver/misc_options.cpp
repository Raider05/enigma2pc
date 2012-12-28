#include <cstring>
#include <lib/driver/misc_options.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <lib/base/init.h>
#include <lib/base/init_num.h>
#include <lib/base/eerror.h>
#include <lib/base/eenv.h>

Misc_Options *Misc_Options::instance = 0;

Misc_Options::Misc_Options()
	:m_12V_output_state(-1)
{
	ASSERT(!instance);
	instance = this;
}

int Misc_Options::set_12V_output(int state)
{
	if (state == m_12V_output_state)
		return 0;
	int fd = open(eEnv::resolve("${sysconfdir}/stb/misc/12V_output").c_str(), O_WRONLY);
	if (fd < 0)
	{
		std::string err= "couldn't open " + eEnv::resolve("${sysconfdir}/stb/misc/12V_output");
		eDebug(err.c_str());
		return -1;
	}
	const char *str=0;
	if (state == 0)
		str = "off";
	else if (state == 1)
		str = "on";
	if (str)
		write(fd, str, strlen(str));
	m_12V_output_state = state;
	close(fd);
	return 0;
}

bool Misc_Options::detected_12V_output()
{
	int fd = open(eEnv::resolve("${sysconfdir}/stb/misc/12V_output").c_str(), O_WRONLY);
	if (fd < 0)
	{
		std::string err= "couldn't open " + eEnv::resolve("${sysconfdir}/stb/misc/12V_output");
		eDebug(err.c_str());
		return false;
	}
	close(fd);
	return true;
}

Misc_Options *Misc_Options::getInstance()
{
	return instance;
}

//FIXME: correct "run/startlevel"
eAutoInitP0<Misc_Options> init_misc_options(eAutoInitNumbers::rc, "misc options");
