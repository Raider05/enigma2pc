# -*- coding: utf-8 -*-
#===============================================================================
# OscamStatus Plugin by puhvogel 2011-2012
#
# This is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2, or (at your option) any later
# version.
#===============================================================================

from __init__ import _
from Plugins.Plugin import PluginDescriptor

from Screens.Screen import Screen
from Screens.MessageBox import MessageBox

from Components.ActionMap import ActionMap
from Components.Pixmap import Pixmap
from Components.Label import Label
from Components.Button import Button
from Components.ProgressBar import ProgressBar
from Components.MenuList import MenuList
from Components.ConfigList import ConfigListScreen
from Components.Sources.List import List
from Components.Sources.StaticText import StaticText

from Tools.LoadPixmap import LoadPixmap
from Tools.Directories import fileExists, resolveFilename, SCOPE_CURRENT_PLUGIN

from enigma import eTimer, getDesktop, eListboxPythonMultiContent, eListbox, gFont, \
                   RT_HALIGN_LEFT, RT_VALIGN_CENTER, ePoint, ePythonMessagePump

from threading import Thread, Lock
import xml.dom.minidom
import urllib2
import ssl

from OscamStatusSetup import oscamServer, readCFG, OscamServerEntriesListConfigScreen, \
                             globalsConfigScreen, LASTSERVER, XOFFSET, EXTMENU, USEECM,\
                             dlg_xh, picons, piconLoader, USEPICONS, PICONPATH

VERSION = "0.61"
TIMERTICK = 10000

# Rechnet vergangene Sekunden in Tage, Stunden, Minuten und Sekunden um...
def elapsedTime(s, fmt, hasDays = False):
	try:
		secs = long(s)
		if hasDays:
			days, secs = divmod(secs, 86400)
		hours, secs = divmod(secs, 3600)
		mins, secs = divmod(secs, 60)
		if hasDays:
			return fmt % (days, hours, mins, secs)
		else:
			return fmt % (hours, mins, secs)
	except ValueError:
		return s

class ThreadQueue:
	def __init__(self):
		self.__list = [ ]
		self.__lock = Lock()

	def push(self, val):
		lock = self.__lock
		lock.acquire()
		self.__list.append(val)
		lock.release()

	def pop(self):
		lock = self.__lock
		lock.acquire()
		ret = self.__list.pop()
		lock.release()
		return ret

THREAD_WORKING = 1
THREAD_FINISHED = 2
THREAD_ERROR = 3

# twisted kann KEIN digest auth, warum auch immer...
class GetPage2(Thread):
	def __init__(self):
		Thread.__init__(self)
		self.__running = False
		self.__messages = ThreadQueue()
		self.__messagePump = ePythonMessagePump()

	def __getMessagePump(self):
		return self.__messagePump

	def __getMessageQueue(self):
		return self.__messages

	def __getRunning(self):
		return self.__running

	MessagePump = property(__getMessagePump)
	Message = property(__getMessageQueue)
	isRunning = property(__getRunning)

	def Start(self, url, username, password):
		if not self.__running:
			self.url = url
			self.username = username
			self.password = password
			self.start()

	def run(self):
		mp = self.__messagePump
		self.__running = True
		self.__cancel = False

		PasswdMgr = urllib2.HTTPPasswordMgrWithDefaultRealm()
		PasswdMgr.add_password(None, self.url, self.username, self.password)

		handler = urllib2.HTTPDigestAuthHandler(PasswdMgr)
		opener = urllib2.build_opener(urllib2.HTTPHandler, handler)

		urllib2.install_opener(opener)
		request = urllib2.Request(self.url)

		self.__messages.push((THREAD_WORKING, "Download Thread is running" ))
		mp.send(0)

		try:
			page = urllib2.urlopen(request, timeout = 5).read()

		except urllib2.URLError, err:
			error = "Error: "
			if hasattr(err, "code"):
				error += str(err.code)
			if hasattr(err, "reason"):
				error += str(err.reason)
			self.__messages.push((THREAD_ERROR, error))

		except Exception:
			self.__messages.push((THREAD_ERROR, "General Exception Error!\n(no Oscamserver?)"))

		else:
			self.__messages.push((THREAD_FINISHED, page))

		mp.send(0)

		self.__running = False
		Thread.__init__(self)


getPage2 = GetPage2()


class oscamdata:
	def __init__(self):
		self.version = "n/a"
		self.revision = "n/a"
		self.starttime = "n/a"
		self.uptime = "n/a"
		self.readonly = "n/a"

class client:
	def __init__(self):
		self.type = "n/a"
		self.name = "n/a"
		self.protocol = "n/a"
		self.protocolext = "n/a"
		self.au = "n/a"
		self.caid = "n/a"
		self.srvid = "n/a"
		self.ecmtime = "n/a"
		self.ecmhistory = "n/a"
		self.answered = "n/a"
		self.service = "n/a"
		self.login = "n/a"
		self.online = "n/a"
		self.idle = "n/a"
		self.ip = "n/a"
		self.port = "n/a"
		self.connection = "n/a"

class user:
	def __init__(self):
		self.name = "n/a"
		self.status = "n/a"
		self.ip = "n/a"
		self.protocol = "n/a"
		self.timeonchannel = "n/a" # ab 6793

class reader:
	def __init__(self):
		self.label = "n/a"
		self.hostaddress = "n/a"
		self.hostport = "n/a"
		self.totalcards = "n/a"
		self.cards = []

class card:
	def __init__(self):
		self.number = "n/a"
		self.caid = "n/a"
		self.system = "n/a"
		self.reshare = "n/a"
		self.hop = "n/a"
		self.shareid = "n/a"
		self.remoteid = "n/a"
		self.totalproviders = "n/a"
		self.providers = []
		self.totalnodes = "n/a"
		self.nodes = []

class provider: 
	def __init__(self):
		self.number = "n/a"
		self.sa = "n/a"
		self.caid = "n/a"
		self.provid = "n/a"
		self.service = "n/a"

class pnode:
	def __init__(self):
		self.number = "n/a"
		self.hexval = "n/a"

class ecm:
	def __init__(self):
		self.caid = "n/a"
		self.provid = "n/a"
		self.srvid = "n/a"
		self.channelname = "n/a"
		self.avgtime = "n/a"
		self.lasttime = "n/a"
		self.rc = "n/a"
		self.rcs = "n/a"
		self.lastrequest = "n/a"
		self.val = "n/a"

class emm:
	def __init__(self):
		self.type = "n/a"
		self.result = "n/a"
		self.val = "n/a"

class readerlist:
	def __init__(self):
		self.label = "n/a"
		self.protocol = "n/a"
		self.type = "n/a"
		self.enabled = "n/a"

# ReaderServiceDataScreen...
class ReaderServiceDataScreen(Screen):
	skin = """
		<screen flags="wfNoBorder" position="%d,0" size="720,%d" name="ReaderDataScreen" >
			<widget render="Label" source="title" position="10,80" size="700,26" valign="center" zPosition="5" transparent="0" foregroundColor="#fcc000" font="Regular;22"/>
			<widget source="data" render="Listbox" position="10,130" size="700,396" scrollbarMode="showOnDemand">
				<convert type="TemplatedMultiContent">
					{"template": [
						MultiContentEntryText(pos = (  1, 0), size = ( 55, 22), font=0, flags = RT_HALIGN_LEFT, text = 0),
						MultiContentEntryText(pos = ( 60, 0), size = ( 80, 22), font=0, flags = RT_HALIGN_LEFT, text = 1),
						MultiContentEntryText(pos = (145, 0), size = (340, 22), font=0, flags = RT_HALIGN_LEFT, text = 2),
						MultiContentEntryText(pos = (500, 0), size = (130, 22), font=0, flags = RT_HALIGN_LEFT, text = 3),
						MultiContentEntryText(pos = (635, 0), size = ( 50, 22), font=0, flags = RT_HALIGN_LEFT, text = 4),
					],
					"fonts": [gFont("Regular", 19)],
					"itemHeight": 22
					}
				</convert>
			</widget>
		</screen>""" % (dlg_xh(720))
	
	def __init__(self, session, r):
		def compare(a, b):
			return cmp(a[0], b[0]) or cmp(a[1], b[1])

		self.skin = ReaderServiceDataScreen.skin
		self.session = session
		Screen.__init__(self, session)

		tmp = []
		for c in r[0].cards:
			for p in c.providers:
				tmp.append((c.caid, p.provid, p.service, c.system, c.number))

		self["title"] = StaticText(str(len(tmp))+' Services@'+r[0].label+'('+r[0].hostaddress+')')

		self["data"] = List(sorted(tmp, compare))
		self["actions"] = ActionMap(["OkCancelActions"],
		{
			"cancel": self.close
		}, -1)

		self.onLayoutFinish.append(self.LayoutFinished)

	def LayoutFinished(self):
		x,h = dlg_xh(self.instance.size().width())
		self.instance.move(ePoint(x, 0))

