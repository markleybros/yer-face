Command Line Usage
==================


Introduction
------------

At any time, you can view a complete command line usage message like so:
```
yer-face --help
```
Following is a listing of command line flags with explanations.

**NOTE:** This document is intended to be a reference. For examples, please see [Examples](Examples.md).

**NOTE:** Unlike many command line interfaces, `yer-face` expects **all** parameters to be passed like `--key=value` or `-k=value` ... Erroneously using something like `--key value` or `-k value` will result in unexpected behavior and errors.


Behavior Flags
--------------

Flags in this section will affect the run time behavior of `yer-face`, sometimes in significant ways.

### Usage Message
_Always confirm usage by referring to `yer-face --help`. The auto-generated usage message is always accurate for your build and version._
```
	-?, -h, --help, --usage (value:true)
		Display command line usage documentation.
```

### Version
_Normally it would be inadvisable to screen-scrape the textual output of `yer-face`, however this flag is explicitly intended to be used in scripts. You will receive the version string (and nothing else) and a return success code._
```
	--version
		Emit the version string to STDOUT and exit.
```

### Configuration File
_Many core settings which affect the run time behavior of the performance capture engine are **only** adjustable via the configuration file. This file is so important that the engine cannot start if it is not present._
```
	--configFile
		Required configuration file. (Indicate the full or relative path to your 'yer-face-config.json' file. Omit to search common locations.)
```

### Headless Operation
_Often in our experience, it is necessary to operate `yer-face` via a remote console where no video display or audio playback is available. In this case, use headless mode to prevent `yer-face` from attempting to use any video display or audio playback._
```
	--headless
		If set, all video display and audio playback is disabled. Intended to be suitable for jobs running in the terminal.
```

### Real Time (Low Latency) Operation
_By default, `yer-face` is optimized for quality, but in exchange performance capture data may lag many seconds behind the actual events being analyzed. If you want the smallest possible lag, use this mode._

Important notes:
- Among other things, this mode enables frame dropping in the performance capture pipeline to keep up with the frames coming from the camera.
- Frame dropping in this manner does **not** affect the stream going to `--outVideo`, which will still contain everything we received from `--inVideo`.

```
	--lowLatency
		If true, will tweak behavior across the system to minimize latency. (Don't use this if the input is pre-recorded!)
```


Logging
-------

### Log Verbosity

_The verbosity of `yer-face` can be adjusted by changing the log severity filter._
```
	-v, --verbose, --verbosity
		Adjust the log level filter. Indicate a positive number to increase the verbosity, a negative number to decrease the verbosity, or specify with no integer to increase the verbosity to a moderate degree.)
```

The available log severities are loosely derived from RFC 5424:
- EMERGENCY -- `system is unusable`
- ALERT -- `action must be taken immediately`
- CRITICAL -- `critical conditions`
- ERROR -- `error conditions`
- WARNING -- `warning conditions`
- NOTICE -- `normal but significant conditions`
- INFO -- `informational messages` **This is the default verbosity.**
- DEBUG1 -- `first-level (lowest density) debug messages`
- DEBUG2 -- `second-level (medium density) debug messages`
- DEBUG3 -- `third-level (high level) debug messages`
- DEBUG4 -- `fourth-level (trace) debug messages`

The verbosity flag allows you to pass an integer adjustment to the verbosity. Some important notes:
- Positive integers move the filter down the list toward `DEBUG4`.
- Negative integers move the filter up the list toward `EMERGENCY`.
- If you indicate the `--verbosity` flag without passing an integer, the log filter will be set to `DEBUG2`.
- Setting the verbosity offset to zero (`-v=0`) has no effect.
- If you want `yer-face` to be completely silent, set `--outLogFile` to `/dev/null`.

### Log Colorization
_For readability, logs can be colorized in the terminal. The default behavior is to auto-detect the output device and only colorize the logs if the output is a TTY._
```
	--outLogColors
		If true, log colorization will be forced on. If false, log colorization will be forced off. If "auto" or not specified, log colorization will auto-detect.
```

