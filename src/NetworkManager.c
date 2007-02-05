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
 * (C) Copyright 2004 Red Hat, Inc.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <glib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <dbus/dbus-glib.h>
#include <libhal.h>
#include <getopt.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <glib/gi18n.h>

#include "NetworkManager.h"
#include "nm-utils.h"
#include "NetworkManagerUtils.h"
#include "nm-device.h"
#include "nm-device-802-3-ethernet.h"
#include "nm-device-802-11-wireless.h"
#include "NetworkManagerPolicy.h"
#include "NetworkManagerDbus.h"
#include "NetworkManagerAP.h"
#include "NetworkManagerAPList.h"
#include "NetworkManagerSystem.h"
#include "nm-named-manager.h"
#include "nm-vpn-act-request.h"
#include "nm-dbus-vpn.h"
#include "nm-dbus-nm.h"
#include "nm-dbus-manager.h"
#include "nm-dbus-device.h"
#include "nm-supplicant-manager.h"
#include "nm-dbus-net.h"
#include "nm-netlink-monitor.h"
#include "nm-logging.h"

#define NM_WIRELESS_LINK_STATE_POLL_INTERVAL (5 * 1000)

#define NM_DEFAULT_PID_FILE	LOCALSTATEDIR"/run/NetworkManager.pid"

#define NO_HAL_MSG "Could not initialize connection to the HAL daemon."

/*
 * Globals
 */
static NMData		*nm_data = NULL;

static gboolean sigterm_pipe_handler (GIOChannel *src, GIOCondition condition, gpointer data);
static void nm_data_free (NMData *data);
static void nm_hal_deinit (NMData *data);

/*
 * nm_get_device_interface_from_hal
 *
 */
static char *nm_get_device_interface_from_hal (LibHalContext *ctx, const char *udi)
{
	char *iface = NULL;

	if (libhal_device_property_exists (ctx, udi, "net.interface", NULL))
	{
		/* Only use Ethernet and Wireless devices at the moment */
		if (libhal_device_property_exists (ctx, udi, "info.category", NULL))
		{
			char *category = libhal_device_get_property_string (ctx, udi, "info.category", NULL);
			if (category && (!strcmp (category, "net.80203") || !strcmp (category, "net.80211")))
			{
				char *temp = libhal_device_get_property_string (ctx, udi, "net.interface", NULL);
				iface = g_strdup (temp);
				libhal_free_string (temp);
			}
			libhal_free_string (category);
		}
	}

	return (iface);
}


/*
 * nm_device_test_wireless_extensions
 *
 * Test whether a given device is a wireless one or not.
 *
 */
static NMDeviceType
discover_device_type (LibHalContext *ctx, const char *udi)
{
	char * category = NULL;

	if (libhal_device_property_exists (ctx, udi, "info.category", NULL))
		category = libhal_device_get_property_string(ctx, udi, "info.category", NULL);
	if (category && (!strcmp (category, "net.80211")))
		return DEVICE_TYPE_802_11_WIRELESS;
	else if (category && (!strcmp (category, "net.80203")))
		return DEVICE_TYPE_802_3_ETHERNET;
	return DEVICE_TYPE_UNKNOWN;
}

/*
 * nm_get_device_driver_name
 *
 * Get the device's driver name from HAL.
 *
 */
static char *
nm_get_device_driver_name (LibHalContext *ctx, const char *udi)
{
	char	*	driver_name = NULL;
	char *	physdev_udi = NULL;

	g_return_val_if_fail (ctx != NULL, NULL);
	g_return_val_if_fail (udi != NULL, NULL);

	physdev_udi = libhal_device_get_property_string (ctx, udi, "net.physical_device", NULL);
	if (physdev_udi && libhal_device_property_exists (ctx, physdev_udi, "info.linux.driver", NULL))
	{
		char *drv = libhal_device_get_property_string (ctx, physdev_udi, "info.linux.driver", NULL);
		driver_name = g_strdup (drv);
		g_free (drv);
	}
	g_free (physdev_udi);

	return driver_name;
}


