#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
#     ||          ____  _ __
#  +------+      / __ )(_) /_______________ _____  ___
#  | 0xBC |     / __  / / __/ ___/ ___/ __ `/_  / / _ \
#  +------+    / /_/ / / /_/ /__/ /  / /_/ / / /_/  __/
#   ||  ||    /_____/_/\__/\___/_/   \__,_/ /___/\___/
#
#  Copyright (C) 2011-2013 Bitcraze AB
#
#  Crazyflie Nano Quadcopter Client
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.
""" CRTP UDP Driver. Work either with the UDP server or with an UDP device
See udpserver.py for the protocol"""
import logging
import re
import socket
import struct
import binascii
import time
import threading
from urllib.parse import urlparse

from .crtpdriver import CRTPDriver
from .crtpstack import CRTPPacket
from .exceptions import WrongUriType

__author__ = 'Bitcraze AB'
__all__ = ['UdpDriver']

logger = logging.getLogger(__name__)


class UdpDriver(CRTPDriver):

    def __init__(self):
        CRTPDriver.__init__(self)
        self.debug = False
        self.link_error_callback = None
        self.link_quality_callback = None
        self.needs_resending = False
        self._recv_count = 0
        self._timeout_count = 0
        self._cksum_error_count = 0
        self._send_count = 0
        self._last_recv_time = None
        self._keepalive_thread = None
        self._keepalive_running = False

    def connect(self, uri, linkQualityCallback, linkErrorCallback):
        if not re.search('^udp://', uri):
            raise WrongUriType('Not an UDP URI')

        parse = urlparse(uri)

        self.link_quality_callback = linkQualityCallback
        self.link_error_callback = linkErrorCallback

        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.addr = (parse.hostname, parse.port)
        self.socket.bind(('', 2399))
        self.socket.settimeout(2.0)
        self.socket.connect(self.addr)

        self._recv_count = 0
        self._timeout_count = 0
        self._cksum_error_count = 0
        self._send_count = 0
        self._last_recv_time = time.time()

        self.socket.sendto('\xFF\x01\x01\x01'.encode(), self.addr)

        logger.info("UDP: Connected to %s:%d", parse.hostname, parse.port)

        # Start keepalive thread to send periodic null packets
        self._keepalive_running = True
        self._keepalive_thread = threading.Thread(
            target=self._keepalive_loop, daemon=True)
        self._keepalive_thread.start()

    def _keepalive_loop(self):
        """Send periodic null commander packets to keep ESP32 alive"""
        while self._keepalive_running:
            try:
                if self.socket is not None:
                    # Send a null commander setpoint (port=3, channel=0)
                    # header = (3 << 4) | (3 << 2) | 0 = 0x3C
                    # This is a CRTP packet on commander port
                    pk_data = struct.pack('<BfffH', 0x30, 0.0, 0.0, 0.0, 0)
                    cksum = 0
                    for b in pk_data:
                        cksum += b
                    cksum %= 256
                    raw = pk_data + bytes([cksum])
                    self.socket.sendto(raw, self.addr)
            except Exception:
                pass
            time.sleep(0.1)

    def receive_packet(self, time=0):
        try:
            data, addr = self.socket.recvfrom(1024)
        except socket.timeout:
            self._timeout_count += 1
            if self._timeout_count % 5 == 1:
                logger.warning(
                    "UDP: recv timeout #%d (last recv %.1fs ago, "
                    "total recv=%d, cksum_err=%d, sent=%d)",
                    self._timeout_count,
                    __import__('time').time() - self._last_recv_time
                    if self._last_recv_time else 0,
                    self._recv_count,
                    self._cksum_error_count,
                    self._send_count)
            return None
        except OSError as e:
            logger.error("UDP: Socket error: %s", e)
            return None

        if data:
            self._last_recv_time = __import__('time').time()
            # take the final byte as the checksum
            cksum_recv = data[len(data)-1]
            # remove the checksum from the data
            data = data[0:(len(data)-1)]
            # calculate checksum and check it with the last byte
            cksum = 0
            for i in data[0:]:
                cksum += i
            cksum %= 256
            if cksum != cksum_recv:
                self._cksum_error_count += 1
                if self._cksum_error_count <= 10 or \
                        self._cksum_error_count % 100 == 0:
                    logger.warning(
                        "UDP: Checksum error #%d: calc=%d recv=%d len=%d",
                        self._cksum_error_count, cksum, cksum_recv, len(data))
                return None
            self._recv_count += 1
            if self._recv_count <= 5 or self._recv_count % 500 == 0:
                logger.info("UDP: recv packet #%d port=%d ch=%d len=%d",
                            self._recv_count,
                            (data[0] >> 4) & 0x0F,
                            data[0] & 0x03,
                            len(data))
            pk = CRTPPacket(data[0], list(data[1:]))
            return pk

        else:
            return None

    def send_packet(self, pk):
        raw = (pk.header,) + pk.datat
        cksum = 0
        for i in raw:
            cksum += i
        cksum %= 256
        raw = raw + (cksum,)
        # change the tuple to bytes
        raw = bytearray(raw)
        self._send_count += 1
        self.socket.sendto(raw, self.addr)
        if self._send_count <= 5 or self._send_count % 500 == 0:
            logger.info("UDP: sent packet #%d port=%d ch=%d len=%d",
                        self._send_count,
                        (pk.header >> 4) & 0x0F,
                        pk.header & 0x03,
                        len(raw))

    def close(self):
        # Stop keepalive thread
        self._keepalive_running = False
        if self._keepalive_thread:
            self._keepalive_thread.join(timeout=1.0)
            self._keepalive_thread = None
        # Remove this from the server clients list
        logger.info("UDP: Closing. Stats: recv=%d, timeouts=%d, "
                     "cksum_err=%d, sent=%d",
                     self._recv_count, self._timeout_count,
                     self._cksum_error_count, self._send_count)
        self.socket.sendto('\xFF\x01\x02\x02'.encode(), self.addr)
        time.sleep(0.1)
        self.socket.close()
        self.socket = None

    def get_name(self):
        return 'udp'

    def scan_interface(self, address):
        address1 = 'udp://192.168.43.42:2390'
        return [[address1, ''], ]
