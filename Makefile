TARGET=iotop

SRCS:=$(wildcard src/*.c)
OBJS:=$(patsubst %c,%o,$(patsubst src/%,bld/%,$(SRCS)))
DEPS:=$(OBJS:.o=.d)

ifndef NO_FLTO
CFLAGS?=-O3 -fno-stack-protector -mno-stackrealign -flto
LDFLAGS+=-O3 -fno-stack-protector -mno-stackrealign -flto
else
CFLAGS?=-O3 -fno-stack-protector -mno-stackrealign
endif

MYCFLAGS:=$(CPPFLAGS) $(CFLAGS) -std=gnu90 -Wall -Wextra
MYLDFLAGS=$(LDFLAGS) -lncurses
STRIP?=strip

PREFIX?=$(DESTDIR)/usr

# use this to disable flto optimizations:
#   make NO_FLTO=1
# and this to enable verbose mode:
#   make V=1

ifeq ("$(V)","1")
Q:=
E:=@true
else
Q:=@
E:=@echo
endif

all: $(TARGET)

$(TARGET): $(OBJS)
	$(E) LD $@
	$(Q)$(CC) -o $@ $^ $(MYLDFLAGS)

bld/%.o: src/%.c bld/.mkdir
	$(E) DE $@
	$(Q)$(CC) $(MYCFLAGS) -MM -MT $@ -MF $(patsubst %.o,%.d,$@) $<
	$(E) CC $@
	$(Q)$(CC) $(MYCFLAGS) -c -o $@ $<

clean:
	$(E) CLEAN
	$(Q)rm -rf ./bld $(TARGET)

install: $(TARGET)
	$(E) STRIP $(TARGET)
	$(Q)$(STRIP) $(TARGET)
	$(E) INSTALL $(TARGET)
	$(Q)install -TD -m 0755 $(TARGET) $(PREFIX)/sbin/$(TARGET)

uninstall:
	$(E) UNINSTALL $(TARGET)
	$(Q)rm $(PREFIX)/sbin/$(TARGET)

bld/.mkdir:
	$(Q)mkdir -p bld
	$(Q)touch bld/.mkdir

-include $(DEPS)

.PHONY: clean install uninstall