# ClientDataScreen...
class ClientDataScreen(Screen):
	part1 = """
		<screen flags="wfNoBorder" position="%d,0" size="440,%d" name="ClientDataScreen" >
			<widget render="Label" source="title"  position=" 20, 70" size="360,26" valign="center" zPosition="5" transparent="0" foregroundColor="#fcc000" font="Regular;22"/>
			<widget render="Label" source="lprotocol" position="  20,100" size="120,20" font="Regular;18"/>
			<widget render="Label" source= "protocol" position="140,100" size="235,20" font="Regular;18"/>
			<widget render="Label" source="lprotocolext" position=" 20,120" size="120,20" font="Regular;18"/>
			<widget render="Label" source= "protocolext" position="140,120" size="235,60" font="Regular;18"/>
			<widget render="Label" source="lau" position=" 20,180" size="120,20" font="Regular;18"/>
			<widget render="Label" source= "au" position="140,180" size="235,20" font="Regular;18"/>
			<widget render="Label" source="lcaid" position=" 20,200" size="120,20" font="Regular;18"/>
			<widget render="Label" source= "caid" position="140,200" size="235,20" font="Regular;18"/>
			<widget render="Label" source="lsrvid" position=" 20,220" size="120,20" font="Regular;18"/>
			<widget render="Label" source= "srvid" position="140,220" size="235,20" font="Regular;18"/>
			<widget render="Label" source="lecmtime" position=" 20,240" size="120,20" font="Regular;18"/>
			<widget render="Label" source= "ecmtime" position="140,240" size="235,20" font="Regular;18"/>
			<widget render="Label" source="lecmhistory" position=" 20,260" size="120,80" font="Regular;18"/>
			<widget render="Label" source="historymax" position=" 345,260" size="40,16" font="Regular;14"/>
			<widget render="Label" source="historymin" position=" 345,326" size="40,16" font="Regular;14"/>""" % (dlg_xh(440))

	ecmhistory = ""
	x = 140
	for i in range(20):
		ecmhistory += "<widget name=\"progress%d\" zPosition=\"4\" position=\"%d,262\" size=\"10,78\" transparent=\"1\" borderColor=\"#404040\" borderWidth=\"1\" orientation=\"orBottomToTop\" />\n" % (i, x)
		x += 10

	part2 = """
			<widget render="Label" source="lanswered" position=" 20,340" size="120,20" font="Regular;18"/>
			<widget render="Label" source= "answered" position="140,340" size="235,20" font="Regular;18"/>
			<widget render="Label" source="lservice" position=" 20,360" size="120,20" font="Regular;18"/>
			<widget render="Label" source= "service" position="140,360" size="235,20" font="Regular;18"/>
			<widget render="Label" source="llogin" position=" 20,380" size="120,20" font="Regular;18"/>
			<widget render="Label" source= "login" position="140,380" size="235,20" font="Regular;18"/>
			<widget render="Label" source="lonline" position=" 20,400" size="120,20" font="Regular;18"/>
			<widget render="Label" source= "online" position="140,400" size="235,20" font="Regular;18"/>
			<widget render="Label" source="lidle" position=" 20,420" size="120,20" font="Regular;18"/>
			<widget render="Label" source= "idle" position="140,420" size="235,20" font="Regular;18"/>
			<widget render="Label" source="lip" position=" 20,440" size="120,20" font="Regular;18"/>
			<widget render="Label" source= "ip" position="140,440" size="235,20" font="Regular;18"/>
			<widget render="Label" source="lport" position=" 20,460" size="120,20" font="Regular;18"/>
			<widget render="Label" source= "port" position="140,460" size="235,20" font="Regular;18"/>
			<widget render="Label" source="lconnection" position=" 20,480" size="120,20" font="Regular;18"/>
			<widget render="Label" source= "connection" position="140,480" size="235,20" font="Regular;18"/>
			<widget name="KeyYellow" pixmap="%s" position="345,475" size="35,25" zPosition="4" transparent="1" alphatest="on"/>
		</screen>""" % resolveFilename(SCOPE_CURRENT_PLUGIN, "Extensions/OscamStatus/icons/bt_yellow.png")

	def __init__(self, session, type, oServer, data):
		self.skin = ClientDataScreen.part1+ClientDataScreen.ecmhistory+ClientDataScreen.part2
		print self.skin
		self.session = session
		self.type = type
		self.oServer = oServer
		Screen.__init__(self, session)

		# dict fuer Status AU
		auEntrys = {"1":"ACTIVE", "0":"OFF", "-1":"ON"}

		self["title"] = StaticText("")
		self["KeyYellow"] = Pixmap()
		self["KeyYellow"].hide()

		if type == "clients":
			self["title"].setText("Client "+data.name+"@"+oServer.serverName)
		else:
			self["title"].setText("Reader "+data.name+"@"+oServer.serverName)
			if data.protocol.startswith("cccam"):
				self["KeyYellow"].show()

		self.ecmhistory = data.ecmhistory.split(',')
		maxh = 0.0
		for i in range(20):
			self["progress%d" % i] = ProgressBar()
			self.ecmhistory[i] = float(self.ecmhistory[i])
			if self.ecmhistory[i] > maxh:
				maxh = self.ecmhistory[i]
		if maxh < 1000.0:
			self.base = 1000.0
		elif maxh < 2000.0:
			self.base = 2000.0
		elif maxh < 3000.0:
			self.base = 3000.0
		elif maxh < 4000.0:
			self.base = 4000.0
		elif maxh < 5000.0:
			self.base = 5000.0
		else:
			self.base = 9999.0

		self.name = data.name
		self.protocol = data.protocol
		self["lprotocol"] = StaticText("protocol:")
		self[ "protocol"] = StaticText(data.protocol)
		self["lprotocolext"] = StaticText("protocolext:")
		self[ "protocolext"] = StaticText(data.protocolext)
		self["lau"] = StaticText("au:")
		self[ "au"] = StaticText(auEntrys[data.au])
		self["lcaid"] = StaticText("caid:")
		self[ "caid"] = StaticText(data.caid)
		self["lsrvid"] = StaticText("srvid:")
		self[ "srvid"] = StaticText(data.srvid)
		self["lecmtime"] = StaticText("ecmtime:")
		self[ "ecmtime"] = StaticText(data.ecmtime)
		self["lecmhistory"] = StaticText("ecmhistory:")
		self[ "historymax"] = StaticText(str(int(self.base)))
		self[ "historymin"] = StaticText("0")
		self["lanswered"] = StaticText("answered:")
		self[ "answered"] = StaticText(data.answered)
		self["lservice"] = StaticText("service:")
		self[ "service"] = StaticText(data.service)
		self["llogin"] = StaticText("login:")
		self[ "login"] = StaticText(data.login)
		self["lonline"] = StaticText("online:")
		self[ "online"] = StaticText(elapsedTime(data.online, "%02dd %02dh %02dm %02ds", True))
		self["lidle"] = StaticText("idle:")
		self[ "idle"] = StaticText(elapsedTime(data.idle, "%02d:%02d:%02d"))
		self["lip"] = StaticText("ip:")
		self[ "ip"] = StaticText(data.ip)
		self["lport"] = StaticText("port:")
		self[ "port"] = StaticText(data.port)
		self["lconnection"] = StaticText("connection:")
		self[ "connection"] = StaticText(data.connection)

		self["actions"] = ActionMap(["OkCancelActions", "ColorActions"],
		{
			"yellow": self.yellowPressed,
			"cancel": self.Close
		}, -1)
		self.onExecBegin.append(self.setProgress)

		self.onLayoutFinish.append(self.LayoutFinished)

	def LayoutFinished(self):
		x,h = dlg_xh(self.instance.size().width())
		self.instance.move(ePoint(x, 0))

	def setProgress(self):
		for i in range(20):
			val = int((100.0*self.ecmhistory[i])/self.base)
			self["progress%d" % i].setValue(val)

	def yellowPressed(self):
		if self.type is not "clients" and self.protocol.startswith("cccam"):
			part = "entitlement&label="+self.name
			self.session.open(ReaderDataScreen, part, self.oServer)

	def Close(self):
		self.close(0)

# "Mutter" aller Downloadfenster...
class DownloadXMLScreen(Screen):
	def __init__(self, session, part, oServer, timerOn=True):
		self.oServer = oServer
		self["title"] = StaticText("")
		if oServer.useSSL: self.url = "https://"
		else: self.url = "http://"
		self.url += oServer.serverIP+":"+oServer.serverPort+"/oscamapi.html?part="+part
		self.oldurl = self.url
		self.download = False
		self.data = False
		self.timer = eTimer()
		self.timer.callback.append(self.downloadXML)
		self.downloadXML()
		self.timerOn = timerOn
		if self.timerOn:
			self.timer.start(TIMERTICK)
		self.newurl = False

	def downloadXML(self):
		self.setTitle("loading...")
		self.download = True
		print "[OscamStatus] loading", self.url
		self.getIndex()		
		# Message Queue initialisieren...
		getPage2.MessagePump.recv_msg.get().append(self.gotThreadMsg)	
		# Download Thread starten...
		getPage2.Start(self.url, self.oServer.username, self.oServer.password)

	def sendNewPart(self, part):
		self.timer.stop()
		if self.oServer.useSSL: url = "https://"
		else: url = "http://"

		url += self.oServer.serverIP+":"+self.oServer.serverPort
		url += "/oscamapi.html?part="+part
		self.newurl = True
		self.url = url
		self.downloadXML()

	def gotThreadMsg(self, msg):
		msg = getPage2.Message.pop()
		if msg[0] == THREAD_ERROR:
			getPage2.MessagePump.recv_msg.get().remove(self.gotThreadMsg)	
			self.download = False
			errStr = str(msg[1])
			if self.timerOn:
				self.timer.stop()
			print "[OscamStatus]", errStr
			info = self.session.open(MessageBox, errStr, MessageBox.TYPE_ERROR)
			info.setTitle("Oscam Status")
			self.close(1)

		elif msg[0] == THREAD_FINISHED:
			getPage2.MessagePump.recv_msg.get().remove(self.gotThreadMsg)	
			self.download = False
			print "[OscamStatus] Download finished"

			self.data = msg[1]
			self.download = False
			# wenn keine xml zurueckkommt stimmt etwas nicht..
			if not "<?xml version=\"1.0\"" in self.data:
				print "[OscamStatus] Oscam Download Error: no xml"
				info = self.session.open(MessageBox, "no xml", MessageBox.TYPE_ERROR)
				info.setTitle("Oscam Download Error")
				self.close(1)
				return

			dom = xml.dom.minidom.parseString(self.data)
			node = dom.getElementsByTagName("error")
			if node:
				if self.timerOn:
					self.timer.stop()
				errmsg = str(node[0].firstChild.nodeValue.strip())
				print "[OscamStatus] Oscam XML Error:", errmsg
				info = self.session.open(MessageBox, errmsg, MessageBox.TYPE_ERROR)
				info.setTitle("Oscam XML Error")
				self.close(1)
			else:
				self.dlAction()

	def getIndex(self):
		try:
			self.oldIndex = self["data"].getIndex()
		except:
			self.oldIndex = 0

	def Close(self):
		if self.timerOn:
			self.timer.stop()
		if self.download:
			getPage2.MessagePump.recv_msg.get().remove(self.gotThreadMsg)	
		self.close(0)

	def setTitle(self, txt):
		self["title"].setText(txt)

