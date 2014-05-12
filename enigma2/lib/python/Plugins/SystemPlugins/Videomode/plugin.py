from Screens.Screen import Screen
from Plugins.Plugin import PluginDescriptor
from Components.SystemInfo import SystemInfo
from Components.ConfigList import ConfigListScreen
from Components.config import getConfigListEntry, config, ConfigBoolean, ConfigNothing, ConfigSlider
from Components.Sources.StaticText import StaticText

from VideoHardware import video_hw

config.misc.videowizardenabled = ConfigBoolean(default = True)

class VideoSetup(Screen, ConfigListScreen):

	def __init__(self, session, hw):
		Screen.__init__(self, session)
		# for the skin: first try VideoSetup, then Setup, this allows individual skinning
		self.skinName = ["VideoSetup", "Setup" ]
		self.setup_title = _("A/V Settings")
		self.hw = hw
		self.onChangedEntry = [ ]

		self.port = "DVI-PC"

		# handle hotplug by re-creating setup
		self.onShow.append(self.startHotplug)
		self.onHide.append(self.stopHotplug)

		self.list = [ ]
		ConfigListScreen.__init__(self, self.list, session = session, on_change = self.changedEntry)

		from Components.ActionMap import ActionMap
		self["actions"] = ActionMap(["SetupActions", "MenuActions"], 
			{
				"cancel": self.keyCancel,
				"save": self.apply,
				"menu": self.closeRecursive,
			}, -2)

		self["key_red"] = StaticText(_("Cancel"))
		self["key_green"] = StaticText(_("OK"))

		self.createSetup()
		self.grabLastGoodMode()
		self.onLayoutFinish.append(self.layoutFinished)

	def layoutFinished(self):
		self.setTitle(self.setup_title)

	def startHotplug(self):
		self.hw.on_hotplug.append(self.createSetup)

	def stopHotplug(self):
		self.hw.on_hotplug.remove(self.createSetup)

	def createSetup(self):
		level = config.usage.setup_level.index

		self.list = [ ]
		self.list.append(getConfigListEntry(_("Refresh Rate"), config.av.videorate))
		self.list.append(getConfigListEntry(_("Aspect Ratio"), config.av.aspect))

		if config.av.aspect.value != "auto":
			self.list.append(getConfigListEntry(_("Display 4:3 content as"), config.av.policy_43))
			if config.av.policy_43.value == "zoom":
				self.list.extend((
					getConfigListEntry(_("Zoom 4:3 X"), config.pc.image4_3_zoom_x),
					getConfigListEntry(_("Zoom 4:3 Y"), config.pc.image4_3_zoom_y)
				))

			self.list.append(getConfigListEntry(_("Display 16:9 content as"), config.av.policy_169))
			if config.av.policy_169.value == "zoom":
				self.list.extend((
					getConfigListEntry(_("Zoom 16:9 X"), config.pc.image16_9_zoom_x),
					getConfigListEntry(_("Zoom 16:9 Y"), config.pc.image16_9_zoom_y)
				))

		self.list.append(getConfigListEntry(_("Deinterlacing"), config.av.deinterlace))
		if config.av.deinterlace.value == "1":
			self.list.append(getConfigListEntry(_("SD deinterlace method"), config.av.deinterlace_sd))
			self.list.append(getConfigListEntry(_("HD deinterlace method"), config.av.deinterlace_hd))

		self.list.append(getConfigListEntry(_("SD sharpness"), config.pc.sd_sharpness))
		self.list.append(getConfigListEntry(_("SD noise reduction"), config.pc.sd_noise))

		self.list.append(getConfigListEntry(_("Fullscreen default"), config.pc.default_fullscreen))

		self.list.append(getConfigListEntry(_("TV resolution width"), config.pc.initial_window_width))
		self.list.append(getConfigListEntry(_("TV resolution height"), config.pc.initial_window_height))

		if level >= 1:
			if SystemInfo["CanDownmixAC3"]:
				self.list.append(getConfigListEntry(_("AC3 downmix"), config.av.downmix_ac3))
			if SystemInfo["CanDownmixDTS"]:
				self.list.append(getConfigListEntry(_("DTS downmix"), config.av.downmix_dts))
			self.list.extend((
				getConfigListEntry(_("General AC3 Delay"), config.av.generalAC3delay),
				getConfigListEntry(_("General PCM Delay"), config.av.generalPCMdelay)
			))

		self.list.append(getConfigListEntry(_("Sound mode"), config.av.sound_mode))
		if config.av.sound_mode.value == "1":
			self.list.append(getConfigListEntry(_("Sound card"), config.av.sound_card))
			self.list.append(getConfigListEntry(_("Sound device"), config.av.sound_device))
			self.list.append(getConfigListEntry(_("Sound output"), config.av.sound_output))

		if SystemInfo["CanChangeOsdAlpha"]:
			self.list.append(getConfigListEntry(_("OSD visibility"), config.av.osd_alpha))

		if not isinstance(config.av.scaler_sharpness, ConfigNothing):
			self.list.append(getConfigListEntry(_("Scaler sharpness"), config.av.scaler_sharpness))

		self["config"].list = self.list
		self["config"].l.setList(self.list)

	def keyLeft(self):
		ConfigListScreen.keyLeft(self)
		self.createSetup()

	def keyRight(self):
		ConfigListScreen.keyRight(self)
		self.createSetup()

	def confirm(self, confirmed):
		if not confirmed:
			config.av.videorate.value = self.last_good[2]
			self.hw.setMode(*self.last_good)
		else:
			self.keySave()

	def grabLastGoodMode(self):
                rate = config.av.videorate.value
		self.last_good = (self.port, rate)

	def apply(self):
		print "Apply"
		rate = config.av.videorate.value
		video_hw.updateAspect(None)
		#deinterlace    = config.av.deinterlace.value
		#deinterlace_sd = config.av.deinterlace_sd.value
		#deinterlace_hd = config.av.deinterlace_hd.value
		if (self.port, rate) != self.last_good:
			self.hw.setMode(self.port, rate)
			from Screens.MessageBox import MessageBox
			self.session.openWithCallback(self.confirm, MessageBox, _("Is this videomode ok?"), MessageBox.TYPE_YESNO, timeout = 20, default = False)
		else:
			self.keySave()

	# for summary:
	def changedEntry(self):
		for x in self.onChangedEntry:
			x()

	def getCurrentEntry(self):
		return self["config"].getCurrent()[0]

	def getCurrentValue(self):
		return str(self["config"].getCurrent()[1].getText())

	def createSummary(self):
		from Screens.Setup import SetupSummary
		return SetupSummary

