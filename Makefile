# use this to disable flto optimizations:
#   make NO_FLTO=1
# and this to enable verbose mode:
#   make V=1

#
# SPDX-License-Identifer: GPL-2.0-or-later
#
# Copyright (C) 2014  Vyacheslav Trushkin
# Copyright (C) 2020  Boian Bonev
#
# This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#

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
MYLIBS=$(LIBS) -lncursesw
MYLDFLAGS=$(LDFLAGS)
STRIP?=strip

PREFIX?=$(DESTDIR)/usr

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
	$(Q)$(CC) -o $@ $(MYLDFLAGS) $^ $(MYLIBS)

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
