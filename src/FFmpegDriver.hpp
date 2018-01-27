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
}

namespace YerFace {

#define YERFACE_INITIAL_BACKING_FRAMES 4

class VideoFrameBacking {
public:
	AVFrame *frameBGR;
	uint8_t *buffer;
	bool inUse;
};

class VideoFrame {
public:
	VideoFrameBacking *frameBacking;
	Mat frameCV;
};

class FFmpegDriver {
public:
	FFmpegDriver(FrameDerivatives *myFrameDerivatives, string myInputFilename, bool myFrameDrop = false);
	~FFmpegDriver();
	bool getIsFrameBufferEmpty(void);
	bool waitForNextVideoFrame(VideoFrame *videoFrame);
	VideoFrame getNextVideoFrame(void);
	void releaseVideoFrame(VideoFrame videoFrame);
private:
	void logAVErr(String msg, int err);
	void openCodecContext(int *streamIndex, AVCodecContext **decoderContext, AVFormatContext *myFormatContext, enum AVMediaType type);
	VideoFrameBacking *getNextAvailableVideoFrameBacking(void);
	VideoFrameBacking *allocateNewFrameBacking(void);
	bool decodePacket(const AVPacket *packet, int streamIndex);
	void initializeDemuxerThread(void);
	void destroyDemuxerThread(void);
	static int runDemuxerLoop(void *ptr);

	FrameDerivatives *frameDerivatives;
	string inputFilename;
	bool frameDrop;

	Logger *logger;

	SDL_mutex *demuxerMutex;
	SDL_cond *demuxerCond;
	SDL_Thread *demuxerThread;
	bool demuxerRunning, demuxerDraining;

	int videoStreamIndex;
	AVCodecContext *videoDecoderContext;
	AVStream *videoStream;
	double videoStreamTimeBase;
	int width, height;
	enum AVPixelFormat pixelFormat, pixelFormatBacking;
	AVFormatContext *formatContext;
	AVFrame *frame;
	struct SwsContext *swsContext;

	int audioStreamIndex;
	AVCodecContext *audioDecoderContext;
	AVStream *audioStream;
	double audioStreamTimeBase;

	uint8_t *videoDestData[4];
	int videoDestLineSize[4];
	int videoDestBufSize;

	SDL_mutex *videoFrameBufferMutex;
	list<VideoFrame> readyVideoFrameBuffer;
	list<VideoFrameBacking *> allocatedFrameBackings;
};

}; //namespace YerFace