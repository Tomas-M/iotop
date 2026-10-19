/* SPDX-License-Identifier: GPL-2.0-or-later

Copyright (C) 2014  Vyacheslav Trushkin
Copyright (C) 2020-2026  Boian Bonev

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

*/

#include "iotop.h"

#include <pwd.h>
#include <errno.h>
#include <stdio.h>
#include <search.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/taskstats.h>
#include <linux/genetlink.h>

// Recent Linux kernel broke the backwards compatibility in struct taskstats,
// rendering the kernel headers useless and forcing userland to make hacks
// like the one below... :(

#include "taskstats-v14.h"
#include "taskstats-v15.h"

/*
 * Generic macros for dealing with netlink sockets. Might be duplicated
 * elsewhere
 */
#define GENLMSG_DATA(glh) ((void *)((char*)NLMSG_DATA(glh)+GENL_HDRLEN))
#define GENLMSG_PAYLOAD(glh) (NLMSG_PAYLOAD(glh,0)-GENL_HDRLEN)
#define NLA_DATA(na) ((void *)((char*)(na)+NLA_HDRLEN))
#define NLA_PAYLOAD(len) (len-NLA_HDRLEN)

#define MAX_MSG_SIZE 1024

struct msgtemplate {
	struct nlmsghdr n;
	struct genlmsghdr g;
	char buf[MAX_MSG_SIZE];
};

static int nl_sock=-1;
static int nl_fam_id=0;

inline int send_cmd(int sock_fd,__u16 nlmsg_type,__u32 nlmsg_pid,__u8 genl_cmd,__u16 nla_type,void *nla_data,int nla_len) {
	struct nlattr *na;
	struct sockaddr_nl nladdr;
	int r,buflen;
	char *buf;

	struct msgtemplate msg;

	memset(&msg,0,sizeof msg);
	// make cppcheck happier; hopefully the optimizer should remove this
	memset(msg.buf,0,sizeof msg.buf);

	msg.n.nlmsg_len=NLMSG_LENGTH(GENL_HDRLEN);
	msg.n.nlmsg_type=nlmsg_type;
	msg.n.nlmsg_flags=NLM_F_REQUEST;
	msg.n.nlmsg_seq=0;
	msg.n.nlmsg_pid=nlmsg_pid;
	msg.g.cmd=genl_cmd;
	msg.g.version=0x1;

	na=(struct nlattr *)GENLMSG_DATA(&msg);
	na->nla_type=nla_type;
	na->nla_len=nla_len+NLA_HDRLEN;

	memcpy(NLA_DATA(na),nla_data,nla_len);
	msg.n.nlmsg_len+=NLMSG_ALIGN(na->nla_len);

	buf=(char *)&msg;
	buflen=msg.n.nlmsg_len;
	memset(&nladdr,0,sizeof nladdr);
	nladdr.nl_family=AF_NETLINK;
	while ((r=sendto(sock_fd,buf,buflen,0,(struct sockaddr *)&nladdr,sizeof nladdr))<buflen) {
		if (r>0) {
			buf+=r;
			buflen-=r;
		} else
			if (errno!=EAGAIN)
				return -1;
	}
	return 0;
}

inline int get_family_id(int sock_fd) {
	struct msgtemplate answ;
	static char name[sizeof TASKSTATS_GENL_NAME];
	struct nlattr *na;
	ssize_t rep_len;
	int id=0;

	snprintf(name,sizeof name,"%s",TASKSTATS_GENL_NAME);
	if (send_cmd(sock_fd,GENL_ID_CTRL,getpid(),CTRL_CMD_GETFAMILY,CTRL_ATTR_FAMILY_NAME,(void *)name,strlen(TASKSTATS_GENL_NAME)+1))
		return 0;

	rep_len=recv(sock_fd,&answ,sizeof answ,0);
	if (rep_len<0||!NLMSG_OK((&answ.n),(size_t)rep_len)||answ.n.nlmsg_type==NLMSG_ERROR)
		return 0;

	na=(struct nlattr *)GENLMSG_DATA(&answ);
	na=(struct nlattr *)((char *)na+NLA_ALIGN(na->nla_len));
	if (na->nla_type==CTRL_ATTR_FAMILY_ID)
		id=*(__u16 *)NLA_DATA(na);

	return id;
}