# OscamDataScreen...
class OscamDataScreen(DownloadXMLScreen):
	skin = """
		<screen flags="wfNoBorder" position="%d,0" size="440,%d" name="OscamDataScreen" >
			<widget render="Label" source="title"    position=" 20, 80" size="400,26" valign="center" zPosition="5" transparent="0" foregroundColor="#fcc000" font="Regular;22"/>
			<widget render="Label" source="lversion" position=" 20,130" size="90,20" font="Regular;18"/>
			<widget render="Label" source= "version" position="115,130" size="360,20" font="Regular;18"/>
			<widget render="Label" source="lrevision" position=" 20,150" size="90,20" font="Regular;18"/>
			<widget render="Label" source= "revision" position="115,150" size="360,20" font="Regular;18"/>
			<widget render="Label" source="lstarttime" position=" 20,170" size="90,20" font="Regular;18"/>
			<widget render="Label" source= "starttime" position="115,170" size="360,20" font="Regular;18"/>
			<widget render="Label" source="luptime" position=" 20,190" size="90,20" font="Regular;18"/>
			<widget render="Label" source= "uptime" position="115,190" size="360,20" font="Regular;18"/>
			<widget render="Label" source="lreadonly" position=" 20,210" size="90,20" font="Regular;18"/>
			<widget render="Label" source= "readonly" position="115,210" size="360,20" font="Regular;18"/>
			<eLabel text="" position="20,450" size="400,2" transparent="0" backgroundColor="#ffffff" />
		</screen>""" % (dlg_xh(440))

	def __init__(self, session, part, oServer):
		self.oServer = oServer
		self.skin = OscamDataScreen.skin
		self.session = session
		Screen.__init__(self, session)

		DownloadXMLScreen.__init__(self, session, part, oServer, False)

		self["lversion"] = StaticText("version:")
		self[ "version"] = StaticText("")
		self["lrevision"] = StaticText("revision:")
		self[ "revision"] = StaticText("")
		self["lstarttime"] = StaticText("starttime:")
		self[ "starttime"] = StaticText("")
		self["luptime"] = StaticText("uptime:")
		self[ "uptime"] = StaticText("")
		self["lreadonly"] = StaticText("readonly:")
		self[ "readonly"] = StaticText("")
		self["actions"] = ActionMap(["OkCancelActions"],

		{
			"cancel": self.Close
		}, -1)

		self.onLayoutFinish.append(self.LayoutFinished)

	def LayoutFinished(self):
		x,h = dlg_xh(self.instance.size().width())
		self.instance.move(ePoint(x, 0))

	def parseXML(self, dom):
		o = oscamdata()
		for node in dom.getElementsByTagName("oscam"):
			o.version = str(node.getAttribute("version"))
			o.revision = str(node.getAttribute("revision"))
			o.starttime = str(node.getAttribute("starttime"))
			o.uptime = str(node.getAttribute("uptime"))
			o.readonly = str(node.getAttribute("readonly"))
		return o

	def dlAction(self):
		dom = xml.dom.minidom.parseString(self.data)
		d = self.parseXML(dom)

		self.setTitle("Oscam Server"+"@"+self.oServer.serverName)
		self[ "version"].setText(d.version)
		self[ "revision"].setText(d.revision)
		self[ "starttime"].setText(d.starttime)
		self[ "uptime"].setText(elapsedTime(d.uptime, "%d days %d hours %d minutes %d seconds", True))
		self[ "readonly"].setText(d.readonly)

# OscamRestartScreen...
class OscamRestartScreen(DownloadXMLScreen):
	skin = """
		<screen flags="wfNoBorder" position="%d,0" size="440,%d" name="OscamRestartScreen" >
			<widget render="Label" source="title"    position=" 20, 80" size="400,26" valign="center" zPosition="5" transparent="0" foregroundColor="#fcc000" font="Regular;22"/>
			<widget render="Label" source="lversion" position=" 20,130" size="90,20" font="Regular;18"/>
			<widget render="Label" source= "version" position="115,130" size="360,20" font="Regular;18"/>
			<widget render="Label" source="lrevision" position=" 20,150" size="90,20" font="Regular;18"/>
			<widget render="Label" source= "revision" position="115,150" size="360,20" font="Regular;18"/>
			<widget render="Label" source="lstarttime" position=" 20,170" size="90,20" font="Regular;18"/>
			<widget render="Label" source= "starttime" position="115,170" size="360,20" font="Regular;18"/>
			<widget render="Label" source="luptime" position=" 20,190" size="90,20" font="Regular;18"/>
			<widget render="Label" source= "uptime" position="115,190" size="360,20" font="Regular;18"/>
			<widget render="Label" source="lreadonly" position=" 20,210" size="90,20" font="Regular;18"/>
			<widget render="Label" source= "readonly" position="115,210" size="360,20" font="Regular;18"/>
			<eLabel text="" position="20,450" size="400,2" transparent="0" backgroundColor="#ffffff" />
			<widget name="ButtonYellow" pixmap="skin_default/buttons/yellow.png" position="20,460" size="140,40" zPosition="4" transparent="1" alphatest="on"/>
			<widget name="ButtonYellowtext" position="20,460" size="140,40" valign="center" halign="center" zPosition="5" transparent="1" foregroundColor="white" font="Regular;16"/>
			<widget name="ButtonBlue" pixmap="skin_default/buttons/blue.png" position="160,460" size="140,40" zPosition="4" transparent="1" alphatest="on"/>
			<widget name="ButtonBluetext" position="160,460" size="140,40" valign="center" halign="center" zPosition="5" transparent="1" foregroundColor="white" font="Regular;16"/>
		</screen>""" % (dlg_xh(440))

	def __init__(self, session, part, oServer):
		self.oServer = oServer
		self.skin = OscamRestartScreen.skin
		self.session = session
		Screen.__init__(self, session)

		DownloadXMLScreen.__init__(self, session, part, oServer, False)

		self["lversion"] = StaticText("version:")
		self[ "version"] = StaticText("")
		self["lrevision"] = StaticText("revision:")
		self[ "revision"] = StaticText("")
		self["lstarttime"] = StaticText("starttime:")
		self[ "starttime"] = StaticText("")
		self["luptime"] = StaticText("uptime:")
		self[ "uptime"] = StaticText("")
		self["lreadonly"] = StaticText("readonly:")
		self[ "readonly"] = StaticText("")

		self["ButtonYellow"] = Pixmap()
		self["ButtonYellowtext"] = Button(_("restart oscam"))
		self["ButtonBlue"] = Pixmap()
		self["ButtonBluetext"] = Button(_("shutdown oscam"))

		self["actions"] = ActionMap(["OkCancelActions", "ColorActions"],
		{
			"yellow": self.yellowPressed,
			"blue": self.bluePressed,
			"cancel": self.Close
		}, -1)
		self.canRestart = False

		self.onLayoutFinish.append(self.LayoutFinished)

	def LayoutFinished(self):
		x,h = dlg_xh(self.instance.size().width())
		self.instance.move(ePoint(x, 0))

	def parseXML(self, dom):
		o = oscamdata()
		for node in dom.getElementsByTagName("oscam"):
			o.version = str(node.getAttribute("version"))
			o.revision = str(node.getAttribute("revision"))
			o.starttime = str(node.getAttribute("starttime"))
			o.uptime = str(node.getAttribute("uptime"))
			o.readonly = str(node.getAttribute("readonly"))
		# erst ab 4717 ist in der xml api restart/shutdown moeglich...
		if int(o.revision) > 4716 and not int(o.readonly):
			self.canRestart = True
		return o

	def dlAction(self):
		if self.newurl:
			self.close(0)
		dom = xml.dom.minidom.parseString(self.data)
		d = self.parseXML(dom)

		self.setTitle("Oscam Server"+"@"+self.oServer.serverName)
		self[ "version"].setText(d.version)
		self[ "revision"].setText(d.revision)
		self[ "starttime"].setText(d.starttime)
		self[ "uptime"].setText(elapsedTime(d.uptime, "%d days %d hours %d minutes %d seconds", True))
		self[ "readonly"].setText(d.readonly)

		if not self.canRestart:
			msg = _("you can\'t shutdown/restart this oscam:\n")
			if not int(d.revision) > 4716:
				msg += _("- oscam revision is %s (min. 4717 needed)")%d.revision
			if int(d.readonly):
				msg += _("- webif is readonly!")
			info = self.session.open(MessageBox, msg, MessageBox.TYPE_ERROR)
			info.setTitle("Oscam Status")
			self.close(1)

	def yellowPressed(self):
		if self.download:
			return
		self.mode = "restart"
		self.mbox(_("really restart oscam@%s?")%self.oServer.serverName)

	def bluePressed(self):
		if self.download:
			return
		self.mode = "shutdown"
		self.mbox(_("really really shutdown oscam@%s?")%self.oServer.serverName)

	def mbox(self, txt):
		msg = self.session.openWithCallback(self.mboxCB, MessageBox, txt, default = False)
		msg.setTitle("Oscam Status")

	def mboxCB(self, retval):
		if retval:
			if self.mode == "restart":
				self.sendNewPart("shutdown&action=restart")
			elif self.mode == "shutdown":
				self.sendNewPart("shutdown&action=shutdown")

