/* SPDX-License-Identifier: GPL-2.0-or-later

Copyright (C) 2014  Vyacheslav Trushkin
Copyright (C) 2020-2026  Boian Bonev

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

*/

#include "iotop.h"

#include <sched.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/resource.h>

#define IOPRIO_CLASS_SHIFT 13
#define IOPRIO_STR_MAXSIZ  10
#define IOPRIO_STR_FORMAT "%2s/%1i"

const char *str_ioprio_class[]={"-","rt","be","id"};

// io priority of a task without own one, derived from its scheduling policy and nice value
inline int ioprio_from_sched(int policy,int nice) {
	int ioprio_nice=(nice+20)/5;

	if (policy==SCHED_FIFO||policy==SCHED_RR)
		return (IOPRIO_CLASS_RT<<IOPRIO_CLASS_SHIFT)+ioprio_nice;
	if (policy==SCHED_IDLE)
		return IOPRIO_CLASS_IDLE<<IOPRIO_CLASS_SHIFT;
	return (IOPRIO_CLASS_BE<<IOPRIO_CLASS_SHIFT)+ioprio_nice;
}

// sched_prio is ioprio_from_sched of pid when already known, -1 to query it
inline int get_ioprio(pid_t pid,int sched_prio) {
	int io_prio,io_class;

	io_prio=syscall(SYS_ioprio_get,IOPRIO_WHO_PROCESS,pid);
	if (io_prio==-1)
		return -1;
	io_class=io_prio>>IOPRIO_CLASS_SHIFT;
	if (!io_class)
		return sched_prio!=-1?sched_prio:ioprio_from_sched(sched_getscheduler(pid),getpriority(PRIO_PROCESS,pid));
	return io_prio;
}

inline int ioprio2class(int ioprio) {
	return ioprio>>IOPRIO_CLASS_SHIFT;
}

inline int ioprio2prio(int ioprio) {
	return ioprio&((1<<IOPRIO_CLASS_SHIFT)-1);
}

inline const char *str_ioprio(int io_prio) {
	static const char corrupted[]="xx/x";
	static char buf[IOPRIO_STR_MAXSIZ];
	int io_class=io_prio>>IOPRIO_CLASS_SHIFT;

	io_prio&=((1<<IOPRIO_CLASS_SHIFT)-1);

	if (io_class>=IOPRIO_CLASS_MAX)
		return corrupted;

	snprintf(buf,sizeof buf,IOPRIO_STR_FORMAT,str_ioprio_class[io_class],io_prio);

	return (const char *)buf;
}

inline int ioprio_value(int class,int prio) {
	return (class<<IOPRIO_CLASS_SHIFT)|prio;
}

inline int set_ioprio(int which,int who,int ioprio_class,int ioprio_prio) {
	return syscall(SYS_ioprio_set,which,who,ioprio_value(ioprio_class,ioprio_prio));
}

