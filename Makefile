#
# Copyright 2017 Cumulus Networks, Inc. All rights reserved.
#
# Author: David Ahern, dsa@cumulusnetworks.com

MAKEFLAGS += --no-print-directory

SUBDIRS=lib-vrf src

all:
	@set -e; \
	for i in $(SUBDIRS); do \
	    echo; echo $$i; $(MAKE) -C $$i; \
	done

clean:
	@set -e; \
	for i in $(SUBDIRS); do \
	    echo; echo $$i; $(MAKE) -C $$i clean; \
	done

distclean: clean
