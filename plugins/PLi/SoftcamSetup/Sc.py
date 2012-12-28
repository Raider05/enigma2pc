from Screens.Screen import Screen
from Screens.MessageBox import MessageBox
from Components.FileList import FileEntryComponent, FileList
from Components.MenuList import MenuList
from Components.ActionMap import ActionMap, NumberActionMap
from Components.Button import Button
from Components.Label import Label
from Components.config import config, ConfigElement, ConfigSubsection, ConfigSelection, ConfigSubList, getConfigListEntry, KEY_LEFT, KEY_RIGHT, KEY_OK
from Components.ConfigList import ConfigList
from Components.Pixmap import Pixmap
import os
from camcontrol import CamControl
from enigma import eTimer, eDVBCI_UI, eListboxPythonStringContent, eListboxPythonConfigContent

class ConfigAction(ConfigElement):
	def __init__(self, action, *args):
		ConfigElement.__init__(self)
		self.value = "(OK)"
		self.action = action
		self.actionargs = args 
	def handleKey(self, key):
		if (key == KEY_OK):
			self.action(*self.actionargs) 

class ScSelection(Screen):
	skin = """
        <screen name="ScSelection" position="center,center" size="560,230" title="Softcam Setup">
                <widget name="entries" position="5,10" size="550,140" />
                <ePixmap name="red" position="0,190" zPosition="1" size="140,40" pixmap="skin_default/buttons/red.png" transparent="1" alphatest="on" />
                <ePixmap name="green" position="140,190" zPosition="1" size="140,40" pixmap="skin_default/buttons/green.png" transparent="1" alphatest="on" />
                <widget name="key_red" position="0,190" zPosition="2" size="140,40" valign="center" halign="center" font="Regular;21" transparent="1" shadowColor="black" shadowOffset="-1,-1" />
                <widget name="key_green" position="140,190" zPosition="2" size="140,40" valign="center" halign="center" font="Regular;21" transparent="1" shadowColor="black" shadowOffset="-1,-1" />
        </screen>"""
	def __init__(self, session):
		Screen.__init__(self, session)

		self["actions"] = ActionMap(["OkCancelActions", "ColorActions", "CiSelectionActions"],
			{
				"left": self.keyLeft,
				"right": self.keyRight,
				"cancel": self.cancel,
				"ok": self.ok,
				"green": self.save,
				"red": self.cancel,
			},-1)

		self.list = [ ]

		self.softcam = CamControl('softcam')
		self.cardserver = CamControl('cardserver')

		menuList = ConfigList(self.list)
		menuList.list = self.list
		menuList.l.setList(self.list)
		self["entries"] = menuList

		softcams = self.softcam.getList()
		cardservers = self.cardserver.getList()

		self.softcams = ConfigSelection(choices = softcams)
		self.softcams.value = self.softcam.current()

		self.list.append(getConfigListEntry(_("Select Softcam"), self.softcams))
		if cardservers:
			self.cardservers = ConfigSelection(choices = cardservers)
			self.cardservers.value = self.cardserver.current()
			self.list.append(getConfigListEntry(_("Select Card Server"), self.cardservers))

		self.list.append(getConfigListEntry(_("Restart softcam"), ConfigAction(self.restart, "s")))
		if cardservers: 
			self.list.append(getConfigListEntry(_("Restart cardserver"), ConfigAction(self.restart, "c"))) 
			self.list.append(getConfigListEntry(_("Restart both"), ConfigAction(self.restart, "sc"))) 

		self["key_red"] = Label(_("Cancel"))
		self["key_green"] = Label(_("OK"))

	def keyLeft(self):
		self["entries"].handleKey(KEY_LEFT)

	def keyRight(self):
		self["entries"].handleKey(KEY_RIGHT)
		
	def ok(self):
		self["entries"].handleKey(KEY_OK)
	

	def restart(self, what):
		self.what = what
		if "s" in what:
			if "c" in what:
				msg = _("Please wait, restarting softcam and cardserver.")
			else:
				msg  = _("Please wait, restarting softcam.")
                elif "c" in what:
			msg = _("Please wait, restarting cardserver.")
		self.mbox = self.session.open(MessageBox, msg, MessageBox.TYPE_INFO)
		self.activityTimer = eTimer()
		self.activityTimer.timeout.get().append(self.doStop)
		self.activityTimer.start(100, False)

	def doStop(self):
		self.activityTimer.stop()
		if "c" in self.what:
			self.cardserver.command('stop')
		if "s" in self.what:
			self.softcam.command('stop')
		self.oldref = self.session.nav.getCurrentlyPlayingServiceReference()
		self.session.nav.stopService()
		# Delay a second to give 'em a chance to stop
		self.activityTimer = eTimer()
		self.activityTimer.timeout.get().append(self.doStart)
		self.activityTimer.start(1000, False)

	def doStart(self):
		self.activityTimer.stop()
		del self.activityTimer 
		if "c" in self.what:
                        self.cardserver.select(self.cardservers.value)
			self.cardserver.command('start')
		if "s" in self.what:
			self.softcam.select(self.softcams.value)
			self.softcam.command('start')
		self.mbox.close()
		self.close()
		self.session.nav.playService(self.oldref)
		del self.oldref

	def restartCardServer(self):
		if hasattr(self, 'cardservers'):
			self.restart("c")
	
	def restartSoftcam(self):
		self.restart("s")

	def save(self):
		what = ''
		if hasattr(self, 'cardservers') and (self.cardservers.value != self.cardserver.current()):
                        what = 'sc'
		elif self.softcams.value != self.softcam.current():
                        what = 's'
                if what:
                	self.restart(what)
		else:
			self.close()

	def cancel(self):
		self.close()
