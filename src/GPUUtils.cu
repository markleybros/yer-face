
#include "GPUUtils.hpp"

// Huge props to wykvictor for this solution https://github.com/opencv/opencv/issues/6295#issuecomment-246647886

__global__ void inRangeCudaKernel(const cv::cuda::PtrStepSz<uchar3> src, cv::cuda::PtrStepSzb dst, int lbc0, int ubc0, int lbc1, int ubc1, int lbc2, int ubc2) {
  int x = blockIdx.x * blockDim.x + threadIdx.x;
  int y = blockIdx.y * blockDim.y + threadIdx.y;

  if (x >= src.cols || y >= src.rows) return;

  uchar3 v = src(y, x);
  if (v.x >= lbc0 && v.x <= ubc0 && v.y >= lbc1 && v.y <= ubc1 && v.z >= lbc2 && v.z <= ubc2)
    dst(y, x) = 255;
  else
    dst(y, x) = 0;
}

void inRangeGPU(cv::cuda::GpuMat &src, cv::Scalar &lowerb, cv::Scalar &upperb, cv::cuda::GpuMat &dst) {
	const int m = 32;
	int numRows = src.rows, numCols = src.cols;

	if (numRows == 0 || numCols == 0) return;
	
	// Attention! Cols Vs. Rows are reversed
	const dim3 gridSize(ceil((float)numCols / m), ceil((float)numRows / m), 1);
	const dim3 blockSize(m, m, 1);

	inRangeCudaKernel<<<gridSize, blockSize>>>(src, dst, lowerb[0], upperb[0], lowerb[1], upperb[1], lowerb[2], upperb[2]);
}
