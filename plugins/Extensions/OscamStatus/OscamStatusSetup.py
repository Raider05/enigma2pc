# -*- coding: utf-8 -*-
#===============================================================================
# V0.29
# This is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2, or (at your option) any later
# version.
#===============================================================================

from __init__ import _
from Screens.Screen import Screen
from Screens.MessageBox import MessageBox

from Components.ActionMap import ActionMap
from Components.Button import Button
from Components.MenuList import MenuList
from Components.ConfigList import ConfigListScreen
from Components.Sources.List import List
from Components.Sources.StaticText import StaticText

from Components.config import config
from Components.config import NoSave
from Components.config import ConfigIP
from Components.config import ConfigText
from Components.config import ConfigYesNo
from Components.config import ConfigInteger
from Components.config import ConfigPassword
from Components.config import ConfigSubsection
from Components.config import getConfigListEntry

from enigma import eListboxPythonMultiContent, eListbox, getDesktop, gFont, RT_HALIGN_LEFT, RT_VALIGN_CENTER, ePoint
from Tools.LoadPixmap import LoadPixmap
from Tools.Directories import resolveFilename, SCOPE_CURRENT_PLUGIN, SCOPE_SKIN

config.plugins.OscamStatus  = ConfigSubsection()
config.plugins.OscamStatus.lastServer = ConfigInteger(default = 0)
config.plugins.OscamStatus.extMenu = ConfigYesNo(default = True)
config.plugins.OscamStatus.xOffset = ConfigInteger(default = 50, limits=(0,100))
config.plugins.OscamStatus.useECM = ConfigYesNo(default = False)
config.plugins.OscamStatus.useIP = ConfigYesNo(default = True)
config.plugins.OscamStatus.usePicons = ConfigYesNo(default = False)
config.plugins.OscamStatus.PiconPath = ConfigText(default = resolveFilename(SCOPE_SKIN,"picon_50x30"), fixed_size = False, visible_width=40)

# export Variablen...
LASTSERVER = config.plugins.OscamStatus.lastServer
EXTMENU = config.plugins.OscamStatus.extMenu
XOFFSET = config.plugins.OscamStatus.xOffset
USEECM = config.plugins.OscamStatus.useECM
USEPICONS = config.plugins.OscamStatus.usePicons
PICONPATH = config.plugins.OscamStatus.PiconPath
picons = None


def dlg_xh(w):
	x = getDesktop(0).size().width() - w - XOFFSET.value
	if x < 0: x = 0
	h = getDesktop(0).size().height()
	return x, h

class globalsConfigScreen(Screen, ConfigListScreen):
	skin = """
		<screen flags="wfNoBorder" position="%d,0" size="440,%d" name="globalsConfigScreen" >
			<widget render="Label" source="title" position="20,80" size="400,26" valign="center" zPosition="5" transparent="0" foregroundColor="#fcc000" font="Regular;22"/>
			<widget name="config" position="20,130" size="400,200" scrollbarMode="showOnDemand" />
			<eLabel text="" position="20,450" size="400,2" transparent="0" backgroundColor="#ffffff" />
			<ePixmap name="ButtonRed" pixmap="skin_default/buttons/red.png" position="20,460" size="140,40" zPosition="4" transparent="1" alphatest="on"/>
			<widget render="Label" source= "ButtonRedtext" position="20,460" size="140,40" valign="center" halign="center" zPosition="5" transparent="1" foregroundColor="white" font="Regular;18"/>
			<ePixmap name="ButtonGreen" pixmap="skin_default/buttons/green.png" position="160,460" size="140,40" zPosition="4" transparent="1" alphatest="on"/>
			<widget render="Label" source= "ButtonGreentext" position="160,460" size="140,40" valign="center" halign="center" zPosition="5" transparent="1" foregroundColor="white" font="Regular;18"/>
		</screen>""" % (dlg_xh(440))

	def __init__(self, session):
		self.skin = globalsConfigScreen.skin
		self.session = session
		Screen.__init__(self, session)

		list = []
		list.append(getConfigListEntry(_("Show Plugin in Extensions Menu"), config.plugins.OscamStatus.extMenu))
		list.append(getConfigListEntry(_("X-Offset (move left)"), config.plugins.OscamStatus.xOffset))
		list.append(getConfigListEntry(_("ECM Time in \"connected\" Dialog"), config.plugins.OscamStatus.useECM))
		list.append(getConfigListEntry(_("Server address always in IP Format"), config.plugins.OscamStatus.useIP))
		list.append(getConfigListEntry(_("Use Picons"), config.plugins.OscamStatus.usePicons))
		list.append(getConfigListEntry(_("Picons Path"), config.plugins.OscamStatus.PiconPath))
		ConfigListScreen.__init__(self, list, session = session)

		self["title"] = StaticText(_("Oscam Status globals Setup"))
		self["ButtonRedtext"] = StaticText(_("return"))
		self["ButtonGreentext"] = StaticText(_("save"))
		self["actions"] = ActionMap(["OkCancelActions", "ColorActions"],
		{
			"red": self.Exit,
			"green": self.Save,
			"cancel": self.Exit
		}, -1)
		self.onLayoutFinish.append(self.LayoutFinished)

	def LayoutFinished(self):
		x,h = dlg_xh(self.instance.size().width())
		self.instance.move(ePoint(x, 0))

	def Save(self):
		for x in self["config"].list:
			x[1].save()
		self.close()

	def Exit(self):
		for x in self["config"].list:
			x[1].cancel()
		self.close()