class VideomodeHotplug:
	def __init__(self, hw):
		self.hw = hw

	def start(self):
		self.hw.on_hotplug.append(self.hotplug)

	def stop(self):
		self.hw.on_hotplug.remove(self.hotplug)

	def hotplug(self, what):
		print "hotplug detected on port '%s'" % (what)
		rate = config.av.videorate.value

		if not self.hw.isModeAvailable(self.port, mode, rate):
			print "mode %s/%s/%s went away!" % (self.port, mode, rate)
			modelist = self.hw.getModeList(self.port)
			if not len(modelist):
				print "sorry, no other mode is available (unplug?). Doing nothing."
				return
			mode = modelist[0][0]
			rate = modelist[0][1]
			print "setting %s/%s/%s" % (self.port, mode, rate)
			self.hw.setMode(self.port, mode, rate)

hotplug = None

def startHotplug():
	global hotplug, video_hw
	hotplug = VideomodeHotplug(video_hw)
	hotplug.start()

def stopHotplug():
	global hotplug
	hotplug.stop()


def autostart(reason, session = None, **kwargs):
	if session is not None:
		global my_global_session
		my_global_session = session
		return

	if reason == 0:
		startHotplug()
	elif reason == 1:
		stopHotplug()

def videoSetupMain(session, **kwargs):
	session.open(VideoSetup, video_hw)

def startSetup(menuid):
	if menuid != "system": 
		return [ ]

	return [(_("A/V Settings"), videoSetupMain, "av_setup", 40)]

def VideoWizard(*args, **kwargs):
	from VideoWizard import VideoWizard
	return VideoWizard(*args, **kwargs)

def Plugins(**kwargs):
	list = [
#		PluginDescriptor(where = [PluginDescriptor.WHERE_SESSIONSTART, PluginDescriptor.WHERE_AUTOSTART], fnc = autostart),
		PluginDescriptor(name=_("Video Setup"), description=_("Advanced Video Setup"), where = PluginDescriptor.WHERE_MENU, needsRestart = False, fnc=startSetup) 
	]
	if config.misc.videowizardenabled.value:
		list.append(PluginDescriptor(name=_("Video Wizard"), where = PluginDescriptor.WHERE_WIZARD, needsRestart = False, fnc=(0, VideoWizard)))
	return list
