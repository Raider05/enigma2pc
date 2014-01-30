from enigma import eTimer, eAVSwitch, eEnv
from Components.config import config, ConfigSelection, ConfigSubDict, ConfigYesNo

from Tools.CList import CList
from Tools.HardwareInfo import HardwareInfo
from Components.AVSwitch import AVSwitch

# The "VideoHardware" is the interface to /usr/local/e2/etc/stb/video.
# It generates hotplug events, and gives you the list of 
# available and preferred modes, as well as handling the currently
# selected mode. No other strict checking is done.
class VideoHardware:
	rates = {
		"50": _("50Hz"),
		"60": _("60Hz"),
	}

	deinterlace_modes = {
		"0": _("bob"),
		"1": _("half temporal"),
		"2": _("half temporal spatial"),
		"3": _("temporal"),
		"4": _("temporal spatial"),
	}

	def getOutputAspect(self):
		ret = (16,9)

		is_widescreen = config.av.aspect.value in ("16_9", "16_10")
		is_auto = config.av.aspect.value == "auto"
		if is_widescreen:
			aspect = {"16_9": "16:9", "16_10": "16:10"}[config.av.aspect.value]
			if aspect == "16:10":
				ret = (16,10)
		elif is_auto:
			try:
				aspect_str = open(eEnv.resolve("${sysconfdir}/stb/vmpeg/0/aspect"), "r").read()
				if aspect_str == "1": # 4:3
					ret = (4,3)
			except IOError:
				pass
		else:  # 4:3
			ret = (4,3)
		return ret

	def __init__(self):
		self.on_hotplug = CList()

		self.createConfig()
#		self.on_hotplug.append(self.createConfig)

#		config.av.colorformat.notifiers = [ ] 
		config.av.aspectratio.notifiers = [ ]
		config.av.tvsystem.notifiers = [ ]
		AVSwitch.getOutputAspect = self.getOutputAspect

		config.av.aspect.addNotifier(self.updateAspect)
		config.av.policy_43.addNotifier(self.updateAspect)
		config.pc.image4_3_zoom_x.addNotifier(self.updateAspect)
		config.pc.image4_3_zoom_y.addNotifier(self.updateAspect)
		config.av.policy_169.addNotifier(self.updateAspect)
		config.pc.image16_9_zoom_x.addNotifier(self.updateAspect)
		config.pc.image16_9_zoom_y.addNotifier(self.updateAspect)

		config.av.deinterlace    = ConfigSelection(choices = {"0": _("Off"), "1": _("On")}, default="0")
		config.av.deinterlace_sd = ConfigSelection(choices = self.deinterlace_modes, default="4")
		config.av.deinterlace_hd = ConfigSelection(choices = self.deinterlace_modes, default="3")
		config.av.deinterlace.addNotifier(self.updateDeinterlace)
		config.av.deinterlace_sd.addNotifier(self.updateDeinterlace)
		config.av.deinterlace_hd.addNotifier(self.updateDeinterlace)

		config.pc.sd_sharpness = ConfigSelection(choices = {"0": _("Off"), "1": _("On")}, default="0")
		config.pc.sd_noise     = ConfigSelection(choices = {"0": _("Off"), "1": _("On")}, default="0")
		config.pc.sd_sharpness.addNotifier(self.updateSDfeatures)
		config.pc.sd_noise.addNotifier(self.updateSDfeatures)
		config.pc.initial_window_width = ConfigSelection(choices = {"0": _("0"), "720": _("720"), "1280": _("1280"), "1366": _("1366"), "1600": _("1600"), "1680": _("1680"), "1920": _("1920")}, default="0")
		config.pc.initial_window_height = ConfigSelection(choices = {"0": _("0"), "576": _("576"), "720": _("720"), "768": _("768"), "1050": _("1050"), "1080": _("1080"), "1200": _("1200")}, default="0")

		# until we have the hotplug poll socket
