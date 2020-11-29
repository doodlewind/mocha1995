#! gmake

ifndef DEPTH
include $(MOCHADEPTH)/config/config.mk
endif

include $(DEPTH)/config/rules.mk
