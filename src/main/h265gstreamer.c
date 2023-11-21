#include <string.h>
#include <stdint.h>
#include <jni.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <pthread.h>
#include <gst/base/gstqueuearray.h>
#include <gst/app/gstappsrc.h>

GST_DEBUG_CATEGORY_STATIC (debug_category);
#define GST_CAT_DEFAULT debug_category

/*
 * These macros provide a way to store the native pointer to CustomData, which might be 32 or 64 bits, into
 * a jlong, which is always 64 bits, without warnings.
 */
#if GLIB_SIZEOF_VOID_P == 8
# define GET_CUSTOM_DATA(env, thiz, fieldID) (CustomData *)(*env)->GetStaticLongField (env, thiz, fieldID)
# define SET_CUSTOM_DATA(env, thiz, fieldID, data) (*env)->SetStaticLongField (env, thiz, fieldID, (jlong)data)
#else
# define GET_CUSTOM_DATA(env, thiz, fieldID) (CustomData *)(jint)(*env)->GetStaticLongField (env, thiz, fieldID)
# define SET_CUSTOM_DATA(env, thiz, fieldID, data) (*env)->SetStaticLongField (env, thiz, fieldID, (jlong)(jint)data)
#endif

//its likely we need to add these plugins

GST_PLUGIN_STATIC_DECLARE(app);
GST_PLUGIN_STATIC_DECLARE(coreelements);
GST_PLUGIN_STATIC_DECLARE(videoparsersbad);
GST_PLUGIN_STATIC_DECLARE(libav);
GST_PLUGIN_STATIC_DECLARE(videoconvertscale);
GST_PLUGIN_STATIC_DECLARE(autodetect);


/* Structure to contain all our information, so we can pass it to callbacks */
typedef struct _CustomData
{
  jobject app;                  /* Application instance, used to call its methods. A global reference is kept. */
  GstElement *pipeline;         /* The running pipeline */
  GstElement *appsrc, *parser, *decoder, *converter,  *sink; /* Elements of the pipeline */

  GMainContext *context;        /* GLib context used to run the main loop */
  GMainLoop *main_loop;         /* GLib main loop */
  gboolean initialized;         /* To avoid informing the UI multiple times about the initialization */
  GstElement *video_sink;       /* The video sink element which receives XOverlay commands */
  ANativeWindow *native_window; /* The Android native window where video will be rendered */

  /* appsrc buffers */
  guint num_buffers;
  guint max_buffers;
  GstQueueArray *queue;

} CustomData;

/* These global variables cache values which are not changing during execution */
static pthread_t gst_app_thread;
static pthread_key_t current_jni_env;
static JavaVM *java_vm;
static jfieldID custom_data_field_id;
static jmethodID set_message_method_id;
static jmethodID on_gstreamer_initialized_method_id;

/*
 * Private methods
 */

/* Register this thread with the VM */
static JNIEnv *
attach_current_thread (void)
{
  JNIEnv *env;
  JavaVMAttachArgs args;

  GST_DEBUG ("Attaching thread %p", (void *)g_thread_self ());
  args.version = JNI_VERSION_1_4;
  args.name = NULL;
  args.group = NULL;

  if ((*java_vm)->AttachCurrentThread (java_vm, &env, &args) < 0) {
    GST_ERROR ("Failed to attach current thread");
    return NULL;
  }

  return env;
}

/* Unregister this thread from the VM */
static void
detach_current_thread (void *env)
{
    (void)env; //prevent compiler error on unused param. This method as a whole is passed into
    //pthread_key_create so perhaps it's used internally?
  GST_DEBUG ("Detaching thread %p", (void *)g_thread_self ());
  (*java_vm)->DetachCurrentThread (java_vm);
}

/* Retrieve the JNI environment for this thread */
static JNIEnv *
get_jni_env (void)
{
  JNIEnv *env;

  if ((env = pthread_getspecific (current_jni_env)) == NULL) {
    env = attach_current_thread ();
    pthread_setspecific (current_jni_env, env);
  }

  return env;
}

/* Change the content of the UI's TextView */
static void
set_ui_message (const gchar * message, CustomData * data)
{
  JNIEnv *env = get_jni_env ();
  GST_DEBUG ("Setting message to: %s", message);
  jstring jmessage = (*env)->NewStringUTF (env, message);
  (*env)->CallStaticVoidMethod (env, data->app, set_message_method_id, jmessage);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
  }
  (*env)->DeleteLocalRef (env, jmessage);
}

/* Retrieve errors from the bus and show them on the UI */
static void
error_cb (GstBus * bus, GstMessage * msg, CustomData * data)
{
    (void)bus;

  GError *err;
  gchar *debug_info;
  gchar *message_string;

  gst_message_parse_error (msg, &err, &debug_info);
  message_string =
      g_strdup_printf ("Error received from element %s: %s",
      GST_OBJECT_NAME (msg->src), err->message);
  g_clear_error (&err);
  g_free (debug_info);
  set_ui_message (message_string, data);
  g_free (message_string);
  gst_element_set_state (data->pipeline, GST_STATE_NULL);
}

