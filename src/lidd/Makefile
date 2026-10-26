# xx-lidd — make [CROSS_COMPILE=aarch64-linux-gnu-] [DESTDIR=...]
CC      = $(CROSS_COMPILE)gcc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=gnu11 -Wall -Wextra -Wformat=2 -Wno-format-nonliteral
LDFLAGS ?=
PREFIX  ?= /usr

OBJS = main.o policy.o platform_linux.o

all: xx-lidd

xx-lidd: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c lidd.h platform_linux.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Los tests se compilan siempre con el compilador del host.
test: tests/test_policy
	./tests/test_policy

tests/test_policy: tests/test_policy.c policy.c lidd.h
	gcc $(CFLAGS) -I. -o $@ tests/test_policy.c policy.c

install: xx-lidd
	install -D -m 0755 xx-lidd $(DESTDIR)$(PREFIX)/bin/xx-lidd
	install -D -m 0644 lidd.conf.example $(DESTDIR)/etc/xx-lidd.conf.example
	for d in pre-sleep post-wake deep-transition; do \
	  install -d $(DESTDIR)/etc/xx-lidd/hooks/$$d.d; \
	  for f in hooks/$$d.d/*; do [ -e "$$f" ] && install -m 0755 "$$f" $(DESTDIR)/etc/xx-lidd/hooks/$$d.d/; done; \
	done

clean:
	rm -f $(OBJS) xx-lidd tests/test_policy

.PHONY: all test install clean