inline void nl_init(void) {
	struct sockaddr_nl addr;
	int sock_fd=socket(PF_NETLINK,SOCK_RAW,NETLINK_GENERIC);

	if (sock_fd<0)
		goto error;

	memset(&addr,0,sizeof addr);
	addr.nl_family=AF_NETLINK;

	if (bind(sock_fd,(struct sockaddr *)&addr,sizeof addr)<0)
		goto error;

	nl_sock=sock_fd;
	nl_fam_id=get_family_id(sock_fd);
	if (!nl_fam_id) {
		fprintf(stderr,"nl_init: couldn't get netlink family id\n");
		exit(EXIT_FAILURE);
	}

	return;

error:
	if (sock_fd>-1)
		close(sock_fd);

	fprintf(stderr,"nl_init: %s\n",strerror(errno));
	exit(EXIT_FAILURE);
}

inline int nl_xxxid_info(pid_t tid,pid_t pid,struct xxxid_stats *stats) {
	if (nl_sock<0) {
		fprintf(stderr,"nl_xxxid_info: nl_sock is %d",nl_sock);
		exit(EXIT_FAILURE);
	}
	if (nl_fam_id==0) { // this will cause recv to wait forever
		fprintf(stderr,"nl_xxxid_info: nl_fam_id is 0");
		exit(EXIT_FAILURE);
	}

	if (send_cmd(nl_sock,nl_fam_id,tid,TASKSTATS_CMD_GET,TASKSTATS_CMD_ATTR_PID,&tid,sizeof tid)) {
		fprintf(stderr,"get_xxxid_info: %s\n",strerror(errno));
		return -1;
	}

	stats->pid=pid;
	stats->tid=tid;

	struct msgtemplate msg;
	ssize_t rv=recv(nl_sock,&msg,sizeof msg,0);

	if (rv<0||!NLMSG_OK((&msg.n),(size_t)rv)||msg.n.nlmsg_type==NLMSG_ERROR) {
		struct nlmsgerr *err=NLMSG_DATA(&msg);

		if (err->error!=-ESRCH)
			fprintf(stderr,"fatal reply error, %d\n",err->error);
		return -1;
	}

	rv=GENLMSG_PAYLOAD(&msg.n);

	struct nlattr *na=(struct nlattr *)GENLMSG_DATA(&msg);
	int len=0;

	while (len<rv) {
		if (!na->nla_len)
			break;
		len+=NLA_ALIGN(na->nla_len);

		if (na->nla_type==TASKSTATS_TYPE_AGGR_TGID||na->nla_type==TASKSTATS_TYPE_AGGR_PID) {
			struct nlattr *na2=(struct nlattr *)NLA_DATA(na);
			int aggr_len=NLA_PAYLOAD(na->nla_len);
			struct nlattr *na2base=na2;
			int len2=0;

			while (len2<aggr_len) {
				if (!na2->nla_len)
					break;
				if (na2->nla_type==TASKSTATS_TYPE_STATS) {
					// NOTE: we use the build system kernel headers for the version field only
					// all the data access is done by using copies of the respective versions
					// of the kernel headers
					// A patch that will fix the problem is proposed to be included in the kernel
					// and the only broken struct taskstats will be the one with v15. But we can
					// not rely on the build system kernel headers for universal access and have
					// to keep the copies.
					// In this way a build with any kernel headers will work everywhere
					struct taskstats *ts=NLA_DATA(na2);
					struct taskstats_v14 *t14=NLA_DATA(na2);
					struct taskstats_v15 *t15=NLA_DATA(na2);

					if (ts->version<IOTOP_TASKSTATS_MINVER) // v3 and below does not have the data we require
						taskstats_ver=ts->version;
					else if (ts->version!=15) { // use v14 for v4..v14 & v16 onwards
						stats->read_bytes=t14->read_bytes;
						stats->write_bytes=t14->write_bytes;
						stats->swapin_delay_total=t14->swapin_delay_total;
						stats->blkio_delay_total=t14->blkio_delay_total;
						stats->euid=t14->ac_uid;
					} else { // exception for v15 only
						stats->read_bytes=t15->read_bytes;
						stats->write_bytes=t15->write_bytes;
						stats->swapin_delay_total=t15->swapin_delay_total;
						stats->blkio_delay_total=t15->blkio_delay_total;
						stats->euid=t15->ac_uid;
					}
				}
				len2+=NLA_ALIGN(na2->nla_len);
				na2=(struct nlattr *)((char *)na2base+len2);
			}
		}
		na=(struct nlattr *)((char *)GENLMSG_DATA(&msg)+len);
	}

	return 0;
}