/* Notify UI about pipeline state changes */
static void
state_changed_cb (GstBus * bus, GstMessage * msg, CustomData * data)
{
    (void)bus;

  GstState old_state, new_state, pending_state;
  gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
  /* Only pay attention to messages coming from the pipeline, not its children */
  if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data->pipeline)) {
    gchar *message = g_strdup_printf ("State changed to %s",
        gst_element_state_get_name (new_state));
    set_ui_message (message, data);
    g_free (message);
  }
}

/* Check if all conditions are met to report GStreamer as initialized.
 * These conditions will change depending on the application */
static void
check_initialization_complete (CustomData * data)
{
  JNIEnv *env = get_jni_env ();
  if (!data->initialized && data->native_window && data->main_loop) {
    GST_DEBUG
        ("Initialization complete, notifying application. native_window:%p main_loop:%p",
         (void *) data->native_window, (void *) data->main_loop);

    /* The main loop is running and we received a native window, inform the sink about it */
    gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (data->video_sink),
        (guintptr) data->native_window);

    (*env)->CallStaticVoidMethod (env, data->app, on_gstreamer_initialized_method_id);
    if ((*env)->ExceptionCheck (env)) {
      GST_ERROR ("Failed to call Java method");
      (*env)->ExceptionClear (env);
    }
    data->initialized = TRUE;
  }
}

/* Main method for the native code. This is executed on its own thread. */
static void *
app_function (void *userdata)
{
  GstBus *bus;
  CustomData *data = (CustomData *) userdata;
  GSource *bus_source;

  GST_DEBUG ("Creating pipeline in CustomData at %p", (void *) data);

  /* Create our own GLib Main Context and make it the default one */
  data->context = g_main_context_new ();
  g_main_context_push_thread_default (data->context);

    /* Create the empty pipeline */
    data->pipeline = gst_pipeline_new ("test-pipeline");

    /* Create the elements */

    //Plugin – app
    //Package – GStreamer Base Plug-ins
    data->appsrc = gst_element_factory_make ("appsrc", "1-appsrc");

    //Plugin – videoparsersbad
    //Package – GStreamer Bad Plug-ins
    data->parser = gst_element_factory_make ("h265parse", "2-parser");

    //Plugin – libav
    //Package – GStreamer FFMPEG Plug-ins
    data->decoder = gst_element_factory_make ("avdec_h265", "3-decoder");

    //Plugin – videoconvertscale
    //Package – GStreamer Base Plug-ins
    data->converter = gst_element_factory_make ("videoconvert", "4-converter");

    //Plugin – autodetect
    //Package – GStreamer Good Plug-ins
    data->sink = gst_element_factory_make ("autovideosink", "5-sink");

    if (!data->pipeline || !data->appsrc || !data->parser || !data->decoder || !data->converter || !data->sink) {
        gchar *message = g_strdup_printf ("Not all elements could be created.");
        set_ui_message (message, data);
        g_free (message);
        return NULL;
    }

    g_object_set (G_OBJECT (data->appsrc),
                  "do-timestamp", TRUE,
                  "is-live", TRUE,
                  "format", GST_FORMAT_TIME,
                  "max-buffers", 5,
                  NULL);

    /* Build the pipeline. */
    gst_bin_add_many (GST_BIN (data->pipeline), data->appsrc, data->parser, data->decoder, data->converter, data->sink, NULL);
    if (!gst_element_link_many (data->appsrc, data->parser, data->decoder, data->converter, data->sink, NULL)) {
        gst_object_unref (data->pipeline);

        gchar *message = g_strdup_printf ("Elements could not be linked.");
        set_ui_message (message, data);
        g_free (message);

        return NULL;
    }

  /* Set the pipeline to READY, so it can already accept a window handle, if we have one */
  gst_element_set_state (data->pipeline, GST_STATE_READY);

  data->video_sink =
      gst_bin_get_by_interface (GST_BIN (data->pipeline),
      GST_TYPE_VIDEO_OVERLAY);
  if (!data->video_sink) {
    GST_ERROR ("Could not retrieve video sink");
    return NULL;
  }

  /* Instruct the bus to emit signals for each received message, and connect to the interesting signals */
  bus = gst_element_get_bus (data->pipeline);
  bus_source = gst_bus_create_watch (bus);
  g_source_set_callback (bus_source, (GSourceFunc) gst_bus_async_signal_func,
      NULL, NULL);
  g_source_attach (bus_source, data->context);
  g_source_unref (bus_source);
  g_signal_connect (G_OBJECT (bus), "message::error", (GCallback) error_cb,
      data);
  g_signal_connect (G_OBJECT (bus), "message::state-changed",
      (GCallback) state_changed_cb, data);
  gst_object_unref (bus);

  /* Create a GLib Main Loop and set it to run */
  GST_DEBUG ("Entering main loop... (CustomData:%p)", (void *) data);
  data->main_loop = g_main_loop_new (data->context, FALSE);
  check_initialization_complete (data);
  g_main_loop_run (data->main_loop);
  GST_DEBUG ("Exited main loop");
  g_main_loop_unref (data->main_loop);
  data->main_loop = NULL;

  /* Free resources */
  g_main_context_pop_thread_default (data->context);
  g_main_context_unref (data->context);
  gst_element_set_state (data->pipeline, GST_STATE_NULL);
  gst_object_unref (data->appsrc);
  gst_object_unref (data->parser);
  gst_object_unref (data->decoder);
  gst_object_unref (data->converter);
  gst_object_unref (data->sink);
  gst_object_unref (data->video_sink);
  gst_object_unref (data->pipeline);

  return NULL;
}

