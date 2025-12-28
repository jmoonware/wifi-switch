import socket
import argparse
import logging
from wifiswitch.picoxface import *
# from picoxface import *

class WifiSwitchDevice():
	def __init__(self,dest='192.168.1.11',port=8225,timeout=2,echo=False):
		self.dest=dest
		self.port=port
		self.timeout=timeout
		self.echo=echo
		try:
			self.socket = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
		except Exception as ex:
			logging.getLogger(__name__).error("Error allocating socket: {0}".format(ex))
	def ReadSwitchState(self):
		return(self.Command(str(pcn.PCOMMAND_READ_SWITCH_STATE),expected_response=response_switch_state))
	def ReadTHPA(self):
		return(self.Command(str(pcn.PCOMMAND_READ_BMEA_VALS),expected_response=response_pico_thp))
	def ReadTHPB(self):
		return(self.Command(str(pcn.PCOMMAND_READ_BMEB_VALS),expected_response=response_pico_thp))
	def Status(self):
		return(self.Command(str(pcn.PCOMMAND_STATUS),expected_response=response_pico_status))
	def Command(self,command_key,command_value=None,expected_response=None):
		res = parse_received(send_packet(self.socket,self.dest,self.port,self.timeout,pico_commands[command_key]['val'],pack16(command_value),echo=self.echo))
		if expected_response:
			if res and len(res)==len(expected_response):
				return(res)
			else:
				logging.getLogger(__name__).error("WifiSwitchDevice: unexpected response {0}, expected {1}".format(res,expected_response))
				return({})
		return(res)	