inline void nl_fini(void) {
	if (nl_sock>-1)
		close(nl_sock);
}

inline void free_stats(struct xxxid_stats *s) {
	if (s->cmdline_short)
		free(s->cmdline_short);
	if (s->cmdline_long)
		free(s->cmdline_long);
	if (s->cmdline_comm)
		free(s->cmdline_comm);
	if (s->pw_name)
		free(s->pw_name);
	if (s->hist!=&hist_none)
		free(s->hist);
	arr_free_noitem(s->threads);

	free(s);
}

static const char *get_user_name_pwuid(uid_t uid,const char *unknown) {
	struct passwd *pwd;

	pwd=getpwuid(uid);
	return pwd&&pwd->pw_name?pwd->pw_name:unknown;
}

struct uid_cache_entry {
	uid_t uid;
	char *pw_name;
	uint64_t valid_until_cycle;
};

static void *uid_tree=NULL;
static uint64_t current_cycle=0;

static int uid_cache_cmp(const void *a,const void *b) {
	const struct uid_cache_entry *ea=a;
	const struct uid_cache_entry *eb=b;

	if (ea->uid<eb->uid)
		return -1;
	if (ea->uid>eb->uid)
		return 1;
	return 0;
}

static void free_uid_cache_entry(void *nodep) {
	struct uid_cache_entry *e=nodep;

	free(e->pw_name);
	free(e);
}

static const char *get_user_name_ttl(uid_t uid,const char *unknown, int ttl_cycles) {
	struct uid_cache_entry key={.uid=uid,.pw_name=NULL};
	struct uid_cache_entry *e = NULL;
	void *node=tfind(&key,&uid_tree,uid_cache_cmp);
	const char *pw_name;

	if (node) {
		e=*(struct uid_cache_entry **)node;
		if (current_cycle < e->valid_until_cycle)
			return e->pw_name ? e->pw_name : unknown;
	}

	pw_name=get_user_name_pwuid(uid,unknown);

	if (!e) {
		e=calloc(1, sizeof *e);
		if (!e)
			return unknown;
		e->uid=uid;
		if (!tsearch(e,&uid_tree,uid_cache_cmp)) {
			free(e);
			return unknown;
		}
	}

	if (pw_name == unknown) {
		/* Negative cache: UID has no username */
		if (e->pw_name) {
			free(e->pw_name);
			e->pw_name=NULL;
		}
		e->valid_until_cycle=ttl_cycles ? (current_cycle + ttl_cycles) : UINT64_MAX;
		return unknown;
	}

	/* Positive cache: update username if changed */
	if (!e->pw_name || strcmp(e->pw_name, pw_name)) {
		char *new_name=strdup(pw_name);
		if (new_name) {
			free(e->pw_name);
			e->pw_name=new_name;
		}
	}

	e->valid_until_cycle=ttl_cycles ? (current_cycle + ttl_cycles) : UINT64_MAX;
	return e->pw_name ? e->pw_name : unknown;
}

static const char *get_user_name_cyclic(uid_t uid,const char *unknown) {
	return get_user_name_ttl(uid, unknown, 1);
}

static const char *get_user_name_static(uid_t uid,const char *unknown) {
	return get_user_name_ttl(uid, unknown, 0);
}

void clear_uid_cache(void) {
	if (uid_tree) {
		tdestroy(uid_tree,free_uid_cache_entry);
		uid_tree=NULL;
	}
}

// cmdline of the last read main process; its threads that follow it in the /proc walk get it from here
static struct {
	pid_t pid;
	char *cmd_long;
	char *cmd_short;
} pcmd={-1,NULL,NULL};

// remember the cmdline of process pid or forget it with pid=-1
static void pcmd_set(pid_t pid,const char *cmd_long,const char *cmd_short) {
	if (pcmd.cmd_long)
		free(pcmd.cmd_long);
	if (pcmd.cmd_short)
		free(pcmd.cmd_short);
	pcmd.pid=-1;
	pcmd.cmd_long=pcmd.cmd_short=NULL;
	if (pid==-1)
		return;
	pcmd.cmd_long=strdup(cmd_long);
	pcmd.cmd_short=strdup(cmd_short);
	if (pcmd.cmd_long&&pcmd.cmd_short)
		pcmd.pid=pid;
}