static NMDevice *
create_nm_device (LibHalContext *ctx,
				  const char *iface,
				  const char *udi)
{
	NMDevice *dev;
	char *driver;
	NMDeviceType type;

	type = discover_device_type (ctx, udi);
	driver = nm_get_device_driver_name (ctx, udi);

	switch (type) {
	case DEVICE_TYPE_802_11_WIRELESS:
		dev = (NMDevice *) nm_device_802_11_wireless_new (iface, udi, driver, FALSE, nm_data);
		break;
	case DEVICE_TYPE_802_3_ETHERNET:
		dev = (NMDevice *) nm_device_802_3_ethernet_new (iface, udi, driver, FALSE, nm_data);
		break;

	default:
		g_assert_not_reached ();
	}

	g_free (driver);

	return dev;
}


/*
 * nm_create_device_and_add_to_list
 *
 * Create a new network device and add it to our device list.
 *
 * Returns:		newly allocated device on success
 *				NULL on failure
 */
NMDevice * nm_create_device_and_add_to_list (NMData *data, const char *udi, const char *iface,
					     gboolean test_device, NMDeviceType test_device_type)
{
	NMDevice	*dev = NULL;

	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (udi  != NULL, NULL);
	g_return_val_if_fail (iface != NULL, NULL);
	g_return_val_if_fail (strlen (iface) > 0, NULL);

	/* If we are called to create a test devices, but test devices weren't enabled
	 * on the command-line, don't create the device.
	 */
	if (!data->enable_test_devices && test_device)
	{
		nm_warning ("attempted to create a test device, "
			    "but test devices were not enabled "
			    "on the command line.");
		return (NULL);
	}

	/* Make sure the device is not already in the device list */
	if ((dev = nm_get_device_by_iface (data, iface)))
		return (NULL);

	if ((dev = create_nm_device (data->hal_ctx, iface, udi))) {
		nm_info ("Now managing %s device '%s'.",
				 NM_IS_DEVICE_802_11_WIRELESS (dev) ? "wireless (802.11)" : "wired Ethernet (802.3)",
				 nm_device_get_iface (dev));

		data->dev_list = g_slist_append (data->dev_list, dev);
		nm_device_deactivate (dev);

		nm_policy_schedule_device_change_check (data);
		nm_dbus_schedule_device_status_change_signal (data, dev, NULL, DEVICE_ADDED);
	}

	return dev;
}


/*
 * nm_remove_device
 *
 * Removes a particular device from the device list.
 */
void nm_remove_device (NMData *data, NMDevice *dev)
{
	g_return_if_fail (data != NULL);
	g_return_if_fail (dev != NULL);

	nm_device_set_removed (dev, TRUE);
	nm_device_stop (dev);
	nm_dbus_schedule_device_status_change_signal (data, dev, NULL, DEVICE_REMOVED);

	g_object_unref (G_OBJECT (dev));

	/* Remove the device entry from the device list and free its data */
	data->dev_list = g_slist_remove (data->dev_list, dev);
}


/*
 * nm_get_active_device
 *
 * Return the currently active device.
 *
 */
NMDevice *nm_get_active_device (NMData *data)
{
	GSList * elt;
	
	g_return_val_if_fail (data != NULL, NULL);

	for (elt = data->dev_list; elt; elt = g_slist_next (elt)) {
		NMDevice * dev = NM_DEVICE (elt->data);

		g_assert (dev);
		if (nm_device_get_act_request (dev))
			return dev;
	}

	return NULL;
}


/*
 * nm_hal_device_added
 *
 */
static void nm_hal_device_added (LibHalContext *ctx, const char *udi)
{
	NMData	*data = (NMData *)libhal_ctx_get_user_data (ctx);
	char		*iface = NULL;

	g_return_if_fail (data != NULL);

	nm_debug ("New device added (hal udi is '%s').", udi );

	/* Sometimes the device's properties (like net.interface) are not set up yet,
	 * so this call will fail, and it will actually be added when hal sets the device's
	 * capabilities a bit later on.
	 */
	if ((iface = nm_get_device_interface_from_hal (data->hal_ctx, udi)))
	{
		nm_create_device_and_add_to_list (data, udi, iface, FALSE, DEVICE_TYPE_UNKNOWN);
		g_free (iface);
	}
}


