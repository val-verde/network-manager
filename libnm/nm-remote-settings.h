/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * Copyright 2008 Novell, Inc.
 * Copyright 2009 - 2011 Red Hat, Inc.
 */

#ifndef __NM_REMOTE_SETTINGS_H__
#define __NM_REMOTE_SETTINGS_H__

#if !defined (__NETWORKMANAGER_H_INSIDE__) && !defined (NETWORKMANAGER_COMPILATION)
#error "Only <NetworkManager.h> can be included directly."
#endif

#include <gio/gio.h>
#include <nm-object.h>
#include <nm-connection.h>
#include <nm-remote-connection.h>

G_BEGIN_DECLS

#define NM_TYPE_REMOTE_SETTINGS            (nm_remote_settings_get_type ())
#define NM_REMOTE_SETTINGS(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_REMOTE_SETTINGS, NMRemoteSettings))
#define NM_REMOTE_SETTINGS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NM_TYPE_REMOTE_SETTINGS, NMRemoteSettingsClass))
#define NM_IS_REMOTE_SETTINGS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_REMOTE_SETTINGS))
#define NM_IS_REMOTE_SETTINGS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), NM_TYPE_REMOTE_SETTINGS))
#define NM_REMOTE_SETTINGS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NM_TYPE_REMOTE_SETTINGS, NMRemoteSettingsClass))

/**
 * NMRemoteSettingsError:
 * @NM_REMOTE_SETTINGS_ERROR_UNKNOWN: unknown or unclassified error
 * @NM_REMOTE_SETTINGS_ERROR_CONNECTION_REMOVED: the #NMRemoteConnection object
 *   was removed before it was completely initialized
 * @NM_REMOTE_SETTINGS_ERROR_CONNECTION_UNAVAILABLE: the #NMRemoteConnection object
 *   is not visible or otherwise unreadable
 * @NM_REMOTE_SETTINGS_ERROR_SERVICE_UNAVAILABLE: NetworkManager is not running.
 *
 * Describes errors that may result from operations involving a #NMRemoteSettings.
 *
 **/
typedef enum {
	NM_REMOTE_SETTINGS_ERROR_UNKNOWN = 0,            /*< nick=UnknownError >*/
	NM_REMOTE_SETTINGS_ERROR_CONNECTION_REMOVED,     /*< nick=ConnectionRemoved >*/
	NM_REMOTE_SETTINGS_ERROR_CONNECTION_UNAVAILABLE, /*< nick=ConnectionUnavailable >*/
	NM_REMOTE_SETTINGS_ERROR_SERVICE_UNAVAILABLE,    /*< nick=ServiceUnavailable >*/
} NMRemoteSettingsError;

#define NM_REMOTE_SETTINGS_ERROR nm_remote_settings_error_quark ()
GQuark nm_remote_settings_error_quark (void);


#define NM_REMOTE_SETTINGS_NM_RUNNING      "nm-running"
#define NM_REMOTE_SETTINGS_CONNECTIONS     "connections"
#define NM_REMOTE_SETTINGS_HOSTNAME        "hostname"
#define NM_REMOTE_SETTINGS_CAN_MODIFY      "can-modify"

#define NM_REMOTE_SETTINGS_CONNECTION_ADDED   "connection-added"
#define NM_REMOTE_SETTINGS_CONNECTION_REMOVED "connection-removed"

typedef struct _NMRemoteSettings NMRemoteSettings;
typedef struct _NMRemoteSettingsClass NMRemoteSettingsClass;

struct _NMRemoteSettings {
	NMObject parent;
};

struct _NMRemoteSettingsClass {
	NMObjectClass parent;

	/* Signals */
	void (*connection_added)   (NMRemoteSettings *settings,
	                            NMRemoteConnection *connection);
	void (*connection_removed) (NMRemoteSettings *settings,
	                            NMRemoteConnection *connection);

	/*< private >*/
	gpointer padding[8];
};

GType nm_remote_settings_get_type (void);

NMRemoteSettings *nm_remote_settings_new (GCancellable  *cancellable,
                                          GError       **error);

void              nm_remote_settings_new_async  (GCancellable         *cancellable,
                                                 GAsyncReadyCallback   callback,
                                                 gpointer              user_data);
NMRemoteSettings *nm_remote_settings_new_finish (GAsyncResult         *result,
                                                 GError              **error);

GSList *nm_remote_settings_list_connections (NMRemoteSettings *settings);

NMRemoteConnection *nm_remote_settings_get_connection_by_id (NMRemoteSettings *settings,
                                                             const char *id);

NMRemoteConnection * nm_remote_settings_get_connection_by_path (NMRemoteSettings *settings,
                                                                const char *path);

NMRemoteConnection *nm_remote_settings_get_connection_by_uuid (NMRemoteSettings *settings,
                                                               const char *uuid);

void                nm_remote_settings_add_connection_async  (NMRemoteSettings *settings,
                                                              NMConnection *connection,
                                                              gboolean save_to_disk,
                                                              GCancellable *cancellable,
                                                              GAsyncReadyCallback callback,
                                                              gpointer user_data);
NMRemoteConnection *nm_remote_settings_add_connection_finish (NMRemoteSettings *settings,
                                                              GAsyncResult *result,
                                                              GError **error);

gboolean nm_remote_settings_load_connections (NMRemoteSettings *settings,
                                              char **filenames,
                                              char ***failures,
                                              GCancellable *cancellable,
                                              GError **error);

gboolean nm_remote_settings_reload_connections (NMRemoteSettings *settings,
                                                GCancellable *cancellable,
                                                GError **error);

void     nm_remote_settings_save_hostname_async  (NMRemoteSettings *settings,
                                                  const char *hostname,
                                                  GCancellable *cancellable,
                                                  GAsyncReadyCallback callback,
                                                  gpointer user_data);
gboolean nm_remote_settings_save_hostname_finish (NMRemoteSettings *settings,
                                                  GAsyncResult *result,
                                                  GError **error);

G_END_DECLS

#endif /* __NM_REMOTE_SETTINGS_H__ */