### Log File Output
_By default, logs will be sent to STDERR. However, using `--outLogFile` logs can be sent to a file or named pipe instead. If you want `yer-face`'s logging framework to be completely silent, you must set `--outLogFile` to a NULL device, such as `/dev/null`. **NOTE:** Under exceptional circumstances, such as a failure to initialize the logging framework, `yer-face` will still emit error messages to STDERR._
```
	--outLogFile
		If specified, log messages will be written to this file. If "-" or not specified, log messages will be written to STDERR.
```


Input Video Flags
-----------------

### Input Video File/Device
_Use this parameter to specify the source of the input video stream. Audio can also be read from this stream if it is multiplexed in a supported container format._

Important notes:
- Default behavior is to attempt reading audio from this input source as well. If you want to alter this behavior, see the `--inAudio` flag.
- Input can be retrieved from a file, a named pipe, STDIN, a URL, or a hardware device.
- As long as `libav` knows how to demultiplex and decode the input, we can theoretically use it. (When in doubt, refer to the ffmpeg documentation.)
- If opening a hardware device this will probably be a string referring to the device (such as `--inVideo='video="Integrated Camera"'` as in the case of DirectShow on Windows) and you will almost certainly need to use the other `--inVideo*` flags to get the behavior you expect.
- In general, one of the two following situations will apply:
  - For real time performance capture situations, you will use `--lowLatency` mode and capture video and audio from hardware devices using `--inVideo` and (optionally) `--inAudio`. This video and audio will be saved to a file using `--outVideo`.
  - For subsequent, higher quality processing of recorded performance capture sessions, you will want to replay the same video and audio (saved via `--outVideo`) into `yer-face` by using the `--inVideo` flag.
  - For more details on this workflow, see the discussion under _Event Data Flags_.

```
	--inVideo
		Video file, URL, or device to open. (Or '-' for STDIN.)
```

### Input Video Codec
_Use this parameter to indicate the codec of the input video._

Important notes:
- Note that this flag cannot _change_ the codec of the input video. It is only used as a hint when opening the video.
- When opening a hardware video capture device or webcam supporting multiple video codecs / formats, this parameter can be required to select the desired codec.
- A full excursus on video codecs is out of scope for this document, but here are some salient points:
  -  **It is essential** for correct operation of `yer-face` that you choose a codec which is high quality.
  -  In particular, compression artifacts from low bitrate or poorly-encoded inter-frame codecs (anything containing P-frames or B-frames like `h264` or `h265`) will contribute to bad results.
  -  If latency is a consideration, you must avoid any type of codec which uses GOP-blocks, because (in my experience) hardware encoders for such codecs tend to introduce an unacceptable amount of lag.
  -  If in doubt, just use `mjpeg`.

```
	--inVideoCodec
		Tell libav to attempt a specific codec when interpreting inVideo. Leave blank for auto-detection.
```

### Input Video Format
_Use this parameter to indicate the format of the input video._

Important notes:
- Note that this flag cannot _change_ the format of the input video. It is only used as a hint when opening the video.
- Generally when opening a file, this `format` will refer to the container type, such as `matroska`, `ogg`, or `mov`.
- Generally when opening a video capture device, this `format` will refer to the driver, such as `video4linux2` on Linux or `dshow` on Windows.
- When opening a hardware video capture device or webcam, this parameter can be required in order to interpret the meaning of the `--inVideo` parameter.

```
	--inVideoFormat
		Tell libav to use a specific format to interpret the inVideo. Leave blank for auto-detection.
```

### Input Video Rate (FPS)
_Use this parameter to indicate the frame rate of the input video._

Important notes:
- Note that this flag cannot _change_ the frame rate of the input video. It is only used as a hint when opening the video.
- When opening a hardware video capture device or webcam supporting multiple frame rates, this parameter can be required to select the desired frame rate.
- Generally, due to the nature of `yer-face`'s smoothing algorithms, the higher the input frame rate is, the better your performance capture quality will be. (Within reason. All the usual caveats about bitrate and light sensitivity apply.)

