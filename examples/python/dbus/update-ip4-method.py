#!/usr/bin/env python
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Copyright (C) 2014 Red Hat, Inc.
#

#
# This example updates a connection's IPv4 method with the Update() method.
#
# This uses the new NM 1.0 setting properties. See add-connection-compat.py
# for a similar example using the backward-compatible properties
#
# Configuration settings are described at
# https://networkmanager.dev/docs/api/latest/ref-settings.html
#

import dbus, sys

if len(sys.argv) < 3:
    print("Usage: %s <uuid> <auto|static> [address prefix gateway]" % sys.argv[0])
    sys.exit(1)

method = sys.argv[2]
if method == "static" and len(sys.argv) < 5:
    print("Usage: %s %s static address prefix [gateway]" % (sys.argv[0], sys.argv[1]))
    sys.exit(1)

# Convert method to NM method
if method == "static":
    method = "manual"

bus = dbus.SystemBus()
proxy = bus.get_object(
    "org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager/Settings"
)
settings = dbus.Interface(proxy, "org.freedesktop.NetworkManager.Settings")

for c_path in settings.ListConnections():
    c_proxy = bus.get_object("org.freedesktop.NetworkManager", c_path)
    c_obj = dbus.Interface(
        c_proxy, "org.freedesktop.NetworkManager.Settings.Connection"
    )
    c_settings = c_obj.GetSettings()

    # Look for the requested connection UUID
    if c_settings["connection"]["uuid"] != sys.argv[1]:
        continue

    # add IPv4 setting if it doesn't yet exist
    if "ipv4" not in c_settings:
        c_settings["ipv4"] = {}

    # clear existing address info
    if c_settings["ipv4"].has_key("addresses"):
        del c_settings["ipv4"]["addresses"]
    if c_settings["ipv4"].has_key("address-data"):
        del c_settings["ipv4"]["address-data"]
    if c_settings["ipv4"].has_key("gateway"):
        del c_settings["ipv4"]["gateway"]

    # set the method and change properties
    c_settings["ipv4"]["method"] = method
    if method == "manual":
        # Add the static IP address, prefix, and (optional) gateway
        addr = dbus.Dictionary(
            {"address": sys.argv[3], "prefix": dbus.UInt32(int(sys.argv[4]))}
        )
        c_settings["ipv4"]["address-data"] = dbus.Array(
            [addr], signature=dbus.Signature("a{sv}")
        )
        if len(sys.argv) == 6:
            c_settings["ipv4"]["gateway"] = sys.argv[5]

    # Save all the updated settings back to NetworkManager
    c_obj.Update(c_settings)
    break

sys.exit(0)
