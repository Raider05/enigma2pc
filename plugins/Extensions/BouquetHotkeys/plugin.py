from Plugins.Plugin import PluginDescriptor
from Components.config import config, ConfigSubsection, ConfigBoolean, ConfigText
from enigma import eServiceReference, eServiceCenter
from Screens.InfoBarGenerics import InfoBarNumberZap, InfoBarPiP, NumberZap
import HotkeysSetup





config.usage.bouquethotkeys = ConfigBoolean(default = False)

config.plugins.BouquetHotkeys = ConfigSubsection()
config.plugins.BouquetHotkeys.key1 = ConfigText("none")
config.plugins.BouquetHotkeys.key2 = ConfigText("none")
config.plugins.BouquetHotkeys.key3 = ConfigText("none")
config.plugins.BouquetHotkeys.key4 = ConfigText("none")
config.plugins.BouquetHotkeys.key5 = ConfigText("none")
config.plugins.BouquetHotkeys.key6 = ConfigText("none")
config.plugins.BouquetHotkeys.key7 = ConfigText("none")
config.plugins.BouquetHotkeys.key8 = ConfigText("none")
config.plugins.BouquetHotkeys.key9 = ConfigText("none")





def OpenBouquetByRef(instance, bouquet):
	if isinstance(bouquet, eServiceReference):
		if instance.servicelist.getRoot() != bouquet:
			instance.servicelist.clearPath()
			if instance.servicelist.bouquet_root != bouquet:
				instance.servicelist.enterPath(instance.servicelist.bouquet_root)
			instance.servicelist.enterPath(bouquet)
		instance.session.execDialog(instance.servicelist)

def keyNumberGlobal(instance, number):
#	print "You pressed number " + str(number)
	if number == 0:
		if isinstance(instance, InfoBarPiP) and instance.pipHandles0Action():
			instance.pipDoHandle0Action()
		else:
			instance.servicelist.recallPrevService()
	else:
		if instance.has_key("TimeshiftActions") and not instance.timeshiftEnabled():
			if config.usage.bouquethotkeys.value and config.usage.multibouquet.value:
				refstr = eval("config.plugins.BouquetHotkeys.key"+str(number)).value
				if not refstr in ("", "none"):
					OpenBouquetByRef(instance, eServiceReference(refstr))
				else:
					instance.session.openWithCallback(instance.numberEntered, NumberZap, number, instance.searchNumber)
			else:
				instance.session.openWithCallback(instance.numberEntered, NumberZap, number, instance.searchNumber)

InfoBarNumberZap.keyNumberGlobal = keyNumberGlobal





def StartMainSession(session, **kwargs):
	if config.usage.bouquethotkeys.value and config.usage.multibouquet.value:
		pass

def OpenHotkeysSetup(session, **kwargs):
	reload(HotkeysSetup)
	session.open(HotkeysSetup.HotkeysSetupScreen)


def Plugins(**kwargs):
	return [PluginDescriptor(name="BouquetHotkeys", description="bouquet hotkeys plugin", where = PluginDescriptor.WHERE_SESSIONSTART, fnc = StartMainSession),
		PluginDescriptor(name="BouquetHotkeys", description="bouquet hotkeys plugin", where = PluginDescriptor.WHERE_PLUGINMENU, fnc = OpenHotkeysSetup)]