/*
 * nm_hal_device_removed
 *
 */
static void nm_hal_device_removed (LibHalContext *ctx, const char *udi)
{
	NMData *   data;
	NMDevice * dev;

	data = (NMData *) libhal_ctx_get_user_data (ctx);
 	g_return_if_fail (data != NULL);

	nm_debug ("Device removed (hal udi is '%s').", udi );

	if ((dev = nm_get_device_by_udi (data, udi))) {
		nm_remove_device (data, dev);
		nm_policy_schedule_device_change_check (data);
	}
}


/*
 * nm_hal_device_new_capability
 *
 */
static void nm_hal_device_new_capability (LibHalContext *ctx, const char *udi, const char *capability)
{
	NMData	*data = (NMData *)libhal_ctx_get_user_data (ctx);

	g_return_if_fail (data != NULL);

	/*nm_debug ("nm_hal_device_new_capability() called with udi = %s, capability = %s", udi, capability );*/

	if (capability && ((strcmp (capability, "net.80203") == 0) || (strcmp (capability, "net.80211") == 0)))
	{
		char *iface;

		if ((iface = nm_get_device_interface_from_hal (data->hal_ctx, udi)))
		{
			nm_create_device_and_add_to_list (data, udi, iface, FALSE, DEVICE_TYPE_UNKNOWN);
			g_free (iface);
		}
	}
}


/*
 * nm_add_initial_devices
 *
 * Add all devices that hal knows about right now (ie not hotplug devices)
 *
 */
void nm_add_initial_devices (NMData *data)
{
	char **	net_devices;
	int		num_net_devices;
	int		i;
	DBusError	error;

	g_return_if_fail (data != NULL);

	dbus_error_init (&error);
	/* Grab a list of network devices */
	net_devices = libhal_find_device_by_capability (data->hal_ctx, "net", &num_net_devices, &error);
	if (dbus_error_is_set (&error))
	{
		nm_warning ("could not find existing networking devices: %s", error.message);
		dbus_error_free (&error);
	}

	if (net_devices)
	{
		for (i = 0; i < num_net_devices; i++)
		{
			char *iface;

			if ((iface = nm_get_device_interface_from_hal (data->hal_ctx, net_devices[i])))
			{
				nm_create_device_and_add_to_list (data, net_devices[i], iface, FALSE, DEVICE_TYPE_UNKNOWN);
				g_free (iface);
			}
		}
	}

	libhal_free_string_array (net_devices);
}


/*
 * nm_state_change_signal_broadcast
 *
 */
static gboolean nm_state_change_signal_broadcast (gpointer user_data)
{
	NMData *data = (NMData *)user_data;
	NMDBusManager *dbus_mgr = NULL;
	DBusConnection *dbus_connection = NULL;

	g_return_val_if_fail (data != NULL, FALSE);

	dbus_mgr = nm_dbus_manager_get ();
	dbus_connection = nm_dbus_manager_get_dbus_connection (dbus_mgr);
	if (dbus_connection)
		nm_dbus_signal_state_change (dbus_connection, data);
	g_object_unref (dbus_mgr);
	return FALSE;
}


/*
 * nm_schedule_state_change_signal_broadcast
 *
 */
void nm_schedule_state_change_signal_broadcast (NMData *data)
{
	guint	  id = 0;
	GSource	* source;

	g_return_if_fail (data != NULL);

	id = g_idle_add (nm_state_change_signal_broadcast, data);
	source = g_main_context_find_source_by_id (NULL, id);
	if (source) {
		g_source_set_priority (source, G_PRIORITY_HIGH);
	}
}


static void
nm_error_monitoring_device_link_state (NmNetlinkMonitor *monitor,
				      GError 	       *error,
				      NMData	       *data)
{
	/* FIXME: Try to handle the error instead of just printing it. */
	nm_warning ("error monitoring wired ethernet link state: %s\n",
		    error->message);
}

