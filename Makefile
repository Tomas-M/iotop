# use this to disable flto optimizations:
#   make NO_FLTO=1
# and this to enable verbose mode:
#   make V=1

#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Copyright (C) 2014  Vyacheslav Trushkin
# Copyright (C) 2020-2024  Boian Bonev
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
CFLAGS?=-O3 -fno-stack-protector -mno-stackrealign
CFLAGS+=-flto=auto
else
CFLAGS?=-O3 -fno-stack-protector -mno-stackrealign
endif

ifdef GCCFANALIZER
CFLAGS+=-fanalyzer
endif

PREFIX?=$(DESTDIR)/usr
INSTALL?=install
STRIP?=strip

PKG_CONFIG?=pkg-config
NCCC?=$(shell $(PKG_CONFIG) --cflags ncursesw)
NCLD?=$(shell $(PKG_CONFIG) --libs ncursesw)
ifeq ("$(NCLD)","")
NCCC:=$(shell $(PKG_CONFIG) --cflags ncurses)
NCLD:=$(shell $(PKG_CONFIG) --libs ncurses)
endif
ifeq ("$(NCLD)","")
NCCC:=
NCLD:=-lncursesw
endif

# for glibc < 2.17, -lrt is required for clock_gettime
NEEDLRT:=$(shell if $(CC) -E glibcvertest.h -o -|grep IOTOP_NEED_LRT|grep -q yes;then echo need; fi)
# some architectures do not have -mno-stackrealign
HAVESREA:=$(shell if $(CC) -mno-stackrealign -xc -c /dev/null -o /dev/null >/dev/null 2>/dev/null;then echo yes;else echo no;fi)
# old comiplers do not have -Wdate-time
HAVEWDTI:=$(shell if $(CC) -Wdate-time -xc -c /dev/null -o /dev/null >/dev/null 2>/dev/null;then echo yes;else echo no;fi)

MYCFLAGS:=$(CPPFLAGS) $(CFLAGS) $(NCCC) -Wall -Wextra -Wformat -Werror=format-security -Wdate-time -D_FORTIFY_SOURCE=2 --std=gnu89 -fPIE
MYLIBS:=$(NCLD) $(LIBS)
MYLDFLAGS:=$(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -fPIE -pie

ifeq ("$(HAVESREA)","no")
MYCFLAGS:=$(filter-out -mno-stackrealign,$(MYCFLAGS))
MYLDFLAGS:=$(filter-out -mno-stackrealign,$(MYLDFLAGS))
endif

ifeq ("$(HAVEWDTI)","no")
MYCFLAGS:=$(filter-out -Wdate-time,$(MYCFLAGS))
MYLDFLAGS:=$(filter-out -Wdate-time,$(MYLDFLAGS))
endif

ifeq ("$(NEEDLRT)","need")
MYLDFLAGS+=-lrt
endif

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
	$(Q)$(INSTALL) -D -m 0755 $(TARGET) $(PREFIX)/sbin/$(TARGET)
	$(Q)$(INSTALL) -D -m 0644 iotop.8 $(PREFIX)/share/man/man8/iotop.8

uninstall:
	$(E) UNINSTALL $(TARGET)
	$(Q)rm -f $(PREFIX)/sbin/$(TARGET)
	$(Q)rm -f $(PREFIX)/share/man/man8/iotop.8

bld/.mkdir:
	$(Q)mkdir -p bld
	$(Q)touch bld/.mkdir

VER:=$(shell grep VERSION src/iotop.h|tr -d '\"'|awk '{print $$3}')
mkotar:
	$(MAKE) clean
	-dh_clean
	tar \
		--xform 's,^[.],iotop-$(VER),' \
		--exclude ./.git \
		--exclude ./.gitignore \
		--exclude ./debian \
		-Jcvf ../iotop-c_$(VER).orig.tar.xz .
	-rm -f ../iotop-c_$(VER).orig.tar.xz.asc
	gpg -a --detach-sign ../iotop-c_$(VER).orig.tar.xz
	cp -fa ../iotop-c_$(VER).orig.tar.xz ../iotop-$(VER).tar.xz
	cp -fa ../iotop-c_$(VER).orig.tar.xz.asc ../iotop-$(VER).tar.xz.asc

re:
	$(Q)$(MAKE) --no-print-directory clean
	$(Q)$(MAKE) --no-print-directory -j

pv:
	@echo CFLAGS: $(CFLAGS)
	@echo LDFLAGS: $(LDFLAGS)
	@echo LIBS: $(LIBS)
	@echo NCCC: $(NCCC)
	@echo NCLD: $(NCLD)
	@echo NEEDLRT: $(NEEDLRT)
	@echo HAVESREA: $(HAVESREA)
	@echo HAVEWDTI: $(HAVEWDTI)
	@echo MYCFLAGS: $(MYCFLAGS)
	@echo MYLDFLAGS: $(MYLDFLAGS)
	@echo MYLIBS: $(MYLIBS)

-include $(DEPS)

.PHONY: all clean install uninstall mkotar re pv