class oscamServer:
	serverName = "localhost"
	serverIP   = "127.0.0.1"
	serverPort = "16001"
	username   = "username"
	password   = "password"
	useSSL     = False

CFG = resolveFilename(SCOPE_CURRENT_PLUGIN, "Extensions/OscamStatus/OscamStatus.cfg")

def readCFG():
	cfg = None
	oscamServers = []
	try:
		cfg = file(CFG, "r")
	except:
		pass
	if cfg:
		print "[OscamStatus] reading config file..."
		d = cfg.read()
		cfg.close()
		for line in d.splitlines():
			v = line.strip().split(' ')
			if len(v) == 6:
				tmp = oscamServer()
				tmp.username = v[0]
				tmp.password = v[1]
				tmp.serverIP = v[2]
				tmp.serverPort = v[3]
				tmp.serverName = v[4]
				tmp.useSSL = bool(int(v[5]))
				oscamServers.append(tmp)
	if len(oscamServers) == 0:
		print "[OscamStatus] no config file found, using defaults..."
		tmp = oscamServer()
		oscamServers.append(tmp)
	return oscamServers

def writeCFG(oscamServers):
	cfg = file(CFG, "w")
	print "[OscamStatus] writing datfile..."
	for line in oscamServers:
		cfg.write(line.username+' ')
		cfg.write(line.password+' ')
		cfg.write(line.serverIP+' ')
		cfg.write(line.serverPort+' ')
		cfg.write(line.serverName+' ')
		cfg.write(str(int(line.useSSL))+'\n')
	cfg.close()

