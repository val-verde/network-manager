/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2014 - 2016 Red Hat, Inc.
 */

#ifndef __NM_SD_H__
#define __NM_SD_H__

#include "src/systemd/sd-dhcp-client.h"
#include "src/systemd/sd-dhcp6-client.h"
#include "src/systemd/sd-lldp-rx.h"

/*****************************************************************************/

guint nm_sd_event_attach_default(void);

/*****************************************************************************
 * expose internal systemd API
 *
 * FIXME: don't use any internal systemd API.
 *****************************************************************************/

struct sd_dhcp_lease;

int dhcp_lease_save(struct sd_dhcp_lease *lease, const char *lease_file);
int dhcp_lease_load(struct sd_dhcp_lease **ret, const char *lease_file);

#endif /* __NM_SD_H__ */
