
#pragma once

#include "opencv2/core/core.hpp"
#include "opencv2/core/cuda.hpp"
#include "opencv2/cudaimgproc.hpp"
#include "opencv2/cudaarithm.hpp"

void inRangeGPU(cv::InputArray _src, cv::Scalar &lowerb, cv::Scalar &upperb, cv::OutputArray _dst);
