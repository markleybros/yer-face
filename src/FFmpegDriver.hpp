#pragma once

#include "Logger.hpp"
#include "FrameDerivatives.hpp"

#include <string>
#include <list>

extern "C" {
#include <libavutil/imgutils.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

namespace YerFace {

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
	FFmpegDriver(FrameDerivatives *myFrameDerivatives, string myInputFilename);
	~FFmpegDriver();
	bool getIsFrameBufferEmpty(void);
	bool waitForNextVideoFrame(VideoFrame *videoFrame);
	VideoFrame getNextVideoFrame(void);
	void releaseVideoFrame(VideoFrame videoFrame);
private:
	void openCodecContext(int *streamIndex, AVCodecContext **decoderContext, AVFormatContext *myFormatContext, enum AVMediaType type);
	VideoFrameBacking *getNextAvailableVideoFrameBacking(void);
	VideoFrameBacking *allocateNewFrameBacking(void);
	bool decodePacket(const AVPacket *packet);
	void initializeDemuxerThread(void);
	void destroyDemuxerThread(void);
	static int runDemuxerLoop(void *ptr);

	FrameDerivatives *frameDerivatives;
	string inputFilename;

	Logger *logger;

	SDL_mutex *demuxerMutex;
	SDL_cond *demuxerCond;
	SDL_Thread *demuxerThread;
	bool demuxerRunning, demuxerStillReading;

	int width, height;
	enum AVPixelFormat pixelFormat, pixelFormatBacking;
	AVFormatContext *formatContext;
	AVCodecContext *videoDecoderContext;
	AVStream *videoStream;
	AVFrame *frame;
	int videoStreamIndex;
	struct SwsContext *swsContext;

	uint8_t *videoDestData[4];
	int videoDestLineSize[4];
	int videoDestBufSize;

	list<VideoFrame> readyVideoFrameBuffer;
	list<VideoFrameBacking *> allocatedFrameBackings;
};

}; //namespace YerFace