# ReaderDataScreen...
class ReaderDataScreen(DownloadXMLScreen):
	x,h = dlg_xh(720)
	skin = """
		<screen flags="wfNoBorder" position="%d,0" size="720,%d" name="ReaderDataScreen" >
			<widget render="Label" source="title" position="10,80" size="700,26" valign="center" zPosition="5" transparent="0" foregroundColor="#fcc000" font="Regular;22"/>
			<widget source="data" render="Listbox" position="10,130" size="700,360" scrollbarMode="showOnDemand">
				<convert type="TemplatedMultiContent">
					{"template": [
						MultiContentEntryText(pos = (  2, 2), size = ( 40, 28), font=2, flags = RT_HALIGN_LEFT, text = 0),
						MultiContentEntryText(pos = ( 50, 5), size = ( 50, 24), font=0, flags = RT_HALIGN_LEFT, text = 1),
						MultiContentEntryText(pos = (110, 5), size = (200, 24), font=0, flags = RT_HALIGN_LEFT, text = 2),
						MultiContentEntryText(pos = (320, 2), size = (140, 24), font=0, flags = RT_HALIGN_LEFT, text = 3),
						MultiContentEntryText(pos = (470, 2), size = (140, 24), font=0, flags = RT_HALIGN_LEFT, text = 4),

						MultiContentEntryText(pos = ( 20,25), size = ( 20, 20), font=1, flags = RT_HALIGN_LEFT, text = 5),
						MultiContentEntryText(pos = ( 50,25), size = (300, 20), font=1, flags = RT_HALIGN_LEFT, text = 6),
						MultiContentEntryText(pos = ( 20,40), size = ( 20, 20), font=1, flags = RT_HALIGN_LEFT, text = 7),
						MultiContentEntryText(pos = ( 50,40), size = (300, 20), font=1, flags = RT_HALIGN_LEFT, text = 8),
						MultiContentEntryText(pos = (320,25), size = ( 20, 20), font=1, flags = RT_HALIGN_LEFT, text = 9),
						MultiContentEntryText(pos = (350,25), size = (300, 20), font=1, flags = RT_HALIGN_LEFT, text = 10),
						MultiContentEntryText(pos = (320,40), size = ( 20, 20), font=1, flags = RT_HALIGN_LEFT, text = 11),
						MultiContentEntryText(pos = (350,40), size = (300, 20), font=1, flags = RT_HALIGN_LEFT, text = 12),
					],
					"fonts": [gFont("Regular", 20), gFont("Regular", 15), gFont("Regular", 24)],
					"itemHeight": 60
					}
				</convert>
			</widget>
			<widget name="KeyYellow" pixmap="%s" position="675,495" size="35,25" zPosition="4" transparent="1" alphatest="on"/>
		</screen>""" % (x, h, resolveFilename(SCOPE_CURRENT_PLUGIN, "Extensions/OscamStatus/icons/bt_yellow.png"))
	
	def __init__(self, session, part, oServer):
		self.skin = ReaderDataScreen.skin
		self.session = session
		Screen.__init__(self, session)

		DownloadXMLScreen.__init__(self, session, part, oServer, False)

		self["data"] = List([])
		self["KeyYellow"] = Pixmap()
		self["actions"] = ActionMap(["OkCancelActions", "ColorActions"],
		{
			"yellow": self.yellowPressed,
			"cancel": self.Close
		}, -1)

		self.onLayoutFinish.append(self.LayoutFinished)

	def LayoutFinished(self):
		x,h = dlg_xh(self.instance.size().width())
		self.instance.move(ePoint(x, 0))

	def parseXML(self, dom):
		d = []
		# readout dom...
		for elem in dom.getElementsByTagName("reader"):
			r = reader()
			r.label = str(elem.getAttribute("label"))
			r.hostaddress = str(elem.getAttribute("hostaddress"))
			r.hostport = str(elem.getAttribute("hostport"))
			for node in elem.getElementsByTagName("cardlist"):
				r.totalcards = str(node.getAttribute("totalcards"))
				for snode in node.childNodes:
					if snode.nodeName == "card":
						c = card()
						c.number = str(snode.getAttribute("number"))
						c.caid = str(snode.getAttribute("caid"))
						c.system = str(snode.getAttribute("system"))
						c.reshare = str(snode.getAttribute("reshare"))
						c.hop = str(snode.getAttribute("hop"))
						for snode2 in snode.childNodes:
							if snode2.nodeName == "shareid":
								if snode2.firstChild and snode2.firstChild.nodeType == snode2.firstChild.TEXT_NODE:
									c.shareid = str(snode2.firstChild.nodeValue.strip())
							if snode2.nodeName == "remoteid":
								if snode2.firstChild and snode2.firstChild.nodeType == snode2.firstChild.TEXT_NODE:
									c.remoteid = str(snode2.firstChild.nodeValue.strip())
							if snode2.nodeName == "providers":
								c.totalproviders = str(snode2.getAttribute("totalproviders"))
								for snode3 in snode2.childNodes:
									if snode3.nodeName == "provider":
										p = provider()
										p.number = str(snode3.getAttribute("number"))
										p.sa = str(snode3.getAttribute("sa"))
										p.caid = str(snode3.getAttribute("caid"))
										p.provid = str(snode3.getAttribute("provid"))
										if snode3.firstChild and snode3.firstChild.nodeType == snode3.firstChild.TEXT_NODE:
											# HACK! auf unterschiedlichen Plattformen wird 'ü' unterschiedlich encodiert?
											p.service = str(snode3.firstChild.nodeValue.strip()).replace("&#195;&#188;", "ü").replace("&#451;&#444;", "ü").replace("&amp;", "&")
											#p.service = unescape(str(snode3.firstChild.nodeValue.strip())).replace("&#451;&#444;", "ü")
										c.providers.append(p)
							if snode2.nodeName == "nodes":
								c.totalproviders = str(snode2.getAttribute("totalnodes"))
								for snode3 in snode2.childNodes:
									if snode3.nodeName == "node":
										n = pnode()
										n.number = str(snode3.getAttribute("number"))
										if snode3.firstChild and snode3.firstChild.nodeType == snode3.firstChild.TEXT_NODE:
											n.hexval = str(snode3.firstChild.nodeValue.strip())
										c.nodes.append(n)
						r.cards.append(c)
				d.append(r)
		return d

	def dlAction(self):
		dom = xml.dom.minidom.parseString(self.data)
		r = self.parseXML(dom)

		list = []
		self.setTitle(r[0].totalcards+' Cards@'+r[0].label+'('+r[0].hostaddress+')')
		for c in r[0].cards:
			p = c.providers
			if len(p) == 4:
				list.append((c.number, c.caid, c.system, 'reshare = '+c.reshare, 'hops = '+c.hop,\
				p[0].number,p[0].service,p[1].number,p[1].service,p[2].number,p[2].service,p[3].number,p[3].service))
			elif len(p) == 3:
				list.append((c.number, c.caid, c.system, 'reshare = '+c.reshare, 'hops = '+c.hop,\
				p[0].number,p[0].service,p[1].number,p[1].service,p[2].number,p[2].service,' ',' '))
			elif len(p) == 2:
				list.append((c.number, c.caid, c.system, 'reshare = '+c.reshare, 'hops = '+c.hop,\
				p[0].number,p[0].service,p[1].number,p[1].service,' ',' ',' ',' '))
			elif len(p) == 1:
				list.append((c.number, c.caid, c.system, 'reshare = '+c.reshare, 'hops = '+c.hop,\
				p[0].number,p[0].service,' ',' ',' ',' ',' ',' '))
		self["data"].setList(list)
		self.r = r

	def yellowPressed(self):
		self.session.open(ReaderServiceDataScreen, self.r)

# LogDataList...
class LogDataList(MenuList):
	def __init__(self, list, fontSize, enableWrapAround = True):
		MenuList.__init__(self, list, enableWrapAround, eListboxPythonMultiContent)
		self.fontSize = fontSize
		self.l.setFont(0, gFont("Regular", self.fontSize))

	def postWidgetCreate(self, instance):
		MenuList.postWidgetCreate(self, instance)
		instance.setItemHeight(self.fontSize + 2)

# LogDataScreen...
class LogDataScreen(DownloadXMLScreen):
	w = getDesktop(0).size().width()
	if w == 1280:
		skin = """
			<screen flags="wfNoBorder" position="0,0" size="1280,720" name="LogDataScreen" >
				<widget render="Label" source="title"  position="40,70" size="700,26" valign="center" zPosition="5" transparent="0" foregroundColor="#fcc000" font="Regular;22"/>
				<widget name="data" position="40,120" size="1200,500" scrollbarMode="showOnDemand" />
			</screen>"""
	elif w == 1024: 
		skin = """
			<screen flags="wfNoBorder" position="0,0" size="1024,576" name="LogDataScreen" >
				<widget render="Label" source="title"  position="42,70" size="700,26" valign="center" zPosition="5" transparent="0" foregroundColor="#fcc000" font="Regular;22"/>
				<widget name="data" position="42,100" size="940,400" scrollbarMode="showOnDemand" />
			</screen>"""
	else:
		skin = """
			<screen flags="wfNoBorder" position="0,0" size="720,576" name="LogDataScreen" >
				<widget render="Label" source="title"  position="10,70" size="700,26" valign="center" zPosition="5" transparent="0" foregroundColor="#fcc000" font="Regular;22"/>
				<widget name="data" position="10,130" size="700,352" scrollbarMode="showOnDemand" />
			</screen>"""
	
	def __init__(self, session, part, oServer):
		self.oServer = oServer
		self.skin = LogDataScreen.skin
		self.session = session
		Screen.__init__(self, session)

		DownloadXMLScreen.__init__(self, session, part, oServer)

		if LogDataScreen.w == 1280:
			self.entryW = 1198
			self.entryH = 18
			self["data"] = LogDataList([], 18)
		elif LogDataScreen.w == 1024:
			self.entryW = 938
			self.entryH = 16
			self["data"] = LogDataList([], 16)
		else:
			self.entryW = 698
			self.entryH = 14
			self["data"] = LogDataList([], 14)

		self["actions"] = ActionMap(["OkCancelActions"],
		{
			"cancel": self.Close
		}, -1)

	def parseXML(self, dom):
		d = "n/a"
		for node in dom.getElementsByTagName("log"):
			d = str(node.firstChild.nodeValue.strip())
		# in der OScam ist noch ein kleiner Bug, im Log befindet sich ein "´",
		# hier steigt der XML Parser aus, wird daher durch ein "'" ersetzt...
		d = d.replace("\xb4","\x27")
		return d

	def dlAction(self):
		dom = xml.dom.minidom.parseString(self.data)
		log = self.parseXML(dom)

		self.setTitle("Logfile@"+self.oServer.serverName)
		list = []
		for line in log.splitlines():
			if "rejected" in line or "invalid" in line: c = "0xff2222" # rot
			elif "written" in line: c = "0xff8c00" # orange
			elif " r " in line: c = "0xffd700" # gelb
			elif " p " in line: c = "0xadff2f" # gruen
			else: c = "0xffffff" # weiss
			item = [line]
			item.append((eListboxPythonMultiContent.TYPE_TEXT, 1, 1, self.entryW, self.entryH, 0, RT_HALIGN_LEFT, line, int(c, 16)))
			list.append(item)
		self["data"].setList(list)
		self["data"].selectionEnabled(0)
		# immer an das Listenende...
		x = len(list)
		if x:
			self["data"].moveToIndex(x-1)

