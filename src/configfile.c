/* SPDX-License-Identifier: GPL-2.0-or-later

Copyright (C) 2014  Vyacheslav Trushkin
Copyright (C) 2020-2026  Boian Bonev

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

*/

#include "iotop.h"

#include <errno.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define CONFIG_DIR1 "/.config"
#define CONFIG_DIR2 "/iotop"
#define CONFIG_NAME "/iotoprc"
#define MAX_OPT 50

static char *av[MAX_OPT]={NULL,};
static char path[PATH_MAX]=""; // the config file that was opened last
static char tmppath[PATH_MAX]=""; // the new config while it is written, renamed over path when complete
static char *ss=NULL;
static int ac=0;

static inline void mkdir_p(const char *dir) {
	char tmp[PATH_MAX];
	char *p=NULL;
	size_t len;

	snprintf(tmp,sizeof tmp,"%s",dir);
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

// open a new file next to the config, so that a failed write does not leave it truncated; when it can
// not get the mode and the owner of the config, e.g. in a directory that is not writable, write in place
static inline FILE *config_tmp_open(void) {
	char real[PATH_MAX];
	struct stat st;
	mode_t um;
	FILE *f;
	int len;
	int fd;

	*tmppath=0;
	if (realpath(path,real)) // replace the target of a symlink, not the link
		strcpy(path,real);
	len=snprintf(tmppath,sizeof tmppath,"%s.XXXXXX",path);
	if (len<0||(size_t)len>=sizeof tmppath) {
		*tmppath=0;
		return fopen(path,"w");
	}
	fd=mkstemp(tmppath);
	if (fd==-1) {
		*tmppath=0;
		return fopen(path,"w");
	}
	if (!stat(path,&st)) {
		if ((st.st_uid!=geteuid()||st.st_gid!=getegid())&&fchown(fd,st.st_uid,st.st_gid))
			goto inplace;
		if (fchmod(fd,st.st_mode&07777))
			goto inplace;
	} else {
		um=umask(0);
		umask(um);
		if (fchmod(fd,0666&~um))
			goto inplace;
	}
	f=fdopen(fd,"w");
	if (f)
		return f;
inplace:
	close(fd);
	unlink(tmppath);
	*tmppath=0;
	return fopen(path,"w");
}

static inline FILE *config_file_open(const char *mode) {
	char dir[PATH_MAX];
	char *xdgconfig;
	char *home;
	int len;

	xdgconfig=getenv("XDG_CONFIG_HOME");
	home=getenv("HOME");

	// an empty XDG_CONFIG_HOME is treated as unset; without a home do not put the config in /
	if (xdgconfig&&*xdgconfig)
		len=snprintf(dir,sizeof dir,"%s%s",xdgconfig,CONFIG_DIR2);
	else if (home&&*home)
		len=snprintf(dir,sizeof dir,"%s%s%s",home,CONFIG_DIR1,CONFIG_DIR2);
	else { // errno tells the callers why there is no file
		errno=ENOENT;
		return NULL;
	}
	if (len<0||(size_t)len>=sizeof dir) { // do not create or open a truncated path
		errno=ENAMETOOLONG;
		return NULL;
	}
	if (*mode=='w') // only writing the config needs the directories
		mkdir_p(dir);
	len=snprintf(path,sizeof path,"%s%s",dir,CONFIG_NAME);
	if (len<0||(size_t)len>=sizeof path) {
		errno=ENAMETOOLONG;
		return NULL;
	}

	if (*mode=='w')
		return config_tmp_open();
	return fopen(path,mode);
}

// the path of the config file that was opened last, for messages
inline const char *config_file_name(void) {
	return path;
}

inline int config_file_load(int *pac,char ***pav) {
	FILE *cf=config_file_open("r");
	ssize_t sz;
	char *s,*e;

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
	if (fseek(cf,0,SEEK_SET)) {
		fclose(cf);
		return -1;
	}
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
		if (!*s) // only ws till the end
			break;
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
		e=s;
		if (*s) {
			*s=0;
			s++;
		}
		if (!strncmp(av[ac-1],"--filter=",9)) { // a space at the end of a regex is a part of it, only the CR of CRLF is not
			if (e>av[ac-1]&&e[-1]=='\r')
				*--e=0;
		} else
			while (e>av[ac-1]&&(e[-1]==' '||e[-1]=='\t'||e[-1]=='\r')) // trim trailing ws, e.g. the CR of CRLF line ends
				*--e=0;
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

// save the config to its file, or write it to stdout for --debug-write
inline int config_file_save(int to_stdout) {
	FILE *cf=to_stdout?stdout:config_file_open("w");
	char *no_color=getenv("NO_COLOR");
	int env_nocolor=no_color&&*no_color; // the default from the environment, not to be saved as a choice

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
	fprintf(cf,"--delay=%d.%03d\n",params.delay_ms/1000,params.delay_ms%1000);
	// --pid is ignored
	// --user is ignored
	// --processes
	if (config.f.processes)
		fprintf(cf,"--processes\n");
	// --flat
	if (config.f.flat)
		fprintf(cf,"--flat\n");
	// --accumulated
	if (config.f.accumulated)
		fprintf(cf,"--accumulated\n");
	// --accum-bw
	if (config.f.accumbw)
		fprintf(cf,"--accum-bw\n");
	// --no-lifetime
	if (config.f.nolifetime)
		fprintf(cf,"--no-lifetime\n");
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
	// --show-vfs
	if (!config.f.hidevfsread&&!config.f.hidevfswrite)
		fprintf(cf,"--show-vfs\n");
	else {
		if (!config.f.hidevfsread)
			fprintf(cf,"--show-vfs-read\n");
		if (!config.f.hidevfswrite)
			fprintf(cf,"--show-vfs-write\n");
	}
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
	// --no-color or --color, when it is not the default that NO_COLOR gives
	if (config.f.nocolor!=env_nocolor)
		fprintf(cf,config.f.nocolor?"--no-color\n":"--color\n");
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
	// --hide-time
	if (config.f.hideclock)
		fprintf(cf,"--hide-time\n");
	// --inverse
	if (config.f.inverse)
		fprintf(cf,"--inverse\n");
	// --filter; a line break, only possible from the command line, does not fit in its line
	if (params.search_regx_ok&&params.search_str&&*params.search_str&&!strpbrk(params.search_str,"\r\n"))
		fprintf(cf,"--filter=%s\n",params.search_str);
	// --uid2name
	if (config.f.uid2name==E_UID2NAME_ALWAYS)
		fprintf(cf,"--uid2name=always\n");
	if (config.f.uid2name==E_UID2NAME_NONE)
		fprintf(cf,"--uid2name=none\n");
	if (config.f.uid2name==E_UID2NAME_CYCLIC)
		fprintf(cf,"--uid2name=cyclic\n");
	if (config.f.uid2name==E_UID2NAME_STATIC)
		fprintf(cf,"--uid2name=static\n");

	if (to_stdout) {
		if (ferror(cf)||fflush(cf))
			return -1;
		return 0;
	}
	// the buffered data is written by fflush, e.g. a full disk shows only now; fsync before the rename keeps
	// a crash from leaving an empty config
	if (ferror(cf)||fflush(cf)||(*tmppath&&fsync(fileno(cf)))) {
		fclose(cf);
		if (*tmppath)
			unlink(tmppath);
		return -1;
	}
	if (fclose(cf)) {
		if (*tmppath)
			unlink(tmppath);
		return -1;
	}
	if (*tmppath&&rename(tmppath,path)) {
		unlink(tmppath);
		return -1;
	}

	return 0;
}
