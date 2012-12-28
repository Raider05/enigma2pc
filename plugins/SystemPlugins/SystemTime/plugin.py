# -*- coding: utf-8 -*-
# System Time Plugin for  E2
# Coded by Dima73 (c) 2012
#
# Version: 1.0-rc1 (1.05.2012 21:00)
# Support: Dima73@inbox.lv
#

import enigma
from enigma import *
from Plugins.Plugin import PluginDescriptor
from Tools.Directories import resolveFilename, fileExists, SCOPE_CONFIG, SCOPE_PLUGINS, SCOPE_LANGUAGE
from Components.config import config, ConfigSubsection, ConfigSelection, getConfigListEntry, ConfigYesNo
from Components.ConfigList import ConfigListScreen
from Screens.Screen import Screen
from Screens.InputBox import InputBox
from Components.Input import Input
from Components.GUIComponent import *
from Components.Sources.StaticText import StaticText
from Components.ActionMap import ActionMap, NumberActionMap
from Screens.MessageBox import MessageBox
from Components.Pixmap import Pixmap
from Screens.Console import Console
from Components.Label import Label
from Screens.Standby import TryQuitMainloop
from Components.Language import language
from os import environ
import os
from time import *
import time
import datetime
import gettext

lang = language.getLanguage()
environ["LANGUAGE"] = lang[:2]
gettext.bindtextdomain("enigma2", resolveFilename(SCOPE_LANGUAGE))
gettext.textdomain("enigma2")
gettext.bindtextdomain("SystemTime", "%s%s" % (resolveFilename(SCOPE_PLUGINS), "SystemPlugins/SystemTime/locale/"))

def _(txt):
	t = gettext.dgettext("SystemTime", txt)
	if t == txt:
		t = gettext.gettext(txt)
	return t

PLUGIN_VERSION = _(" ver. 1.0")

config.plugins.SystemTime = ConfigSubsection()
config.plugins.SystemTime.choiceSystemTime = ConfigSelection([("0", _("Transponder Time")),("1", _("Internet Time"))], default="0")
config.plugins.SystemTime.useNTPminutes = ConfigSelection(default = "30", choices = [("5", _("5 min")),("15", _("15 min")),("30", _("30 min")), ("60", _("1 hour")), ("120", _("2 hours")),("240", _("4 hours")), ("720", _("12 hours"))])
config.plugins.SystemTime.syncNTPcoldstart = ConfigYesNo(default = False)
config.plugins.SystemTime.syncNTPtime   = ConfigSelection(choices = [("1", _("Press OK"))], default = "1")
config.plugins.SystemTime.syncDVBtime = ConfigSelection(choices = [("1", _("Press OK"))], default = "1")
config.plugins.SystemTime.syncManually = ConfigSelection(choices = [("1", _("Press OK"))], default = "1")

from NTPSyncPoller import NTPSyncPoller
ntpsyncpoller = None

