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

static void usbi_hotplug_disconnect_device(struct libusb_context *ctx,
	struct libusb_device *device,
	struct hotplug_list *it)
{
	struct device_list *device_node;
	
	list_for_each_entry(device_node, (struct list_head*)&it->device_list, list, struct device_list) {
		if (device_node->device != device) continue;
				
		it->driver->disconnect(ctx, device);
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
				list_add(&dev->list, (struct list_head*)&it->device_list);
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
	struct hotplug_list *node = malloc(sizeof(struct hotplug_list));
	
	if (!node) return LIBUSB_ERROR_NO_MEM;

	list_init((struct list_head*)&node->device_list);
	node->device_list.device = NULL;
				 
	node->driver = driver;

	USBI_GET_CONTEXT(ctx);

	usbi_mutex_lock(&ctx->hotplug_drivers_lock);

	list_add((struct list_head*)node, &ctx->hotplug_drivers);

	usbi_mutex_unlock(&ctx->hotplug_drivers_lock);

	if (driver->flags & LIBUSB_HOTPLUG_ENUMERATE) {
		int i, len;
		struct libusb_device **devs;

		len = (int) libusb_get_device_list(ctx, &devs);
		if (len < 0) {
			libusb_hotplug_deregister(ctx, driver);
			return len;
		}

		for (i = 0; i < len; i++) {
			int error = usbi_hotplug_match_driver(ctx, devs[i], node);
			
			if (!error) {
				list_add(&devs[i]->list, (struct list_head*)&node->device_list);
			}
		}

		libusb_free_device_list(devs, 1);
	}

	return LIBUSB_SUCCESS;
}

void API_EXPORTED libusb_hotplug_deregister (
	struct libusb_context *ctx,
	const libusb_hotplug *driver)
{
	struct hotplug_list *it, *next;
	struct device_list *device_node;

	/* check for hotplug support */
	if (!libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) {
		return;
	}

	USBI_GET_CONTEXT(ctx);

	usbi_mutex_lock(&ctx->hotplug_drivers_lock);
	list_for_each_entry_safe(it, next, &ctx->hotplug_drivers, list, struct hotplug_list) {
		if (it->driver != driver) continue;
		
		list_for_each_entry(device_node, (struct list_head*)&it->device_list, list, struct device_list) {
			driver->disconnect(ctx, device_node->device);
		}
			
		list_del((struct list_head*)it);
		free(it);
		
	}
	usbi_mutex_unlock(&ctx->hotplug_drivers_lock);
}

void usbi_hotplug_deregister_all(struct libusb_context *ctx) {
	struct hotplug_list *it, *next;
	struct device_list *device_node;

	usbi_mutex_lock(&ctx->hotplug_drivers_lock);
	
	list_for_each_entry_safe(it, next, &ctx->hotplug_drivers, list, struct hotplug_list) {
		list_for_each_entry(device_node, (struct list_head*)&it->device_list, list, struct device_list) {
			it->driver->disconnect(ctx, device_node->device);
		}
		
		list_del((struct list_head*)it);
		free(it);
	}

	usbi_mutex_unlock(&ctx->hotplug_drivers_lock);
}
