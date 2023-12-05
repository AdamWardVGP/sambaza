# Sambaza - H265

This is a forked version of [Sambaza](https://github.com/Auterion/sambaza) project. A RTSP video streaming pipeline for H264 on Android.

This fork instead streams H265 frames and enables GStreamer logging for debugging purposes as well.

The fork has no official releases and instead uses a narrowed configuration of the previously supplied Github Actions to build a single [arm64 target aar](https://github.com/AdamWardVGP/sambaza/actions/runs/7066487852).

Usage of the library remains consistent with the [setup instructions](https://git.sr.ht/~jonasvautherin/sambaza) in the original project.

For a receiving client this was validated primarily via the GStreamer command line tools:
`gst-launch-1.0 rtspsrc location="rtsp://<net_adapter_ip>:<port>/stream1" latency=0 buffer-mode=3 ! rtph265depay ! avdec_h265 ! videoconvert ! autovideosink`