# ReaderlistScreen...
class ReaderlistScreen(DownloadXMLScreen):
	skin = """
		<screen flags="wfNoBorder" position="%d,0" size="440,%d" name="ReaderlistScreen" >
			<widget render="Label" source="title"  position=" 20, 80" size="400,26" valign="center" zPosition="5" transparent="0" foregroundColor="#fcc000" font="Regular;22"/>
			<widget render="Label" source="label0" position=" 50,130" size="180,20" valign="center" zPosition="5" transparent="0" foregroundColor="black" backgroundColor="white" font="Regular;18"/>
			<widget render="Label" source="label1" position="230,130" size="130,20" valign="center" zPosition="5" transparent="0" foregroundColor="black" backgroundColor="white" font="Regular;18"/>
			<widget render="Label" source="label2" position="360,130" size=" 60,20" valign="center" zPosition="5" transparent="0" foregroundColor="black" backgroundColor="white" font="Regular;18"/>
			<widget source="data" render="Listbox" position="20,153" size="400,288" scrollbarMode="showOnDemand">
				<convert type="TemplatedMultiContent">
					{"template": [
						MultiContentEntryPixmapAlphaTest(pos = (5, 4), size = (16, 16), png = 0),
						MultiContentEntryText(pos = ( 30, 2), size = (175, 24), font=0, flags = RT_HALIGN_LEFT, text = 1),
						MultiContentEntryText(pos = (215, 2), size = (135, 24), font=0, flags = RT_HALIGN_LEFT, text = 2),
						MultiContentEntryText(pos = (355, 2), size = ( 15, 24), font=0, flags = RT_HALIGN_LEFT, text = 3),
					],
					"fonts": [gFont("Regular", 20)],
					"itemHeight": 24
					}
				</convert>
			</widget>
			<eLabel text="" position="20,450" size="400,2" transparent="0" backgroundColor="#ffffff" />
			<widget name="ButtonRed" pixmap="skin_default/buttons/red.png" position="20,460" size="140,40" zPosition="4" transparent="1" alphatest="on"/>
			<widget name="ButtonRedtext" position="20,460" size="140,40" valign="center" halign="center" zPosition="5" transparent="1" foregroundColor="white" font="Regular;16"/>
			<widget name="ButtonGreen" pixmap="skin_default/buttons/green.png" position="20,460" size="140,40" zPosition="2" transparent="1" alphatest="on"/>
			<widget name="ButtonGreentext" position="20,460" size="140,40" valign="center" halign="center" zPosition="3" transparent="1" foregroundColor="white" font="Regular;16"/>
		</screen>""" % (dlg_xh(440))
	
	def __init__(self, session, part, oServer):
		self.oServer = oServer
		self.skin = ReaderlistScreen.skin
		self.session = session
		Screen.__init__(self, session)

		DownloadXMLScreen.__init__(self, session, part, oServer)

		self["label0"] = StaticText(_("Reader"))
		self["label1"] = StaticText(_("Protocol"))
		self["label2"] = StaticText(_("type"))

		self["data"] = List(list)
		self["ButtonRed"] = Pixmap()
		self["ButtonRedtext"] = Button(_("disable reader"))
		self["ButtonGreen"] = Pixmap()
		self["ButtonGreentext"] = Button(_("enable reader"))

		self["actions"] = ActionMap(["OkCancelActions", "ColorActions", "DirectionActions"],
		{
			"up": self.upPressed,
			"down": self.downPressed,			
			"left": self.leftPressed,
			"right": self.rightPressed,			
			"red": self.redPressed,
			"green": self.greenPressed,
			"cancel": self.Close
		}, -1)

		self.icondis = LoadPixmap(cached=True, path=resolveFilename(SCOPE_CURRENT_PLUGIN, 
		                      "Extensions/OscamStatus/icons/disabled.png"))
		self.iconena = LoadPixmap(cached=True, path=resolveFilename(SCOPE_CURRENT_PLUGIN, 
		                      "Extensions/OscamStatus/icons/enabled.png"))

		self.isReadonly = True
		self.index = 0

		self.onLayoutFinish.append(self.LayoutFinished)

	def LayoutFinished(self):
		x,h = dlg_xh(self.instance.size().width())
		self.instance.move(ePoint(x, 0))

	def parseXML(self, dom):
		readers = []
		# Abfragen ob WebIf readonly...
		for node in dom.getElementsByTagName("oscam"):
			readonly = str(node.getAttribute("readonly"))
		if not int(readonly):
			self.isReadonly = False
		for elem in dom.getElementsByTagName("readers"):
			for node in elem.getElementsByTagName("reader"):
				r = readerlist()
				r.label = str(node.getAttribute("label"))
				r.protocol = str(node.getAttribute("protocol"))
				r.type = str(node.getAttribute("type"))
				r.enabled = str(node.getAttribute("enabled"))
				readers.append(r)
		return readers

	def dlAction(self):
		if self.newurl:
			self.newurl = False
			self.url = self.oldurl
			self.downloadXML()
			self.timer.start(TIMERTICK)
			return
		dom = xml.dom.minidom.parseString(self.data)
		self.readers = self.parseXML(dom)
		self.setList()

	def setList(self):
		list = []
		self.setTitle("All Readers @"+self.oServer.serverName)
		for index, r in enumerate(self.readers):
			if r.enabled=="1":
				list.append((self.iconena, r.label, r.protocol, r.type, r.enabled, index))
			else:
				list.append((self.icondis, r.label, r.protocol, r.type, r.enabled, index))
		self["data"].setList(list)
		if self.index < len(list):
			self["data"].setIndex(self.index)

		if self.isReadonly:
			self["ButtonRed"].hide()
			self["ButtonRedtext"].hide()
			self["ButtonGreen"].hide()
			self["ButtonGreentext"].hide()
		self.setupButtons()

	def setupButtons(self):
		self.index = self["data"].getCurrent()[5]
		if not self.isReadonly:
			if self["data"].getCurrent()[4] == "0":
				self["ButtonRed"].hide()
				self["ButtonRedtext"].hide()
				self["ButtonGreen"].show()
				self["ButtonGreentext"].show()
			else:
				self["ButtonRed"].show()
				self["ButtonRedtext"].show()
				self["ButtonGreen"].hide()
				self["ButtonGreentext"].hide()

	def redPressed(self):
		if self.download or \
		   self["data"].count()==0 or \
		   self.isReadonly or \
		   self["data"].getCurrent()[4] == "0":
			return
		part = "readerlist&action=disable&label="+self["data"].getCurrent()[1]
		self.sendNewPart(part)

	def greenPressed(self):
		if self.download or \
		   self["data"].count()==0 or \
		   self.isReadonly or \
		   self["data"].getCurrent()[4] == "1":
			return
		part = "readerlist&action=enable&label="+self["data"].getCurrent()[1]
		self.sendNewPart(part)

	def upPressed(self):
		if self.download:
			return
		self["data"].selectPrevious()
		self.setupButtons()

	def downPressed(self):			
		if self.download:
			return
		self["data"].selectNext()
		self.setupButtons()

	def leftPressed(self):
		pass

	def rightPressed(self):			
		pass

