#//------------------------------------------------------------------------
#//
#// Common configuration options for the MOCHA tree
#//
#//------------------------------------------------------------------------

!ifdef BUILD_OPT
!else
MOCHAFILE=1
!endif

!ifdef MOCHAFILE
LCFLAGS=$(LCFLAGS) -DMOCHAFILE
!endif

LIBMOCHA=$(DIST)\lib\mocha$(OS_RELEASE).lib
LINCS=$(LINCS) -I$(DEPTH)\mocha\include -I$(DEPTH)\dist\public\security
LLIBS=$(LLIBS) $(LIBMOCHA)