/*
 * Java Bindings
 */

/* Static class initializer: retrieve method and field IDs */

/* Instruct the native code to create its internal data structure, pipeline and thread */
static void
gst_native_init (JNIEnv * env, jobject thiz)
{
  CustomData *data = g_new0 (CustomData, 1);
  SET_CUSTOM_DATA (env, thiz, custom_data_field_id, data);
  GST_DEBUG_CATEGORY_INIT (debug_category, "h265gstreamer", 0,
      "Android Gstreamer");
  gst_debug_set_threshold_for_name ("h265gstreamer", GST_LEVEL_DEBUG);
  GST_DEBUG ("Created CustomData at %p", (void *) data);
  data->app = (*env)->NewGlobalRef (env, thiz);
  GST_DEBUG ("Created GlobalRef for app object at %p", data->app);
  pthread_create (&gst_app_thread, NULL, &app_function, data);
}

/* Quit the main loop, remove the native thread and free resources */
static void
gst_native_finalize (JNIEnv * env, jobject thiz)
{
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data)
    return;
  GST_DEBUG ("Quitting main loop...");
  g_main_loop_quit (data->main_loop);
  GST_DEBUG ("Waiting for thread to finish...");
  pthread_join (gst_app_thread, NULL);
  GST_DEBUG ("Deleting GlobalRef for app object at %p", data->app);
  (*env)->DeleteGlobalRef (env, data->app);
  GST_DEBUG ("Freeing CustomData at %p", (void *) data);
  g_free (data);
  SET_CUSTOM_DATA (env, thiz, custom_data_field_id, NULL);
  GST_DEBUG ("Done finalizing");
}

/* Set pipeline to PLAYING state */
static void
gst_native_play (JNIEnv * env, jobject thiz)
{
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data)
    return;
  GST_DEBUG ("Setting state to PLAYING");
  gst_element_set_state (data->pipeline, GST_STATE_PLAYING);
}

/* Set pipeline to PAUSED state */
static void
gst_native_pause (JNIEnv * env, jobject thiz)
{
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data)
    return;
  GST_DEBUG ("Setting state to PAUSED");
  gst_element_set_state (data->pipeline, GST_STATE_PAUSED);
}


static void
gst_native_surface_init (JNIEnv * env, jobject thiz, jobject surface)
{
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data)
    return;
  ANativeWindow *new_native_window = ANativeWindow_fromSurface (env, surface);
  GST_DEBUG ("Received surface %p (native window %p)", (void *) surface, (void *) new_native_window);

  if (data->native_window) {
    ANativeWindow_release (data->native_window);
    if (data->native_window == new_native_window) {
      GST_DEBUG ("New native window is the same as the previous one %p", (void *) data->native_window);
      if (data->video_sink) {
        gst_video_overlay_expose (GST_VIDEO_OVERLAY (data->video_sink));
        gst_video_overlay_expose (GST_VIDEO_OVERLAY (data->video_sink));
      }
      return;
    } else {
      GST_DEBUG ("Released previous native window %p", (void *)data->native_window);
      data->initialized = FALSE;
    }
  }
  data->native_window = new_native_window;

  check_initialization_complete (data);
}

static void
gst_native_surface_finalize (JNIEnv * env, jobject thiz)
{
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data)
    return;
  GST_DEBUG ("Releasing Native Window %p", (void *) data->native_window);

  if (data->video_sink) {
    gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (data->video_sink),
        (guintptr) NULL);
    gst_element_set_state (data->pipeline, GST_STATE_READY);
  }

  ANativeWindow_release (data->native_window);
  data->native_window = NULL;
  data->initialized = FALSE;
}

