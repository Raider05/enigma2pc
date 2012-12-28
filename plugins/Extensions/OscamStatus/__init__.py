# -*- coding: utf-8 -*-
#===============================================================================
# OscamStatus Plugin by puhvogel 2011-2012
#
# This is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2, or (at your option) any later
# version.
#===============================================================================

from Components.Language import language
from Tools.Directories import resolveFilename, SCOPE_PLUGINS, SCOPE_LANGUAGE
from os import environ as os_environ
import gettext

def localeInit():
	lang = language.getLanguage()[:2] # getLanguage returns e.g. "fi_FI" for "language_country"
	os_environ["LANGUAGE"] = lang # Enigma doesn't set this (or LC_ALL, LC_MESSAGES, LANG). gettext needs it!
	gettext.bindtextdomain("OscamStatus", resolveFilename(SCOPE_PLUGINS, "Extensions/OscamStatus/locale"))

def _(txt):
	t = gettext.dgettext("OscamStatus", txt)
	if t == txt:
		print "[OscamStatus] fallback to default translation for", txt
		t = gettext.gettext(txt)
	return t

localeInit()
language.addCallback(localeInit)
