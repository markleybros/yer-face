#pragma once

#include "Logger.hpp"
#include "FrameDerivatives.hpp"

#include <string>
#include <list>

extern "C" {
#include <libavutil/imgutils.h>
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libswscale/swscale.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

namespace YerFace {

#define YERFACE_FRAME_DURATION_ESTIMATE_BUFFER 10
#define YERFACE_INITIAL_VIDEO_BACKING_FRAMES 60

class FrameDerivatives;

class MediaContext {
public:
	MediaContext(void);
	
	AVFormatContext *formatContext;

	AVFrame *frame;

	int videoStreamIndex;
	AVCodecContext *videoDecoderContext;
	AVStream *videoStream;

	int audioStreamIndex;
	AVCodecContext *audioDecoderContext;
	AVStream *audioStream;

	SDL_mutex *demuxerMutex;
	SDL_cond *demuxerCond;
	SDL_Thread *demuxerThread;
	bool demuxerRunning;

	bool demuxerDraining;
};

class VideoFrameBacking {
public:
	AVFrame *frameBGR;
	uint8_t *buffer;
	bool inUse;
};

class VideoFrame {
public:
	double timestamp, estimatedEndTimestamp;
	VideoFrameBacking *frameBacking;
	Mat frameCV;
};

class AudioFrameCallback {
public:
	int64_t channelLayout;
	enum AVSampleFormat sampleFormat;
	int sampleRate;
	void *userdata;
	std::function<void(void *userdata, uint8_t *buf, int audioSamples, int audioBytes, double timestamp)> callback;
};

class AudioFrameBacking {
public:
	double timestamp;
	uint8_t **bufferArray;
	int bufferSamples;
	int audioSamples, audioBytes;
};

class AudioFrameResampler {
public:
	int numChannels;
	SwrContext *swrContext;
	list<AudioFrameBacking> audioFrameBackings;
};

class AudioFrameHandler {
public:
	AudioFrameResampler resampler;
	AudioFrameCallback audioFrameCallback;
};

class FFmpegDriver {
public:
	FFmpegDriver(FrameDerivatives *myFrameDerivatives, bool myFrameDrop, bool myLowLatency, bool myListAllAvailableOptions);
	~FFmpegDriver();
	void openInputMedia(string inFile, enum AVMediaType type, String inFormat, String inSize, String inChannels, String inRate, String inCodec, bool tryAudio);
	void rollDemuxerThreads(void);
	bool getIsAudioInputPresent(void);
	bool getIsVideoFrameBufferEmpty(void);
	VideoFrame getNextVideoFrame(void);
	bool waitForNextVideoFrame(VideoFrame *videoFrame);
	void releaseVideoFrame(VideoFrame videoFrame);
	void registerAudioFrameCallback(AudioFrameCallback audioFrameCallback);
	void stopAudioCallbacksNow(void);
private:
	void logAVErr(String msg, int err);
	void openCodecContext(int *streamIndex, AVCodecContext **decoderContext, AVFormatContext *myFormatContext, enum AVMediaType type);
	VideoFrameBacking *getNextAvailableVideoFrameBacking(void);
	VideoFrameBacking *allocateNewVideoFrameBacking(void);
	bool decodePacket(MediaContext *context, const AVPacket *packet, int streamIndex);
	void initializeDemuxerThread(MediaContext *context, enum AVMediaType type);
	void rollDemuxerThread(MediaContext *context, enum AVMediaType type);
	void destroyDemuxerThreads(void);
	void destroyDemuxerThread(MediaContext *context, enum AVMediaType type);
	static int runVideoDemuxerLoop(void *ptr);
	static int runAudioDemuxerLoop(void *ptr);
	int innerDemuxerLoop(MediaContext *context, enum AVMediaType type, bool includeAudio);
	void pumpDemuxer(MediaContext *context, AVPacket *packet, enum AVMediaType type);
	double calculateEstimatedEndTimestamp(double startTimestamp);
	bool flushAudioHandlers(bool draining);
	bool getIsAudioDraining(void);
	bool getIsVideoDraining(void);
	double resolveFrameTimestamp(MediaContext *context, AVFrame *frame, enum AVMediaType type);
	void resolveStreamStartTime(MediaContext *context, enum AVMediaType type);
	void recursivelyListAllAVOptions(void *obj, string depth = "-");

	FrameDerivatives *frameDerivatives;
	bool frameDrop, lowLatency;

	Logger *logger;

	std::list<double> frameStartTimes;

	MediaContext videoContext, audioContext;

	int width, height;
	enum AVPixelFormat pixelFormat, pixelFormatBacking;
	struct SwsContext *swsContext;

	SDL_mutex *videoStreamMutex;
	double videoStreamTimeBase;
	double videoStreamRealStartTime;
	double videoStreamSyncDelta;
	double videoStreamInitialTimestamp;
	bool videoStreamRealStartTimeSet, videoStreamInitialTimestampSet;
	double newestVideoFrameTimestamp;
	double newestVideoFrameEstimatedEndTimestamp;

	SDL_mutex *audioStreamMutex;
	double audioStreamTimeBase;
	double audioStreamRealStartTime;
	double audioStreamSyncDelta;
	double audioStreamInitialTimestamp;
	bool audioStreamRealStartTimeSet, audioStreamInitialTimestampSet;
	double newestAudioFrameTimestamp;

	uint8_t *videoDestData[4];
	int videoDestLineSize[4];
	int videoDestBufSize;

	SDL_mutex *videoFrameBufferMutex;
	list<VideoFrame> readyVideoFrameBuffer;
	list<VideoFrameBacking *> allocatedVideoFrameBackings;
	bool readyVideoFrameBufferEmptyWarning;

	std::vector<AudioFrameHandler *> audioFrameHandlers;
	bool audioCallbacksOkay;
};

}; //namespace YerFace