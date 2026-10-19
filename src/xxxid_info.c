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

// seq comes back in the reply and tells which request it belongs to
inline int send_cmd(int sock_fd,__u16 nlmsg_type,__u32 nlmsg_pid,__u32 seq,__u8 genl_cmd,__u16 nla_type,void *nla_data,int nla_len) {
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
	msg.n.nlmsg_seq=seq;
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
			if (errno!=EAGAIN&&errno!=EINTR)
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
	if (send_cmd(sock_fd,GENL_ID_CTRL,getpid(),0,CTRL_CMD_GETFAMILY,CTRL_ATTR_FAMILY_NAME,(void *)name,strlen(TASKSTATS_GENL_NAME)+1))
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

// one value that tells the exe of a task, to notice an exec between two cycles; it is only compared for equality
// the device and inode are folded together to keep the task data small: multiplying by 2^64 divided by the
// golden ratio (Fibonacci hashing) spreads the inode over all 64 bits, so a different exe almost never gives
// the same value
static inline uint64_t exe_id(uint64_t dev,uint64_t inode) {
	return dev^(inode*(uint64_t)0x9e3779b97f4a7c15);
}

// comm has room for TS_COMM_LEN+1 chars; it gets the name of a user task, kernel threads are left to /proc
// because there the name also tells the workqueue
inline int nl_xxxid_info(pid_t tid,pid_t pid,struct xxxid_stats *stats,char *comm) {
	static __u32 seq=0;

	if (nl_sock<0) {
		fprintf(stderr,"nl_xxxid_info: nl_sock is %d",nl_sock);
		exit(EXIT_FAILURE);
	}
	if (nl_fam_id==0) { // this will cause recv to wait forever
		fprintf(stderr,"nl_xxxid_info: nl_fam_id is 0");
		exit(EXIT_FAILURE);
	}

	*comm=0;
	seq++;
	if (send_cmd(nl_sock,nl_fam_id,tid,seq,TASKSTATS_CMD_GET,TASKSTATS_CMD_ATTR_PID,&tid,sizeof tid)) {
		fprintf(stderr,"get_xxxid_info: %s\n",strerror(errno));
		return -1;
	}

	stats->pid=pid;
	stats->tid=tid;

	struct msgtemplate msg;
	ssize_t rv;

	for (;;) {
		rv=recv(nl_sock,&msg,sizeof msg,0);
		if (rv<0&&errno==EINTR) // e.g. SIGWINCH of a terminal resize; the reply is still coming
			continue;
		if (rv<0) {
			fprintf(stderr,"get_xxxid_info: %s\n",strerror(errno));
			return -1;
		}
		if (!NLMSG_OK((&msg.n),(size_t)rv))
			return -1;
		if (msg.n.nlmsg_seq==seq)
			break;
		// a late reply to an earlier request that was given up; reading it as the reply of this one
		// would shift the data of all the following tasks
	}
	if (msg.n.nlmsg_type==NLMSG_ERROR) {
		struct nlmsgerr *err=NLMSG_DATA(&msg);

		// the error code is there only in a complete error message
		if ((size_t)rv>=NLMSG_LENGTH(sizeof *err)&&err->error!=-ESRCH)
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
						stats->read_char=t14->read_char;
						stats->write_char=t14->write_char;
						stats->swapin_delay_total=t14->swapin_delay_total;
						stats->blkio_delay_total=t14->blkio_delay_total;
						stats->euid=t14->ac_uid;
						stats->io_prio=ioprio_from_sched(t14->ac_sched,(int8_t)t14->ac_nice);
						if (ts->version>=12) { // the exe fields are there since v12, zero for kernel threads
							stats->btime=t14->ac_btime64;
							stats->exe=exe_id(t14->ac_exe_dev,t14->ac_exe_inode);
							if (t14->ac_exe_inode) {
								memcpy(comm,t14->ac_comm,TS_COMM_LEN);
								comm[TS_COMM_LEN]=0;
							}
						}
					} else { // exception for v15 only
						stats->read_bytes=t15->read_bytes;
						stats->write_bytes=t15->write_bytes;
						stats->read_char=t15->read_char;
						stats->write_char=t15->write_char;
						stats->swapin_delay_total=t15->swapin_delay_total;
						stats->blkio_delay_total=t15->blkio_delay_total;
						stats->euid=t15->ac_uid;
						stats->io_prio=ioprio_from_sched(t15->ac_sched,(int8_t)t15->ac_nice);
						stats->btime=t15->ac_btime64;
						stats->exe=exe_id(t15->ac_exe_dev,t15->ac_exe_inode);
						if (t15->ac_exe_inode) { // zero for kernel threads
							memcpy(comm,t15->ac_comm,TS_COMM_LEN);
							comm[TS_COMM_LEN]=0;
						}
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
	// pw_name is interned, not owned
	if (s->hist!=&hist_none) {
		if (s->hist->proc!=&hist_p_none)
			free(s->hist->proc);
		free(s->hist);
	}
	arr_free_noitem(s->threads);

	free(s);
}

static const char *get_user_name_pwuid(uid_t uid,const char *unknown) {
	struct passwd *pwd;

	pwd=getpwuid(uid);
	return pwd&&pwd->pw_name?pwd->pw_name:unknown;
}

static int pw_cmp(const void *a,const void *b) {
	return strcmp(a,b);
}

// user names are kept until exit and shared by the tasks and the uid cache; there are as many of them as distinct users
static const char *pw_intern(const char *name) {
	static void *pw_tree=NULL;
	char **f;
	char *s;

	f=tfind(name,&pw_tree,pw_cmp);
	if (f)
		return *f;
	s=strdup(name);
	if (!s)
		return NULL;
	if (!tsearch(s,&pw_tree,pw_cmp)) {
		free(s);
		return NULL;
	}
	return s;
}

struct uid_cache_entry {
	uid_t uid;
	const char *pw_name; // interned
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

static const char *get_user_name_ttl(uid_t uid,const char *unknown,int ttl_cycles) {
	struct uid_cache_entry key={.uid=uid,.pw_name=NULL};
	struct uid_cache_entry *e=NULL;
	void *node=tfind(&key,&uid_tree,uid_cache_cmp);
	const char *pw_name;

	if (node) {
		e=*(struct uid_cache_entry **)node;
		if (current_cycle<e->valid_until_cycle)
			return e->pw_name;
	}

	// a uid without user name is cached as unknown
	pw_name=pw_intern(get_user_name_pwuid(uid,unknown));
	if (!pw_name)
		return unknown;

	if (!e) {
		e=calloc(1,sizeof *e);
		if (!e)
			return pw_name;
		e->uid=uid;
		if (!tsearch(e,&uid_tree,uid_cache_cmp)) {
			free(e);
			return pw_name;
		}
	}

	e->pw_name=pw_name;
	e->valid_until_cycle=ttl_cycles?(current_cycle+ttl_cycles):UINT64_MAX;
	return e->pw_name;
}

static const char *get_user_name_cyclic(uid_t uid,const char *unknown) {
	return get_user_name_ttl(uid,unknown,1);
}

static const char *get_user_name_static(uid_t uid,const char *unknown) {
	return get_user_name_ttl(uid,unknown,0);
}

// the names stay interned, the tasks still point to them
void clear_uid_cache(void) {
	if (uid_tree) {
		tdestroy(uid_tree,free);
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

// a reused cmdline is read again after this many cycles; setproctitle changes it without exec
#define CMDLINE_REREAD 30

static struct xxxid_stats_arr *cmdline_src=NULL; // the previous cycle during fetch_data, cmdlines are reused from it
static int cmdline_force=0; // set by cmdline_reread: the next cycle reads all cmdlines instead of reusing them

// make the next cycle read all cmdlines
void cmdline_reread(void) {
	cmdline_force=1;
}

// the item of the previous cycle to take the cmdline of main process s from, NULL to read it
static struct xxxid_stats *cmdline_prev(struct xxxid_stats *s,const char *comm) {
	struct xxxid_stats *o;

	if (!cmdline_src||cmdline_force||!*comm||!s->btime) // comm is known only for user tasks with taskstats v12+
		return NULL;
	if (((uint64_t)s->tid+current_cycle)%CMDLINE_REREAD==0) // staggered, so each cycle reads a part of them
		return NULL;
	o=arr_find(cmdline_src,s->tid);
	if (!o||o->exited||o->exe!=s->exe) // another process or another exe
		return NULL;
	// the kernel gives the start time as the current second minus the elapsed seconds, both rounded down,
	// so for the same task it may differ by one between two cycles
	if (o->btime+1<s->btime||s->btime+1<o->btime) // another process with the same tid
		return NULL;
	if (!o->cmdline_long||!o->cmdline_short)
		return NULL;
	if (strcmp(comm,o->cmdline_comm?o->cmdline_comm:o->cmdline_short)) // renamed, maybe by exec of the same exe
		return NULL;
	return o;
}

// copy the cmdline that is already known to s; returns 1 if both copies are made
static int cmdline_copy(struct xxxid_stats *s,const char *cmd_long,const char *cmd_short) {
	s->cmdline_long=strdup(cmd_long);
	s->cmdline_short=strdup(cmd_short);
	return s->cmdline_long&&s->cmdline_short;
}

inline struct xxxid_stats *make_stats(pid_t tid,pid_t pid) {
	static const char unknown[]="<unknown>";
	struct xxxid_stats *s;
	char comm[TS_COMM_LEN+1];
	char uid_str[32];
	int prio;

	// a task that is already gone fails the queries below and is checked for at the end
	s=calloc(1,sizeof *s);
	if (!s)
		return NULL;
	s->hist=&hist_none; // own history is allocated when the first value is stored
	s->io_prio=-1; // nl_xxxid_info stores here the io priority derived from the scheduling policy and nice value

	if (nl_xxxid_info(tid,pid,s,comm))
		s->error_x=1;

	prio=get_ioprio(tid,s->error_x?-1:s->io_prio);
	if (prio==-1) {
		s->error_i=1;
		s->io_prio=0;
	} else
		s->io_prio=prio;

	if (tid!=pid&&pid==pcmd.pid) // all threads of a process have the same cmdline
		cmdline_copy(s,pcmd.cmd_long,pcmd.cmd_short);
	else {
		struct xxxid_stats *o=tid==pid?cmdline_prev(s,comm):NULL;
		int res=0;

		if (o)
			res=cmdline_copy(s,o->cmdline_long,o->cmdline_short);
		else if (!(s->btime&&!*comm)) // a kernel thread has no cmdline; taskstats tells it since v12
			res=read_cmdline(tid,&s->cmdline_long,&s->cmdline_short);

		// an exited main thread has empty cmdline while the rest of the threads still have it, so keep only a real one
		if (tid==pid)
			pcmd_set(res?pid:-1,s->cmdline_long,s->cmdline_short);
	}
	read_comm(tid,comm,&s->cmdline_long,&s->cmdline_short,&s->cmdline_comm);

	if (!s->cmdline_long)
		s->cmdline_long=strdup(unknown);
	if (!s->cmdline_short)
		s->cmdline_short=strdup(unknown);
	// cmdline_comm can be NULL
	if (config.f.uid2name==E_UID2NAME_NONE) {
		snprintf(uid_str,sizeof uid_str,"%4u",s->euid);
		s->pw_name=pw_intern(uid_str);
	} else if (config.f.uid2name==E_UID2NAME_CYCLIC)
		s->pw_name=get_user_name_cyclic(s->euid,unknown);
	else if (config.f.uid2name==E_UID2NAME_STATIC)
		s->pw_name=get_user_name_static(s->euid,unknown);
	else
		s->pw_name=pw_intern(get_user_name_pwuid(s->euid,unknown));
	if (!s->pw_name) // pw_name is never freed, a static string is fine
		s->pw_name=unknown;

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
				s->read_char_p=s->read_char;
				s->write_char_p=s->write_char;
			}
			if (arr_append(a,s)) // the list is sorted once at the end by arr_finalize
				free_stats(s);
		}
	}
}

// count of recently found main processes kept while attaching threads; must be a power of 2
#define PROC_CACHE_SZ 128

// prev is the previous cycle, NULL if there is none
inline struct xxxid_stats_arr *fetch_data(filter_callback filter,struct xxxid_stats_arr *prev) {
	struct xxxid_stats *pcache[PROC_CACHE_SZ]={NULL}; // main processes, by pid
	struct xxxid_stats_arr *a=arr_alloc();
	int i;

	current_cycle++;

	if (!a)
		return NULL;

	cmdline_src=prev;
	pidgen_cb(pid_cb,a,filter);
	pcmd_set(-1,NULL,NULL); // valid only during the walk
	cmdline_src=NULL;
	cmdline_force=0;
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
					p->read_char_p+=s->read_char;
					p->write_char_p+=s->write_char;
				}
			}
		}
	}
	return a;
}

