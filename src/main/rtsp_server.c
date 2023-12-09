/*
 * Sambaza H265
 * An RTSP server for H265 video frames
 *
 * Sambaza H265 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * Sambaza H265 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with Sambaza H265.  If not, see <http://www.gnu.org/licenses/>.
 *
 * This fork is based on commit [3a46d6c](https://github.com/Auterion/sambaza/commit/3a46d6cca5b7375b3a58b77fcf9a42e468479c55) of Sambaza
 * - Changes to this file are merely the exchange of h264 plugins for h265parse & h265pay
 *
 * Modified Library info:
 * - Modification Author: [Adam Ward](https://github.com/AdamWardVGP)
 *
 * Original library info:
 * - Base Library: Sambaza
 * - Original Author: [Jonas Vautherin](https://git.sr.ht/~jonasvautherin/)
 * - Original License: GNU Lesser General Public License
 * - Original License Version: v2.1
 * - Original License URL: https://git.sr.ht/~jonasvautherin/sambaza/tree/main/item/LICENSE
 */
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <stdio.h>
#include <stdlib.h>

#include "rtsp_server.h"

#include "appsrc_factory.h"
#include "gstbuffer_to_sink.h"
#include "rtspsrc_to_sink.h"

static void closed_handler(GstRTSPClient *client, gpointer user_data);

static void
teardown_request_handler(GstRTSPClient *client, GstRTSPContext *ctx, gpointer user_data);

static void
client_connected_handler(GstRTSPServer *server, GstRTSPClient *client, gpointer user_data);

static void closed_handler(GstRTSPClient *client, __attribute__ ((unused)) gpointer user_data) {
    GstRTSPConnection *connection = gst_rtsp_client_get_connection(client);
    GstRTSPUrl *client_url = gst_rtsp_connection_get_url(connection);
    const gchar *client_ip = client_url->host;
    guint16 client_port = client_url->port;
    g_print("Closed client: %s:%d\n", client_ip, client_port);
}

static void teardown_request_handler(__attribute__ ((unused)) GstRTSPClient *client,
                                     __attribute__ ((unused)) GstRTSPContext *ctx,
                                     __attribute__ ((unused)) gpointer user_data) {
    g_print("Teardown client\n");
}

static void
client_connected_handler(__attribute__ ((unused)) GstRTSPServer *server,
                         GstRTSPClient *client,
                         __attribute__ ((unused)) gpointer user_data) {
    g_signal_connect(client, "teardown-request", G_CALLBACK(teardown_request_handler), NULL);
    g_signal_connect(client, "closed", G_CALLBACK(closed_handler), NULL);

    GstRTSPConnection *connection = gst_rtsp_client_get_connection(client);
    GstRTSPUrl *client_url = gst_rtsp_connection_get_url(connection);
    const gchar *client_ip = client_url->host;
    guint16 client_port = client_url->port;
    g_print("Client connected: %s:%d!\n", client_ip, client_port);
}

static void add_mount_point(GstRTSPServer *server, GstRTSPMediaFactory *factory, const char *path) {
    GstRTSPMountPoints *mount_points = gst_rtsp_server_get_mount_points(server);
    gst_rtsp_mount_points_add_factory(mount_points, path, factory);
    g_object_unref(mount_points);
}

static void remove_mount_point(GstRTSPServer *server, const char *path) {
    GstRTSPMountPoints *mount_points = gst_rtsp_server_get_mount_points(server);
    gst_rtsp_mount_points_remove_factory(mount_points, path);
    g_object_unref(mount_points);
}

static GstRTSPServer *create_rtsp_server(int port) {
    GstRTSPServer *server = gst_rtsp_server_new();
    gchar port_str[50];
    sprintf(port_str, "%d", port);
    gst_rtsp_server_set_service(server, port_str);
    g_signal_connect(server, "client-connected", G_CALLBACK(client_connected_handler), NULL);

    return server;
}

static GstRTSPMediaFactory *
create_factory(SkywayAppSinkProxy *skyway_app_sink_proxy, const char *launch_str) {
    g_print("Creating appsrc factory\n");
    AppSrcFactory *app_src_factory = app_src_factory_new();
    gst_rtsp_media_factory_set_shared(GST_RTSP_MEDIA_FACTORY(app_src_factory), TRUE);
    gst_rtsp_media_factory_set_launch(GST_RTSP_MEDIA_FACTORY(app_src_factory), launch_str);
    app_src_factory->appsink = skyway_app_sink_proxy;

    return GST_RTSP_MEDIA_FACTORY(app_src_factory);
}

SkywayRtspServer *skyway_rtsp_server_new(int port) {
    SkywayRtspServer *skyway_rtsp_server = malloc(sizeof(SkywayRtspServer));
    skyway_rtsp_server->server = create_rtsp_server(port);
    skyway_rtsp_server->src = NULL;

    return skyway_rtsp_server;
}

int skyway_add_rtspsrc_stream(SkywayRtspServer *server, const char *location, const char *path) {
    SkywayRtspSrcToSink *skyway_rtsp_src_to_sink = skyway_rtsp_src_to_sink_new();
    if (!skyway_rtsp_src_to_sink_prepare(skyway_rtsp_src_to_sink, location)) {
        g_printerr("Failed to prepare SkywayRtspSrcToSink\n");
        return FALSE;
    }

    const char *launch_str = "appsrc do-timestamp=true format=time is-live=true ! queue ! rtph265pay config-interval=-1 name=pay0";
    GstRTSPMediaFactory *factory = create_factory(SKYWAY_APP_SINK_PROXY(skyway_rtsp_src_to_sink),
                                                  launch_str);
    add_mount_point(server->server, factory, path);

    return TRUE;
}

void skyway_add_pushable_stream(SkywayRtspServer *server, const char *path) {
    SkywayGstBufferToSink *skyway_gst_buffer_to_sink = skyway_gstbuffer_to_sink_new();
    server->src = skyway_gst_buffer_to_sink; // TODO remove later, now support only one pushable stream per server

    const char *launch_str = "appsrc do-timestamp=true format=time is-live=true ! h265parse config-interval=-1 ! queue ! rtph265pay name=pay0";
    GstRTSPMediaFactory *factory = create_factory(SKYWAY_APP_SINK_PROXY(skyway_gst_buffer_to_sink),
                                                  launch_str);
    add_mount_point(server->server, factory, path);
}

void skyway_remove_stream(SkywayRtspServer *server, const char *path) {
    remove_mount_point(server->server, path);
}
