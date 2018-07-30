#
# Copyright 2018 Cumulus Networks, Inc. All rights reserved.
#
# Author: David Ahern, dsa@cumulusnetworks.com

CFLAGS += -Wall -Werror

runvrf: runvrf.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c -o $@ $<

clean:
	rm -f runvrf runvrf.o

distclean: clean
