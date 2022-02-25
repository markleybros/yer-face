#pragma once

#include "Logger.hpp"
#include "Utilities.hpp"
#include "FrameServer.hpp"
#include "WorkerPool.hpp"

#include <string>
#include <list>

extern "C" {
#include <libavutil/imgutils.h>
// #include <libavutil/timestamp.h>
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
#define YERFACE_MAX_PUMPTIME 67 //If a/v stream pumping is taking longer than 1/15th of a second, we may have a hardware problem.

#define YERFACE_AVLOG_LEVELMAP_MIN 0		//Less than this gets dropped.
#define YERFACE_AVLOG_LEVELMAP_ALERT 8		//Less than this (libav* defines 0-7 as PANIC) gets mapped to our LOG_SEVERITY_ALERT
#define YERFACE_AVLOG_LEVELMAP_CRIT 16		//Less than this (libav* defines 8-15 as FATAL) gets mapped to our LOG_SEVERITY_CRIT
#define YERFACE_AVLOG_LEVELMAP_ERR 24		//Less than this (libav* defines 16-23 as ERROR) gets mapped to our LOG_SEVERITY_ERR
#define YERFACE_AVLOG_LEVELMAP_WARNING 32	//Less than this (libav* defines 24-31 as WARNING) gets mapped to our LOG_SEVERITY_WARNING
#define YERFACE_AVLOG_LEVELMAP_INFO 40		//Less than this (libav* defines 32-39 as INFO) gets mapped to our LOG_SEVERITY_INFO
#define YERFACE_AVLOG_LEVELMAP_MAX 32		//Greater than this gets dropped.

class FrameServer;
class WorkerPool;
class FFmpegDriver;

enum FFmpegDriverInputAudioChannelMap {
	CHANNELMAP_NONE = 0,
	CHANNELMAP_LEFT_ONLY = 1,
	CHANNELMAP_RIGHT_ONLY = 2
};

class MediaInputOutputStreamPair {
public:
	AVStream **in, **out;
	int *outStreamIndex;
};

class MediaInputContext {
public:
	MediaInputContext(void);

	FFmpegDriverInputAudioChannelMap inputAudioChannelMap;
	
	AVFormatContext *formatContext;

	AVPacket *packet;
	AVFrame *frame;
	FrameNumber frameNumber;

	int videoStreamIndex;
	AVCodecContext *videoDecoderContext;
	AVStream *videoStream;
	int64_t videoStreamPTSOffset;
	int64_t videoMuxLastPTS;
	int64_t videoMuxLastDTS;

	int audioStreamIndex;
	AVCodecContext *audioDecoderContext;
	AVStream *audioStream;
	int64_t audioStreamPTSOffset;
	int64_t audioMuxLastPTS;
	int64_t audioMuxLastDTS;

	bool demuxerDraining;

	SDL_mutex *demuxerMutex;
	SDL_Thread *demuxerThread;
	bool demuxerThreadRunning;

	FFmpegDriver *driver;

	bool initialized;
};

class MediaOutputContext {
public:
	MediaOutputContext(void);

	AVOutputFormat *outputFormat;
	AVFormatContext *formatContext;

	AVStream *videoStream;
	int videoStreamIndex;
	AVStream *audioStream;
	int audioStreamIndex;

	SDL_mutex *multiplexerMutex;
	SDL_cond *multiplexerCond;
	SDL_Thread *multiplexerThread;
	bool multiplexerThreadRunning;

	std::list<AVPacket *> outputPackets;

	bool initialized;
};

class VideoFrameBacking {
public:
	AVFrame *frameBGR;
	uint8_t *buffer;
	bool inUse;
};

class VideoFrame {
public:
	bool valid;
	FrameTimestamps timestamp;
	VideoFrameBacking *frameBacking;
	cv::Mat frameCV;
};

class AudioFrameCallback {
public:
	int64_t channelLayout;
	enum AVSampleFormat sampleFormat;
	int sampleRate;
	void *userdata;
	std::function<void(void *userdata, uint8_t *buf, int audioSamples, int audioBytes, double timestamp)> audioFrameCallback;
	std::function<void(void *userdata)> isDrainedCallback;
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
	int channelMapping[2];
	SwrContext *swrContext;
	list<AudioFrameBacking> audioFrameBackings;
};

class AudioFrameHandler {
public:
	bool drained;
	AudioFrameResampler resampler;
	AudioFrameCallback audioFrameCallback;
};

class FFmpegDriver {
public:
	FFmpegDriver(Status *myStatus, FrameServer *myFrameServer, bool myLowLatency, bool myListAllAvailableOptions);
	~FFmpegDriver() noexcept(false);
	void openInputMedia(string inFile, enum AVMediaType type, string inFormat, string inSize, string inChannels, string inRate, string inCodec, string inputAudioChannelMap, bool tryAudio);
	void openOutputMedia(string outFile);
	void setVideoCaptureWorkerPool(WorkerPool *workerPool);
	void rollWorkerThreads(void);
	bool getIsAudioInputPresent(void);
	bool getIsVideoFrameBufferEmpty(void);
	VideoFrame getNextVideoFrame(void);
	bool pollForNextVideoFrame(VideoFrame *videoFrame);
	void releaseVideoFrame(VideoFrame videoFrame);
	void registerAudioFrameCallback(AudioFrameCallback audioFrameCallback);
	void stopAudioCallbacksNow(void);
private:
	void logAVErr(string msg, int err);
	void openCodecContext(int *streamIndex, AVCodecContext **decoderContext, AVFormatContext *myFormatContext, enum AVMediaType type);
	VideoFrameBacking *getNextAvailableVideoFrameBacking(void);
	VideoFrameBacking *allocateNewVideoFrameBacking(void);
	bool decodePacket(MediaInputContext *inputContext, int streamIndex, bool drain);
	void destroyDemuxerThread(MediaInputContext *inputContext);
	void destroyMuxerThread(void);
	static int runOuterDemuxerLoop(void *ptr);
	static int runOuterMuxerLoop(void *ptr);
	int innerDemuxerLoop(MediaInputContext *inputContext);
	int innerMuxerLoop(void);
	void pumpDemuxer(MediaInputContext *inputContext, enum AVMediaType type);
	bool flushAudioHandlers(bool draining);
	bool getIsAudioDraining(void);
	bool getIsVideoDraining(void);
	FrameTimestamps resolveFrameTimestamp(MediaInputContext *inputContext, enum AVMediaType type);
	void recursivelyListAllAVOptions(void *obj, string depth = "-");
	bool getIsAllocatedVideoFrameBackingsFull(void);
	int64_t applyPTSOffset(int64_t pts, int64_t offset);
	static void logAVCallback(void *ptr, int level, const char *fmt, va_list args);
	static void logAVWrapper(int level, const char *fmt, ...);

	Status *status;
	FrameServer *frameServer;
	bool lowLatency;
	WorkerPool *videoCaptureWorkerPool;

	Logger *logger;

	double firstFormatStartTime;
	bool firstFormatStartTimeIsSet;

	std::list<double> frameStartTimes;

	MediaInputContext videoInContext, audioInContext;
	MediaOutputContext outputContext;

	int width, height;
	enum AVPixelFormat pixelFormat, pixelFormatBacking;
	struct SwsContext *swsContext;

	SDL_mutex *videoStreamMutex;
	double videoStreamTimeBase;
	double newestVideoFrameTimestamp;
	double newestVideoFrameEstimatedEndTimestamp;

	SDL_mutex *audioStreamMutex;
	double audioStreamTimeBase;
	double newestAudioFrameTimestamp;
	double newestAudioFrameEstimatedEndTimestamp;

	uint8_t *videoDestData[4];
	int videoDestLineSize[4];
	int videoDestBufSize;

	SDL_mutex *videoFrameBufferMutex;
	std::list<VideoFrame> readyVideoFrameBuffer;
	std::list<VideoFrameBacking *> allocatedVideoFrameBackings;

	SDL_mutex *audioFrameHandlersMutex;
	std::vector<AudioFrameHandler *> audioFrameHandlers;
	bool audioFrameHandlersOkay;

	static Logger *avLogger;
	static SDL_mutex *avLoggerMutex;
};

}; //namespace YerFace