# ReaderstatsScreen...
class ReaderstatsScreen(DownloadXMLScreen):
	# picon skin...
	skin1 = """
		<screen flags="wfNoBorder" position="%d,0" size="720,%d" name="ReaderstatsScreen" >
			<widget render="Label" source="title" position="20,80" size="680,26" valign="center" zPosition="5" transparent="0" foregroundColor="#fcc000" font="Regular;22"/>
			<widget render="Label" source="lEMMerror"   position="  0,130" size="175,40" valign="center" halign="center" zPosition="5" transparent="1" foregroundColor="white" font="Regular;18"/>
			<widget render="Label" source="lEMMwritten" position="175,130" size="175,40" valign="center" halign="center" zPosition="5" transparent="1" foregroundColor="white" font="Regular;18"/>
			<widget render="Label" source="lEMMskipped" position="350,130" size="175,40" valign="center" halign="center" zPosition="5" transparent="1" foregroundColor="white" font="Regular;18"/>
			<widget render="Label" source="lEMMblocked" position="525,130" size="175,40" valign="center" halign="center" zPosition="5" transparent="1" foregroundColor="white" font="Regular;18"/>
			<widget name="EMMerror"   position="  0,170" size="175,20" valign="center" halign="center" zPosition="5" transparent="1" foregroundColor="white" font="Regular;18"/>
			<widget name="EMMwritten" position="175,170" size="175,20" valign="center" halign="center" zPosition="5" transparent="1" foregroundColor="white" font="Regular;18"/>
			<widget name="EMMskipped" position="350,170" size="175,20" valign="center" halign="center" zPosition="5" transparent="1" foregroundColor="white" font="Regular;18"/>
			<widget name="EMMblocked" position="525,170" size="175,20" valign="center" halign="center" zPosition="5" transparent="1" foregroundColor="white" font="Regular;18"/>
			<widget render="Label" source="label0" position=" 20,203" size=" 60,20" valign="center" zPosition="5" transparent="0" foregroundColor="black" backgroundColor="white" font="Regular;18"/>
			<widget render="Label" source="label1" position=" 80,203" size="225,20" valign="center" zPosition="5" transparent="0" foregroundColor="black" backgroundColor="white" font="Regular;18"/>
			<widget render="Label" source="label2" position="305,203" size="110,20" valign="center" zPosition="5" transparent="0" foregroundColor="black" backgroundColor="white" font="Regular;18"/>
			<widget render="Label" source="label3" position="415,203" size="100,20" valign="center" zPosition="5" transparent="0" foregroundColor="black" backgroundColor="white" font="Regular;18"/>
			<widget render="Label" source="label4" position="515,203" size=" 95,20" valign="center" zPosition="5" transparent="0" foregroundColor="black" backgroundColor="white" font="Regular;18"/>
			<widget render="Label" source="label5" position="610,203" size=" 90,20" valign="center" zPosition="5" transparent="0" foregroundColor="black" backgroundColor="white" font="Regular;18"/>
			<widget source="data" render="Listbox" position="20,225" size="680,288" scrollbarMode="showOnDemand">
				<convert type="TemplatedMultiContent">
					{"template": [
						MultiContentEntryText(pos = (  5, 2), size = ( 50, 24), font=0, flags = RT_HALIGN_RIGHT, text = 0),
						MultiContentEntryText(pos = (115, 2), size = (165, 24), font=0, flags = RT_HALIGN_LEFT, text = 1),
						MultiContentEntryText(pos = (285, 2), size = (105, 24), font=0, flags = RT_HALIGN_LEFT, text = 2),
						MultiContentEntryText(pos = (400, 2), size = ( 95, 24), font=0, flags = RT_HALIGN_LEFT, text = 3),
						MultiContentEntryText(pos = (500, 2), size = ( 90, 24), font=0, flags = RT_HALIGN_LEFT, text = 4),
						MultiContentEntryText(pos = (590, 2), size = ( 90, 24), font=0, flags = RT_HALIGN_LEFT, text = 5),
						MultiContentEntryPixmapAlphaTest(pos = (65, 0), size = (50, 30), png = 6),
					],
					"fonts": [gFont("Regular", 20)],
					"itemHeight": 30
					}
				</convert>
			</widget>
		</screen>""" % (dlg_xh(720))

	# ohne picons...
	skin2 = """
		<screen flags="wfNoBorder" position="%d,0" size="720,%d" name="ReaderstatsScreen" >
			<widget render="Label" source="title" position="20,80" size="680,26" valign="center" zPosition="5" transparent="0" foregroundColor="#fcc000" font="Regular;22"/>
			<widget render="Label" source="lEMMerror"   position="  0,130" size="175,40" valign="center" halign="center" zPosition="5" transparent="1" foregroundColor="white" font="Regular;18"/>
			<widget render="Label" source="lEMMwritten" position="175,130" size="175,40" valign="center" halign="center" zPosition="5" transparent="1" foregroundColor="white" font="Regular;18"/>
			<widget render="Label" source="lEMMskipped" position="350,130" size="175,40" valign="center" halign="center" zPosition="5" transparent="1" foregroundColor="white" font="Regular;18"/>
			<widget render="Label" source="lEMMblocked" position="525,130" size="175,40" valign="center" halign="center" zPosition="5" transparent="1" foregroundColor="white" font="Regular;18"/>
			<widget name="EMMerror"   position="  0,170" size="175,20" valign="center" halign="center" zPosition="5" transparent="1" foregroundColor="white" font="Regular;18"/>
			<widget name="EMMwritten" position="175,170" size="175,20" valign="center" halign="center" zPosition="5" transparent="1" foregroundColor="white" font="Regular;18"/>
			<widget name="EMMskipped" position="350,170" size="175,20" valign="center" halign="center" zPosition="5" transparent="1" foregroundColor="white" font="Regular;18"/>
			<widget name="EMMblocked" position="525,170" size="175,20" valign="center" halign="center" zPosition="5" transparent="1" foregroundColor="white" font="Regular;18"/>
			<widget render="Label" source="label0" position=" 20,203" size=" 60,20" valign="center" zPosition="5" transparent="0" foregroundColor="black" backgroundColor="white" font="Regular;18"/>
			<widget render="Label" source="label1" position=" 80,203" size="225,20" valign="center" zPosition="5" transparent="0" foregroundColor="black" backgroundColor="white" font="Regular;18"/>
			<widget render="Label" source="label2" position="305,203" size="110,20" valign="center" zPosition="5" transparent="0" foregroundColor="black" backgroundColor="white" font="Regular;18"/>
			<widget render="Label" source="label3" position="415,203" size="100,20" valign="center" zPosition="5" transparent="0" foregroundColor="black" backgroundColor="white" font="Regular;18"/>
			<widget render="Label" source="label4" position="515,203" size=" 95,20" valign="center" zPosition="5" transparent="0" foregroundColor="black" backgroundColor="white" font="Regular;18"/>
			<widget render="Label" source="label5" position="610,203" size=" 90,20" valign="center" zPosition="5" transparent="0" foregroundColor="black" backgroundColor="white" font="Regular;18"/>
			<widget source="data" render="Listbox" position="20,225" size="680,288" scrollbarMode="showOnDemand">
				<convert type="TemplatedMultiContent">
					{"template": [
						MultiContentEntryText(pos = (  5, 2), size = ( 50, 24), font=0, flags = RT_HALIGN_RIGHT, text = 0),
						MultiContentEntryText(pos = ( 65, 2), size = (215, 24), font=0, flags = RT_HALIGN_LEFT, text = 1),
						MultiContentEntryText(pos = (285, 2), size = (105, 24), font=0, flags = RT_HALIGN_LEFT, text = 2),
						MultiContentEntryText(pos = (400, 2), size = ( 95, 24), font=0, flags = RT_HALIGN_LEFT, text = 3),
						MultiContentEntryText(pos = (500, 2), size = ( 90, 24), font=0, flags = RT_HALIGN_LEFT, text = 4),
						MultiContentEntryText(pos = (590, 2), size = ( 90, 24), font=0, flags = RT_HALIGN_LEFT, text = 5),
					],
					"fonts": [gFont("Regular", 20)],
					"itemHeight": 24
					}
				</convert>
			</widget>
		</screen>""" % (dlg_xh(720))

	def __init__(self, session, reader, part, oServer):
		self.oServer = oServer
		self.reader = reader
		if USEPICONS.value:
			self.skin = ReaderstatsScreen.skin1
		else:
			self.skin = ReaderstatsScreen.skin2
		self.session = session
		Screen.__init__(self, session)

		DownloadXMLScreen.__init__(self, session, part, oServer)

		self["label0"] = StaticText(_("req."))
		self["label1"] = StaticText(_("channelname"))
		self["label2"] = StaticText(_("caid:srvid"))
		self["label3"] = StaticText(_("status"))
		self["label4"] = StaticText(_("lasttime"))
		self["label5"] = StaticText(_("avgtime"))
		self["lEMMerror"]   = StaticText("EMM error\nUK / G / S / UQ")
		self["lEMMwritten"] = StaticText("EMM written\nUK / G / S / UQ")
		self["lEMMskipped"] = StaticText("EMM skipped\nUK / G / S / UQ")
		self["lEMMblocked"] = StaticText("EMM blocked\nUK / G / S / UQ")
		self["EMMerror"]   = Label("")
		self["EMMwritten"] = Label("")
		self["EMMskipped"] = Label("")
		self["EMMblocked"] = Label("")

		self["data"] = List(list)

		self["actions"] = ActionMap(["OkCancelActions"],
		{
			"cancel": self.Close
		}, -1)

		self.onLayoutFinish.append(self.LayoutFinished)

	def LayoutFinished(self):
		x,h = dlg_xh(self.instance.size().width())
		self.instance.move(ePoint(x, 0))

	def parseXML(self, dom):
		emms = []
		ecms = []
		for elem in dom.getElementsByTagName("emmstats"):
			for node in elem.getElementsByTagName("emm"):
				em = emm()
				em.type = str(node.getAttribute("type"))
				em.result = str(node.getAttribute("result"))
				if node.firstChild and node.firstChild.nodeType == node.firstChild.TEXT_NODE:
					em.val = str(node.firstChild.nodeValue.strip())
				emms.append(em)
		for elem in dom.getElementsByTagName("ecmstats"):
			for node in elem.getElementsByTagName("ecm"):
				ec = ecm()
				ec.caid = str(node.getAttribute("caid"))
				ec.provid = str(node.getAttribute("provid"))
				ec.srvid = str(node.getAttribute("srvid"))
				ec.channelname = str(node.getAttribute("channelname"))
				ec.avgtime = str(node.getAttribute("avgtime"))
				ec.lasttime = str(node.getAttribute("lasttime"))
				ec.rc = str(node.getAttribute("rc"))
				ec.rcs = str(node.getAttribute("rcs"))
				ec.lastrequest = str(node.getAttribute("lastrequest"))
				if node.firstChild and node.firstChild.nodeType == node.firstChild.TEXT_NODE:
					ec.val = str(node.firstChild.nodeValue.strip())
				ecms.append(ec)
		return emms, ecms

	def dlAction(self):
		dom = xml.dom.minidom.parseString(self.data)
		self.emms, self.ecms = self.parseXML(dom)
		self.setList()

	def setList(self):
		def compare(a, b):
			return cmp(int(b[0]), int(a[0]))
		EMMerror = EMMwritten = EMMskipped = EMMblocked = ""
		for r in ["error", "written", "skipped", "blocked"]:
			for t in ["unknown", "global", "shared", "unique"]:
				for e in self.emms:
					if r==e.result and t==e.type:
						if r == "error":
							EMMerror += e.val+" / "
						if r == "written":
							EMMwritten += e.val+" / "
						if r == "skipped":
							EMMskipped += e.val+" / "
						if r == "blocked":
							EMMblocked += e.val+" / "
		self["EMMerror"].setText(EMMerror[:-3])
		self["EMMwritten"].setText(EMMwritten[:-3])
		self["EMMskipped"].setText(EMMskipped[:-3])
		self["EMMblocked"].setText(EMMblocked[:-3])

		list = []
		self.setTitle("Status Reader "+self.reader+" @"+self.oServer.serverName)
		for e in self.ecms:
			if USEPICONS.value:
				picon = picons.getPicon(e.srvid)
				list.append((e.val, e.channelname, e.caid+":"+e.srvid, e.rcs, e.lasttime, e.avgtime, picon))
			else:
				list.append((e.val, e.channelname, e.caid+":"+e.srvid, e.rcs, e.lasttime, e.avgtime))

		# Nach Anzahl der Anfragen sortieren...
		list.sort(compare)
		self["data"].setList(list)
		self["data"].setIndex(self.oldIndex)

# UserstatsScreen...
class UserstatsScreen(DownloadXMLScreen):
	skin = """
		<screen flags="wfNoBorder" position="%d,0" size="720,%d" name="UserstatsScreen" >
			<widget render="Label" source="title"  position=" 20, 80" size="680,26" valign="center" zPosition="5" transparent="0" foregroundColor="#fcc000" font="Regular;22"/>
			<widget render="Label" source="label0" position=" 50,130" size="145,20" valign="center" zPosition="5" transparent="0" foregroundColor="black" backgroundColor="white" font="Regular;18"/>
			<widget render="Label" source="label1" position="195,130" size="145,20" valign="center" zPosition="5" transparent="0" foregroundColor="black" backgroundColor="white" font="Regular;18"/>
			<widget render="Label" source="label2" position="340,130" size="155,20" valign="center" zPosition="5" transparent="0" foregroundColor="black" backgroundColor="white" font="Regular;18"/>
			<widget render="Label" source="label3" position="495,130" size="205,20" valign="center" zPosition="5" transparent="0" foregroundColor="black" backgroundColor="white" font="Regular;18"/>
			<widget source="data" render="Listbox" position="20,153" size="680,288" scrollbarMode="showOnDemand">
				<convert type="TemplatedMultiContent">
					{"template": [
						MultiContentEntryPixmapAlphaTest(pos = (5, 4), size = (16, 16), png = 0),
						MultiContentEntryText(pos = ( 30, 2), size = (140, 24), font=0, flags = RT_HALIGN_LEFT, text = 1),
						MultiContentEntryText(pos = (175, 2), size = (140, 24), font=0, flags = RT_HALIGN_LEFT, text = 2),
						MultiContentEntryText(pos = (320, 2), size = (150, 24), font=0, flags = RT_HALIGN_LEFT, text = 3),
						MultiContentEntryText(pos = (475, 2), size = (205, 24), font=0, flags = RT_HALIGN_LEFT, text = 4),
					],
					"fonts": [gFont("Regular", 20)],
					"itemHeight": 24
					}
				</convert>
			</widget>
			<eLabel text="" position="20,450" size="680,2" transparent="0" backgroundColor="#ffffff" />
			<widget name="ButtonRed" pixmap="skin_default/buttons/red.png" position="20,460" size="140,40" zPosition="4" transparent="1" alphatest="on"/>
			<widget name="ButtonRedtext" position="20,460" size="140,40" valign="center" halign="center" zPosition="5" transparent="1" foregroundColor="white" font="Regular;16"/>
			<widget name="ButtonGreen" pixmap="skin_default/buttons/green.png" position="20,460" size="140,40" zPosition="2" transparent="1" alphatest="on"/>
			<widget name="ButtonGreentext" position="20,460" size="140,40" valign="center" halign="center" zPosition="3" transparent="1" foregroundColor="white" font="Regular;16"/>
		</screen>""" % (dlg_xh(720))
	
	def __init__(self, session, part, oServer):
		self.oServer = oServer
		self.skin = UserstatsScreen.skin
		self.session = session
		Screen.__init__(self, session)

		DownloadXMLScreen.__init__(self, session, part, oServer)

		self["label0"] = StaticText(_("Client"))
		self["label1"] = StaticText(_("IP"))
		self["label2"] = StaticText(_("Status"))
		self["label3"] = StaticText(_("Protocol"))
		self["data"] = List(list, enableWrapAround = True)
		self["ButtonRed"] = Pixmap()
		self["ButtonRedtext"] = Button(_("disable client"))
		self["ButtonGreen"] = Pixmap()
		self["ButtonGreentext"] = Button(_("enable client"))

		self["actions"] = ActionMap(["OkCancelActions", "ColorActions", "DirectionActions"],
		{
			"up": self.upPressed,
			"down": self.downPressed,			
			"left": self.leftPressed,
			"right": self.rightPressed,			
			"red": self.redPressed,
			"green": self.greenPressed,
			"cancel": self.Close
		}, -1)

		self.icondis = LoadPixmap(cached=True, path=resolveFilename(SCOPE_CURRENT_PLUGIN, 
		                      "Extensions/OscamStatus/icons/disabled.png"))
		self.iconena = LoadPixmap(cached=True, path=resolveFilename(SCOPE_CURRENT_PLUGIN, 
		                      "Extensions/OscamStatus/icons/enabled.png"))

		self.isReadonly = True
		self.index = 0
        
		self.onLayoutFinish.append(self.LayoutFinished)

	def LayoutFinished(self):
		x,h = dlg_xh(self.instance.size().width())
		self.instance.move(ePoint(x, 0))

	def parseXML(self, dom):
		d = []
		# Abfragen Oscam Version und Berechtigung WebIf...
		for node in dom.getElementsByTagName("oscam"):
			revision = str(node.getAttribute("revision"))
			readonly = str(node.getAttribute("readonly"))
		# erst ab 5442 kann in der xml api geschrieben werden...
		if int(revision) > 5441 and not int(readonly):
			self.isReadonly = False

		for elem in dom.getElementsByTagName("users"):
			for node in elem.getElementsByTagName("user"):
				u = user()
				u.name = str(node.getAttribute("name"))
				u.status = str(node.getAttribute("status"))
				u.ip = str(node.getAttribute("ip"))
				u.protocol = str(node.getAttribute("protocol"))
				# erst ab 6793 gibt es "timeonchannel"...
				if int(revision) > 6792:
					for node2 in node.getElementsByTagName("stats"):
						for snode in node2.childNodes:
							if snode.nodeName == "timeonchannel":
								if snode.firstChild and snode.firstChild.nodeType == snode.firstChild.TEXT_NODE:
									u.timeonchannel = str(snode.firstChild.nodeValue.strip())
				d.append(u)
		return d

	def dlAction(self):
		if self.newurl:
			self.newurl = False
			self.url = self.oldurl
			self.downloadXML()
			self.timer.start(TIMERTICK)
			return
		dom = xml.dom.minidom.parseString(self.data)
		self.users = self.parseXML(dom)
		self.setList()

	def setList(self):
		list = []
		self.setTitle("All Clients@"+self.oServer.serverName)
		for index, u in enumerate(self.users):
			if u.timeonchannel != "n/a":
				self["label1"].setText(_("time on channel"))
				u.ip = u.timeonchannel
			if "disabled" in u.status:
				list.append((self.icondis, u.name, u.ip, u.status, u.protocol, index))        
			else:
				list.append((self.iconena, u.name, u.ip, u.status, u.protocol, index))        
		self["data"].setList(list)
		if self.index < len(list):
			self["data"].setIndex(self.index)

		if self.isReadonly:
			self["ButtonRed"].hide()
			self["ButtonRedtext"].hide()
			self["ButtonGreen"].hide()
			self["ButtonGreentext"].hide()
		self.setupButtons()

	def setupButtons(self):
		self.index = self["data"].getCurrent()[5]
		if not self.isReadonly:
			if "disabled" in self["data"].getCurrent()[3]:
				self["ButtonRed"].hide()
				self["ButtonRedtext"].hide()
				self["ButtonGreen"].show()
				self["ButtonGreentext"].show()
			else:
				self["ButtonRed"].show()
				self["ButtonRedtext"].show()
				self["ButtonGreen"].hide()
				self["ButtonGreentext"].hide()

	def redPressed(self):
		if self.download or \
		   self["data"].count()==0 or \
		   self.isReadonly or \
		   "disabled" in self["data"].getCurrent()[3]:
			return
		part = "userconfig&user="+self["data"].getCurrent()[1]+"&disabled=1&action=Save"
		self.sendNewPart(part)

	def greenPressed(self):
		if self.download or \
		   self["data"].count()==0 or \
		   self.isReadonly or \
		   not "disabled" in self["data"].getCurrent()[3]:
			return
		part = "userconfig&user="+self["data"].getCurrent()[1]+"&disabled=0&action=Save"
		self.sendNewPart(part)

	def upPressed(self):
		if self.download:
			return
		self["data"].selectPrevious()
		self.setupButtons()

	def downPressed(self):			
		if self.download:
			return
		self["data"].selectNext()
		self.setupButtons()

	def leftPressed(self):
		pass

	def rightPressed(self):			
		pass

