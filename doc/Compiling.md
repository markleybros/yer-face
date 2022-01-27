Compiling
=========

Introduction
------------

This is a _very_ simple walkthrough for compiling YerFace on your system.

You will need to install a number of dependencies first. For instructions on that, see the appropriate Dependencies document for your system.

HOWTO
-----

Once you've cloned (or untarred) the repository, you will need to `cd` into it. From there, the following will probably get you what you need:

```
# Set up a build directory for your build assets.
mkdir build
cd build

# Configure the build system.
cmake ..

# Compile with an appropriate number of threads.
make -j 4
```

For testing and invokation examples, see [Examples.md](Examples.md)

**If you run into trouble,** please feel free to open a pull request or an issue and we'll be happy to help!
