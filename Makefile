CC_=gcc -std=c99
CC=$(CC_) -O2 -DNDEBUG
#CC=$(CC_) -g -Wall # DEBUG HERE

PKG_CONFIG=pkg-config

INPUT=guest_heartbeater.c
OUTPUT=guest_heartbeater

GLIB_CFLAGS=$(shell $(PKG_CONFIG) --cflags glib-2.0)
GLIB_LIBS=$(shell $(PKG_CONFIG) --libs glib-2.0)

GIO_CFLAGS=$(shell $(PKG_CONFIG) --cflags gio-2.0)
GIO_LIBS=$(shell $(PKG_CONFIG) --libs gio-2.0)

GOBJECT_CFLAGS=$(shell $(PKG_CONFIG) --cflags gobject-2.0)
GOBJCET_LIBS=$(shell $(PKG_CONFIG) --libs gobject-2.0)


INCLUDE=$(GLIB_CFLAGS) $(GIO_CFLAGS) $(GOBJECT_CFLAGS) -I./GuestSDK/include -L./GuestSDK/lib/lib64
LINK=$(GLIB_LIBS) $(GIO_LIBS) $(GOBJCET_LIBS) -lappmonitorlib -lvmtools

build: $(INPUT)
	$(CC) $(INPUT) $(INCLUDE) -o $(OUTPUT) $(LINK)

clean:
	rm -f *.o $(OUTPUT)

rebuild: clean build

# run with "LD_LIBRARY_PATH=GuestSDK/lib/lib64/ ./guest_heartbeater --help"
# check with "echo u | sudo tee /proc/sysrq-trigger"
