#
# Copyright 2017 Cumulus Networks, Inc. All rights reserved.
#
# Author: David Ahern, dsa@cumulusnetworks.com

CFLAGS = -Wall -I/usr/include/libnl3
LDFLAGS = -lnl-route-3 -lnl-3

vrf-dns-helper: vrf-dns-helper.o
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c -o $@ $<

clean:
	rm -f vrf-dns-helper vrf-dns-helper.o

distclean: clean
