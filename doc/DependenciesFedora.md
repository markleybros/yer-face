Fedora Dependencies
===================


Introduction
------------

This document is intended to serve as a rough guide for setting up your Fedora (circa 31) system for building Yer-Face. Hopefully this will only be necessary until we figure out a good binary packaging strategy. :)


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

- You'll want to install RPM Fusion and get your Nvidia drivers working via their repository. They're generally more up-to-date than the CUDA ones.
- Now install the CUDA 10.2 repository for Fedora 29 (should work on Fedora 31): `sudo dnf config-manager --add-repo http://developer.download.nvidia.com/compute/cuda/repos/fedora29/x86_64/cuda-fedora29.repo`
    - Edit your `/etc/yum.repos.d/cuda-fedora29.repo` file and add this line at the end: `exclude=xorg-x11-drv-nvidia*,akmod-nvidia*,kmod-nvidia*,nvidia-driver*,nvidia-settings,nvidia-xconfig,nvidia-persistenced,cuda-nvidia-kmod-common,dkms-nvidia,nvidia-libXNVCtrl`
    - This will prevent the Cuda repository from trashing your Nvidia driver setup.
- Now install the Cuda metapackage: `sudo dnf install cuda`
  - This will install the Cuda tools as well as the Cuda libraries and toolkit as needed.
  - As mentioned above, make sure it doesn't install any nvidia kernel modules, as these will conflict with the RPM Fusion ones.


CUDA-Compatible GCC
-------------------

As mentioned at RPM Fusion's wiki under [_Known Issues, GCC Version_](https://rpmfusion.org/Howto/CUDA#GCC_version), the version of GCC which ships with Fedora is too new to be supported by Cuda, so we need to install GCC 8 in a non-destructive way.

My preferred method for dealing with this is Software Collections. Install the `devtoolset-8-toolchain` like so:

```
dnf install https://rpmfind.net/linux/centos/7/extras/x86_64/Packages/centos-release-scl-rh-2-3.el7.centos.noarch.rpm
dnf install http://dl.kwizart.net/compat-libgfortran5-8.3.1-1.fc29.noarch.rpm
dnf install devtoolset-8-toolchain
```

Once this is done, you can use `scl run devtoolset-8 bash` to enable a shell with the GCC 8 that you need.

**Important:** You _must_ use the `scl run` command to enable GCC 8 for any subsequent compilation instructions.


OpenCV
------

You will probably need to build OpenCV from scratch. :( **FIXME:** When OpenCV 4+ is available in the repositories, this will no longer be necessary.

```
# Clone the OpenCV and OpenCV Contrib Modules projects. Make sure you clone them into these exact directories!
git clone https://github.com/opencv/opencv.git opencv
git clone https://github.com/opencv/opencv_contrib.git opencv_contrib

# Change into the build directory.
cd opencv
mkdir build
cd build

# Configure the source tree. (See below for CMAKE NOTES.)
cmake -D WITH_CUDA=ON -D ENABLE_FAST_MATH=1 -D CUDA_FAST_MATH=1 -D WITH_CUBLAS=1 -D BUILD_LIST=core,calib3d,cudev,imgcodecs -D CMAKE_INSTALL_PREFIX=/usr/local -D OPENCV_EXTRA_MODULES_PATH=../../opencv_contrib/modules/ ..

# Compile with a sufficient number of threads.
cmake --build . --config Release -- -j 8

# Install on the system.
sudo cmake --build . --config Release --target install
```

**CMAKE NOTES:**

_NOTE:_ Make sure in the CMake output, under "OpenCV modules," the list includes:
- core
- calib3d
- imgcodecs


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
cmake -D USE_AVX_INSTRUCTIONS=ON ..

# Compile in release mode.
cmake --build . --config Release -- -j 8

# Install on the system.
sudo cmake --build . --config Release --target install
```

**CMAKE NOTES:** If you are running into a problem with cmake complaining about `cublas_device`, make sure you are running the latest version of cmake. See: https://github.com/davisking/dlib/issues/1490


FFmpeg
------

**NOTE** At this time (Fedora 29) the version of ffmpeg available via DNF is new enough:

```
dnf -y install ffmpeg ffmpeg-libs ffmpeg-devel
```

CMU Sphinx / Pocketsphinx
-------------------------

It doesn't look like the CMU Sphinx project is in any hurry to make a stable release containing the latest features (which we depend upon) so you'll need to pull the sources down from Github and build it from scratch.

```
# You'll need these packages. You may need other things too, I make no guarantees!
dnf -y install swig bison bison-devel

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

