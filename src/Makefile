#! gmake

MOCHADEPTH = ..

include $(MOCHADEPTH)/config/config.mk

SRCS    =               \
	  mo_array.c    \
	  mo_atom.c     \
	  mo_bcode.c    \
	  mo_bool.c     \
	  mo_cntxt.c    \
	  mo_date.c     \
	  mo_emit.c     \
	  mo_fun.c      \
	  mo_java.c      \
	  mo_math.c     \
	  mo_num.c      \
	  mo_obj.c      \
	  mo_parse.c    \
	  mo_scan.c     \
	  mo_scope.c    \
	  mo_str.c      \
	  mocha.c       \
	  mochaapi.c    \
	  mochalib.c    \
	  $(NULL)

ifeq ($(OS_ARCH), SunOS)
MUST_BOOTLEG_ALLOCA = 1
endif

ifeq ($(OS_ARCH), IRIX)
MUST_BOOTLEG_ALLOCA = 1
endif

ifeq ($(OS_ARCH), AIX)
MUST_BOOTLEG_ALLOCA = 1
endif

ifeq ($(OS_ARCH), HP-UX)
MUST_BOOTLEG_ALLOCA = 1
endif

ifeq ($(OS_ARCH), WIN32)
MUST_BOOTLEG_ALLOCA = 1
endif

ifdef MUST_BOOTLEG_ALLOCA
SRCS	+= alloca.c
endif

OBJS    = $(SRCS:.c=.o)

LIBRARY = mocha.a

include $(MOCHADEPTH)/config/rules.mk

export:: $(LIBRARY)
	$(INSTALL) -m 444 $(LIBRARY) $(DIST)/lib

install:: export

# hardwire dependencies on mocha.def
../include/mo_bcode.h mo_bcode.c: ../include/mocha.def

MOJAVA_CFLAGS = \
	-I$(DEPTH)/sun-java/md-include \
	-I$(DEPTH)/sun-java/include \
	-I$(DEPTH)/sun-java/include/_gen \
	-I$(DEPTH)/sun-java/netscape/javascript/_gen \
	-I$(DEPTH)/sun-java/netscape/applet/_gen \
	-I$(DEPTH)/lib/libjava

# add some more includes for mo_java.c
$(OBJDIR)/mo_java.o: mo_java.c
	@$(MAKE_OBJDIR)
	$(CC) -o $@ -c $(CFLAGS) $(MOJAVA_CFLAGS) mo_java.c
