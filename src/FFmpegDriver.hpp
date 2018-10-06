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

#define YERFACE_INITIAL_VIDEO_BACKING_FRAMES 60
#define YERFACE_INITIAL_AUDIO_BACKING_FRAMES 10
#define YERFACE_RESAMPLE_BUFFER_HEADROOM 8192

class MediaContext {
public:
	MediaContext(void);
	
	AVFormatContext *formatContext;

	int videoStreamIndex;
	AVCodecContext *videoDecoderContext;
	AVStream *videoStream;

	int audioStreamIndex;
	AVCodecContext *audioDecoderContext;
	AVStream *audioStream;

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
	double timestamp;
	VideoFrameBacking *frameBacking;
	Mat frameCV;
};

class AudioFrameCallback {
public:
	int64_t channelLayout;
	enum AVSampleFormat sampleFormat;
	int sampleRate;
	void *userdata;
	std::function<void(void *userdata, uint8_t *buf, int audioSamples, int audioBytes, int bufferSize, double timestamp)> callback;
};

class AudioFrameResampler {
public:
	int numChannels;
	SwrContext *swrContext;
	uint8_t **bufferArray;
	int bufferSamples, bufferLineSize;
};

class AudioFrameHandler {
public:
	AudioFrameResampler resampler;
	AudioFrameCallback audioFrameCallback;
};

class FFmpegDriver {
public:
	FFmpegDriver(FrameDerivatives *myFrameDerivatives, bool myFrameDrop, bool myLowLatency);
	~FFmpegDriver();
	void openInputMedia(string inFile, enum AVMediaType type, String inFormat, String inSize, String inRate, String inCodec, bool tryAudio);
	void rollDemuxerThread(void);
	bool getIsAudioInputPresent(void);
	bool getIsVideoFrameBufferEmpty(void);
	VideoFrame getNextVideoFrame(void);
	bool waitForNextVideoFrame(VideoFrame *videoFrame);
	void releaseVideoFrame(VideoFrame videoFrame);
	void registerAudioFrameCallback(AudioFrameCallback audioFrameCallback);
private:
	void logAVErr(String msg, int err);
	void openCodecContext(int *streamIndex, AVCodecContext **decoderContext, AVFormatContext *myFormatContext, enum AVMediaType type);
	VideoFrameBacking *getNextAvailableVideoFrameBacking(void);
	VideoFrameBacking *allocateNewVideoFrameBacking(void);
	bool decodePacket(MediaContext *context, const AVPacket *packet, int streamIndex);
	void initializeDemuxerThread(void);
	void destroyDemuxerThread(void);
	static int runDemuxerLoop(void *ptr);
	void pumpDemuxer(MediaContext *context, AVPacket *packet);

	FrameDerivatives *frameDerivatives;
	bool frameDrop, lowLatency;

	Logger *logger;

	SDL_mutex *demuxerMutex;
	SDL_cond *demuxerCond;
	SDL_Thread *demuxerThread;
	bool demuxerRunning;

	MediaContext videoContext, audioContext;

	double videoStreamTimeBase;
	int width, height;
	enum AVPixelFormat pixelFormat, pixelFormatBacking;
	AVFrame *frame;
	struct SwsContext *swsContext;

	double audioStreamTimeBase;

	uint8_t *videoDestData[4];
	int videoDestLineSize[4];
	int videoDestBufSize;

	SDL_mutex *videoFrameBufferMutex;
	list<VideoFrame> readyVideoFrameBuffer;
	list<VideoFrameBacking *> allocatedVideoFrameBackings;
	bool readyVideoFrameBufferEmptyWarning;

	std::vector<AudioFrameHandler *> audioFrameHandlers;
};

}; //namespace YerFace