JNIEXPORT void JNICALL
Java_org_freedesktop_gstreamer_tutorials_tutorial_13_Tutorial3_pushFrameNative(
        JNIEnv *env,
        jobject thiz,
        jlong pts,
        jbyteArray buffer,
        jstring caps) {

    GST_DEBUG ("Enqueue frame data");

    CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
    if (!data)
        return;

//    GstStateChangeReturn ret = gst_element_get_state (
//            data->pipeline, NULL, NULL,
//            GST_CLOCK_TIME_NONE);
//
//    if(ret) {
//
//    }

    jbyte *buffer_ptr = (*env)->GetByteArrayElements(env, buffer, NULL);
    jsize buffer_size = (*env)->GetArrayLength(env, buffer);

    GstBuffer *gst_buffer = gst_buffer_new_wrapped(buffer_ptr, buffer_size);

    if (pts == -1) {
        GST_BUFFER_PTS(gst_buffer) = GST_CLOCK_TIME_NONE;
    } else {
        GST_BUFFER_PTS(gst_buffer) = pts;
    }

    //describes the types a pad can handle
    const char *native_caps = (*env)->GetStringUTFChars(env, caps, 0);
    GstCaps *gst_caps = NULL;
    if (strlen(native_caps) > 0) {
        gst_caps = gst_caps_from_string(native_caps);
    }

    //A composite of GSTBuffer and metadata
    GstSample *sample = gst_sample_new(gst_buffer, gst_caps, NULL, NULL);

    gst_sample_unref(sample);
    (*env)->ReleaseByteArrayElements(env, buffer, buffer_ptr, JNI_ABORT);
    (*env)->ReleaseStringUTFChars(env, caps, native_caps);

    while (data->max_buffers > 0 && data->num_buffers >= data->max_buffers) {
        g_print("Dropping oldest sample\n");
        gst_queue_array_pop_head(data->queue);
        data->num_buffers--;
    }

    gst_queue_array_push_tail(data->queue, sample);
    data->num_buffers++;

    GstAppSrc *appsrc = GST_APP_SRC(data->appsrc);
    if (!appsrc) {
        g_print("Unable to get app source");
        return;
    }

    gst_app_src_push_sample(appsrc, sample);
}

/* List of implemented native methods */
static JNINativeMethod native_methods[] = {
  {"nativeInit", "()V", (void *) gst_native_init},
  {"nativeFinalize", "()V", (void *) gst_native_finalize},
  {"nativePlay", "()V", (void *) gst_native_play},
  {"nativePause", "()V", (void *) gst_native_pause},
  {"nativeSurfaceInit", "(Ljava/lang/Object;)V",
      (void *) gst_native_surface_init},
  {"nativeSurfaceFinalize", "()V", (void *) gst_native_surface_finalize},
};

/* Library initializer */
jint
JNI_OnLoad (JavaVM * vm, void *reserved)
{
  JNIEnv *env = NULL;
    (void)reserved;

  java_vm = vm;
  // in the other example the plugins are setup gst_init_static_plugins, since that's no longer autogenerated we'll register them here
  GST_PLUGIN_STATIC_REGISTER(app);
  GST_PLUGIN_STATIC_REGISTER(coreelements);
  GST_PLUGIN_STATIC_REGISTER(videoparsersbad);
  GST_PLUGIN_STATIC_REGISTER(libav);
  GST_PLUGIN_STATIC_REGISTER(videoconvertscale);
  GST_PLUGIN_STATIC_REGISTER(autodetect);

  if ((*vm)->GetEnv (vm, (void **) &env, JNI_VERSION_1_4) != JNI_OK) {
    __android_log_print (ANDROID_LOG_ERROR, "h265gstreamer",
        "Could not retrieve JNIEnv");
    return 0;
  }
  jclass klass = (*env)->FindClass (env,
      "com/auterion/sambaza/JniBinding$Companion");
  (*env)->RegisterNatives (env, klass, native_methods,
      G_N_ELEMENTS (native_methods));

  jclass klass2 = (*env)->FindClass (env,"com/auterion/sambaza/JniBinding");

  custom_data_field_id =
            (*env)->GetStaticFieldID (env, klass2, "nativeCustomData", "J");

  set_message_method_id =
            (*env)->GetStaticMethodID (env, klass2, "setMessage", "(Ljava/lang/String;)V");

  on_gstreamer_initialized_method_id =
            (*env)->GetStaticMethodID (env, klass2, "onGStreamerInitialized", "()V");

    if (!custom_data_field_id || !set_message_method_id
        || !on_gstreamer_initialized_method_id) {
        /* We emit this message through the Android log instead of the GStreamer log because the later
         * has not been initialized yet.
         */
        __android_log_print (ANDROID_LOG_ERROR, "h265gstreamer",
                             "The calling class does not implement all necessary interface methods");
    }

  pthread_key_create (&current_jni_env, detach_current_thread);

  return JNI_VERSION_1_4;
}