static NmNetlinkMonitor *
nm_monitor_setup (NMData *data)
{
	GError *error = NULL;
	NmNetlinkMonitor *monitor;

	monitor = nm_netlink_monitor_new (data);
	nm_netlink_monitor_open_connection (monitor, &error);
	if (error != NULL)
	{
		nm_warning ("could not monitor wired ethernet devices: %s",
			    error->message);
		g_error_free (error);
		g_object_unref (monitor);
		return NULL;
	}

	g_signal_connect (G_OBJECT (monitor), "error",
			  G_CALLBACK (nm_error_monitoring_device_link_state),
			  data);

	nm_netlink_monitor_attach (monitor, NULL);

	/* Request initial status of cards */
	nm_netlink_monitor_request_status (monitor, NULL);
	return monitor;
}

static gboolean
nm_hal_init (NMData *data,
             DBusConnection *connection)
{
	gboolean	success = FALSE;
	DBusError	error;

	g_return_val_if_fail (data != NULL, FALSE);
	g_return_val_if_fail (connection != NULL, FALSE);

	/* Clean up an old context */
	if (data->hal_ctx) {
		nm_warning ("a HAL context already existed.  BUG.");
		nm_hal_deinit (data);
	}

	/* Initialize a new libhal context */
	if (!(data->hal_ctx = libhal_ctx_new ())) {
		nm_warning ("Could not get connection to the HAL service.");
		goto out;
	}

	libhal_ctx_set_dbus_connection (data->hal_ctx, connection);

	dbus_error_init (&error);
	if (!libhal_ctx_init (data->hal_ctx, &error)) {
		nm_error ("libhal_ctx_init() failed: %s\n"
			  "Make sure the hal daemon is running?", 
			  error.message);
		goto out;
	}

	libhal_ctx_set_user_data (data->hal_ctx, data);
	libhal_ctx_set_device_added (data->hal_ctx, nm_hal_device_added);
	libhal_ctx_set_device_removed (data->hal_ctx, nm_hal_device_removed);
	libhal_ctx_set_device_new_capability (data->hal_ctx, nm_hal_device_new_capability);

	libhal_device_property_watch_all (data->hal_ctx, &error);
	if (dbus_error_is_set (&error)) {
		nm_error ("libhal_device_property_watch_all(): %s", error.message);
		libhal_ctx_shutdown (data->hal_ctx, NULL);
		goto out;
	}

	/* Add any devices we know about */
	nm_add_initial_devices (data);
	success = TRUE;

out:
	if (!success) {
		if (dbus_error_is_set (&error))
			dbus_error_free (&error);
		if (data->hal_ctx) {
			libhal_ctx_free (data->hal_ctx);
			data->hal_ctx = NULL;
		}
	}

	return success;
}

static void
nm_hal_deinit (NMData *data)
{
	DBusError error;

	g_return_if_fail (data != NULL);

	if (!data->hal_ctx)
		return;

	dbus_error_init (&error);
	libhal_ctx_shutdown (data->hal_ctx, &error);
	if (dbus_error_is_set (&error)) {
		nm_warning ("libhal shutdown failed - %s", error.message);
		dbus_error_free (&error);
	}
	libhal_ctx_free (data->hal_ctx);
	data->hal_ctx = NULL;
}


/*
 * nm_data_new
 *
 * Create data structure used in callbacks from libhal.
 *
 */
static NMData *nm_data_new (gboolean enable_test_devices)
{
	NMData * data;
	guint    id;

	data = g_slice_new0 (NMData);

	data->main_loop = g_main_loop_new (NULL, FALSE);

	/* Allow clean shutdowns by having the thread which receives the signal
	 * notify the main thread to quit, rather than having the receiving
	 * thread try to quit the glib main loop.
	 */
	if (pipe (data->sigterm_pipe) < 0) {
		nm_error ("Couldn't create pipe: %s", g_strerror (errno));
		return NULL;
	}
	data->sigterm_iochannel = g_io_channel_unix_new (data->sigterm_pipe[0]);
	id = g_io_add_watch (data->sigterm_iochannel,
	                     G_IO_IN | G_IO_ERR,
	                     sigterm_pipe_handler,
	                     data);

	/* Initialize the access point lists */
	data->allowed_ap_list = nm_ap_list_new (NETWORK_TYPE_ALLOWED);
	data->invalid_ap_list = nm_ap_list_new (NETWORK_TYPE_INVALID);
	if (!data->allowed_ap_list || !data->invalid_ap_list)
	{
		nm_data_free (data);
		nm_warning ("could not create access point lists.");
		return NULL;
	}

	/* Create watch functions that monitor cards for link status. */
	if (!(data->netlink_monitor = nm_monitor_setup (data)))
	{
		nm_data_free (data);
		nm_warning ("could not create netlink monitor.");
		return NULL;
	}

	data->enable_test_devices = enable_test_devices;
	data->wireless_enabled = TRUE;
	return data;
}