# StatusDataScreen...
class StatusDataScreen(DownloadXMLScreen):
	# picon skin...
	skin1 = """
		<screen flags="wfNoBorder" position="%d,0" size="720,%d" name="StatusDataScreen" >
			<widget render="Label" source="title"  position=" 20, 80" size="680,26" valign="center" zPosition="5" transparent="0" foregroundColor="#fcc000" font="Regular;22"/>
			<widget render="Label" source="label0" position=" 20,130" size="140,20" valign="center" zPosition="5" transparent="0" foregroundColor="black" backgroundColor="white" font="Regular;18"/>
			<widget render="Label" source="label1" position="160,130" size="146,20" valign="center" zPosition="5" transparent="0" foregroundColor="black" backgroundColor="white" font="Regular;18"/>
			<widget render="Label" source="label2" position="306,130" size=" 34,20" valign="center" zPosition="5" transparent="0" foregroundColor="black" backgroundColor="white" font="Regular;18"/>
			<widget render="Label" source="label3" position="340,130" size=" 80,20" valign="center" zPosition="5" transparent="0" foregroundColor="black" backgroundColor="white" font="Regular;18"/>
			<widget render="Label" source="label4" position="420,130" size="280,20" valign="center" zPosition="5" transparent="0" foregroundColor="black" backgroundColor="white" font="Regular;17"/>
			<widget source="data" render="Listbox" position="20,153" size="680,288" scrollbarMode="showOnDemand">
				<convert type="TemplatedMultiContent">
					{"template": [
						MultiContentEntryText(pos = (  2, 2), size = (140, 24), font=0, flags = RT_HALIGN_LEFT, text = 0),
						MultiContentEntryText(pos = (145, 2), size = (140, 24), font=0, flags = RT_HALIGN_LEFT, text = 1),
						MultiContentEntryPixmapAlphaTest(pos = (290, 4), size = (16, 16), png = 2),
						MultiContentEntryText(pos = (310, 2), size = ( 90, 24), font=0, flags = RT_HALIGN_CENTER, text = 3),
						MultiContentEntryPixmapAlphaTest(pos = (405, 0), size = (50, 30), png = 6),
						MultiContentEntryText(pos = (460, 2), size = (220, 24), font=1, flags = RT_HALIGN_LEFT, text = 4),
					],
					"fonts": [gFont("Regular", 20), gFont("Regular", 18)],
					"itemHeight": 30
					}
				</convert>
			</widget>
			<eLabel text="" position="20,450" size="680,2" transparent="0" backgroundColor="#ffffff" />
			<widget name="ButtonYellow" pixmap="skin_default/buttons/yellow.png" position="20,460" size="140,40" zPosition="4" transparent="1" alphatest="on"/>
			<widget name="ButtonYellowtext" position="20,460" size="140,40" valign="center" halign="center" zPosition="5" transparent="1" foregroundColor="white" font="Regular;16"/>
			<widget name="ButtonBlue" pixmap="skin_default/buttons/blue.png" position="160,460" size="140,40" zPosition="4" transparent="1" alphatest="on"/>
			<widget name="ButtonBluetext" position="160,460" size="140,40" valign="center" halign="center" zPosition="5" transparent="1" foregroundColor="white" font="Regular;16"/>
		</screen>""" % (dlg_xh(720))
	# ohne picons...
	skin2 = """
		<screen flags="wfNoBorder" position="%d,0" size="720,%d" name="StatusDataScreen" >
			<widget render="Label" source="title"  position=" 20, 80" size="680,26" valign="center" zPosition="5" transparent="0" foregroundColor="#fcc000" font="Regular;22"/>
			<widget render="Label" source="label0" position=" 20,130" size="160,20" valign="center" zPosition="5" transparent="0" foregroundColor="black" backgroundColor="white" font="Regular;18"/>
			<widget render="Label" source="label1" position="180,130" size="150,20" valign="center" zPosition="5" transparent="0" foregroundColor="black" backgroundColor="white" font="Regular;18"/>
			<widget render="Label" source="label2" position="326,130" size=" 34,20" valign="center" zPosition="5" transparent="0" foregroundColor="black" backgroundColor="white" font="Regular;18"/>
			<widget render="Label" source="label3" position="360,130" size=" 90,20" valign="center" zPosition="5" transparent="0" foregroundColor="black" backgroundColor="white" font="Regular;18"/>
			<widget render="Label" source="label4" position="450,130" size="250,20" valign="center" zPosition="5" transparent="0" foregroundColor="black" backgroundColor="white" font="Regular;17"/>
			<widget source="data" render="Listbox" position="20,153" size="680,288" scrollbarMode="showOnDemand">
				<convert type="TemplatedMultiContent">
					{"template": [
						MultiContentEntryText(pos = (  2, 2), size = (154, 24), font=0, flags = RT_HALIGN_LEFT, text = 0),
						MultiContentEntryText(pos = (160, 2), size = (158, 24), font=0, flags = RT_HALIGN_LEFT, text = 1),
						MultiContentEntryPixmapAlphaTest(pos = (310, 4), size = (16, 16), png = 2),
						MultiContentEntryText(pos = (340, 2), size = ( 88, 24), font=0, flags = RT_HALIGN_LEFT, text = 3),
						MultiContentEntryText(pos = (430, 2), size = (250, 24), font=0, flags = RT_HALIGN_LEFT, text = 4),
					],
					"fonts": [gFont("Regular", 20)],
					"itemHeight": 24
					}
				</convert>
			</widget>
			<eLabel text="" position="20,450" size="680,2" transparent="0" backgroundColor="#ffffff" />
			<widget name="ButtonYellow" pixmap="skin_default/buttons/yellow.png" position="20,460" size="140,40" zPosition="4" transparent="1" alphatest="on"/>
			<widget name="ButtonYellowtext" position="20,460" size="140,40" valign="center" halign="center" zPosition="5" transparent="1" foregroundColor="white" font="Regular;16"/>
			<widget name="ButtonBlue" pixmap="skin_default/buttons/blue.png" position="160,460" size="140,40" zPosition="4" transparent="1" alphatest="on"/>
			<widget name="ButtonBluetext" position="160,460" size="140,40" valign="center" halign="center" zPosition="5" transparent="1" foregroundColor="white" font="Regular;16"/>
		</screen>""" % (dlg_xh(720))

	def __init__(self, session, type, part, oServer):
		self.type = type
		self.oServer = oServer
		if USEPICONS.value:
			self.skin = StatusDataScreen.skin1
		else:
			self.skin = StatusDataScreen.skin2
		self.session = session
		Screen.__init__(self, session)

		DownloadXMLScreen.__init__(self, session, part, oServer)

		self["data"] = List([])
		if self.type == "clients":
			self["label0"] = StaticText(_("Name"))
			self["label1"] = StaticText(_("Reader"))
			self["label2"] = StaticText(_("AU"))
			if USEECM.value:
				self["label3"] = StaticText(_("ECM Time"))
			else:
				self["label3"] = StaticText(_("Idle Time"))
			self["label4"] = StaticText(_("Channel"))
			self["ButtonYellow"] = Pixmap()
			self["ButtonYellowtext"] = Button(_("show client info"))
			self["ButtonBlue"] = Pixmap()
			self["ButtonBluetext"] = Button(_("hide idle clients"))
		else:
			self["label0"] = StaticText(_("Name"))
			self["label1"] = StaticText(_("Status"))
			self["label2"] = StaticText(_("AU"))
			if USEECM.value:
				self["label3"] = StaticText(_("ECM Time"))
			else:
				self["label3"] = StaticText(_("Idle Time"))
			if USEPICONS.value:
				self["label4"] = StaticText(_("Channel  Protocol"))
			else:
				self["label4"] = StaticText(_("Protocol"))
			self["ButtonYellow"] = Pixmap()
			self["ButtonYellowtext"] = Button(_("show reader info"))
			self["ButtonBlue"] = Pixmap()
			self["ButtonBluetext"] = Button(_("show readerstats"))

		self["actions"] = ActionMap(["OkCancelActions", "ColorActions"],
		{
			"yellow": self.yellowPressed,
			"blue": self.bluePressed,
			"cancel": self.Close
		}, -1)

		auRed = LoadPixmap(cached=True, path=resolveFilename(SCOPE_CURRENT_PLUGIN, 
		                      "Extensions/OscamStatus/icons/au_red.png"))
		auGreen = LoadPixmap(cached=True, path=resolveFilename(SCOPE_CURRENT_PLUGIN, 
		                      "Extensions/OscamStatus/icons/au_green.png"))
		auYellow = LoadPixmap(cached=True, path=resolveFilename(SCOPE_CURRENT_PLUGIN, 
		                      "Extensions/OscamStatus/icons/au_yellow.png"))
		# dict fuer Status AU
		self.auEntrys = {"1":auGreen, "0":auRed, "-1":auYellow}

		self.hideIdle = False

		self.onLayoutFinish.append(self.LayoutFinished)

	def LayoutFinished(self):
		x,h = dlg_xh(self.instance.size().width())
		self.instance.move(ePoint(x, 0))

	def parseXML(self, dom):
		d = []
		for elem in dom.getElementsByTagName("status"):
			for node in elem.getElementsByTagName("client"):
				c = client()
				c.type = str(node.getAttribute("type"))
				c.name = str(node.getAttribute("name"))
				c.protocol = str(node.getAttribute("protocol"))
				c.protocolext = str(node.getAttribute("protocolext"))
				c.au = str(node.getAttribute("au"))

				for snode in node.childNodes:
					if snode.nodeName == "request":
						c.caid = str(snode.getAttribute("caid"))
						c.srvid = str(snode.getAttribute("srvid"))
						c.ecmtime = str(snode.getAttribute("ecmtime"))
						c.ecmhistory = str(snode.getAttribute("ecmhistory"))
						c.answered = str(snode.getAttribute("answered"))
						if snode.firstChild and snode.firstChild.nodeType == snode.firstChild.TEXT_NODE:
							c.service = str(snode.firstChild.nodeValue.strip())

					if snode.nodeName == "times":
						c.login = str(snode.getAttribute("login"))
						c.online = str(snode.getAttribute("online"))
						c.idle = str(snode.getAttribute("idle"))

					if snode.nodeName == "connection":
						c.ip = str(snode.getAttribute("ip"))
						c.port = str(snode.getAttribute("port"))
						if snode.firstChild and snode.firstChild.nodeType == snode.firstChild.TEXT_NODE:
							c.connection = str(snode.firstChild.nodeValue.strip())
				d.append(c)
		return d

	def dlAction(self):
		dom = xml.dom.minidom.parseString(self.data)
		self.status = self.parseXML(dom)
		self.setList()

	def setList(self):
		dlist = []
		if self.type == "clients":
			self.setTitle("Clients@"+self.oServer.serverName)
			for index, c in enumerate(self.status):
				if c.type == "c":
					if USEECM.value:
						idle = c.ecmtime
					else:
						idle = elapsedTime(c.idle, "%02d:%02d:%02d")

					if self.hideIdle:
						if c.answered != "":
							if USEPICONS.value:
								picon = picons.getPicon(c.srvid)
								dlist.append((c.name, c.answered, self.auEntrys[c.au], idle, c.service, index, picon))
							else:
								dlist.append((c.name, c.answered, self.auEntrys[c.au], idle, c.service, index))
					else:
						if USEPICONS.value:
							picon = picons.getPicon(c.srvid)
							dlist.append((c.name, c.answered, self.auEntrys[c.au], idle, c.service, index, picon))
						else:
							dlist.append((c.name, c.answered, self.auEntrys[c.au], idle, c.service, index))

		elif self.type == "readers":
			self.setTitle("Readers@"+self.oServer.serverName)
			for index, c in enumerate(self.status):
				if USEECM.value:
					idle = c.ecmtime
				else:
					idle = elapsedTime(c.idle, "%02d:%02d:%02d")

				if c.type == "r" or c.type == "p":
					if USEPICONS.value:
						picon = picons.getPicon(c.srvid)
						dlist.append((c.name, c.connection, self.auEntrys[c.au], idle, c.protocol, index, picon))
					else:
						dlist.append((c.name, c.connection, self.auEntrys[c.au], idle, c.protocol, index))

		self["data"].setList(dlist)
		self["data"].setIndex(self.oldIndex)

	def yellowPressed(self):
		if self.download or self["data"].count()==0:
			return
		index = self["data"].getCurrent()[5]
		if index is not None:
			self.timer.stop()
			self.session.openWithCallback(self.backCB, ClientDataScreen, self.type, self.oServer, self.status[index])

	def bluePressed(self):
		if self.download:
			return
		if self.type == "readers" and self["data"].count():
			reader = self["data"].getCurrent()[0]
			part = "readerstats&label="+reader
			self.session.openWithCallback(self.backCB, ReaderstatsScreen, reader, part, self.oServer)
			return
		if self.hideIdle:
			self["ButtonBluetext"].setText(_("hide idle clients"))
		else:
			self["ButtonBluetext"].setText(_("show idle clients"))
		self.hideIdle = not self.hideIdle
		self.setList()

	def backCB(self, retval):
		self.timer.start(TIMERTICK)

