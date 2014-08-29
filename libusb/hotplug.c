/* -*- Mode: C; indent-tabs-mode:t ; c-basic-offset:8 -*- */
/*
 * Hotplug functions for libusb
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

#include <config.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#include <assert.h>

#include "libusbi.h"
#include "hotplug.h"

struct device_list {
	struct list_head list;
	struct libusb_device *device;
};

struct hotplug_list {
	struct list_head list;
	const libusb_hotplug *driver;
	struct device_list device_list; /* Devices associated with this driver.  */
};

/* This tests the drivers filter against the device. 
 * If it matches, we fire off the callback corresponding to the event. 
 */
static int usbi_hotplug_match_driver(struct libusb_context *ctx,
	struct libusb_device* dev,
	struct hotplug_list *driver_it)
{
	const struct libusb_hotplug *driver = driver_it->driver;
	
	if (driver->vid != LIBUSB_HOTPLUG_MATCH_ANY &&
	    driver->vid != dev->device_descriptor.idVendor) {
		return 1;
	}

	if (driver->pid != LIBUSB_HOTPLUG_MATCH_ANY &&
	    driver->pid != dev->device_descriptor.idProduct) {
		return 1;
	}

	if (driver->dev_class != LIBUSB_HOTPLUG_MATCH_ANY  &&
	    driver->dev_class != dev->device_descriptor.bDeviceClass) {
		return 1;
	}


	return driver->connect(ctx, dev);
}

static void usbi_hotplug_connect_device(struct libusb_context *ctx,
	struct libusb_device *device,
	struct hotplug_list *it)
{
	struct device_list *device_node = malloc(sizeof(struct device_list));
	device_node->device = device;
	list_add((struct list_head*)device_node, (struct list_head*)&it->device_list);
}

static void usbi_hotplug_disconnect_all(struct libusb_context *ctx,
	struct hotplug_list *it)
{
	struct device_list *device_node, *next;
	
	list_for_each_entry_safe(device_node, next, (struct list_head*)&it->device_list, list, struct device_list) {
		it->driver->disconnect(ctx, device_node->device);
		list_del((struct list_head*)device_node);
		free(device_node);
	}
}

static void usbi_hotplug_disconnect_device(struct libusb_context *ctx,
	struct libusb_device *device,
	struct hotplug_list *it)
{
	struct device_list *device_node, *next;
	
	list_for_each_entry_safe(device_node, next, (struct list_head*)&it->device_list, list, struct device_list) {
		if (device_node->device != device) continue;
				
		it->driver->disconnect(ctx, device);
		list_del((struct list_head*)device_node);
		free(device_node);
	}
}

void usbi_hotplug_match(struct libusb_context *ctx, struct libusb_device *dev,
	libusb_hotplug_event event)
{
	struct hotplug_list *it, *next;

	usbi_mutex_lock(&ctx->hotplug_drivers_lock);

	list_for_each_entry_safe(it, next, &ctx->hotplug_drivers, list, struct hotplug_list) {
		if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED) {
			int error = usbi_hotplug_match_driver(ctx, dev, it);
			
			if (!error) {
				usbi_hotplug_connect_device(ctx, dev, it);
			}
		}
		else if(event == LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT) {
			usbi_hotplug_disconnect_device(ctx, dev, it);
		}
		
	}

	usbi_mutex_unlock(&ctx->hotplug_drivers_lock);

	/* the backend is expected to call the callback for each active transfer */
}

int API_EXPORTED libusb_hotplug_register(
	libusb_context *ctx,
	const libusb_hotplug *driver)
{
	struct hotplug_list *node;
	struct libusb_device *dev;
	
	/* check for hotplug support */
	if (!libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) {
		return LIBUSB_ERROR_NOT_SUPPORTED;
	}
	
	/* check for sane values */
	if (!driver ||
	    (LIBUSB_HOTPLUG_MATCH_ANY != driver->vid && (~0xffff & driver->vid)) ||
	    (LIBUSB_HOTPLUG_MATCH_ANY != driver->pid && (~0xffff & driver->pid)) ||
	    (LIBUSB_HOTPLUG_MATCH_ANY != driver->dev_class && (~0xff & driver->dev_class))) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	
	/* allocate new node for driver list */
	node = malloc(sizeof(struct hotplug_list));
	
	if (!node) return LIBUSB_ERROR_NO_MEM;

	list_init((struct list_head*)&node->device_list);
	
	node->device_list.device = NULL;		 
	node->driver = driver;

	USBI_GET_CONTEXT(ctx);

	usbi_mutex_lock(&ctx->hotplug_drivers_lock);
	list_add((struct list_head*)node, &ctx->hotplug_drivers);
	usbi_mutex_unlock(&ctx->hotplug_drivers_lock);

	if (driver->flags & LIBUSB_HOTPLUG_ENUMERATE) {
		list_for_each_entry(dev, &ctx->usb_devs, list, struct libusb_device) {
			usbi_hotplug_match(ctx, dev, LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED);
		}
	}

	return LIBUSB_SUCCESS;
}

void API_EXPORTED libusb_hotplug_deregister (
	struct libusb_context *ctx,
	const libusb_hotplug *driver)
{
	struct hotplug_list *it, *next;

	/* check for hotplug support */
	if (!libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) {
		return;
	}

	USBI_GET_CONTEXT(ctx);

	usbi_mutex_lock(&ctx->hotplug_drivers_lock);
	list_for_each_entry_safe(it, next, &ctx->hotplug_drivers, list, struct hotplug_list) {
		if (it->driver != driver) continue;
		
		usbi_hotplug_disconnect_all(ctx, it);
			
		list_del((struct list_head*)it);
		free(it);
		
	}
	usbi_mutex_unlock(&ctx->hotplug_drivers_lock);
}

void usbi_hotplug_deregister_all(struct libusb_context *ctx) {
	struct hotplug_list *it, *next;

	usbi_mutex_lock(&ctx->hotplug_drivers_lock);
	
	list_for_each_entry_safe(it, next, &ctx->hotplug_drivers, list, struct hotplug_list) {
		usbi_hotplug_disconnect_all(ctx, it);
	}

	usbi_mutex_unlock(&ctx->hotplug_drivers_lock);
}