class SystemTimeSetupScreen(Screen, ConfigListScreen):
	global PLUGIN_VERSION
	skin = """
		<screen position="center,center" size="800,250" title="Setup System Time" >
			<ePixmap position="0,30" zPosition="1" size="200,2" pixmap="/usr/lib/enigma2/python/Plugins/SystemPlugins/SystemTime/images/red.png" alphatest="blend" />
			<widget name="key_red" position="0,0" zPosition="2" size="200,30" font="Regular; 17" halign="center" valign="center" backgroundColor="background" foregroundColor="white" transparent="1" />
			<ePixmap position="200,30" zPosition="1" size="200,2" pixmap="/usr/lib/enigma2/python/Plugins/SystemPlugins/SystemTime/images/green.png" alphatest="blend" />
			<widget name="key_green" position="200,0" zPosition="2" size="200,30" font="Regular; 17" halign="center" valign="center" backgroundColor="background" foregroundColor="white" transparent="1" />
			<ePixmap position="400,30" zPosition="1" size="200,2" pixmap="/usr/lib/enigma2/python/Plugins/SystemPlugins/SystemTime/images/yellow.png" alphatest="blend" />
			<widget name="key_yellow" position="400,0" zPosition="2" size="200,30" font="Regular; 17" halign="center" valign="center" backgroundColor="background" foregroundColor="white" transparent="1" />
			<ePixmap position="600,30" zPosition="1" size="200,2" pixmap="/usr/lib/enigma2/python/Plugins/SystemPlugins/SystemTime/images/blue.png" alphatest="blend" />
			<widget name="key_blue" position="600,0" zPosition="2" size="200,30" font="Regular; 17" halign="center" valign="center" backgroundColor="background" foregroundColor="white" transparent="1" />		
			<widget name="config" position="25,50" size="740,155" scrollbarMode="showOnDemand"/>
			<ePixmap pixmap="skin_default/div-h.png" position="0,210" zPosition="1" size="800,2" />
			<widget source="global.CurrentTime" render="Label" position="250,218" size="430, 21" font="Regular; 20" halign="left" foregroundColor="white" backgroundColor="background" transparent="1">
			<convert type="ClockToText">Date</convert>
			</widget>
			<ePixmap alphatest="on" pixmap="skin_default/icons/clock.png" position="690,215" size="14,14" zPosition="1" />
			<widget font="Regular;19" halign="left" position="710,218" render="Label" size="55,20" source="global.CurrentTime" transparent="1" valign="center" zPosition="1">
			<convert type="ClockToText">Default</convert>
			</widget>
			<widget font="Regular;15" halign="left" position="762,215" render="Label" size="27,17" source="global.CurrentTime" transparent="1" valign="center" zPosition="1">
			<convert type="ClockToText">Format::%S</convert>
			</widget>
		</screen>"""
	def __init__(self, session, servicelist = None, args = None):
		self.servicelist = servicelist
		self.skin = SystemTimeSetupScreen.skin
		self.setup_title = _("Setup System Time") + PLUGIN_VERSION
		Screen.__init__(self, session)
		
		self["key_red"] = Label(_("Cancel"))
		self["key_green"] = Label(_("Save"))
		self["key_yellow"] = Label(_("Restart GUI"))
		self["key_blue"] = Label(" ")

		self["actions"] = ActionMap(["SetupActions", "ColorActions"], 
		{
			"ok": self.keyGo,
			"save": self.keyGreen,
			"cancel": self.cancel,
			"red": self.keyRed,
			"yellow":self.keyYellow,
			"blue": self.keyBlue,
		}, -2)

		ConfigListScreen.__init__(self, [])
		self.initConfig()
		self.createSetup()
		self.onClose.append(self.__closed)
		self.onLayoutFinish.append(self.__layoutFinished)
		self["config"].onSelectionChanged.append(self.configPosition)

	def __closed(self):
		pass

	def __layoutFinished(self):
		self.setTitle(self.setup_title)

	def initConfig(self):
		def getPrevValues(section):
			res = { }
			for (key,val) in section.content.items.items():
				if isinstance(val, ConfigSubsection):
					res[key] = getPrevValues(val)
				else:
					res[key] = val.value
			return res
		
		self.ST = config.plugins.SystemTime
		self.prev_values  = getPrevValues(self.ST)
		self.cfg_choiceSystemTime = getConfigListEntry(_("Sync time using"), config.plugins.SystemTime.choiceSystemTime)
		self.cfg_useNTPminutes = getConfigListEntry(_("Sync NTP every (minutes)"), config.plugins.SystemTime.useNTPminutes)
		self.cfg_syncNTPcoldstart = getConfigListEntry(_("Sync NTP cold start"), config.plugins.SystemTime.syncNTPcoldstart)
		self.cfg_syncNTPtime = getConfigListEntry(_("Sync now internet"), config.plugins.SystemTime.syncNTPtime)
		self.cfg_syncDVBtime = getConfigListEntry(_("Sync now current transponder"), config.plugins.SystemTime.syncDVBtime)
		self.cfg_syncManually = getConfigListEntry(_("Set system time manually"), config.plugins.SystemTime.syncManually)

	def createSetup(self):
		list = [ self.cfg_choiceSystemTime ]
		if self.ST.choiceSystemTime.value == "1":
			list.append(self.cfg_useNTPminutes)
			list.append(self.cfg_syncDVBtime)
		list.append(self.cfg_syncNTPcoldstart)
		list.append(self.cfg_syncNTPtime)
		list.append(self.cfg_syncManually)
		self["config"].list = list
		self["config"].l.setList(list)

	def newConfig(self):
		cur = self["config"].getCurrent()
		if cur in (self.cfg_choiceSystemTime, self.cfg_useNTPminutes):
			self.createSetup()

	def keyGo(self):
		ConfigListScreen.keyOK(self)
		sel = self["config"].getCurrent()[1]
		if sel == self.ST.syncNTPtime:
			if os.path.exists("/usr/sbin/ntpdate"):
				cmd = '/usr/sbin/ntpdate -v -u pool.ntp.org && echo "\n"'
				self.session.open(MyConsole, _("Time sync with NTP..."), [cmd])
			else:
				self.session.open(MessageBox,_("'ntpdate' not installed !"), MessageBox.TYPE_ERROR, timeout = 3 )
		if sel == self.ST.syncDVBtime:
			if os.path.exists("/usr/bin/dvbdate"):
				cmd = '/usr/bin/dvbdate -p -s -f && echo "\n"'
				self.session.open(MyConsole, _("Time sync with DVB..."), [cmd])
			else:
				self.session.open(MessageBox,_("'dvbdate' not installed !"), MessageBox.TYPE_ERROR, timeout = 3 )
		if sel == self.ST.syncManually:
			ChangeTimeWizzard(self.session)
	
	def configPosition(self):
		self["key_blue"].setText("")
		idx = self["config"].getCurrent()[1]
		if idx == self.ST.syncDVBtime:
			self["key_blue"].setText(_("Choice transponder"))

	def keyRed(self):
		def setPrevValues(section, values):
			for (key,val) in section.content.items.items():
				value = values.get(key, None)
				if value is not None:
					if isinstance(val, ConfigSubsection):
						setPrevValues(val, value)
					else:
						val.value = value
		setPrevValues(self.ST, self.prev_values)
		self.ST.save()

	def addNTPcoldstart(self):
		if os.path.exists("/usr/sbin/ntpdate"):
			if not fileExists("/etc/rcS.d/S42ntpdate.sh"):
				os.system("echo -e '#!/bin/sh\n\n[ -x /usr/bin/ntpdate ] && /usr/bin/ntpdate -b -s -u pool.ntp.org\n\nexit 0' >> /etc/rcS.d/S42ntpdate.sh")
			if fileExists("/etc/rcS.d/S42ntpdate.sh"):
				os.chmod("/etc/rcS.d/S42ntpdate.sh", 0755)
		else:
			self.session.open(MessageBox,_("'ntpdate' not installed !"), MessageBox.TYPE_ERROR, timeout = 3 )
	
	def removeNTPcoldstart(self):
		if fileExists("/etc/rcS.d/S42ntpdate.sh"):
			os.system("rm -rf /etc/rcS.d/S42ntpdate.sh")

	def keyGreen(self):
		if self.ST.syncNTPcoldstart.value:
			self.addNTPcoldstart()
		if not self.ST.syncNTPcoldstart.value:
			self.removeNTPcoldstart()
		if self.ST.choiceSystemTime.value == "0":
			enigma.eDVBLocalTimeHandler.getInstance().setUseDVBTime(True)
			config.misc.useTransponderTime.setValue(True)
			config.misc.useTransponderTime.save()
		if self.ST.choiceSystemTime.value == "1":
			enigma.eDVBLocalTimeHandler.getInstance().setUseDVBTime(False)
			config.misc.useTransponderTime.setValue(False)
			config.misc.useTransponderTime.save()
		self.ST.save()
		self.session.openWithCallback(self.restartGuiNow, MessageBox, _("Restart the GUI now?"), MessageBox.TYPE_YESNO, default = False)
		
	def restartGuiNow(self, answer):
		if answer is True:
			self.session.open(TryQuitMainloop, 3)
		else:
			self.close()

	def cancel(self):
		self.keyRed()
		self.close()
				
	def keyYellow(self):
		self.session.openWithCallback(self.restartGui, MessageBox, _("Restart the GUI now?"), MessageBox.TYPE_YESNO)
		
	def restartGui(self, answer):
		if answer is True:
			self.session.open(TryQuitMainloop, 3)
	
	def keyBlue(self):
		if self["key_blue"].getText() == _("Choice transponder"):
			if self.servicelist is None:
				for (dlg,flag) in self.session.dialog_stack:
					if dlg.__class__.__name__ == "InfoBar":
						self.servicelist = dlg.servicelist
						break
			if not self.servicelist is None:
				self.session.execDialog(self.servicelist)

	def keyLeft(self):
		ConfigListScreen.keyLeft(self)
		self.newConfig()

	def keyRight(self):
		ConfigListScreen.keyRight(self)
		self.newConfig()
		