# mainScreen...
class OscamStatus(Screen):
	skin = """
		<screen flags="wfNoBorder" position="%d,0" size="440,%d" name="OscamStatus" >
			<widget render="Label" source="title" position="20,80" size="400,26" valign="center" zPosition="5" transparent="0" foregroundColor="#fcc000" font="Regular;22"/>
			<widget source="menu" render="Listbox" position="20,130" size="400,320" scrollbarMode="showOnDemand">
				<convert type="TemplatedMultiContent">
					{"template": [
						MultiContentEntryPixmapAlphaTest(pos = (5, 4), size = (32, 32), png = 0),
						MultiContentEntryText(pos = (50, 0), size = (280, 40), font=0, flags = RT_HALIGN_LEFT|RT_VALIGN_CENTER, text = 1),
					],
					"fonts": [gFont("Regular", 20)],
					"itemHeight": 40
					}
				</convert>
			</widget>
		</screen>"""  % (dlg_xh(440))

	def __init__(self, session):
		self.skin = OscamStatus.skin
		self.session = session
		Screen.__init__(self, session)

		ipath = resolveFilename(SCOPE_CURRENT_PLUGIN, "Extensions/OscamStatus/icons/")
		icon = []
		for i in ["icon1", "icon2", "icon3", "icon4", "icon5", "icon6", "icon7", "icon8"]:
			icon.append(LoadPixmap(cached = True, path = ipath + i + ".png"))

		list = []
		list.append((icon[0], _("show connected clients"), "clients"))        
		list.append((icon[1], _("show all clients"), "allClients"))        
		list.append((icon[2], _("show connected readers"), "readers"))        
		list.append((icon[3], _("show all readers"), "allReaders"))        
		list.append((icon[4], _("show logfile"), "log"))        
		list.append((icon[5], _("server info"), "info"))        
		list.append((icon[6], _("server restart/shutdown"), "restart"))        
		list.append((icon[7], _("server setup"), "setup"))        

		self["title"] = StaticText("")
		self["menu"] = List(list)
		self["actions"] = ActionMap(["OkCancelActions", "DirectionActions", "MenuActions"],
		{
			"ok": self.action,
			"cancel": self.close,
			"menu": self.globalsDlg
		}, -1)

		oscamServers = readCFG()
		index = LASTSERVER.value
		if index+1 > len(oscamServers):
			index = 0

		self.SetupCB(oscamServers[index])
		self.piconsLoaded = False
		if USEPICONS.value:
			self.loadPicons()

	def loadPicons(self):
		global picons
		picons = piconLoader(PICONPATH.value)
		if picons.hasLoaded:
			self.piconsLoaded = True
			print "[OscamStatus] Picons activated..."
		else:
			USEPICONS.value = False

	def action(self):
		returnValue = self["menu"].getCurrent()[2]
		if returnValue is not None:
			if returnValue is "clients":
				self.session.open(StatusDataScreen, "clients", "status", self.oServer)
			elif returnValue is "allClients":
				self.session.open(UserstatsScreen, "userstats", self.oServer)
			elif returnValue is "readers":
				self.session.open(StatusDataScreen, "readers", "status", self.oServer)
			elif returnValue is "allReaders":
				# part=readerlist erst ab 5773 ...
				self.session.open(ReaderlistScreen, "readerlist", self.oServer)
			elif returnValue is "log":
				self.session.open(LogDataScreen, "status&appendlog=1", self.oServer)
			elif returnValue is "info":
				self.session.open(OscamDataScreen, "status", self.oServer)
			elif returnValue is "setup":
				self.session.openWithCallback(self.SetupCB, OscamServerEntriesListConfigScreen)
			elif returnValue is "restart":
				self.session.open(OscamRestartScreen, "status", self.oServer)

	def SetupCB(self, entry):
		if entry:
			self.oServer = entry
			self["title"].setText("Oscam Status "+VERSION+" @"+self.oServer.serverName)

	def globalsDlg(self):
		self.oldpath = PICONPATH.value
		self.session.openWithCallback(self.globalsCB, globalsConfigScreen)

	def globalsCB(self):
		x,h = dlg_xh(self.instance.size().width())
		self.instance.move(ePoint(x, 0))
		if USEPICONS.value:
			if not self.piconsLoaded or self.oldpath != PICONPATH.value:
				self.loadPicons()

def main(session,**kwargs):
	session.open(OscamStatus)
def Plugins(**kwargs):
	l = [PluginDescriptor(name="Oscam Status", description=_("whats going on?"), where = PluginDescriptor.WHERE_PLUGINMENU, icon="OscamStatus.png", fnc=main)]
	if EXTMENU.value:
		l.append(PluginDescriptor(name="Oscam Status", description=_("whats going on?"), where = PluginDescriptor.WHERE_EXTENSIONSMENU, icon="OscamStatus.png", fnc=main))
	return l