#		self.timer = eTimer()
#		self.timer.callback.append(self.readPreferredModes)
#		self.timer.start(1000)

		config.av.sound_mode    = ConfigSelection(choices = {"0": _("default"), "1": _("custom")}, default="0")
		config.av.sound_card    = ConfigSelection(choices = {"0": _("card0"), "1": _("card1"), "2": _("card2"), "3": _("card3")}, default="0")
		config.av.sound_device  = ConfigSelection(choices = {"0": _("dev0"), "1": _("dev1"), "2": _("dev2"), "3": _("dev3"),  "4": _("dev4"), "5": _("dev5"), "6": _("dev6"),  "7": _("dev7"), "8": _("dev8"), "9": _("dev9")}, default="0")
		config.av.sound_output  = ConfigSelection(choices = {"1": _("Stereo 2.0"), "8": _("Surround 5.1"), "11": _("Surround 7.1"), "12": _("Pass Through")}, default="1")
		config.av.sound_mode.addNotifier(self.updateSoundMode)
		config.av.sound_card.addNotifier(self.updateSoundMode)
		config.av.sound_device.addNotifier(self.updateSoundMode)
		config.av.sound_output.addNotifier(self.updateSoundMode)

	def setMode(self, port, rate, force = None):
		print "setMode - port:", port, "rate:", rate
		# we can ignore "port"
		mode_50 = self.rates.get(50)
		mode_60 = self.rates.get(60)
		if mode_50 is None or force == 60:
			mode_50 = mode_60
		if mode_60 is None or force == 50: 
			mode_60 = mode_50

		#try:
		#	open("/usr/local/e2/etc/stb/video/videomode_50hz", "w").write(mode_50)
		#	open("/usr/local/e2/etc/stb/video/videomode_60hz", "w").write(mode_60)
		#except IOError:
		#	try:
		#		# fallback if no possibility to setup 50/60 hz mode
		#		open("/usr/local/e2/etc/stb/video/videomode", "w").write(mode_50)
		#	except IOError:
		#		print "setting videomode failed."

		#try:
		#	open("/etc/videomode", "w").write(mode_50) # use 50Hz mode (if available) for booting
		#except IOError:
		#	print "writing initial videomode to /etc/videomode failed."

		self.updateDeinterlace(None)
		self.updateSDfeatures(None)
		self.updateAspect(None)

	def saveMode(self, port, mode, rate):
		print "saveMode", port, mode, rate

		config.av.videorate.value = rate
		config.av.videorate.save()

	def createConfig(self, *args):
		hw_type = HardwareInfo().get_device_name()
		has_hdmi = HardwareInfo().has_hdmi()
		lst = []

		config.av.videorate = ConfigSelection(choices = self.rates)

	def setConfiguredMode(self):
		rate = config.av.videorate.value
		self.setMode("DVI-PC", rate)

	def updateAspect(self, cfgelement):
		# based on;
		#   config.av.videoport.value: current video output device
		#     Scart: 
		#   config.av.aspect:
		#     4_3:            use policy_169
		#     16_9,16_10:     use policy_43
		#     auto            always "bestfit"
		#   config.av.policy_169
		#     letterbox       use letterbox
		#     panscan         use panscan
		#     scale           use bestfit
		#   config.av.policy_43
		#     pillarbox       use panscan
		#     panscan         use letterbox  ("panscan" is just a bad term, it's inverse-panscan)
		#     nonlinear       use nonlinear
		#     scale           use bestfit

		valstr = config.av.aspect.value
		if valstr == "auto":
			val = 0
		elif valstr == "4_3":
			val = 2
		elif valstr == "16_9":
			val = 3
		elif valstr == "16_10":
			val = 3
		eAVSwitch.getInstance().setAspectRatio(val)

		valstr = config.av.policy_43.value
		if valstr == "zoom":
			val = 4
		elif valstr == "pillarbox":
			val = 3
		elif valstr == "panscan":
			val = 2
		elif valstr == "nonlinear":
			val = 1
		else:
			val = 0
		eAVSwitch.getInstance().setPolicy43(val)
		
		valstr = config.av.policy_169.value
		if valstr == "zoom":
			val = 3
		elif valstr == "letterbox":
			val = 2
		elif valstr == "panscan":
			val = 1
		else:
			val = 0
		eAVSwitch.getInstance().setPolicy169(val)
		eAVSwitch.getInstance().setZoom(int(config.pc.image4_3_zoom_x.value), int(config.pc.image4_3_zoom_y.value), int(config.pc.image16_9_zoom_x.value), int(config.pc.image16_9_zoom_y.value))
		eAVSwitch.getInstance().updateScreen()

	def updateSoundMode(self, cfgelement):
		print "-> update Sound Mode !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"

	def updateDeinterlace(self, cfgelement):
		print "-> update deinterlace !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
		eAVSwitch.getInstance().setDeinterlace(int(config.av.deinterlace.value), int(config.av.deinterlace_sd.value), int(config.av.deinterlace_hd.value))

	def updateSDfeatures(self, cfgelement):
		print "-> update SD features !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
		eAVSwitch.getInstance().setSDfeatures(int(config.pc.sd_sharpness.value), int(config.pc.sd_noise.value))

config.av.edid_override = ConfigYesNo(default = False)
video_hw = VideoHardware()
video_hw.setConfiguredMode()
