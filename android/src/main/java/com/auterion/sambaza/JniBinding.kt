package com.auterion.sambaza

import android.util.Log

class JniBinding {
  companion object {

    /**
     * Houses all JNI methods to interface with a GStreamer based pipeline connected with an Android
     * SurfaceView.
     */
    init {
      System.loadLibrary("sambaza")
    }

    /** Native code will use this to keep internal state */
    private val nativeCustomData: Long = 0

    /** Initialize native code. Application is responsible to call this early and provide
     * file path to build pipeline */
    public external fun gstNativeInit(filepath: String)

    /** Destroy pipeline and shutdown native code */
    public external fun nativeFinalize()

    /** Set pipeline to PLAYING */
    external fun nativePlay()

    /** Set pipeline to PAUSED */
    external fun nativePause()

    /** Initialize native class: cache Method IDs for callbacks */
    external fun nativeSurfaceInit(surface: Any)

    external fun pushFrameNative(pts: Long, buffer: ByteArray, caps: String)

    external fun nativeSurfaceFinalize()

    // Called from native code. This sets the content of the TextView from the UI thread.
    @JvmStatic
    private fun setMessage(message: String) {
      Log.v("JNIBinding", "got native msg: $message")
    }

    // Called from native code. Native code calls this once it has created its pipeline and
    // the main loop is running, so it is ready to accept commands.
    @JvmStatic
    private fun onGStreamerInitialized() {
      Log.i("JNIBinding", "Gst initialized.")
    }
  }
}