class OscamServerEntryList(MenuList):
	def __init__(self, list, enableWrapAround = True):
		MenuList.__init__(self, list, enableWrapAround, eListboxPythonMultiContent)
		self.l.setFont(0, gFont("Regular", 20))
		self.l.setFont(1, gFont("Regular", 18))
		self.pic0 = LoadPixmap(cached=True, path=resolveFilename(SCOPE_SKIN, "skin_default/icons/lock_off.png"))
		self.pic1 = LoadPixmap(cached=True, path=resolveFilename(SCOPE_SKIN, "skin_default/icons/lock_on.png"))

	def postWidgetCreate(self, instance):
		MenuList.postWidgetCreate(self, instance)
		instance.setItemHeight(30)

	def makeList(self, index):
		self.list = []
		oscamServers = readCFG()
		for cnt, i in enumerate(oscamServers):
			res = [i]
			if cnt == index:
				if self.pic1:
					res.append((eListboxPythonMultiContent.TYPE_PIXMAP_ALPHATEST, 5, 1, 25, 24, self.pic1))
				else:
					res.append((eListboxPythonMultiContent.TYPE_TEXT, 5, 3, 25, 24, 1, RT_HALIGN_LEFT|RT_VALIGN_CENTER, 'x'))
			else:
				if self.pic0:
					res.append((eListboxPythonMultiContent.TYPE_PIXMAP_ALPHATEST, 5, 1, 25, 24, self.pic0))
				else:
					res.append((eListboxPythonMultiContent.TYPE_TEXT, 5, 3, 25, 24, 1, RT_HALIGN_LEFT|RT_VALIGN_CENTER, ' '))
			res.append((eListboxPythonMultiContent.TYPE_TEXT,  40, 3, 120, 24, 1, RT_HALIGN_LEFT|RT_VALIGN_CENTER, i.serverName))
			res.append((eListboxPythonMultiContent.TYPE_TEXT, 165, 3, 200, 24, 1, RT_HALIGN_LEFT|RT_VALIGN_CENTER, i.serverIP))
			#res.append((eListboxPythonMultiContent.TYPE_TEXT, 410, 3,  65, 24, 1, RT_HALIGN_LEFT|RT_VALIGN_CENTER, i.serverPort))
			if i.useSSL: tx = "SSL"
			else: tx = ""
			res.append((eListboxPythonMultiContent.TYPE_TEXT, 370, 3, 30, 24, 1, RT_HALIGN_LEFT|RT_VALIGN_CENTER, tx))
			self.list.append(res)
		self.l.setList(self.list)
		self.moveToIndex(index)

# OscamServerEntriesListConfigScreen...
class OscamServerEntriesListConfigScreen(Screen):
	skin = """
		<screen flags="wfNoBorder" position="%d,0" size="440,%d" name="OscamServerEntriesListConfigScreen" >
			<widget render="Label" source="title" position="20,80" size="360,26" valign="center" zPosition="5" transparent="0" foregroundColor="#fcc000" font="Regular;22"/>
			<widget name="list" position="20,130" size="400,288" scrollbarMode="showOnDemand" />
			<eLabel text="" position="20,450" size="400,2" transparent="0" backgroundColor="#ffffff" />
			<ePixmap name="ButtonGreen" pixmap="skin_default/buttons/green.png" position="10,460" size="140,40" zPosition="4" transparent="1" alphatest="on"/>
			<widget render="Label" source= "ButtonGreentext" position="10,460" size="140,40" valign="center" halign="center" zPosition="5" transparent="1" foregroundColor="white" font="Regular;18"/>
			<ePixmap name="ButtonYellow" pixmap="skin_default/buttons/yellow.png" position="150,460" size="140,40" zPosition="4" transparent="1" alphatest="on"/>
			<widget render="Label" source= "ButtonYellowtext" position="150,460" size="140,40" valign="center" halign="center" zPosition="5" transparent="1" foregroundColor="white" font="Regular;18"/>
			<ePixmap name="ButtonBlue" pixmap="skin_default/buttons/blue.png" position="290,460" size="140,40" zPosition="4" transparent="1" alphatest="on"/>
			<widget render="Label" source= "ButtonBluetext" position="290,460" size="140,40" valign="center" halign="center" zPosition="5" transparent="1" foregroundColor="white" font="Regular;18"/>
		</screen>""" % (dlg_xh(440))

	def __init__(self, session):
		self.skin = OscamServerEntriesListConfigScreen.skin
		self.session = session
		Screen.__init__(self, session)

		self["list"] = OscamServerEntryList([])
		self["list"].makeList(config.plugins.OscamStatus.lastServer.value)

		self["title"] = StaticText(_("Oscam Servers"))
		self["ButtonGreentext"] = StaticText(_("new"))
		self["ButtonYellowtext"] = StaticText(_("edit"))
		self["ButtonBluetext"] = StaticText(_("delete"))

		self["actions"] = ActionMap(["OkCancelActions", "ColorActions"],
		{
			"green": self.keyNew,
			"yellow": self.keyEdit,
			"blue": self.keyDelete,
			"ok": self.keyOk,
			"cancel": self.keyClose
		}, -1)

		self.onLayoutFinish.append(self.LayoutFinished)

	def LayoutFinished(self):
		x,h = dlg_xh(self.instance.size().width())
		self.instance.move(ePoint(x, 0))

	def updateEntrys(self):
		self["list"].makeList(config.plugins.OscamStatus.lastServer.value)

	def keyNew(self):
		self.session.openWithCallback(self.updateEntrys, OscamServerEntryConfigScreen, None, -1)

	def keyEdit(self):
		try:
			entry = self["list"].l.getCurrentSelection()[0]
		except:
			entry = None
		if entry:
			self.session.openWithCallback(self.updateEntrys, OscamServerEntryConfigScreen, entry, self["list"].getSelectionIndex())

	def keyDelete(self):
		try:
			self.index = self["list"].getSelectionIndex()
		except:
			self.index = -1
		if self.index > -1:
			if self.index == config.plugins.OscamStatus.lastServer.value:
				print "[OscamStatus] you can not delete the active entry..."
				return
		message = _("Do you really want to delete this entry?")
		msg = self.session.openWithCallback(self.Confirmed, MessageBox, message)
		msg.setTitle("Oscam Status")


	def Confirmed(self, confirmed):
		if not confirmed:
			return
		oscamServers = readCFG()
		del oscamServers[self.index]
		writeCFG(oscamServers)
		if self.index < config.plugins.OscamStatus.lastServer.value:
			config.plugins.OscamStatus.lastServer.value -= 1
		self.updateEntrys()

	def keyOk(self):
		try:
			entry = self["list"].l.getCurrentSelection()[0]
		except:
			entry = None
		if entry:
			config.plugins.OscamStatus.lastServer.value = self["list"].getSelectionIndex()
			config.plugins.OscamStatus.lastServer.save()
			self.close(entry)

	def keyClose(self):
		self.close(False)


