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
 * - Changes to this file are merely the exchange of h264 plugins for h265depay
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
#include "rtspsrc_to_sink.h"

#include <gst/gst.h>

typedef struct _SkywayRtspSrcToSinkPrivate {
    GstElement *rtsp_source;
    GstElement *rtph265depay;
    GstElement *appsink;
    GstElement *pipeline;
    gulong pad_added_handle;
    gulong pad_removed_handle;
    gulong eos_handle;
    gulong new_sample_handle;
} SkywayRtspSrcToSinkPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SkywayRtspSrcToSink, skyway_rtsp_src_to_sink, SKYWAY_TYPE_APP_SINK_PROXY)

static void skyway_rtsp_src_to_sink_class_init(SkywayRtspSrcToSinkClass *klass);

static void skyway_rtsp_src_to_sink_init(SkywayRtspSrcToSink *self);

static void pad_added_handler(__attribute__ ((unused)) GstElement *src, GstPad *new_pad,
                              SkywayRtspSrcToSinkPrivate *data);

static void pad_removed_handler(__attribute__ ((unused)) GstElement *src, GstPad *pad,
                                SkywayRtspSrcToSinkPrivate *data);

static gboolean skyway_rtsp_src_to_sink_play(SkywayRtspSrcToSink *self);

static void skyway_rtsp_src_to_sink_stop(SkywayRtspSrcToSink *self);

static void
eos_handler(__attribute__ ((unused)) GstElement *src, SkywayAppSinkProxy *skyway_app_sink_proxy);

static GstFlowReturn
new_sample_handler(GstElement *sink, SkywayAppSinkProxy *skyway_app_sink_proxy);

static GstSample *skyway_rtsp_src_to_sink_pull_sample(SkywayRtspSrcToSink *self);

static void skyway_rtsp_src_to_sink_dispose(GObject *object);

static void skyway_rtsp_src_to_sink_class_init(SkywayRtspSrcToSinkClass *klass) {
    g_print("skyway_rtsp_src_to_sink_class_init()\n");

    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = skyway_rtsp_src_to_sink_dispose;

    klass->parent_class.play = skyway_rtsp_src_to_sink_play;
    klass->parent_class.stop = skyway_rtsp_src_to_sink_stop;
    klass->parent_class.pull_sample = (GstSample *(*)(
            SkywayAppSinkProxy *)) skyway_rtsp_src_to_sink_pull_sample;
}

static void skyway_rtsp_src_to_sink_init(__attribute__ ((unused)) SkywayRtspSrcToSink *self) {
    g_print("skyway_rtsp_src_to_sink_init()\n");
}

SkywayRtspSrcToSink *skyway_rtsp_src_to_sink_new() {
    return g_object_new(SKYWAY_TYPE_RTSP_SRC_TO_SINK, NULL);
}

gboolean skyway_rtsp_src_to_sink_prepare(SkywayRtspSrcToSink *self, const char *location) {
    g_print("skyway_rtsp_src_to_sink_prepare()\n");
    SkywayRtspSrcToSinkPrivate *priv = skyway_rtsp_src_to_sink_get_instance_private(self);

    priv->rtsp_source = gst_element_factory_make("rtspsrc", NULL);
    priv->rtph265depay = gst_element_factory_make("rtph265depay", NULL);
    priv->appsink = gst_element_factory_make("appsink", NULL);
    priv->pipeline = gst_pipeline_new(NULL);

    if (!priv->rtsp_source || !priv->rtph265depay || !priv->appsink || !priv->pipeline) {
        g_printerr("skyway_rtsp_src_to_sink_init: not all elements could be created\n");
        return FALSE;
    }

    g_object_set(priv->rtsp_source, "location", location, NULL);
    g_object_set(priv->rtsp_source, "latency", 40, NULL);
    g_object_set(priv->appsink, "emit-signals", TRUE, NULL);
    g_object_set(priv->appsink, "drop", TRUE, NULL);
    g_object_set(priv->appsink, "max-buffers", 60, NULL);

    priv->pad_added_handle = g_signal_connect(priv->rtsp_source, "pad-added",
                                              G_CALLBACK(pad_added_handler), priv);
    priv->pad_removed_handle = g_signal_connect(priv->rtsp_source, "pad-removed",
                                                G_CALLBACK(pad_removed_handler), priv);
    priv->eos_handle = g_signal_connect(priv->appsink, "eos", G_CALLBACK(eos_handler), self);
    priv->new_sample_handle = g_signal_connect(priv->appsink, "new-sample",
                                               G_CALLBACK(new_sample_handler), self);

    gst_bin_add_many(GST_BIN(priv->pipeline), priv->rtsp_source, priv->rtph265depay,
                     priv->appsink, NULL);

    return TRUE;
}

