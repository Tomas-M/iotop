TARGET=iotop

SRCS:=$(wildcard src/*.c)
OBJS:=$(patsubst %c,%o,$(patsubst src/%,bld/%,$(SRCS)))
DEPS:=$(OBJS:.o=.d)

CFLAGS=-Wall -O3 -std=gnu90 -fno-stack-protector -mno-stackrealign
LDFLAGS=-lncurses
STRIP?=strip

PREFIX=/usr

# use this to disable flto optimizations:
#   make NO_FLTO=1
# and this to enable verbose mode:
#   make V=1

ifndef NO_FLTO
CFLAGS+=-flto
LDFLAGS+=-flto -O3
endif

ifeq ("$(V)","1")
Q:=
vecho:=@true
else
Q:=@
VE:=@echo
endif

$(TARGET): $(OBJS)
	$(VE) LD $@
	$(Q)$(CC) -o $@ $^ $(LDFLAGS)

bld/%.o: src/%.c bld/.mkdir
	$(VE) DEP $@
	$(Q)$(CC) $(CFLAGS) -MM -MT $@ -MF $(patsubst %.o,%.d,$@) $<
	$(VE) CC $@
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

clean:
	$(VE) CLEAN
	$(Q)rm -rf ./bld $(TARGET)

install: $(TARGET)
	$(VE) STRIP $(TARGET)
	$(Q)$(STRIP) $(TARGET)
	$(VE) INSTALL $(TARGET)
	$(Q)cp $(TARGET) $(PREFIX)/bin/$(TARGET)

uninstall:
	$(VE) UNINSTALL $(TARGET)
	$(Q)rm $(PREFIX)/bin/$(TARGET)

bld/.mkdir:
	$(Q)mkdir -p bld
	$(Q)touch bld/.mkdir

-include $(DEPS)

.PHONY: clean install uninstall
