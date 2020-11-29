#! gmake

DEPTH = $(MOCHADEPTH)/..

include $(DEPTH)/config/config.mk

ifndef BUILD_OPT
MOCHAFILE = 1
endif

ifdef MOCHAFILE
DEFINES += -DMOCHAFILE
endif

INCLUDES += -I$(MOCHADEPTH)/include -I$(DIST)/include/nspr

LIBMOCHA = $(DIST)/lib/mocha.a