```
	--inVideoRate
		Tell libav to attempt a specific frame rate when interpreting inVideo. Leave blank for auto-detection.
```

### Input Video Size (Resolution)
_Use this parameter to indicate the resolution (width x height) of the input video._

Important notes:
- Note that this flag cannot _change_ the resolution of the input video. It is only used as a hint when opening the video.
- When opening a hardware video capture device or webcam supporting multiple frame resolutions, this parameter can be required to select the desired resolution.
- Generally, due to the nature of `yer-face`'s markerless performance capture algorithms, the higher the input resolution is, the better your performance capture quality will be.
- **However,** for well-framed video where the subject's face is filling the center of the frame, the effect of resolution on the quality of the output plateaus very quickly.
- _When possible, consider trading a higher FPS for a lower resolution. Your mileage may vary._

```
	--inVideoSize
		Tell libav to attempt a specific resolution when interpreting inVideo. Leave blank for auto-detection.
```


Input Audio Flags
-----------------

### Input Audio File/Device
_By default, we will try to read audio from the same source as the video, specified by `--inVideo`. If you prefer to read from a separate audio source, use this parameter to specify the source of the input audio stream._

Important notes:
- Processing audio is **optional** for `yer-face`, but it has many positive effects.
- Notes about the meaning and interpretation of `--inVideo` also apply to this flag. Particularly if you're trying to read from an audio capture device, you should read those notes.
- While `--inVideo` can read both video _and_ audio, this flag precludes that capability and forces `yer-face` to ignore any audio stream present in `--inVideo`.
- While `--inVideo` can read both video _and_ audio, this flag always ignores any video streams it finds.

```
	--inAudio
		Audio file, URL, or device to open. Alternatively: '' (blank string, the default) we will try to read the audio from inVideo. '-' we will try to read the audio from STDIN. 'ignore' we will ignore all audio from all sources.
```

### Input Audio Channel Mapping
_Often, depending on your input audio interface, a single microphone will occupy only the left or right channel of an audio stream which presents itself as stereo. Use this flag to handle this situation._

Important notes:
- This flag is applicable regardless of whether the audio stream is coming from `--inAudio` or `--inVideo`.
- One indication that you need to use this flag would be if your in-app volume meter shows a maximum of 50% even if your input audio interface is peaking. Your in-app volume meter should show a peak (red flash) if your audio interface shows one.

```
	--inAudioChannelMap
		Alter the input audio channel mapping. Set to "left" to interpret only the left channel, "right" to interpret only the right channel, and leave blank for the default.
```

### Number of Input Audio Channels
_Use this parameter to indicate the number of channels in the input audio._

Important notes:
- This flag is applicable regardless of whether the audio stream is coming from `--inAudio` or `--inVideo`.
- Note that this flag cannot _change_ the number of channels in the input audio. It is only used as a hint when opening the audio.
- When opening a hardware audio interface, this parameter may be required to select the desired capture mode.

```
	--inAudioChannels
		Tell libav to attempt a specific number of channels when interpreting inAudio. Leave blank for auto-detection.
```

### Input Audio Codec
_Use this parameter to indicate the codec of the input audio._

Important notes:
- This flag is applicable regardless of whether the audio stream is coming from `--inAudio` or `--inVideo`.
- Note that this flag cannot _change_ the codec of the input audio. It is only used as a hint when opening the audio.
- When opening a hardware audio interface, this parameter can be required to select the desired codec.

```
	--inAudioCodec
		Tell libav to attempt a specific codec when interpreting inAudio. Leave blank for auto-detection.
```

### Input Audio Format
_Use this parameter to indicate the format of the input audio._

Important notes:
- Note that this flag cannot _change_ the format of the input audio. It is only used as a hint when opening the audio.
- Notes about the meaning and interpretation of `--inVideoFormat` also apply to this flag. Particularly if you're trying to read from an audio capture device, you should read those notes.

