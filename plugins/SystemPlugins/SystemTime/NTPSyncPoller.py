from enigma import eTimer
from Components.config import config
from Components.Console import Console

_session = None

class NTPSyncPoller:
	"""Automatically Poll NTP"""
	def __init__(self):
		self.timer = eTimer()
		self.Console = Console()

	def start(self):
		if not self.timer.callback:
			self.timer.callback.append(self.NTPStart)
		self.timer.startLongTimer(0)

	def stop(self):
		if self.timer.callback:
			self.timer.callback.remove(self.NTPStart)
		self.timer.stop()

	def NTPStart(self):
		if config.plugins.SystemTime.choiceSystemTime.value == "1":
			self.Console.ePopen('/usr/bin/ntpdate -b -s -u pool.ntp.org')
		else:
			self.Console.ePopen('/usr/bin/dvbdate -p -s -f')
		self.timer.startLongTimer(int(config.plugins.SystemTime.useNTPminutes.value) * 60)
