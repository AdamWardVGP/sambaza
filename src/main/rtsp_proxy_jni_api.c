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
 * - Changes to this file are the `gstAndroidLog` method and setting of
 * `gst_debug_set_default_threshold` and `gst_debug_add_log_function`
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
#include <jni.h>
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <android/log.h>

#include "appsink_proxy.h"
#include "gstbuffer_to_sink.h"
#include "rtsp_server.h"

GST_PLUGIN_STATIC_DECLARE(app);

GST_PLUGIN_STATIC_DECLARE(coreelements);

GST_PLUGIN_STATIC_DECLARE(rtp);

GST_PLUGIN_STATIC_DECLARE(rtpmanager);

GST_PLUGIN_STATIC_DECLARE(rtsp);

GST_PLUGIN_STATIC_DECLARE(tcp);

GST_PLUGIN_STATIC_DECLARE(udp);

GST_PLUGIN_STATIC_DECLARE(videoparsersbad);


typedef struct _SkywayHandles {
    GMainLoop *main_loop;
    guint server_handle; // TODO have a collection of handles for each server -> actually we want one server but multiple streams
} SkywayHandles;

static void gstAndroidLog(GstDebugCategory * category,
                          GstDebugLevel      level,
                          const gchar      * file,
                          const gchar      * function,
                          gint               line,
                          GObject          * object,
                          GstDebugMessage  * message,
                          gpointer           user_data)
{
    (void)line;
    (void)object;
    (void)user_data;

    if (level <= gst_debug_category_get_threshold (category))
    {
        __android_log_print(ANDROID_LOG_ERROR, "SambasaDebug", "%s,%s: %s",
                            file, function, gst_debug_message_get(message));
    }
}

JNIEXPORT jlong JNICALL
Java_com_auterion_sambaza_JniApi_00024Companion_initNative(__attribute__ ((unused)) JNIEnv *env,
                                                               __attribute__ ((unused)) jobject thiz) {
    gst_debug_set_default_threshold(GST_LEVEL_INFO);
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wpedantic"

    gst_debug_add_log_function(gstAndroidLog, NULL, NULL);

    #pragma GCC diagnostic pop

    GError *err;
    gboolean init_succeeded = gst_init_check(NULL, NULL, &err);
    if (!init_succeeded) {
        g_printerr("Error initializing gstreamer: %s\n", err->message);
        return 0;
    }

    GST_PLUGIN_STATIC_REGISTER(app);
    GST_PLUGIN_STATIC_REGISTER(coreelements);
    GST_PLUGIN_STATIC_REGISTER(rtp);
    GST_PLUGIN_STATIC_REGISTER(rtpmanager);
    GST_PLUGIN_STATIC_REGISTER(rtsp);
    GST_PLUGIN_STATIC_REGISTER(tcp);
    GST_PLUGIN_STATIC_REGISTER(udp);
    GST_PLUGIN_STATIC_REGISTER(videoparsersbad);

    SkywayHandles *handles = malloc(sizeof(SkywayHandles));
    handles->main_loop = g_main_loop_new(NULL, FALSE);

    return (jlong) handles;
}

JNIEXPORT void JNICALL
Java_com_auterion_sambaza_JniApi_00024Companion_runMainLoopNative(
        __attribute__ ((unused)) JNIEnv *env,
        __attribute__ ((unused)) jobject thiz,
        jlong main_loop_handle) {
    SkywayHandles *handles = (SkywayHandles *) main_loop_handle;

    if (handles->main_loop && !g_main_loop_is_running(handles->main_loop)) {
        g_main_loop_run(handles->main_loop);
    }
}

JNIEXPORT jlong JNICALL
Java_com_auterion_sambaza_JniApi_00024Companion_createRtspServerNative(
        __attribute__ ((unused)) JNIEnv *env,
        __attribute__ ((unused)) jobject thiz,
        jint port) {
    jlong server = (jlong) skyway_rtsp_server_new(port);
    return server;
}

JNIEXPORT jint JNICALL
Java_com_auterion_sambaza_JniApi_00024Companion_getPortNative(
        __attribute__ ((unused)) JNIEnv *env,
        __attribute__ ((unused)) jobject thiz,
        jlong skyway_server_handle) {
    SkywayRtspServer *server = (SkywayRtspServer *) skyway_server_handle;
    return server->port;
}

