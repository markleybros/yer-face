#pragma once

#include "Logger.hpp"
#include "FrameDerivatives.hpp"

#include <string>

extern "C" {
#include <libavutil/imgutils.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

namespace YerFace {

class FFmpegDriver {
public:
	FFmpegDriver(FrameDerivatives *myFrameDerivatives, string myInputFilename);
	~FFmpegDriver();
private:
	void openCodecContext(int *streamIndex, AVCodecContext **decoderContext, AVFormatContext *myFormatContext, enum AVMediaType type);
	bool getIsFrameBufferEmpty(void);
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
	enum AVPixelFormat pixelFormat;
	AVFormatContext *formatContext;
	AVCodecContext *videoDecoderContext;
	AVStream *videoStream;
	AVFrame *frame, *frameBGR;
	int videoStreamIndex;
	struct SwsContext *swsContext;

	uint8_t *videoDestData[4];
	int videoDestLineSize[4];
	int videoDestBufSize;
};

}; //namespace YerFace