/* SPDX-License-Identifier: GPL-2.0-or-later

Copyright (C) 2014  Vyacheslav Trushkin
Copyright (C) 2020-2026  Boian Bonev

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

*/

#include "iotop.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

static inline int _read_task_delayacct(int *da) {
	char buf[10],*t;
	ssize_t bs;
	int fd;

	if (!da)
		return EINVAL;

	if (-1==(fd=open("/proc/sys/kernel/task_delayacct",O_RDONLY)))
		return ENOENT;

	bs=read(fd,buf,sizeof buf-1);
	if (bs<0) {
		close(fd);
		return errno;
	}
	if (bs==sizeof buf-1) {
		close(fd);
		return ENODATA;
	}
	buf[bs]=0;
	*da=strtoull(buf,&t,10);
	if (*t!='\n') {
		close(fd);
		return ENODATA;
	}
	close(fd);
	return 0;
}

inline int has_task_delayacct(void) {
	int da=0;

	if (_read_task_delayacct(&da))
		return 0;
	return 1;
}

inline int read_task_delayacct(void) {
	int da=0,r;

	r=_read_task_delayacct(&da);
	if (!r)
		return da;
	if (r==ENOENT||r==ENODATA)
		return 1;
	return 0;
}

inline int write_task_delayacct(int da) {
	char *v=da?"1\n":"0\n";
	ssize_t ws;
	int fd;

	if (-1==(fd=open("/proc/sys/kernel/task_delayacct",O_WRONLY)))
		return ENOENT;

	ws=write(fd,v,strlen(v));
	if (ws<0) {
		close(fd);
		return errno;
	}
	if (strlen(v)==(size_t)ws) {
		close(fd);
		return 0;
	}
	close(fd);
	return ENODATA;
}
