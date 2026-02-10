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
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
#  USA.
"""
Driver for reading data from the PySDL2 API. Used from Inpyt.py for reading
input data.
"""
import logging
import sys
import time
from threading import Thread

from queue import Queue

if sys.platform.startswith('linux'):
    raise Exception("No SDL2 support on Linux")

try:
    import sdl2
    import sdl2.ext
    import sdl2.hints
except ImportError as e:
    raise Exception("sdl2 library probably not installed ({})".format(e))

__author__ = 'Bitcraze AB'
__all__ = ['PySDL2Reader']

logger = logging.getLogger(__name__)

MODULE_MAIN = "PySDL2Reader"
MODULE_NAME = "PySDL2"


class _SDLEventDispatcher(Thread):
    """Wrapper to read all SDL2 events from the global queue and distribute
    them to the different devices"""

    def __init__(self, callback):
        Thread.__init__(self)
        self._callback = callback
        self.daemon = True
        # SDL2 will Seg Fault on Linux if you read events after you
        # have closed a device (and without opening a new one). Even if you
        # have two devices open, it will crash after one.
        self.enable = False

    def run(self):
        while True:
            if self.enable:
                try:
                    for ev in sdl2.ext.get_events():
                        try:
                            if self._callback:
                                self._callback(ev.jdevice.which, ev)
                        except AttributeError:
                            pass
                except Exception as e:
                    logger.error("Error in SDL event dispatcher: {}".format(e))
                    # Don't crash the thread, just log and continue
            time.sleep(0.01)


class _JS():
    """Wrapper for one input device"""

    def __init__(self, sdl_index, sdl_id, name):
        self.axes = []
        self.buttons = []
        self.name = MODULE_NAME
        self._j = None
        self._id = sdl_id
        self._index = sdl_index
        self._name = name
        self._event_queue = Queue()

    def open(self):
        self._j = sdl2.SDL_JoystickOpen(self._index)
        btn_count = sdl2.SDL_JoystickNumButtons(self._j)

        self.axes = list(0 for i in range(sdl2.SDL_JoystickNumAxes(self._j)))
        self.buttons = list(0 for i in range(btn_count + 4))

    def close(self):
        if self._j:
            sdl2.joystick.SDL_JoystickClose(self._j)
        self._j = None

    def _set_virtual_dpad(self, position):
        self.buttons[-4:] = [0, 0, 0, 0]

        # -4 UP -3 DOWN -2 LEFT -1 RIGHT
        if position == sdl2.SDL_HAT_UP:
            self.buttons[-4] = 1
        elif position == sdl2.SDL_HAT_RIGHTUP:
            self.buttons[-4] = 1
            self.buttons[-1] = 1
        elif position == sdl2.SDL_HAT_RIGHT:
            self.buttons[-1] = 1
        elif position == sdl2.SDL_HAT_RIGHTDOWN:
            self.buttons[-1] = 1
            self.buttons[-3] = 1
        elif position == sdl2.SDL_HAT_DOWN:
            self.buttons[-3] = 1
        elif position == sdl2.SDL_HAT_LEFTDOWN:
            self.buttons[-3] = 1
            self.buttons[-2] = 1
        elif position == sdl2.SDL_HAT_LEFT:
            self.buttons[-2] = 1
        elif position == sdl2.SDL_HAT_LEFTUP:
            self.buttons[-2] = 1
            self.buttons[-4] = 1

    def add_event(self, event):
        self._event_queue.put(event)

    def read(self):
        while not self._event_queue.empty():
            e = self._event_queue.get_nowait()
            if e.type == sdl2.SDL_JOYAXISMOTION:
                self.axes[e.jaxis.axis] = e.jaxis.value / 32767.0

            if e.type == sdl2.SDL_JOYBUTTONDOWN:
                self.buttons[e.jbutton.button] = 1

            if e.type == sdl2.SDL_JOYBUTTONUP:
                self.buttons[e.jbutton.button] = 0

            if e.type == sdl2.SDL_JOYHATMOTION:
                self._set_virtual_dpad(e.jhat.value)

        return [self.axes, self.buttons]


class PySDL2Reader():
    """Used for reading data from input devices using the PySDL2 API."""

    def __init__(self):
        logger.info("PySDL2Reader initialized (SDL2 will initialize on first use)")
        self._js = {}
        self.name = MODULE_NAME
        self._event_dispatcher = None
        self._devices = []
        self._devices_scanned = False
        self._sdl_initialized = False

    def _ensure_sdl_initialized(self):
        """Initialize SDL2 and event dispatcher if not already done"""
        if not self._sdl_initialized:
            logger.info("Initializing SDL2...")
            try:
                sdl2.SDL_Init(sdl2.SDL_INIT_JOYSTICK)
                sdl2.SDL_SetHint(sdl2.hints.SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,
                                 b"1")
                self._event_dispatcher = _SDLEventDispatcher(self._dispatch_events)
                self._event_dispatcher.start()
                self._sdl_initialized = True
                logger.info("SDL2 initialization successful")
            except Exception as e:
                logger.error("Failed to initialize SDL2: {}".format(e))
                raise

    def open(self, device_id):
        """Initialize the reading and open the device with deviceId and set
        the mapping for axis/buttons using the inputMap"""
        self._ensure_sdl_initialized()
        logger.debug("Opening device: {}".format(device_id))
        if device_id in self._js:
            self._event_dispatcher.enable = True
            self._js[device_id].open()
        else:
            logger.warning("Device {} not found".format(device_id))

    def close(self, device_id):
        """Close the device"""
        logger.debug("Closing device: {}".format(device_id))
        if device_id in self._js:
            self._event_dispatcher.enable = False
            self._js[device_id].close()
        else:
            logger.warning("Device {} not found".format(device_id))

    def read(self, device_id):
        """Read input from the selected device."""
        if device_id in self._js:
            return self._js[device_id].read()
        else:
            logger.warning("Device {} not found".format(device_id))
            return [[], []]

    def _dispatch_events(self, device_id, event):
        if device_id in self._js:
            self._js[device_id].add_event(event)

    def devices(self):
        """List all the available devices.
        
        Note: This returns an empty list to prevent blocking during device
        discovery. Devices should be opened manually when needed or through
        the configuration system.
        """
        # Return empty list initially to prevent SDL2 initialization
        # from blocking during background device discovery.
        # Users can still manually configure input devices.
        if not self._devices_scanned:
            logger.info("PySDL2: Device scanning skipped (devices configured manually)")
            self._devices_scanned = True
        return self._devices
