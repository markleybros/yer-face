Ubuntu Dependencies
===================


Introduction
------------

This document is intended to serve as a rough guide for setting up your Ubuntu (circa 20.04 LTS) system for building Yer-Face. (You shouldn't need this! Try one of the binary downloads instead.)

CMake
-----

```
apt-get install cmake
```


SDL2
----

```
apt-get install libsdl2-dev
```


CUDA
----

**IMPORTANT:** It's best if you do **not** already have the proprietary Nvidia drivers installed, as CUDA will want to install them for itself.

That said:
- You'll most likely need CUDA 11.
- Follow the `deb (network)` installation instructions found here: https://developer.nvidia.com/cuda-downloads
- At least one post-installation step is required:
  - `echo 'export PATH="${PATH}":/usr/local/cuda/bin' | sudo tee /etc/profile.d/cuda.sh`
- You will probably need to reboot to get everything working.


OpenCV
------

As of Ubuntu 20.04, we no longer need to build OpenCV from source. Install from apt like so:

```
apt-get install libopencv-dev libopencv-calib3d-dev libopencv-imgcodecs-dev libopencv-core-dev
```


Dlib
----

You will probably need to build Dlib from scratch, especially if you want Cuda to work.

Dependencies for Dlib include cuDNN. Recently, cuDNN became available within the Nvidia Cuda apt repository, so you can install all of Dlib's dependencies via apt.

```
apt-get install libopenblas-base libopenblas-dev libcudnn8 libcudnn8-dev
```

Download the latest version of Dlib from:
- http://dlib.net/

```
# Configure the source tree.
cmake -D USE_AVX_INSTRUCTIONS=ON ..

# Compile in release mode.
cmake --build . --config Release -- -j 8

# Install on the system.
sudo cmake --build . --config Release --target install
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
./autogen.sh --without-python
make
sudo make install

# Build pocketsphinx
cd ../pocketsphinx
./autogen.sh --without-python
make
sudo make install
```
