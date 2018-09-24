
#include "GPUUtils.hpp"

using namespace cv;
using namespace cv::cuda;

// Huge props to wykvictor for this solution https://github.com/opencv/opencv/issues/6295#issuecomment-246647886

__global__ void inRangeCudaKernel(const cv::cuda::PtrStepSz<uchar3> src, cv::cuda::PtrStepSzb dst, int lbc0, int ubc0, int lbc1, int ubc1, int lbc2, int ubc2) {
	int x = blockIdx.x * blockDim.x + threadIdx.x;
	int y = blockIdx.y * blockDim.y + threadIdx.y;

	if(x >= src.cols || y >= src.rows) return;

	uchar3 v = src(y, x);
	if(v.x >= lbc0 && v.x <= ubc0 && v.y >= lbc1 && v.y <= ubc1 && v.z >= lbc2 && v.z <= ubc2)
		dst(y, x) = 255;
	else
		dst(y, x) = 0;
}

void inRangeGPU(cv::InputArray _src, cv::Scalar &lowerb, cv::Scalar &upperb, cv::OutputArray _dst) {
	const int m = 32;

	GpuMat src = _src.getGpuMat();
	const int depth = _src.depth();
	int numRows = src.rows, numCols = src.cols;

	CV_Assert( depth == CV_8U );
	CV_Assert( src.channels() == 3 );
	CV_Assert( numRows > 0 );
	CV_Assert( numCols > 0 );

	_dst.create(_src.size(), CV_8UC1);
	GpuMat dst = _dst.getGpuMat();

	// Attention! Cols Vs. Rows are reversed
	const dim3 gridSize(ceil((float)numCols / m), ceil((float)numRows / m), 1);
	const dim3 blockSize(m, m, 1);

	inRangeCudaKernel<<<gridSize, blockSize>>>(src, dst, lowerb[0], upperb[0], lowerb[1], upperb[1], lowerb[2], upperb[2]);
}
