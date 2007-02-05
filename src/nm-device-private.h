/* NetworkManager -- Network link manager
 *
 * Dan Williams <dcbw@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * (C) Copyright 2005 Red Hat, Inc.
 */

#ifndef NM_DEVICE_PRIVATE_H
#define NM_DEVICE_PRIVATE_H

#include "nm-device.h"
#include "NetworkManagerMain.h"

typedef struct NMDbusCBData {
	NMDevice *		dev;
	NMAccessPoint *	ap;
	NMData *		data;
} NMDbusCBData;


void			nm_device_set_device_type (NMDevice *dev, NMDeviceType type);

gboolean		nm_device_is_activated (NMDevice *dev);

NMIP4Config *	nm_device_new_ip4_autoip_config (NMDevice *self);

void			nm_device_activate_schedule_stage3_ip_config_start (struct NMActRequest *req);

void			nm_device_state_changed (NMDevice *device, NMDeviceState state);


#endif	/* NM_DEVICE_PRIVATE_H */
