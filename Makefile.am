if COND_CHECK
  MAYBE_CHECK = tests
endif
SUBDIRS = libltdl src map $(MAYBE_CHECK)

EXTRA_DIST = \
	doxygen.cfg

ACLOCAL_AMFLAGS = -I m4

include $(top_srcdir)/doxygen.am
MOSTLYCLEANFILES = $(DX_CLEANFILES)
doc: doxygen-doc