static void device_stop_and_free (NMDevice *dev, gpointer user_data)
{
	g_return_if_fail (dev != NULL);

	nm_device_set_removed (dev, TRUE);
	nm_device_deactivate (dev);
	g_object_unref (G_OBJECT (dev));
}


/*
 * nm_data_free
 *
 *   Free data structure used in callbacks.
 *
 */
static void nm_data_free (NMData *data)
{
	NMVPNActRequest *req;

	g_return_if_fail (data != NULL);

	/* Kill any active VPN connection */
	if ((req = nm_vpn_manager_get_vpn_act_request (data->vpn_manager)))
		nm_vpn_manager_deactivate_vpn_connection (data->vpn_manager, nm_vpn_act_request_get_parent_dev (req));

	/* Stop and destroy all devices */
	g_slist_foreach (data->dev_list, (GFunc) device_stop_and_free, NULL);
	g_slist_free (data->dev_list);

	if (data->netlink_monitor) {
		g_object_unref (G_OBJECT (data->netlink_monitor));
		data->netlink_monitor = NULL;
	}

	nm_ap_list_unref (data->allowed_ap_list);
	nm_ap_list_unref (data->invalid_ap_list);

	nm_dbus_method_list_unref (data->nm_methods);
	nm_dbus_method_list_unref (data->device_methods);

	nm_vpn_manager_dispose (data->vpn_manager);
	g_object_unref (data->named_manager);

	g_main_loop_unref (data->main_loop);
	g_io_channel_unref(data->sigterm_iochannel);

	nm_hal_deinit (data);

	g_slice_free (NMData, data);
}

int nm_get_sigterm_pipe (void)
{
	return nm_data->sigterm_pipe[1];
}

static gboolean sigterm_pipe_handler (GIOChannel *src, GIOCondition condition, gpointer user_data)
{
	NMData *		data = user_data;

	nm_info ("Caught terminiation signal");
	g_main_loop_quit (data->main_loop);
	return FALSE;
}

static void
nm_name_owner_changed_handler (NMDBusManager *mgr,
                               DBusConnection *connection,
                               const char *name,
                               const char *old,
                               const char *new,
                               gpointer user_data)
{
	NMData * data = (NMData *) user_data;
	gboolean old_owner_good = (old && (strlen (old) > 0));
	gboolean new_owner_good = (new && (strlen (new) > 0));

	/* Only care about signals from HAL */
	if (strcmp (name, "org.freedesktop.Hal") == 0) {
		if (!old_owner_good && new_owner_good) {
			/* HAL just appeared */
			if (!nm_hal_init (data, connection)) {
				nm_error (NO_HAL_MSG);
				exit (EXIT_FAILURE);
			}
		} else if (old_owner_good && !new_owner_good) {
			/* HAL went away.  Bad HAL. */
			nm_hal_deinit (data);
		}
	} else if (strcmp (name, NMI_DBUS_SERVICE) == 0) {
		if (!old_owner_good && new_owner_good) {
			/* NMI appeared, update stuff */
			nm_policy_schedule_allowed_ap_list_update (data);
			nm_dbus_vpn_schedule_vpn_connections_update (data);
		} else if (old_owner_good && !new_owner_good) {
			/* nothing */
		}
	}
}

static void
nm_dbus_connection_changed_handler (NMDBusManager *mgr,
                                    DBusConnection *connection,
                                    gpointer user_data)
{
	NMData *data = (NMData *) user_data;
	char * 	owner;

	if (!connection) {
		nm_hal_deinit (data);
		return;
	}

	if ((owner = nm_dbus_manager_get_name_owner (mgr, "org.freedesktop.Hal"))) {
		if (!nm_hal_init (data, connection)) {
			nm_error (NO_HAL_MSG);
			exit (EXIT_FAILURE);
		}
		g_free (owner);
	}
}

