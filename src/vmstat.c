/* SPDX-License-Identifier: GPL-2.0-or-later

Copyright (C) 2014  Vyacheslav Trushkin
Copyright (C) 2020-2024  Boian Bonev

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

#define BSIZ 4096
#define PGIN "\npgpgin "
#define PGOU "\npgpgout "

inline int get_vm_counters(uint64_t *pgpgin,uint64_t *pgpgou) {
	ssize_t bs=BSIZ,bp=0;
	char *buf,*pi,*po,*t;
	int fd;

	if (!pgpgin||!pgpgou)
		return EINVAL;

	if (-1==(fd=open("/proc/vmstat",O_RDONLY)))
		return ENOENT;

	buf=malloc(bs);
	if (!buf) {
		close(fd);
		return ENOMEM;
	}

	for (;;) {
		ssize_t l=read(fd,buf+bp,bs-bp);

		if (l<=0)
			break;
		if (l==bs-bp) {
			t=realloc(buf,bs+BSIZ);

			if (!t) {
				// it requires hell of an effort to silence a bogus warning...
				#pragma GCC diagnostic push
				// silence gcc about unknown -Wunknown-warning-option
				#pragma GCC diagnostic ignored "-Wpragmas"
				// silence clang about unknown -Wuse-after-free
				#pragma GCC diagnostic ignored "-Wunknown-warning-option"
				// silence the warning itself
				#pragma GCC diagnostic ignored "-Wuse-after-free"
				free(buf); // gcc-13 yields false positive -Wuse-after-free here
				#pragma GCC diagnostic pop
				close(fd);
				return ENOMEM;
			}
			buf=t;
			bs+=BSIZ;
		}
		bp+=l;
	}
	buf[bp]=0;
	pi=strstr(buf,PGIN);
	po=strstr(buf,PGOU);
	if (!pi||!po) {
		free(buf);
		close(fd);
		return ENOENT;
	}
	*pgpgin=1024*strtoull(pi+strlen(PGIN),&t,10);
	if (*t!='\n') {
		free(buf);
		close(fd);
		return ENOENT;
	}
	*pgpgou=1024*strtoull(po+strlen(PGOU),&t,10);
	if (*t!='\n') {
		free(buf);
		close(fd);
		return ENOENT;
	}
	free(buf);
	close(fd);
	return 0;
}