JNIEXPORT void JNICALL
Java_com_auterion_sambaza_JniApi_00024Companion_startNative(
        __attribute__ ((unused)) JNIEnv *env,
        __attribute__ ((unused)) jobject thiz,
        jlong skyway_server_handle,
        jlong main_loop_handle) {
    SkywayHandles *handles = (SkywayHandles *) main_loop_handle;

    SkywayRtspServer *server = (SkywayRtspServer *) skyway_server_handle;
    handles->server_handle = (jlong) gst_rtsp_server_attach(server->server, NULL);
    server->port = gst_rtsp_server_get_bound_port(server->server);
}

JNIEXPORT void JNICALL
Java_com_auterion_sambaza_JniApi_00024Companion_stopNative(__attribute__ ((unused)) JNIEnv *env,
                                                               __attribute__ ((unused)) jobject thiz,
                                                               jlong skyway_server_handle,
                                                               jlong main_loop_handle) {
    SkywayHandles *handles = (SkywayHandles *) main_loop_handle;

    SkywayRtspServer *server = (SkywayRtspServer *) skyway_server_handle;
    // TODO server->src is set only for pushable proxy
    if (server->src) {
        skyway_app_sink_proxy_stop(server->src);
    }
    g_source_remove(handles->server_handle);
    g_object_unref(server->server);

    g_main_loop_quit(handles->main_loop);
}

JNIEXPORT void JNICALL
Java_com_auterion_sambaza_JniApi_00024Companion_addRtspSrcStreamNative(
        JNIEnv *env,
        __attribute__ ((unused)) jobject thiz,
        jlong skyway_server_handle,
        jstring location,
        jstring path) {

    const char *native_location = (*env)->GetStringUTFChars(env, location, 0);
    const char *native_path = (*env)->GetStringUTFChars(env, path, 0);

    SkywayRtspServer *server = (SkywayRtspServer *) skyway_server_handle;
    skyway_add_rtspsrc_stream(server, native_location, native_path);

    (*env)->ReleaseStringUTFChars(env, location, native_location);
    (*env)->ReleaseStringUTFChars(env, path, native_path);
}

JNIEXPORT void JNICALL
Java_com_auterion_sambaza_JniApi_00024Companion_addPushableStreamNative(
        JNIEnv *env,
        __attribute__ ((unused)) jobject thiz,
        jlong skyway_server_handle,
        jstring path) {

    const char *native_path = (*env)->GetStringUTFChars(env, path, 0);

    SkywayRtspServer *server = (SkywayRtspServer *) skyway_server_handle;
    skyway_add_pushable_stream(server, native_path);

    (*env)->ReleaseStringUTFChars(env, path, native_path);
}

JNIEXPORT void JNICALL
Java_com_auterion_sambaza_JniApi_00024Companion_removeStreamNative(
        JNIEnv *env,
        __attribute__ ((unused)) jobject thiz,
        jlong skyway_server_handle,
        jstring path) {
    const char *native_path = (*env)->GetStringUTFChars(env, path, 0);
    SkywayRtspServer *server = (SkywayRtspServer *) skyway_server_handle;
    skyway_remove_stream(server, native_path);
    (*env)->ReleaseStringUTFChars(env, path, native_path);
}

JNIEXPORT void JNICALL
Java_com_auterion_sambaza_JniApi_00024Companion_pushFrameNative(
        JNIEnv *env,
        __attribute__ ((unused)) jobject thiz,
        jlong skyway_server_handle,
        jlong pts,
        jbyteArray buffer,
        jstring caps) {
    jbyte *buffer_ptr = (*env)->GetByteArrayElements(env, buffer, NULL);
    jsize buffer_size = (*env)->GetArrayLength(env, buffer);
    const char *native_caps = (*env)->GetStringUTFChars(env, caps, 0);

    SkywayRtspServer *server = (SkywayRtspServer *) skyway_server_handle;
    SkywayGstBufferToSink *gst_buffer_to_sink = (SkywayGstBufferToSink *) server->src;

    GstBuffer *gst_buffer = gst_buffer_new_wrapped(buffer_ptr, buffer_size);

    if (pts == -1) {
        GST_BUFFER_PTS(gst_buffer) = GST_CLOCK_TIME_NONE;
    } else {
        GST_BUFFER_PTS(gst_buffer) = pts;
    }

    GstCaps *gst_caps = NULL;
    if (strlen(native_caps) > 0) {
        gst_caps = gst_caps_from_string(native_caps);
    }

    GstSample *sample = gst_sample_new(gst_buffer, gst_caps, NULL, NULL);
    skyway_gstbuffer_to_sink_push_sample(gst_buffer_to_sink, sample);
    gst_sample_unref(sample);

    (*env)->ReleaseByteArrayElements(env, buffer, buffer_ptr, JNI_ABORT);
    (*env)->ReleaseStringUTFChars(env, caps, native_caps);
}