static void pad_added_handler(__attribute__ ((unused)) GstElement *src, GstPad *new_pad,
                              SkywayRtspSrcToSinkPrivate *data) {
    gchar *name = gst_pad_get_name(new_pad);
    g_print("A new pad %s is created\n", name);
    g_free(name);

    if (gst_element_link(data->rtsp_source, data->rtph265depay) != TRUE) {
        g_printerr("Elements (rtsp_source->rtph265depay) could not be linked!\n");
        return;
    }

    if (gst_element_link_many(data->rtph265depay, data->appsink, NULL) != TRUE) {
        g_printerr("Elements (rtph265depay->appsink) could not be linked!\n");
        return;
    }
}

static void pad_removed_handler(__attribute__ ((unused)) GstElement *src, GstPad *pad,
                                SkywayRtspSrcToSinkPrivate *data) {
    gchar *name = gst_pad_get_name(pad);
    g_print("pad removed: %s\n", name);
    g_free(name);

    gst_element_unlink_many(data->rtsp_source, data->rtph265depay, data->appsink, NULL);
}

static void
eos_handler(__attribute__ ((unused)) GstElement *src, SkywayAppSinkProxy *skyway_app_sink_proxy) {
    skyway_app_sink_proxy_emit_eos(skyway_app_sink_proxy);
}

static GstFlowReturn new_sample_handler(__attribute__ ((unused)) GstElement *sink,
                                        SkywayAppSinkProxy *skyway_app_sink_proxy) {
    return skyway_app_sink_proxy_emit_new_sample(skyway_app_sink_proxy);
}

static gboolean skyway_rtsp_src_to_sink_play(SkywayRtspSrcToSink *self) {
    SkywayRtspSrcToSinkPrivate *priv = skyway_rtsp_src_to_sink_get_instance_private(self);

    gst_element_set_state(priv->pipeline, GST_STATE_PLAYING);
    GstStateChangeReturn ret = gst_element_get_state(priv->pipeline, NULL, NULL,
                                                     GST_CLOCK_TIME_NONE);

    if (ret != GST_STATE_CHANGE_SUCCESS) {
        g_printerr("Failed to set appsink pipeline to PLAYING\n");
        gst_element_set_state(priv->pipeline, GST_STATE_NULL);
        return FALSE;
    }

    return TRUE;
}

static void skyway_rtsp_src_to_sink_stop(SkywayRtspSrcToSink *self) {
    SkywayRtspSrcToSinkPrivate *priv = skyway_rtsp_src_to_sink_get_instance_private(self);
    gst_element_set_state(priv->pipeline, GST_STATE_NULL);
}

static GstSample *skyway_rtsp_src_to_sink_pull_sample(SkywayRtspSrcToSink *self) {
    SkywayRtspSrcToSinkPrivate *priv = skyway_rtsp_src_to_sink_get_instance_private(self);
    GstSample *sample;
    g_signal_emit_by_name(priv->appsink, "pull-sample", &sample);
    return sample;
}

static void skyway_rtsp_src_to_sink_dispose(GObject *object) {
    g_print("skyway_rtsp_src_to_sink_dispose()\n");
    SkywayRtspSrcToSinkPrivate *priv = skyway_rtsp_src_to_sink_get_instance_private(
            SKYWAY_RTSP_SRC_TO_SINK(object));

    g_signal_handler_disconnect(priv->appsink, priv->new_sample_handle);
    g_signal_handler_disconnect(priv->appsink, priv->eos_handle);
    g_signal_handler_disconnect(priv->rtsp_source, priv->pad_removed_handle);
    g_signal_handler_disconnect(priv->rtsp_source, priv->pad_added_handle);

    gst_object_unref(priv->pipeline);

    G_OBJECT_CLASS (skyway_rtsp_src_to_sink_parent_class)->dispose(object);
}
