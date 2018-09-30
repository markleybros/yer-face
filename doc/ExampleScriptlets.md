Example Scriptlets
==================

Introduction
------------

The purpose of this document is to outline some examples of how to invoke _YerFace_ for various purposes.

In the following examples, you will need to have some environment variables set, like so:

```
export INPUT_VIDEO=/dev/videoX
export OUTPUT_VIDEO=/tmp/output.nut
export PIXEL_FORMAT=mjpeg
export FPS=60
export RESOLUTION=1920x1080
```

Invoking YerFace On a Test Video
--------------------------------

Here's a quick way to check that YerFace is working on one of the test videos:

```
build/bin/yer-face --captureFile=data/test-videos/QuickBrownFox.mkv
```

This should run without error.

Examining Your Camera's Capabilities
------------------------------------

In my case, I have a [Logitech Brio 4K](https://www.logitech.com/en-us/product/brio) webcam, which has a maximum resolution of 4K. You can get a detailed list of supported modes with this command:

```
v4l2-ctl -d "${INPUT_VIDEO}" --list-formats-ext
```

Setting Camera Exposure
-----------------------

When trying to capture markers on the screen, it is pretty awful when the camera is automatically adjusting the exposure out from under you.

To set and adjust manual exposure, you probably want a few commands like this:

```
v4l2-ctl -d "${INPUT_VIDEO}" -c exposure_auto=1
v4l2-ctl -d "${INPUT_VIDEO}" -c exposure_absolute=300
```

On the other hand, you may find it more useful to use a GUI to adjust the camera's settings. In which case you can use `gtk-v4l` since it has an easy and straightforward interface.

Previewing Camera Input
-----------------------

To check the camera's connection and preview it, I use a command like this:

```
ffplay -framerate "${FPS}" -f video4linux2 -pixel_format "${PIXEL_FORMAT}" \
    -video_size "${RESOLUTION}" -i "${INPUT_VIDEO}"
```

Note that, in my case, my camera supports up to `60 FPS` in `Full HD (1920x1080)` mode, when using `mjpeg` as the format.

Your mileage may vary, but keep an eye on the ffplay logs to see if it is actually respecting your preferences.

**NOTE:** You will get a deprecated pixel format warning, but if you switch from `mjpeg` to `yuvj422p` it will produce incorrect results.

Capturing Audio and Video
-------------------------

Capturing directly to a file looks something like this:

```
ffmpeg -framerate "${FPS}" -y -f video4linux2 -pixel_format "${PIXEL_FORMAT}" \
    -video_size "${RESOLUTION}" -i "${INPUT_VIDEO}" -f pulse -i default \
    -acodec copy -vcodec copy -f nut "${OUTPUT_VIDEO}"
```

This will produce [a NUT file](https://ffmpeg.org/nut.html) which you can play back via `ffplay` to confirm that your Audio and Video is working correctly and is synchronized.

**NOTE:** This command uses the default input device of your PulseAudio system as the audio input. You may need to use your system preferences panel to select the best audio input for your situation.

Sending Live Video into YerFace
-------------------------------

Let's put it all together into a live video stream:

```
ffmpeg -framerate "${FPS}" -f video4linux2 -pixel_format "${PIXEL_FORMAT}" \
    -video_size "${RESOLUTION}" -i "${INPUT_VIDEO}" -f pulse -i default \
    -acodec copy -vcodec copy -f nut pipe:1 | \
        tee "${OUTPUT_VIDEO}" | \
        build/bin/yer-face --captureFile=- --lowLatency
```

**NOTE:** As you can see, this command is the same capture command, but instead of saving to a file, we are outputting to the STDOUT pipe. The `tee` command saves a copy of the video to a file. The `--captureFile=-` flag tells YerFace to receive video from STDIN.

Dealing with Less Computing Power
---------------------------------

If you find your computer isn't powerful enough to run at a reasonable frame rate, you can adjust some of the parameters to reduce the input quality and speed things up:

```
export FPS=30
export RESOLUTION=1280x720
```
