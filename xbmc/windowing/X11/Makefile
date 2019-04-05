<<<<<<< HEAD
SRCS  = WinSystemX11.cpp
SRCS += WinSystemX11GL.cpp
SRCS += WinSystemX11GLES.cpp
SRCS += XRandR.cpp
=======
SRCS=WinSystemX11.cpp \
     WinSystemX11GLContext.cpp \
     GLContextEGL.cpp \
     GLContextGLX.cpp \
     XRandR.cpp \
>>>>>>> 0ac305f7cf... X11: add EGL
     
LIB   = windowing_X11.a

include ../../../Makefile.include
-include $(patsubst %.cpp,%.P,$(patsubst %.c,%.P,$(SRCS)))
