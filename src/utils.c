/* SPDX-License-Identifier: GPL-2.0-or-later

Copyright (C) 2014  Vyacheslav Trushkin
Copyright (C) 2020-2022  Boian Bonev

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

*/

#include "iotop.h"

#include <time.h>
#include <fcntl.h>
#include <stdio.h>
#include <wchar.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

inline char *read_cmdline(int pid,int isshort) {
	char *rv=NULL;
	char path[30];
	int fd;

	snprintf(path,sizeof path,"/proc/%d/cmdline",pid);
	fd=open(path,O_RDONLY);
	if (fd!=-1) {
		char *dbuf=malloc(BUFSIZ+1);
		ssize_t n,p=0,sz=BUFSIZ;

		if (!dbuf) {
			close(fd);
			return NULL;
		}

		do {
			n=read(fd,dbuf+p,sz-p);
			if (n==sz-p) {
				char *t=realloc(dbuf,sz+BUFSIZ+1);

				if (!t) {
					close(fd);
					free(dbuf);
					return NULL;
				}
				dbuf=t;
				sz+=BUFSIZ;
			}
			if (n>0)
				p+=n;
		} while (n>0);

		if (p>0) {
			dbuf[p]=0;
			if (isshort&&(dbuf[0]=='/'||(p>1&&dbuf[0]=='.'&&dbuf[1]=='/')||(p>2&&dbuf[0]=='.'&&dbuf[1]=='.'&&dbuf[2]=='/'))) {
				char *ep;

				ep=strrchr(dbuf,'/');
				if (ep&&ep[1]) {
					char *t=strdup(ep+1);

					if (t) {
						free(dbuf);
						dbuf=t;
						p=strlen(t)+1;
					}
				}
			}

			if (!isshort) {
				ssize_t k;

				for (k=0;k<p;k++)
					dbuf[k]=dbuf[k]?dbuf[k]:' ';
			}
			rv=dbuf;
		} else
			free(dbuf);
		close(fd);
	}

	if (rv)
		return rv;

	snprintf(path,sizeof path,"/proc/%d/status",pid);
	fd=open(path,O_RDONLY);
	if (fd!=-1) {
		char buf[BUFSIZ+1];
		ssize_t n=read(fd,buf,BUFSIZ);

		close(fd);

		if (n>0) {
			char *eol,*tab;

			buf[n]=0;
			eol=strchr(buf,'\n');
			tab=strchr(buf,'\t');
			if (eol&&tab&&eol>tab) {
				size_t rvlen;

				eol[0]=0;
				rvlen=strlen(tab+1)+3;
				rv=malloc(rvlen);
				if (rv)
					snprintf(rv,rvlen,!isshort?"[%s]":"%s",tab+1);
			}
		}
	}

	return rv;
}

inline void pidgen_cb(pg_cb cb,void *hint1,void *hint2) {
	DIR *pr;

	if ((pr=opendir("/proc"))) {
		struct dirent *de=readdir(pr);

		for (;de;de=readdir(pr)) {
			char *eol=NULL;
			char path[30];
			int havt=0;
			pid_t pid;
			DIR *tr;

			pid=strtol(de->d_name,&eol,10);
			if (*eol!='\0')
				continue;
			snprintf(path,sizeof path,"/proc/%d/task",pid);
			if ((tr=opendir(path))) {
				struct dirent *tde=readdir(tr);

				for (;tde;tde=readdir(tr)) {
					pid_t tid;

					eol=NULL;
					tid=strtol(tde->d_name,&eol,10);
					if (*eol!='\0')
						continue;
					havt=1;
					cb(pid,tid,hint1,hint2);
				}
				closedir(tr);
			}
			if (!havt)
				cb(pid,pid,hint1,hint2);
		}
		closedir(pr);
	}
}

inline int64_t monotime(void) {
	struct timespec ts;
	int64_t res;

	clock_gettime(CLOCK_MONOTONIC,&ts);
	res=ts.tv_sec*1000;
	res+=ts.tv_nsec/1000000;
	return res;
}

inline const char *esc_low_ascii1(char c) {
	// some architectures have char type unsigned by default
	// while others have a signed char; make the check for
	// printing range universal
	unsigned char uc=*(unsigned char *)(void *)&c;
	static char ehex[0x20][6];
	static int initialized=0;

	if (uc>=0x20) // no escaping needed
		return NULL;
	if (!initialized) {
		int i;

		for (i=0;i<0x20;i++)
			sprintf(ehex[i],"\\0x%02x",i);
		initialized=1;
	}
	switch (c) {
		case 0x00: // shorter form
			return "\\0";
		case 0x07:
			return "\\a";
		case 0x08:
			return "\\b";
		case 0x09:
			return "\\t";
		case 0x0a:
			return "\\n";
		case 0x0b:
			return "\\v";
		case 0x0c:
			return "\\f";
		case 0x0d:
			return "\\r";
		case 0x1b:
			return "\\e";
		default:
			return ehex[(unsigned)c];
	}
}

inline char *esc_low_ascii(char *p) {
	char *s=p,*res,*rp;
	int rc=0;

	if (!p)
		return NULL;

	// count
	while (*s) {
		const char *rs=esc_low_ascii1(*s++);

		if (!rs)
			rc++;
		else
			rc+=strlen(rs);
	}
	res=malloc(rc+1);
	if (!res)
		return NULL;
	// copy, start over from the beginning
	// two-pass over the string is faster than using realloc
	s=p;
	rp=res;
	while (*s) {
		const char *rs=esc_low_ascii1(*s++);

		if (!rs)
			*rp++=s[-1];
		else
			while (*rs)
				*rp++=*rs++;
	}
	*rp=0;
	return res;
}

#define UBLEN 1024

inline char *u8strpadt(const char *s,ssize_t rlen) {
	char *d=malloc(UBLEN);
	size_t dl=UBLEN;
	size_t si=0;
	size_t di=0;
	size_t tl=0;
	size_t len;
	size_t sl;
	wchar_t w;

	if (rlen<0)
		len=0;
	else
		len=rlen;
	if (!d)
		return NULL;
	if (!s)
		s="(null)";

	sl=strlen(s);
	if (mbtowc(NULL,NULL,0)) {
	}
	for (;;) {
		int cl;
		int tw;

		if (!s[si])
			break;

		cl=mbtowc(&w,s+si,sl-si);
		if (cl<=0) {
			si++;
			continue;
		}
		if (dl-di<(size_t)cl+1) {
			char *t;

			dl+=UBLEN;
			t=realloc(d,dl);
			if (!t) {
				free(d);
				return NULL;
			}
			d=t;
		}
		tw=wcwidth(w);
		if (tw<0) {
			si+=cl;
			continue;
		}
		if (tw&&tw+tl>len)
			break;
		memcpy(d+di,s+si,cl);
		di+=cl;
		si+=cl;
		tl+=tw;
		d[di]=0;
	}
	while (tl<len) {
		if (dl-di<1+1) {
			char *t;

			dl+=UBLEN;
			t=realloc(d,dl);
			if (!t) {
				free(d);
				return NULL;
			}
			d=t;
		}
		d[di++]=' ';
		d[di]=0;
		tl++;
	}
	return d;
}

inline int is_a_dir(const char *p) {
	struct stat st;

	if (stat(p,&st))
		return 0;
	return (st.st_mode&S_IFMT)==S_IFDIR;
}

inline int is_a_process(pid_t tid) {
	char path[30];

	snprintf(path,sizeof path,"/proc/%d",tid);
	return is_a_dir(path);
}