class MyConsole(Console):
	skin = """<screen position="center,center" size="500,240" title="Command execution..." >
			<widget name="text" position="10,10" size="485,230" font="Regular;20" />
		</screen>"""
	def __init__(self, session, title = "My Console...", cmdlist = None):
		Console.__init__(self, session, title, cmdlist)
		
class ChangeTimeWizzard(Screen):
	def __init__(self, session):
		self.session = session
		jetzt = time.time()
		timezone = datetime.datetime.utcnow()
		delta = (jetzt - time.mktime(timezone.timetuple())) 
		self.oldtime = strftime("%Y:%m:%d %H:%M",localtime())
		self.session.openWithCallback(self.askForNewTime,InputBox, title=_("Please Enter new System time and press OK !"), text="%s" % (self.oldtime), maxSize=16, type=Input.NUMBER)

	def askForNewTime(self,newclock):
		try:
			length=len(newclock)
		except:
			length=0
		if newclock is None:
			self.skipChangeTime(_("no new time"))
		elif (length == 16) is False:
			self.skipChangeTime(_("new time string too short"))
		elif (newclock.count(" ") < 1) is True:
			self.skipChangeTime(_("invalid format"))
		elif (newclock.count(":") < 3) is True:
			self.skipChangeTime(_("invalid format"))
		else:
			full=[]
			full=newclock.split(" ",1)
			newdate=full[0]
			newtime=full[1]
			parts=[]
			parts=newdate.split(":",2)
			newyear=parts[0]
			newmonth=parts[1]
			newday=parts[2]
			parts=newtime.split(":",1)
			newhour=parts[0]
			newmin=parts[1]
			maxmonth = 31
			if (int(newmonth) == 4) or (int(newmonth) == 6) or (int(newmonth) == 9) or (int(newmonth) == 11) is True:
				maxmonth=30
			elif (int(newmonth) == 2) is True:
				if ((4*int(int(newyear)/4)) == int(newyear)) is True:
					maxmonth=28
				else:
					maxmonth=27
			if (int(newyear) < 2007) or (int(newyear) > 2027)  or (len(newyear) < 4) is True:
				self.skipChangeTime(_("invalid year %s") %newyear)
			elif (int(newmonth) < 0) or (int(newmonth) >12) or (len(newmonth) < 2) is True:
				self.skipChangeTime(_("invalid month %s") %newmonth)
			elif (int(newday) < 1) or (int(newday) > maxmonth) or (len(newday) < 2) is True:
				self.skipChangeTime(_("invalid day %s") %newday)
			elif (int(newhour) < 0) or (int(newhour) > 23) or (len(newhour) < 2) is True:
				self.skipChangeTime(_("invalid hour %s") %newhour)
			elif (int(newmin) < 0) or (int(newmin) > 59) or (len(newmin) < 2) is True:
				self.skipChangeTime(_("invalid minute %s") %newmin)
			else:
				self.newtime = "%s%s%s%s%s" %(newmonth,newday,newhour,newmin,newyear)
				self.session.openWithCallback(self.DoChangeTimeRestart,MessageBox,_("Apply the new System time?"), MessageBox.TYPE_YESNO)

	def DoChangeTimeRestart(self,answer):
		if answer is None:
			self.skipChangeTime(_("answer is None"))
		if answer is False:
			self.skipChangeTime(_("you were not confirming"))
		else:
			os.system("date %s" % (self.newtime))

	def skipChangeTime(self,reason):
		self.session.open(MessageBox,(_("Change system time was canceled, because %s") % reason), MessageBox.TYPE_WARNING)	

def startup(session=None, **kwargs):
	global ntpsyncpoller
	ntpsyncpoller = NTPSyncPoller()
	ntpsyncpoller.start()
		
def main(menuid, **kwargs):
	if menuid == "system":
		return [(_("System Time"), OpenSetup, "system_time_setup", None)]
	else:
		return []

def OpenSetup(session, **kwargs):
	servicelist = kwargs.get('servicelist', None)
	session.open(SystemTimeSetupScreen, servicelist)

def Plugins(**kwargs):
	return [PluginDescriptor(name = "System Time", description = _("System time your box"), where = PluginDescriptor.WHERE_MENU, fnc = main),
					PluginDescriptor(name = "System Time", description = "", where = PluginDescriptor.WHERE_SESSIONSTART, fnc = startup)]
