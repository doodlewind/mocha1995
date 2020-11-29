#! gmake

MOCHADEPTH = ..

SRCS	= mo_shell.c
OBJS	= $(SRCS:.c=.o)

ifndef BUILD_OPT
PROGRAM	= mocha
endif

LIBS	= $(LIBMOCHA) $(LIBNSPR) $(OS_LIBS)
LDFLAGS	= $(LIBS) -lm

include $(MOCHADEPTH)/config/rules.mk

ifeq ($(OS_CONFIG), SunOS5.3)
LDFLAGS += -lsocket -lnsl
endif

ifeq ($(OS_CONFIG), SunOS5.4)
LDFLAGS += -lsocket -lnsl
endif

mocha.pure: $(OBJS)
	purify $(CC) $(NOMD_OS_CFLAGS) $(OBJS) $(LDFLAGS) -o $@

ifndef BUILD_OPT
install:: $(PROGRAM)
	$(INSTALL) $(PROGRAM) $(DIST)/bin
endif
