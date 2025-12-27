import socket
import struct
import logging
from enum import Enum, auto

class ResponseElement:
	def __init__(self,name,byte_number,scale=1,element_type=int,signed=False):
		self.name = name
		self.byte_number = byte_number
		self.scale=scale
		self.element_type=element_type
		self.signed = signed
	def parse(self,raw_bytes,offset=0):
		slbytes = raw_bytes[offset:offset+self.byte_number]
		if self.element_type==int:
			ret = self.element_type.from_bytes(slbytes,'little',signed=self.signed)
		elif self.element_type==float:
			[ret] = struct.unpack('<f',slbytes)
		elif self.element_type==str:
			ret = slbytes.decode()
		else:
			raise ValueError("ResponseElement: parse: unknown type {0}".format(self.element_type))
		if self.element_type != str:
			fret = float(ret)/self.scale
		else:
			fret=ret
		return(fret)

class pcn(Enum):
	PCOMMAND_RESERVED=auto(0)
	PCOMMAND_STATUS	= auto()
	PCOMMAND_UPTIME = auto()
	PCOMMAND_READ_SWITCH_STATE = auto()
	PCOMMAND_READ_BMEA_VALS = auto()
	PCOMMAND_READ_BMEB_VALS = auto()
	PCOMMAND_READ_BOARD_T  = auto()
	PCOMMAND_NUM_READ_COMMANDS = auto()
	PCOMMAND_SET_SWITCH_STATE = auto()
	PCOMMAND_SET_THP_UPDATE_TIME = auto()
	def __str__(self):
		return(self.name)	

class response_pico_thp(Enum):
	bme_T_C = auto()
	bme_H_perc = auto()
	bme_P_inHg  = auto()
	def __str__(self):
		return(self.name)

class response_pico_status(Enum):
	uptime_s = auto()
	board_T_C = auto()
	packet_count = auto()
	bme_update_s = auto()
	gizmo_error = auto()
	version = auto()
	def __str__(self):
		return(self.name)
	
pico_commands = {
  str(pcn.PCOMMAND_RESERVED):{'val':pcn.PCOMMAND_RESERVED.value,'responses':
	[
	
	],
	'default_payload':bytearray()},
  str(pcn.PCOMMAND_STATUS):{'val':pcn.PCOMMAND_STATUS.value,'responses':
	[
		ResponseElement(str(response_pico_status.uptime_s), 4),
		ResponseElement(str(response_pico_status.board_T_C), 2, scale=10),
		ResponseElement(str(response_pico_status.packet_count), 4),
		ResponseElement(str(response_pico_status.bme_update_s), 2, scale=1000),
		ResponseElement(str(response_pico_status.gizmo_error), 2, element_type=int),
		ResponseElement(str(response_pico_status.version), 9, element_type=str),
	],
	'default_payload':bytearray()},
  str(pcn.PCOMMAND_UPTIME):{'val':pcn.PCOMMAND_UPTIME.value,'responses':
	[
		ResponseElement("uptime_s", 4),
	],
	'default_payload':bytearray()},
  str(pcn.PCOMMAND_READ_SWITCH_STATE):{'val':pcn.PCOMMAND_READ_SWITCH_STATE.value,'responses':
	[
		ResponseElement("switch1_state", 1),
		ResponseElement("switch2_state", 1),
		ResponseElement("switch3_state", 1),
	],
	'default_payload':bytearray()},
  str(pcn.PCOMMAND_READ_BMEA_VALS):{'val':pcn.PCOMMAND_READ_BMEA_VALS.value,'responses':
	[
		ResponseElement(str(response_pico_thp.bme_T_C), 2,scale=10,signed=True),
		ResponseElement(str(response_pico_thp.bme_H_perc), 2,scale=10),
		ResponseElement(str(response_pico_thp.bme_P_inHg), 2,scale=100),
	],
	'default_payload':bytearray()},
  str(pcn.PCOMMAND_READ_BMEB_VALS):{'val':pcn.PCOMMAND_READ_BMEB_VALS.value,'responses':
	[
		ResponseElement(str(response_pico_thp.bme_T_C), 2,scale=10,signed=True),
		ResponseElement(str(response_pico_thp.bme_H_perc), 2,scale=10),
		ResponseElement(str(response_pico_thp.bme_P_inHg), 2,scale=100),
	],
	'default_payload':bytearray()},
  str(pcn.PCOMMAND_READ_BOARD_T):{'val':pcn.PCOMMAND_READ_BOARD_T.value,'responses':
	[
		ResponseElement("board_T_C", 2,scale=10),
	],
	'default_payload':bytearray()},
  str(pcn.PCOMMAND_NUM_READ_COMMANDS):{'val':pcn.PCOMMAND_NUM_READ_COMMANDS.value,'responses':
	[
	
	],
	'default_payload':bytearray()},
  str(pcn.PCOMMAND_SET_SWITCH_STATE):{'val':pcn.PCOMMAND_SET_SWITCH_STATE.value,'responses':
	[
		ResponseElement("sent_switch1_state", 1),
		ResponseElement("sent_switch2_state", 1),
		ResponseElement("sent_switch3_state", 1),
	],
	'default_payload':bytearray([0,0,0])},
  str(pcn.PCOMMAND_SET_THP_UPDATE_TIME):{'val':pcn.PCOMMAND_SET_THP_UPDATE_TIME.value,'responses':
	[
		ResponseElement("sent_thp_update_time_s", 2),
	],
	'default_payload':bytearray([50,0])},
}

