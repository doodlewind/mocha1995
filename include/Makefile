#! gmake

MOCHADEPTH = ..

HEADERS	=		\
	  mo_atom.h	\
	  mo_bcode.h	\
	  mo_cntxt.h	\
	  mo_emit.h	\
	  mo_java.h	\
	  mo_parse.h	\
	  mo_prvtd.h    \
	  mo_pubtd.h    \
	  mo_scan.h	\
	  mo_scope.h	\
	  mocha.def	\
	  mocha.h	\
	  mochaapi.h	\
	  $(NULL)

include $(MOCHADEPTH)/config/rules.mk

export:: $(HEADERS)
	$(INSTALL) -m 444 $(HEADERS) $(DIST)/include/mocha

install:: export
