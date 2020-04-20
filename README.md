# Optix 7 Template
 
# About this Repository

This code is heavily based on Ingo Wald's tutorial code for the
SIGGRAPH 2019 Optix 7 Course
(https://gitlab.com/ingowald/optix7course)

This is supposed to be an easy repository to just clone and start straight to the important stuff
in Optix. Other than actual code, the only changes that you should probably do is to change minimum
things in both CMake files (in the root of the repository and in each project's folder).

The project also includes GLFW (quick integration with OpenGL for easy interactivity) and Ingo Wald's
GPU Development Toolkit (https://gitlab.com/ingowald/gdt). GDT contains some CMakes macros and scripts
that allow for super easy integration of CUDA compiling inside your project, so it is necessary.

The following compilation steps are taken straight out of the course's original repository.


# Building the Code

This code was intentionally written with minimal dependencies,
requiring only CMake (as a build system), your favorite
compiler (tested with Visual Studio 2017 and 2019 under Windows, and GCC under
Linux), and the OptiX 7 SDK (including CUDA 10.1 and NVIDIA driver recent
enough to support OptiX).

## Dependencies

- a compiler
    - On Windows, tested with Visual Studio 2017 and 2019 community editions
    - On Linux, tested with Ubuntu 18 and Ubuntu 19 default gcc installs
- CUDA 10.1
    - Download from developer.nvidia.com
    - on Linux, suggest to put `/usr/local/cuda/bin` into your `PATH`
- latest NVIDIA developer driver that comes with the SDK
    - download from http://developer.nvidia.com/optix and click "Get OptiX"
- OptiX 7 SDK
    - download from http://developer.nvidia.com/optix and click "Get OptiX"
    - on linux, suggest to set the environment variable `OptiX_INSTALL_DIR` to wherever you installed the SDK.  
    `export OptiX_INSTALL_DIR=<wherever you installed OptiX 7.0 SDK>`
    - on windows, the installer should automatically put it into the right directory

The only *external* library we use is GLFW for windowing, and
even this one we build on the fly under Windows, so installing
it is required only under Linux. 

Detailed steps below:

## Building under Linux

- Install required packages

    - on Debian/Ubuntu: `sudo apt install libglfw3-dev cmake-curses-gui`
    - on RedHat/CentOS/Fedora (tested CentOS 7.7): `sudo yum install cmake3 glfw-devel freeglut-devel`

- Clone the code
```
    git clone https://gitlab.com/ingowald/optix7course.git
    cd optix7course
```

- create (and enter) a build directory
```
    mkdir build
    cd build
```

- configure with cmake
    - Ubuntu: `cmake ..`
    - CentOS 7: `cmake3 ..`

- and build
```
    make
```

## Building under Windows

- Install Required Packages
	- see above: CUDA 10.1, OptiX 7 SDK, latest driver, and cmake
- download or clone the source repository
- Open `CMake GUI` from your start menu
	- point "source directory" to the downloaded source directory
	- point "build directory" to <source directory>/build (agree to create this directory when prompted)
	- click 'configure', then specify the generator as Visual Studio 2017 or 2019, and the Optional platform as x64. If CUDA, SDK, and compiler are all properly installed this should enable the 'generate' button. If not, make sure all dependencies are properly installed, "clear cache", and re-configure.
	- click 'generate' (this creates a Visual Studio project and solutions)
	- click 'open project' (this should open the project in Visual Studio)