inline struct xxxid_stats *make_stats(pid_t tid,pid_t pid) {
	static const char unknown[]="<unknown>";
	struct xxxid_stats *s;
	char uid_str[32];
	int prio;

	if (!is_a_process(tid))
		return NULL;

	s=calloc(1,sizeof *s);
	if (!s)
		return NULL;
	s->hist=&hist_none; // own history is allocated when the first value is stored

	if (nl_xxxid_info(tid,pid,s))
		s->error_x=1;


	prio=get_ioprio(tid);
	if (prio==-1) {
		s->error_i=1;
		s->io_prio=0;
	} else
		s->io_prio=prio;

	if (tid!=pid&&pid==pcmd.pid)
		read_cmdlines(tid,pcmd.cmd_long,pcmd.cmd_short,&s->cmdline_long,&s->cmdline_short,&s->cmdline_comm);
	else {
		int res=read_cmdlines(tid,NULL,NULL,&s->cmdline_long,&s->cmdline_short,&s->cmdline_comm);

		// an exited main thread has empty cmdline while the rest of the threads still have it, so keep only a real one
		if (tid==pid)
			pcmd_set(res?pid:-1,s->cmdline_long,s->cmdline_short);
	}

	if (!s->cmdline_long)
		s->cmdline_long=strdup(unknown);
	if (!s->cmdline_short)
		s->cmdline_short=strdup(unknown);
	// cmdline_comm can be NULL
	if (config.f.uid2name==E_UID2NAME_NONE) {
		snprintf(uid_str,sizeof uid_str,"%4u",s->euid);
		s->pw_name=strdup(uid_str);
	} else if (config.f.uid2name==E_UID2NAME_CYCLIC)
		s->pw_name=strdup(get_user_name_cyclic(s->euid,unknown));
	else if (config.f.uid2name==E_UID2NAME_STATIC)
		s->pw_name=strdup(get_user_name_static(s->euid,unknown));
	else
		s->pw_name=strdup(get_user_name_pwuid(s->euid,unknown));

	if ((s->error_x||s->error_i||!s->cmdline_long||!s->cmdline_short)&&!is_a_process(tid)) { // process exited in the meantime
		free_stats(s);
		return NULL;
	}
	return s;
}

static void pid_cb(pid_t pid,pid_t tid,struct xxxid_stats_arr *a,filter_callback filter) {
	struct xxxid_stats *s=make_stats(tid,pid);

	if (s) {
		if (filter&&filter(s))
			free_stats(s);
		else {
			if (s->pid==s->tid) { // main process, copy own data to aggregated process data
				s->swapin_delay_total_p=s->swapin_delay_total;
				s->blkio_delay_total_p=s->blkio_delay_total;
				s->read_bytes_p=s->read_bytes;
				s->write_bytes_p=s->write_bytes;
			}
			if (arr_append(a,s)) // the list is sorted once at the end by arr_finalize
				free_stats(s);
		}
	}
}

// count of recently found main processes kept while attaching threads; must be a power of 2
#define PROC_CACHE_SZ 128

inline struct xxxid_stats_arr *fetch_data(filter_callback filter) {
	struct xxxid_stats *pcache[PROC_CACHE_SZ]={NULL}; // main processes, by pid
	struct xxxid_stats_arr *a=arr_alloc();
	int i;

	current_cycle++;

	if (!a)
		return NULL;

	pidgen_cb(pid_cb,a,filter);
	pcmd_set(-1,NULL,NULL); // valid only during the walk
	arr_finalize(a);

	for (i=0;a->arr&&i<a->length;i++) {
		struct xxxid_stats *s=a->arr[i];

		if (s->pid!=s->tid) { // maintain a thread list for each process
			struct xxxid_stats **pc=&pcache[s->pid&(PROC_CACHE_SZ-1)];
			struct xxxid_stats *p=*pc;

			if (!p||p->tid!=s->pid) { // the threads of a process mostly get close tids, so it is usually cached
				p=arr_find(a,s->pid); // main process' tid=thread's pid
				if (p)
					*pc=p;
			}
			if (p) {
				// aggregate thread data into the main process
				if (!p->threads)
					p->threads=arr_alloc();
				if (p->threads) {
					arr_add(p->threads,s);
					p->swapin_delay_total_p=mymax(p->swapin_delay_total_p,s->swapin_delay_total);
					p->blkio_delay_total_p=mymax(p->blkio_delay_total_p,s->blkio_delay_total);
					p->read_bytes_p+=s->read_bytes;
					p->write_bytes_p+=s->write_bytes;
				}
			}
		}
	}
	return a;
}

