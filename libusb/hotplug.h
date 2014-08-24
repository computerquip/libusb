/* -*- Mode: C; indent-tabs-mode:t ; c-basic-offset:8 -*- */
/*
 * Hotplug support for libusb
 * Copyright © 2012-2013 Nathan Hjelm <hjelmn@mac.com>
 * Copyright © 2012-2013 Peter Stuge <peter@stuge.se>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#if !defined(USBI_HOTPLUG_H)
#define USBI_HOTPLUG_H

#ifndef LIBUSBI_H
#include "libusbi.h"
#endif

struct libusb_hotplug_message {
	libusb_hotplug_event event;
	struct libusb_device *device;
};

typedef struct libusb_hotplug_message libusb_hotplug_message;

void usbi_hotplug_deregister_all(struct libusb_context *ctx);
void usbi_hotplug_match(struct libusb_context *ctx, struct libusb_device *dev,
			libusb_hotplug_event event);

#endif
