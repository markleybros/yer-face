Examples
========


Introduction / Prerequisites
----------------------------

The purpose of this document is to outline some **examples** of how to invoke _YerFace_ for various purposes. For details on command line usage, please see [CommandLineUsage](CommandLineUsage.md).


### Environment Varibles

In the following examples you will need to have some environment variables set, like so:

```
export INVIDEO=/dev/videoX       # Input video device.
export VIDEOFORMAT=video4linux2  # Input video driver.
export VIDEOCODEC=mjpeg          # Desired camera codec.
export FPS=60                    # Desired frame rate.
export RESOLUTION=1920x1080      # Desired resolution.
export INAUDIO=default           # Input audio device.
export AUDIOFORMAT=pulse         # Input audio driver.
export OUTVIDEO=output.nut       # Output video file.
export OUTEVENTS=output          # Output events and percap data.
```

**Note:** These example settings are generally appropriate for hardware video and audio capture Linux. You will probably need to adjust for your situation.

**Note:** You will probably get deprecated pixel format warnings regarding `mjpeg`, but if you switch from `mjpeg` to `yuvj422p` you will probably get incorrect results. ¯\\_(ツ)_/¯


### Dealing with Less Computing Power

If you find your computer isn't powerful enough to run at a reasonable frame rate, you can adjust some of the parameters to reduce the input quality and speed things up:

```
export FPS=30
export RESOLUTION=1280x720
```


Invoking YerFace!
-----------------


### Running a Real Time Performance Capture Session

```
yer-face --lowLatency \
    --inVideo="${INVIDEO}" --inVideoFormat="${VIDEOFORMAT}" \
    --inVideoCodec="${VIDEOCODEC}" --inVideoRate="${FPS}" \
    --inVideoSize="${RESOLUTION}" \
    --inAudio="${INAUDIO}" --inAudioFormat="${AUDIOFORMAT}" \
    --outVideo="${OUTVIDEO}" \
    --outEventData="${OUTEVENTS}.lowLatency.dat"
```


### Re-Running a Previous Session At Higher Quality

Note in this case we're using the output video and output events from the previous example, but we are not using `--lowLatency`, so the results should be higher quality.

```
yer-face \
    --inVideo="${OUTVIDEO}" --inEventData="${OUTEVENTS}.lowLatency.dat" \
    --outEventData="${OUTEVENTS}.highQuality.dat"
```


Validating Your System / Troubleshooting
----------------------------------------


For the purpose of troubleshooting, as well as testing your system independently of `yer-face`, here are some examples which might help.


### Previewing Camera Video

To check the camera's connection and preview it, I use a command like this:

```
ffplay -framerate "${FPS}" \
    -f "${VIDEOFORMAT}" -pixel_format "${VIDEOCODEC}" \
    -video_size "${RESOLUTION}" -i "${INVIDEO}"
```

Note that, in my case, my camera supports up to `60 FPS` in `Full HD (1920x1080)` mode, when using `mjpeg` as the format.

Your mileage may vary, but keep an eye on the ffplay logs to see if it is actually respecting your preferences.


### Capturing Synchronized Audio and Video using FFmpeg

Capturing directly to a file looks something like this:

```
ffmpeg -framerate "${FPS}" -y \
    -f "${VIDEOFORMAT}" -pixel_format "${VIDEOCODEC}" \
    -video_size "${RESOLUTION}" -i "${INVIDEO}" \
    -f "${AUDIOFORMAT}" -i "${INAUDIO}" \
    -acodec copy -vcodec copy -f nut "${OUTVIDEO}"
```

This will produce [a NUT file](https://ffmpeg.org/nut.html) which you can play back via `ffplay` to confirm that your Audio and Video is working correctly and is synchronized.


### Examining Your Camera's Capabilities

**FIXME:** This is applicable to Linux only. Find a better way to do this.

In my case, I have a [Logitech Brio 4K](https://www.logitech.com/en-us/product/brio) webcam, which has a maximum resolution of 4K. You can get a detailed list of supported modes with this command:

```
v4l2-ctl -d "${INVIDEO}" --list-formats-ext
```


### Setting Camera Exposure

**FIXME:** This is applicable to Linux only. Find a better way to do this.

It may be desirable in some circumstances (bright window behind the subject) to manually adjust and pin the camera exposure.

To set and adjust manual exposure, you probably want a few commands like this:

```
v4l2-ctl -d "${INVIDEO}" -c exposure_auto=1
v4l2-ctl -d "${INVIDEO}" -c exposure_absolute=300
```

On the other hand, you may find it more useful to use a GUI to adjust the camera's settings. In which case you can use `gtk-v4l` since it has an easy and straightforward interface.


Deprecated Examples
-------------------


### Invoking YerFace On a Test Video (Deprecated)

**FIXME:** At the moment, we aren't shipping test videos, because they were too big (and they were out of date). Let us know if you miss having test videos and we'll bump this up on the priority.

Here's a quick way to check that YerFace is working on one of the test videos:

```
yer-face --inVideo=data/test-videos/QuickBrownFox.mkv
```

### Piping Live Video into YerFace (Deprecated)

**Note:** This method is deprecated in favor of the `--outVideo` method described above.

Let's put it all together into a live video stream:

```
ffmpeg -framerate "${FPS}" \
    -f "${VIDEOFORMAT}" -pixel_format "${VIDEOCODEC}" \
    -video_size "${RESOLUTION}" -i "${INVIDEO}" \
    -f "${AUDIOFORMAT}" -i "${INAUDIO}" \
    -acodec copy -vcodec copy -f nut pipe:1 | \
        tee "${OUTVIDEO}" | \
        yer-face --inVideo=- --lowLatency
```

**NOTE:** As you can see, this command is the same capture command, but instead of saving to a file, we are outputting to the STDOUT pipe. The `tee` command saves a copy of the video to a file. The `--inVideo=-` flag tells YerFace to receive video from STDIN. (For more input control options, see [CommandLineUsage](CommandLineUsage.md).)
