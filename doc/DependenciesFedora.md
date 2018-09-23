Fedora Dependencies
===================


Introduction
------------

This document is intended to serve as a rough guide for setting up your Fedora (circa 28) system for building Yer-Face. Hopefully this will only be necessary until we figure out a good binary packaging strategy. :)


SDL2
----

```
dnf -y install SDL2 SDL2-devel
```


CUDA
----

Follow the directions at RPM Fusion:
- https://rpmfusion.org/Howto/CUDA

In brief:

- Do **not** install the Nvidia Driver from RPM Fusion's nonfree repository. You will need RPM Fusion, but just don't install the nvidia driver.
- Then install the Cuda repository for fedora 27 (yes, 27): `sudo dnf install https://developer.download.nvidia.com/compute/cuda/repos/fedora27/x86_64/cuda-repo-fedora27-10.0.130-1.x86_64.rpm`
- Then install the Software Collections repo: `sudo dnf install http://mirror.centos.org/centos/7/extras/x86_64/Packages/centos-release-scl-rh-2-2.el7.centos.noarch.rpm`
- Now install the Cuda metapackage: `sudo dnf install cuda`
  - This will install the Cuda tools as well as the Cuda libraries **and** the Nvidia driver to make everything work.
  - You will want to **avoid** installing any Nvidia driver updates from RPM Fusion accidentally, as this seems to break things.
- Now install a compatible (GCC 7) toolchain: `sudo dnf install devtoolset-7-binutils devtoolset-7-gcc devtoolset-7-gcc-c++ devtoolset-7-libstdc++-devel devtoolset-7-runtime` (Not all of the toolchain will install, but we don't need everything.)


**IMPORTANT:** From this point forward, every single step must be run inside of `scl enable devtoolset-7 bash` otherwise CUDA will not work and you will get weird linking errors. This even applies to running Blender!


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
sudo dnf -y install openblas openblas-devel
```

You'll want to install cuDNN:
- It will require you to sign up for a free Nvidia Developer account.
- https://developer.nvidia.com/cudnn
- http://docs.nvidia.com/deeplearning/sdk/cudnn-install/index.html

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

**NOTE** At this time (Fedora 28) the version of ffmpeg available via DNF is new enough:

```
dnf -y install ffmpeg ffmpeg-libs ffmpeg-devel
```

It is **no longer required** to use a custom build of ffmpeg. If you **really** need to build FFmpeg from scratch, the following instructions apply:

```
dnf -y install nasm yasm yasm-devel frei0r-plugins-opencv frei0r-plugins frei0r-devel gnutls gnutls-devel 'ladspa*' libass libass-devel libbluray libbluray-utils libbluray-devel gsm-devel lame lame-libs lame-devel openjpeg2 openjpeg2-tools openjpeg2-devel opus opus-devel opus-tools pulseaudio-libs pulseaudio-libs-devel soxr soxr-devel speex speex-tools speex-devel libtheora libtheora-devel theora-tools libv4l libv4l-devel vo-amrwbenc vo-amrwbenc-devel libvorbis libvorbis-devel vorbis-tools libvpx libvpx-devel libvpx-utils x264 x264-libs x264-devel x265 x265-libs x265-devel xvidcore xvodcore-devel openal-soft openal-soft-devel opencl-utils opencl-utils-devel opencl-headers ocl-icd ocl-icd-devel libgcrypt libgcrypt-devel libcdio libcdio-devel libcdio-paranoia libcdio-paranoia-devel
```

Then something like...

```
# Check out the desired FFmpeg release branch. (3.4 or the like.)
git clone --branch 'release/3.4' https://git.ffmpeg.org/ffmpeg.git ffmpeg

# Change into the FFmpeg directory
cd ffmpeg

# Configure the source tree
./configure \
    --prefix=/usr/local \
    --arch=x86_64 \
    --optflags='-O2 -g -pipe -Wall -Werror=format-security -Wp,-D_FORTIFY_SOURCE=2 -fexceptions -fstack-protector-strong --param=ssp-buffer-size=4 -grecord-gcc-switches -specs=/usr/lib/rpm/redhat/redhat-hardened-cc1 -m64 -mtune=generic' \
    --extra-ldflags='-Wl,-z,relro -specs=/usr/lib/rpm/redhat/redhat-hardened-ld ' \
    --extra-cflags='-I/usr/include/nvenc ' \
    --enable-libvo-amrwbenc \
    --enable-version3 \
    --enable-bzlib \
    --disable-crystalhd \
    --enable-fontconfig \
    --enable-frei0r \
    --enable-gcrypt \
    --enable-gnutls \
    --enable-ladspa \
    --enable-libass \
    --enable-libbluray \
    --enable-libcdio \
    --enable-indev=jack \
    --enable-libfreetype \
    --enable-libfribidi \
    --enable-libgsm \
    --enable-libmp3lame \
    --enable-nvenc \
    --enable-openal \
    --enable-opencl \
    --enable-opengl \
    --enable-libopenjpeg \
    --enable-libopus \
    --enable-libpulse \
    --enable-libsoxr \
    --enable-libspeex \
    --enable-libtheora \
    --enable-libvorbis \
    --enable-libv4l2 \
    --enable-libvpx \
    --enable-libx264 \
    --enable-libx265 \
    --enable-libxvid \
    --enable-avfilter \
    --enable-avresample \
    --enable-postproc \
    --enable-pthreads \
    --disable-static \
    --enable-shared \
    --enable-gpl \
    --disable-debug \
    --disable-stripping \
    --enable-runtime-cpudetect

# Compile with a sufficient number of threads.
make -j 8

# Install on the system.
sudo make install
```


CMU Sphinx / Pocketsphinx
-------------------------

As of June 2018, Pocketsphinx within the Fedora repositories is far too old for our purposes. We need to compile from source. :-\

```
# You'll need swig. You may need other things too, I make no guarantees!
dnf -y install swig

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

