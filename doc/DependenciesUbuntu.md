Ubuntu Dependencies
===================


Introduction
------------

This document is intended to serve as a rough guide for setting up your Ubuntu (circa 18.04 LTS) system for building Yer-Face. Hopefully this will only be necessary until we figure out a good binary packaging strategy. :)


SDL2
----

```
apt-get install libsdl2-dev
```


CUDA
----

**IMPORTANT:** It's best if you do **not** already have the proprietary Nvidia drivers installed, as CUDA will want to install them for itself.

That said:
- Fetch the `deb (network)` file from here: https://developer.nvidia.com/cuda-downloads
- It'll be a URL like: https://developer.download.nvidia.com/compute/cuda/repos/ubuntu1804/x86_64/cuda-repo-ubuntu1804_10.0.130-1_amd64.deb
- There will be installation instructions on the page like:
  - `sudo dpkg -i cuda-repo-ubuntu1804_10.0.130-1_amd64.deb`
  - `sudo apt-key adv --fetch-keys https://developer.download.nvidia.com/compute/cuda/repos/ubuntu1804/x86_64/7fa2af80.pub`
  - `sudo apt-get update`
  - `sudo apt-get install cuda`
- At least one post-installation step is required:
  - `echo 'export PATH="${PATH}":/usr/local/cuda/bin' | sudo tee /etc/profile.d/cuda.sh`
- You will probably need to reboot to get everything working.


OpenCV
------

You will probably need to build OpenCV from scratch. :(

```
# Clone the OpenCV and OpenCV Contrib Modules projects. Make sure you clone them into these exact directories!
git clone https://github.com/opencv/opencv.git opencv
git clone https://github.com/opencv/opencv_contrib.git opencv_contrib

# Change into the build directory.
cd opencv
mkdir build
cd build

# Configure the source tree. (See below for CMAKE NOTES.)
cmake -D WITH_CUDA=ON -D ENABLE_FAST_MATH=1 -D CUDA_FAST_MATH=1 -D WITH_CUBLAS=1 -D BUILD_opencv_cudacodec=OFF -D CMAKE_BUILD_TYPE=Release -D CMAKE_INSTALL_PREFIX=/usr/local -D OPENCV_EXTRA_MODULES_PATH=../../opencv_contrib/modules/ ..

# Compile with a sufficient number of threads.
make -j 8

# Install on the system.
sudo make install
```

**CMAKE NOTES:**

_NOTE:_ Make sure in the CMake output, under "OpenCV modules," the list includes:
- core
- tracking
- calib3d
- cudaimgproc
- cudafilters


Dlib
----

You will probably need to build Dlib from scratch, especially if you want Cuda to work.

```
sudo apt-get install libopenblas-base libopenblas-dev
```

You'll want to install cuDNN:
- It will require you to sign up for a free Nvidia Developer account.
- https://developer.nvidia.com/cudnn
- http://docs.nvidia.com/deeplearning/sdk/cudnn-install/index.html
- Download the appropriate debian packages, then install them with something like:
  - `sudo dpkg -i libcudnn7_7.3.0.29-1+cuda10.0_amd64.deb libcudnn7-dev_7.3.0.29-1+cuda10.0_amd64.deb libcudnn7-doc_7.3.0.29-1+cuda10.0_amd64.deb`

Then download the latest version of Dlib from:
- http://dlib.net/

```
# Configure the source tree.
cmake --config Release ..

# Compile with a sufficient number of threads.
make -j 8

# Install on the system.
sudo make install
```

**CMAKE NOTES:** If you are running into a problem with cmake complaining about `cublas_device`, make sure you are running the latest version of cmake. See: https://github.com/davisking/dlib/issues/1490

FFmpeg
------

```
apt-get install ffmpeg libavcodec-dev libavdevice-dev libavformat-dev libavutil-dev libswresample-dev libswscale-dev
```

That should be enough to get yer-face to compile.


CMU Sphinx / Pocketsphinx
-------------------------

You'll probably want to compile this from source.

```
# You'll need these packages. You may need other packages too, I make no guarantees!
apt-get install swig autoconf libtool automake bison python3-dev

# Do the clone thing.
git clone https://github.com/cmusphinx/sphinxbase.git
git clone https://github.com/cmusphinx/pocketsphinx.git

# Build sphinxbase
cd sphinxbase
./autogen.sh
make
sudo make install

# Build pocketsphinx
cd ../pocketsphinx
./autogen.sh
make
sudo make install
```

