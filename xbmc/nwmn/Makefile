SRCS = GUIDialogMN.cpp
SRCS += GUIDialogMNDemand.cpp
SRCS += UtilitiesRed.cpp
SRCS += PlayerManagerMN.cpp
SRCS += PythonBindingsMN.cpp
SRCS += UpdateManagerMN.cpp

LIB   = nwmn.a

include ../../Makefile.include
-include $(patsubst %.cpp,%.P,$(patsubst %.c,%.P,$(SRCS)))