ACK = 0x06
NACK = 0x15

def pack16(val):
	ret = bytearray()
	if val and len(val) > 0:
		try:
			ival = int(float(val))
			ret.append(ival&255)
			ret.append((ival>>8)&255)
		except ValueError:
			logging.getLogger().error("pack16: can't convert {0}".format(val))
	return(ret)

def send_packet(sock, dest, port, timeout, command, command_payload_bytes=None, echo=False,ackbyte=ACK):
	# default response
	rec_msg = bytearray()
	rec_msg.append(int(NACK))
	rec_msg.append(0)
	rec_msg.append(0)
	rec_msg.append(0)

	# build payload
	payload = bytearray()
	payload.append(int(ACK))
	payload.append(int(command))
	if command_payload_bytes and len(command_payload_bytes) > 0:
		for b in command_payload_bytes:
			payload.append(int(b))
	else: # just send two zeros by default
		command_payload_bytes.append(0)
		command_payload_bytes.append(0)

	# add checksum bytes
	outgoing_checksum=0;
	for b in payload:
		outgoing_checksum+=int(b);
	
	payload.append(outgoing_checksum&255)
	payload.append(outgoing_checksum>>8)
	
	destination_addr = (dest,int(port))
	try:
		sock.settimeout(float(timeout))
		sock.sendto(payload,destination_addr)
		rec_msg, sender = sock.recvfrom(4096)
	except socket.timeout as to:
		logging.getLogger().error("send_packet: Timeout after {0} s\n".format(float(timeout)))
	except ValueError as ve:
		logging.getLogger().error("send_packet: value error {0} s\n".format(ve))

	if echo:	
		logging.getLogger().info("Sent: {0}".format(' '.join(['{0:x}'.format(x) for x in payload])))
		logging.getLogger().info("Received: {0}".format(' '.join(['{0:x}'.format(x) for x in rec_msg])))
	
	# validate checksum if needed
	if rec_msg[0]==ACK and len(rec_msg) > 2:
		computed_checksum = 0;
		for x in rec_msg[:-2]:
			computed_checksum += int(x)
	
		received_checksum = int(rec_msg[-2])+(int(rec_msg[-1])<<8)
		if received_checksum!=computed_checksum:
			logging.getLogger().error("Bad checksum: Received {0} vs Computed {1}\n".format(received_checksum,computed_checksum))
		elif echo:
			logging.getLogger().info("Valid checksum: {0}".format(received_checksum))
	return(rec_msg[:-2])

def parse_received(rec,ackbyte=ACK):
	# parse results of pico_commands
	parsed_ret = {}
	if len(rec) > 1: # got something back
		if rec[0]!=ackbyte:
			return(parsed_ret)
		command_key = ''
		for k in pico_commands.keys():
			if rec[1]==pico_commands[k]['val']:
				command_key = k
		if command_key in pico_commands:
			expected_len=0
			for rel in pico_commands[command_key]['responses']:
				expected_len+=rel.byte_number
			if len(rec)!=expected_len+2:
				logging.getLogger().error("Unexpected packet len {0} (expected {1}) in command {2}".format(len(rec), expected_len,command_key))
			rec_idx=2
			for rel in pico_commands[command_key]['responses']:
				parsed_ret[rel.name]=rel.parse(rec,rec_idx)
				rec_idx+=rel.byte_number
	return(parsed_ret)
	
