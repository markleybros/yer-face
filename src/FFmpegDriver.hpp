#pragma once

#include "Logger.hpp"
#include "FrameDerivatives.hpp"

#include <string>

extern "C" {
#include <libavutil/imgutils.h>
#include <libavformat/avformat.h>
}

namespace YerFace {

class FFmpegDriver {
public:
	FFmpegDriver(FrameDerivatives *myFrameDerivatives, string myInputFilename);
	~FFmpegDriver();
private:
	void openCodecContext(int *streamIndex, AVCodecContext **decoderContext, AVFormatContext *myFormatContext, enum AVMediaType type);

	FrameDerivatives *frameDerivatives;
	string inputFilename;

	Logger *logger;

	int width, height;
	enum AVPixelFormat pixelFormat;
	AVFormatContext *formatContext;
	AVCodecContext *videoDecoderContext;
	AVStream *videoStream;
	int videoStreamIndex;

	uint8_t *videoDestData[4];
	int videoDestLineSize[4];
	int videoDestBufSize;
};

}; //namespace YerFace