# OscamServerEntryConfigScreen...
class OscamServerEntryConfigScreen(Screen, ConfigListScreen):
	skin = """
		<screen flags="wfNoBorder" position="%d,0" size="440,%d" name="OscamServerEntryConfigScreen" >
			<widget render="Label" source="title" position="20,80" size="400,26" valign="center" zPosition="5" transparent="0" foregroundColor="#fcc000" font="Regular;22"/>
			<widget name="config" position="20,130" size="400,200" scrollbarMode="showOnDemand" />
			<eLabel text="" position="20,450" size="400,2" transparent="0" backgroundColor="#ffffff" />
			<ePixmap name="ButtonRed" pixmap="skin_default/buttons/red.png" position="20,460" size="140,40" zPosition="4" transparent="1" alphatest="on"/>
			<widget render="Label" source= "ButtonRedtext" position="20,460" size="140,40" valign="center" halign="center" zPosition="5" transparent="1" foregroundColor="white" font="Regular;18"/>
			<ePixmap name="ButtonGreen" pixmap="skin_default/buttons/green.png" position="160,460" size="140,40" zPosition="4" transparent="1" alphatest="on"/>
			<widget render="Label" source= "ButtonGreentext" position="160,460" size="140,40" valign="center" halign="center" zPosition="5" transparent="1" foregroundColor="white" font="Regular;18"/>
		</screen>""" % (dlg_xh(440))

	def __init__(self, session, entry, index):
		self.skin = OscamServerEntryConfigScreen.skin
		self.session = session
		Screen.__init__(self, session)

		if entry == None:
			entry = oscamServer()
		self.index = index

		# Server Adresse IP-Format oder TextFormat?
		serverIP = self.isIPaddress(entry.serverIP)
		if serverIP and config.plugins.OscamStatus.useIP.value:
			self.isIP = True
		else:
			self.isIP = False

		serverPort = int(entry.serverPort)

		self.serverNameConfigEntry = NoSave(ConfigText(default = entry.serverName, fixed_size = False, visible_width=20))
		if self.isIP:
			self.serverIPConfigEntry = NoSave(ConfigIP( default = serverIP, auto_jump=True))
		else:
			self.serverIPConfigEntry = NoSave(ConfigText(default = entry.serverIP, fixed_size = False, visible_width=20))
			self.serverIPConfigEntry.setUseableChars(u'1234567890aAbBcCdDeEfFgGhHiIjJkKlLmMnNoOpPqQrRsStTuUvVwWxXyYzZ.-_')
		self.portConfigEntry       = NoSave(ConfigInteger(default = serverPort, limits=(0,65536)))
		self.usernameConfigEntry   = NoSave(ConfigText(default = entry.username, fixed_size = False, visible_width=20))
		self.passwordConfigEntry   = NoSave(ConfigPassword(default = entry.password, fixed_size = False))
		self.useSSLConfigEntry     = NoSave(ConfigYesNo(entry.useSSL))

		ConfigListScreen.__init__(self, [], session = session)
		self.createSetup()

		self["title"] = StaticText(_("Oscam Server Setup"))
		self["ButtonRedtext"] = StaticText(_("return"))
		self["ButtonGreentext"] = StaticText(_("save"))
		self["actions"] = ActionMap(["OkCancelActions", "ColorActions"],
		{
			"red": self.Close,
			"green": self.Save,
			"cancel": self.Close
		}, -1)

		self.onLayoutFinish.append(self.LayoutFinished)

	def LayoutFinished(self):
		x,h = dlg_xh(self.instance.size().width())
		self.instance.move(ePoint(x, 0))

	def createSetup(self):
		self.list = []
		self.list.append(getConfigListEntry(_("Oscam Server Name"), self.serverNameConfigEntry))
		self.list.append(getConfigListEntry(_("Oscam Server Address"), self.serverIPConfigEntry))
		self.list.append(getConfigListEntry(_("Port"), self.portConfigEntry))
		self.list.append(getConfigListEntry(_("Username (httpuser)"), self.usernameConfigEntry))
		self.list.append(getConfigListEntry(_("Password (httppwd)"), self.passwordConfigEntry))
		self.list.append(getConfigListEntry(_("use SSL"), self.useSSLConfigEntry))
		self["config"].setList(self.list)

	def isIPaddress(self, txt):
		theIP = txt.split('.')
		if len(theIP) != 4:
			return False
		serverIP = []
		for x in theIP:
			try:
				serverIP.append(int(x))
			except:
				return False
		return serverIP

	def Close(self):
		self.close()

	def Save(self):
		entry = oscamServer()
		entry.username   = self.usernameConfigEntry.value
		entry.password   = self.passwordConfigEntry.value
		entry.serverName = self.serverNameConfigEntry.value
		if self.isIP:
			entry.serverIP   = "%d.%d.%d.%d" % tuple(self.serverIPConfigEntry.value)
		else:
			entry.serverIP   = self.serverIPConfigEntry.value
		entry.serverPort = str(self.portConfigEntry.value)
		entry.useSSL     = self.useSSLConfigEntry.value
		oscamServers = readCFG()
		if self.index == -1:
			oscamServers.append(entry)
		else:
			oscamServers[self.index] = entry
		writeCFG(oscamServers)
		self.close()