static void
write_pidfile (const char *pidfile)
{
 	char pid[16];
	int fd;
 
	if ((fd = open (pidfile, O_CREAT|O_WRONLY|O_TRUNC, 00644)) < 0)
	{
		nm_warning ("Opening %s failed: %s", pidfile, strerror (errno));
		return;
	}
 	snprintf (pid, sizeof (pid), "%d", getpid ());
	if (write (fd, pid, strlen (pid)) < 0)
		nm_warning ("Writing to %s failed: %s", pidfile, strerror (errno));
	if (close (fd))
		nm_warning ("Closing %s failed: %s", pidfile, strerror (errno));
}


/*
 * nm_print_usage
 *
 * Prints program usage.
 *
 */
static void nm_print_usage (void)
{
	fprintf (stderr,
		"\n"
		"NetworkManager monitors all network connections and automatically\n"
		"chooses the best connection to use.  It also allows the user to\n"
		"specify wireless access points which wireless cards in the computer\n"
		"should associate with.\n"
		"\n");
}


/*
 * main
 *
 */
int
main (int argc, char *argv[])
{
	GOptionContext *opt_ctx = NULL;
	gboolean		become_daemon = FALSE;
	gboolean		enable_test_devices = FALSE;
	gboolean		show_usage = FALSE;
	char *		pidfile = NULL;
	char *		user_pidfile = NULL;
	NMDBusManager *	dbus_mgr;
	DBusConnection *dbus_connection;
	NMSupplicantManager * sup_mgr = NULL;
	int			exit_status = EXIT_FAILURE;
	guint32     id;

	GOptionEntry options[] = {
		{"no-daemon", 0, 0, G_OPTION_ARG_NONE, &become_daemon, "Don't become a daemon", NULL},
		{"pid-file", 0, 0, G_OPTION_ARG_STRING, &user_pidfile, "Specify the location of a PID file", NULL},
		{"enable-test-devices", 0, 0, G_OPTION_ARG_NONE, &enable_test_devices, "Allow dummy devices to be created via DBUS methods [DEBUG]", NULL},
		{"info", 0, 0, G_OPTION_ARG_NONE, &show_usage, "Show application information", NULL},
		{NULL}
	};

	if (getuid () != 0) {
		g_printerr ("You must be root to run NetworkManager!\n");
		goto exit;
	}

	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* Parse options */
	opt_ctx = g_option_context_new("");
	g_option_context_add_main_entries(opt_ctx, options, NULL);
	g_option_context_parse(opt_ctx, &argc, &argv, NULL);
	g_option_context_free(opt_ctx);

	if (show_usage == TRUE) {
		nm_print_usage();
		exit_status = EXIT_SUCCESS;
		goto exit;
	}

	pidfile = g_strdup (user_pidfile ? user_pidfile : NM_DEFAULT_PID_FILE);

	/* Tricky: become_daemon is FALSE by default, so unless it's TRUE because
	 * of a CLI option, it'll become TRUE after this
	 */
	become_daemon = !become_daemon;
	if (become_daemon) {
		if (daemon (0, 0) < 0) {
			int saved_errno;

			saved_errno = errno;
			nm_error ("Could not daemonize: %s [error %u]",
			          g_strerror (saved_errno),
			          saved_errno);
			goto exit;
		}
		write_pidfile (pidfile);
	}

	/*
	 * Set the umask to 0022, which results in 0666 & ~0022 = 0644.
	 * Otherwise, if root (or an su'ing user) has a wacky umask, we could
	 * write out an unreadable resolv.conf.
	 */
	umask (022);

	g_type_init ();
	if (!g_thread_supported ())
		g_thread_init (NULL);
	dbus_g_thread_init ();
	
	nm_logging_setup (become_daemon);
	nm_info ("starting...");

	nm_system_init();

	/* Initialize our instance data */
	nm_data = nm_data_new (enable_test_devices);
	if (!nm_data) {
		nm_error ("Failed to initialize.");
		goto pidfile;
	}

	/* Initialize our DBus service & connection */
	dbus_mgr = nm_dbus_manager_get ();
	dbus_connection = nm_dbus_manager_get_dbus_connection (dbus_mgr);
	if (!dbus_connection) {
		nm_error ("Failed to initialize. "
		          "Either dbus is not running, or the "
		          "NetworkManager dbus security policy "
		          "was not loaded.");
		goto done;
	}
	g_signal_connect (G_OBJECT (dbus_mgr), "name-owner-changed",
	                  G_CALLBACK (nm_name_owner_changed_handler), nm_data);
	g_signal_connect (G_OBJECT (dbus_mgr), "dbus-connection-changed",
	                  G_CALLBACK (nm_dbus_connection_changed_handler), nm_data);
	id = nm_dbus_manager_register_signal_handler (dbus_mgr,
	                                              NMI_DBUS_INTERFACE,
	                                              NULL,
	                                              nm_dbus_nmi_signal_handler,
	                                              nm_data);
	nm_data->nmi_sig_handler_id = id;

	/* Register DBus method handlers for the main NM objects */
	nm_data->nm_methods = nm_dbus_nm_methods_setup (nm_data);
	nm_dbus_manager_register_method_list (dbus_mgr, nm_data->nm_methods);
	nm_data->device_methods = nm_dbus_device_methods_setup (nm_data);
	nm_dbus_manager_register_method_list (dbus_mgr, nm_data->device_methods);
	nm_data->net_methods = nm_dbus_net_methods_setup (nm_data);

	/* Initialize the supplicant manager */
	sup_mgr = nm_supplicant_manager_get ();
	if (!sup_mgr) {
		nm_error ("Failed to initialize the supplicant manager.");
		goto done;
	}

	nm_data->vpn_manager = nm_vpn_manager_new (nm_data);
	if (!nm_data->vpn_manager) {
		nm_warning ("Failed to start the VPN manager.");
		goto done;
	}

	nm_data->named_manager = nm_named_manager_new ();
	if (!nm_data->named_manager) {
		nm_warning ("Failed to start the named manager.");
		goto done;
	}

	/* Start our DBus service */
	if (!nm_dbus_manager_start_service (dbus_mgr)) {
		nm_warning ("Failed to start the named manager.");
		goto done;
	}

	/* If Hal is around, grab a device list from it */
	if (nm_dbus_manager_name_has_owner (dbus_mgr, "org.freedesktop.Hal")) {
		if (!nm_hal_init (nm_data, dbus_connection)) {
			nm_error (NO_HAL_MSG);
			goto done;
		}
	}

	/* If NMI is running, grab allowed wireless network lists from it ASAP */
	if (nm_dbus_manager_name_has_owner (dbus_mgr, NMI_DBUS_SERVICE)) {
		nm_policy_schedule_allowed_ap_list_update (nm_data);
		nm_dbus_vpn_schedule_vpn_connections_update (nm_data);
	}

	/* We run dhclient when we need to, and we don't want any stray ones
	 * lying around upon launch.
	 */
//	nm_system_kill_all_dhcp_daemons ();

	/* Bring up the loopback interface. */
	nm_system_enable_loopback ();

	/* Get modems, ISDN, and so on's configuration from the system */
	nm_data->dialup_list = nm_system_get_dialup_config ();

	/* Run the main loop */
	nm_policy_schedule_device_change_check (nm_data);
	nm_schedule_state_change_signal_broadcast (nm_data);
	exit_status = EXIT_SUCCESS;
	g_main_loop_run (nm_data->main_loop);

done:
	nm_print_open_socks ();

	nm_dbus_manager_remove_signal_handler (dbus_mgr, nm_data->nmi_sig_handler_id);

	nm_data_free (nm_data);

	if (sup_mgr)
		g_object_unref (sup_mgr);

	/* nm_data_free needs the dbus connection, so must kill the
	 * dbus manager after that.
	 */
	g_object_unref (dbus_mgr);
	nm_logging_shutdown ();

pidfile:
	if (pidfile)
		unlink (pidfile);
	g_free (pidfile);

exit:
	exit (exit_status);
}
