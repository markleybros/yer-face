Dependencies
============


Introduction
------------

FIXME - These instructions are all from the perspective of Fedora (circa 26/27).


SDL2
----

```
dnf -y install SDL2 SDL2-devel
```


CUDA
----

Follow the directions at RPM Fusion:
- https://rpmfusion.org/Howto/NVIDIA
- https://rpmfusion.org/Howto/CUDA


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
cmake -D CUDA_NVCC_FLAGS=--expt-relaxed-constexpr -D CMAKE_BUILD_TYPE=Release -D CMAKE_INSTALL_PREFIX=/usr/local -D OPENCV_EXTRA_MODULES_PATH=../../opencv_contrib/modules/ ..

# Compile with a sufficient number of threads. (See below for COMPILE NOTES.)
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
- cudafilters.

**COMPILE NOTES:**

_NOTE:_ If you get errors like `identifier '__float128' is undefined` you need to apply this fix:
- https://git.archlinux.org/svntogit/community.git/commit/trunk?h=packages/cuda&id=ae90e4d243510e9565e66e9e8e08c509f5719fe0
- Basically just add this line "#define _BITS_FLOATN_H" to the top of this file: /usr/local/cuda/include/host_defines.h

_NOTE:_ If you get errors like `/usr/bin/ccache: invalid option -- 'E'` you need to install the correct version of GCC:
- https://rpmfusion.org/Howto/CUDA#GCC_version
- http://mirror.centos.org/centos/7/extras/x86_64/Packages/centos-release-scl-rh-2-2.el7.centos.noarch.rpm
- `scl enable devtoolset-6 bash`


Dlib
----

You will probably need to build Dlib from scratch, especially if you want Cuda to work.

```
dnf -y install openblas openblas-devel
```

You'll want to install cuDNN:
- It will require you to sign up for a free Nvidia Developer account.
- https://developer.nvidia.com/cudnn
- http://docs.nvidia.com/deeplearning/sdk/cudnn-install/index.html

Then download the latest version of Dlib from:
- http://dlib.net/

```
# Configure the source tree. (See below for CMAKE NOTES.)
cmake --config Release ..
```

**CMAKE NOTES:**

_NOTE:_ If you get errors like `CUDA was found but your compiler failed to compile a simple CUDA program` you need to make sure you're using the correct version of GCC:
- Check above in the OpenCV section for more information.
- `scl enable devtoolset-6 bash`


FFmpeg
------

You might be able to get away with using FFmpeg packaged for your distro:

```
dnf -y install ffmpeg ffmpeg-libs ffmpeg-devel
```

If you **really** need to build FFmpeg from scratch, make sure its dependencies are installed first:

```
dnf -y install nasm yasm yasm-devel frei0r-plugins-opencv frei0r-plugins frei0r-devel gnutls gnutls-devel 'ladspa*' libass libass-devel libbluray libbluray-utils libbluray-devel gsm-devel lame lame-libs lame-devel openjpeg2 openjpeg2-tools openjpeg2-devel opus opus-devel opus-tools pulseaudio-libs pulseaudio-libs-devel soxr soxr-devel speex speex-tools speex-devel libtheora libtheora-devel theora-tools libv4l libv4l-devel vo-amrwbenc vo-amrwbenc-devel libvorbis libvorbis-devel vorbis-tools libvpx libvpx-devel libvpx-utils x264 x264-libs x264-devel x265 x265-libs x265-devel xvidcore xvodcore-devel openal-soft openal-soft-devel opencl-utils opencl-utils-devel opencl-headers ocl-icd ocl-icd-devel libgcrypt libgcrypt-devel libcdio libcdio-devel libcdio-paranoia libcdio-paranoia-devel
```

Then you would do something like the following:

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