from os import path, listdir
class piconLoader:
	def __init__(self, Path):
		self.hasLoaded = False
		if path.exists(Path):
			self.Path = Path
			self.picons = {}
			for fname in listdir(self.Path):
				items = fname.split('_')
				# Nur Datei mit sref Namen, also picon png...
				if len(items) > 8:
					#t  = self.picons.get(items[3], 'n/a')
					#if t != 'n/a':
					#	print fname, t
					self.picons[items[3]] = fname
			if len(self.picons) == 0:
				print "[OscamStatus] ERROR: no picons found!"
				return
			else:
				print "[OscamStatus] %d picons found..." % len(self.picons)
				self.hasLoaded = True
				return
		else:
			print "[OscamStatus] ERROR: wrong picons path!"
			return

	def getPicon(self, srvid):
		# FTA Sender oder nicht gefunden...
		if srvid == '0000':
			return None

		# keine fuehrenden Nullen...
		while srvid[0] == '0':
			srvid = srvid[1:]

		# Dateinamen aus dict holen...
		fname = self.picons.get(srvid, 'n/a')
		if fname != 'n/a':
			piconpath = self.Path+'/'+fname
			return LoadPixmap(cached=True, path=piconpath)
		else:
			return LoadPixmap(cached=True, path=resolveFilename(SCOPE_CURRENT_PLUGIN, "Extensions/OscamStatus/icons/unknown.png"))

