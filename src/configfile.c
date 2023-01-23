/* SPDX-License-Identifier: GPL-2.0-or-later

Copyright (C) 2014  Vyacheslav Trushkin
Copyright (C) 2020-2023  Boian Bonev

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

*/

#include "iotop.h"

#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define CONFIG_PATH "/.config/iotop"
#define CONFIG_NAME "/iotoprc"
#define MAX_OPT 50

static char *av[MAX_OPT]={NULL,};
static char *ss=NULL;
static int ac=0;

static inline void mkdir_p(const char *dir) {
	char tmp[PATH_MAX];
	char *p=NULL;
	size_t len;

	snprintf(tmp,sizeof(tmp),"%s",dir);
	len=strlen(tmp);
	if (tmp[len-1]=='/')
		tmp[len-1]=0;
	for (p=tmp+1;*p;p++)
		if (*p=='/') {
			*p=0;
			mkdir(tmp,S_IRWXU);
			*p='/';
		}
	mkdir(tmp,S_IRWXU);
}

static inline FILE *config_file_open(const char *mode) {
	char path[PATH_MAX];
	char *home;

	home=getenv("HOME");
	if (!home)
		home="";

	strcpy(path,home);
	strcat(path,CONFIG_PATH);
	mkdir_p(path);
	strcat(path,CONFIG_NAME);

	return fopen(path,mode);
}

inline int config_file_load(int *pac,char ***pav) {
	FILE *cf=config_file_open("r");
	ssize_t sz;
	char *s;
	char *e;

	if (!cf)
		return -1;
	if (fseek(cf,0,SEEK_END)) {
		fclose(cf);
		return -1;
	}
	sz=ftell(cf);
	if (sz<=0) {
		fclose(cf);
		return -1;
	}
	rewind(cf);
	ss=calloc(1,sz+1);
	if (!ss) {
		fclose(cf);
		return -1;
	}
	if ((size_t)sz!=fread(ss,1,sz,cf)) { // couldn't read all data
		free(ss);
		ss=NULL;
		fclose(cf);
		return -1;
	}

	av[ac++]="iotop"; // dummy program name
	s=ss;
	while (*s) {
		while (*s&&(*s==' '||*s=='\t'||*s=='\r')) // skip ws
			s++;
		if (*s=='\n') { // skip empty lines
			s++;
			continue;
		}
		if (*s=='#') { // skip comments
			while (*s&&*s!='\n')
				s++;
			if (*s=='\n')
				s++;
			continue;
		}
		// found an option
		av[ac]=s;
		if (ac>=MAX_OPT-1) {
			fprintf(stderr,"Too many options in config file\n");
			free(ss);
			ss=NULL;
			fclose(cf);
			return -1;
		}
		ac++;
		while (*s&&*s!='\n')
			s++;
		e=s-1;
		if (*s) {
			*s=0;
			s++;
		}
		while (e>av[ac-1]&&(*e==' '||*e=='\t')) // trim trailing white space
			*e--=0;
	}

	fclose(cf);
	*pac=ac;
	*pav=av;
	return 0;
}

inline void config_file_free(void) {
	if (ss)
		free(ss);
	ss=NULL;
	memset(av,0,sizeof av);
	ac=0;
}

inline int config_file_save(void) {
	FILE *cf=config_file_open("w");

	if (!cf)
		return -1;

	fprintf(cf,"# iotop configuration file\n");
	fprintf(cf,"# empty lines are ignored, comments start with #\n");
	fprintf(cf,"# each line contains a single option\n");
	fprintf(cf,"\n");

	// --version is ignored
	// --help is ignored
	// --help-type
	if (config.f.helptype==0)
		fprintf(cf,"--help-type=none\n");
	if (config.f.helptype==1)
		fprintf(cf,"--help-type=win\n");
	if (config.f.helptype==2)
		fprintf(cf,"--help-type=inline\n");
	// --batch is ignored
	// --only
	if (config.f.only)
		fprintf(cf,"--only\n");
	// --iter is ignored
	// --delay
	fprintf(cf,"--delay=%d\n",params.delay);
	// --pid is ignored
	// --user is ignored
	// --processes
	if (config.f.processes)
		fprintf(cf,"--processes\n");
	// --accumulated
	if (config.f.accumulated)
		fprintf(cf,"--accumulated\n");
	// --kilobytes
	if (config.f.kilobytes)
		fprintf(cf,"--kilobytes\n");
	// --timestamp is ignored
	// --quiet is ignored
	// --fullcmdline
	if (config.f.fullcmdline)
		fprintf(cf,"--fullcmdline\n");
	// --hide-pid
	if (config.f.hidepid)
		fprintf(cf,"--hide-pid\n");
	// --hide-prio
	if (config.f.hideprio)
		fprintf(cf,"--hide-prio\n");
	// --hide-user
	if (config.f.hideuser)
		fprintf(cf,"--hide-user\n");
	// --hide-read
	if (config.f.hideread)
		fprintf(cf,"--hide-read\n");
	// --hide-write
	if (config.f.hidewrite)
		fprintf(cf,"--hide-write\n");
	// --hide-swapin
	if (config.f.hideswapin)
		fprintf(cf,"--hide-swapin\n");
	// --hide-io
	if (config.f.hideio)
		fprintf(cf,"--hide-io\n");
	// --hide-graph
	if (config.f.hidegraph)
		fprintf(cf,"--hide-graph\n");
	// --hide-command
	if (config.f.hidecmd)
		fprintf(cf,"--hide-command\n");
	// --dead-x
	if (config.f.deadx)
		fprintf(cf,"--dead-x\n");
	// --hide-exited
	if (config.f.hideexited)
		fprintf(cf,"--hide-exited\n");
	// --no-color
	if (config.f.nocolor)
		fprintf(cf,"--no-color\n");
	// --reverse-graph
	if (config.f.reverse_graph)
		fprintf(cf,"--reverse-graph\n");
	// --grtype
	if (config.f.grtype==E_GR_IO)
		fprintf(cf,"--grtype=io\n");
	if (config.f.grtype==E_GR_R)
		fprintf(cf,"--grtype=r\n");
	if (config.f.grtype==E_GR_W)
		fprintf(cf,"--grtype=w\n");
	if (config.f.grtype==E_GR_RW)
		fprintf(cf,"--grtype=rw\n");
	if (config.f.grtype==E_GR_SW)
		fprintf(cf,"--grtype=sw\n");
	// --si
	if (config.f.base==1000)
		fprintf(cf,"--si\n");
	// --threshold
	fprintf(cf,"--threshold=%d\n",config.f.threshold);
	// --ascii
	if (!config.f.unicode)
		fprintf(cf,"--ascii\n");

	fclose(cf);

	return 0;
}