```
	--inAudioFormat
		Tell libav to use a specific format to interpret the inAudio. Leave blank for auto-detection.
```

### Input Audio Rate (Hz)
_Use this parameter to indicate the sample rate of the input audio._

Important notes:
- Note that this flag cannot _change_ the sample rate of the input audio. It is only used as a hint when opening the audio.
- When opening a hardware audio interface supporting multiple sample rates, this parameter can be required to select the desired sample rate.
- **Important:** `yer-face`'s Sphinx-based speech recognition requires the audio sample rate to be 16000 Hz. _`yer-face` will automatically re-sample the audio_ to match this requirement, however if the source audio has a sample rate lower than 16000 Hz, _you will experience a significant reduction in quality._

```
	--inAudioRate
		Tell libav to attempt a specific sample rate when interpreting inAudio. Leave blank for auto-detection.
```


Output Video and Audio Flags
----------------------------

### Output Video and Audio
_Use this parameter to enable saving of multiplexed video and audio to a file._

Important notes:
- This would be the lowest latency option for recording video and audio during a performance capture session, because the A/V is piped directly to the performance capture pipeline in an independent thread, while being saved off to a file concurrently.
- When using this mode, input video and audio codecs are always copied **directly** to the output without re-encoding.
- The container format of the output data is inferred from the file extension.
- Aside from saving performance audio for later studio use, this flag enables a studio workflow which is described in detail under _Event Data Flags_ as well as `--inVideo`.

```
	--outVideo
		Output file for captured video and audio. Together with the "outEventData" file, this can be used to re-run a previous capture session.
```


Event Data Flags
----------------

### Output Event Data
_Use this parameter to specify the output file destination for recording performance capture and event data._

Important notes:
- This data stream contains the raw performance capture data produced by the engine, as well as event data (such as basis flags and, eventually, character control hints).
- The data stream contained in this file is identical to the data stream provided by the WebSockets interface. However, while the WebSockets interface is suitable for real time applications, this file output is not.
- This file is suitable for being directly imported into Blender as keyframe animation by using the `yerface_blender` plugin.
- Another primary purpose of this is to support the following workflow:
  - Record a performance capture session in real time with `--lowLatency` so the performer and other crew can receive real time feedback on the quality of the performance.
  - Re-run the same performance capture session at some later time **without** `--lowLatency` to take advantage of improved quality processing. (See `--inEventData`.)
  - Import the final event data file into Blender for animation purposes.
  - For more details on this workflow, see also `--inVideo` and `--outVideo`.

```
	--outEventData
		Output event data / replay file. (Includes performance capture data.)
```

### Input Event Data
_Use this parameter to provide event data to the engine for replaying previous sessions._

Important notes:
- This flag is exclusively used for replaying previous performance capture sessions.
- The input file for this flag should have been generated by a previous invocation using the `--outEventData` flag.
- Without this flag, events such as basis flags or character control hints would not be preserved.
- See a detailed explanation of the workflow at `--outEventData`.

```
	--inEventData
		Input event data / replay file. (Previously generated outEventData, for re-processing recorded sessions.)
```


Preview Settings
----------------

**Note:** These settings are intended mostly for development and troubleshooting purposes, and are of limited use in real production scenarios.

### Enabling Preview of Audio
_Use this flag to play the captured audio from `--inVideo` or `--inAudio` out via your computer's sound card._

Important notes:
- The output audio device and output audio quality are selected automatically by SDL2. This should be your operating system's default audio device.
- The purpose of this audio playback is for troubleshooting and is not for high fidelity playback.
- The replayed audio will appear to "stutter" in accordance with the availability of audio packets and the difference in time scales between the processing speed and the audio stream.

```
	--previewAudio
		If true, will preview processed audio out the computer's sound device.
```

### Generating a Preview Image Sequence
_Use this flag to save the preview display to an image sequence._

```
	--previewImgSeq
		If set, is presumed to be the file name prefix of the output preview image sequence.
```
