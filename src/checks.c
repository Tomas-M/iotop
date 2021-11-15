/* SPDX-License-Identifer: GPL-2.0-or-later

Copyright (C) 2014  Vyacheslav Trushkin
Copyright (C) 2020,2021  Boian Bonev

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

*/

#include "iotop.h"

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <syscall.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <linux/capability.h>

inline int system_checks(void) {
	int vm_event_counters=0;
	int root_or_netadm=0;
	int io_accounting=0;
	struct stat s;
	uint64_t i,o;

	if (geteuid()==0)
		root_or_netadm=1;
	else {
		struct __user_cap_header_struct caphdr={.version=_LINUX_CAPABILITY_VERSION_3,.pid=0,};
		struct __user_cap_data_struct cap[_LINUX_CAPABILITY_U32S_3];

		if (!syscall(SYS_capget,&caphdr,cap))
			if (cap[CAP_TO_INDEX(CAP_NET_ADMIN)].effective&CAP_TO_MASK(CAP_NET_ADMIN))
				root_or_netadm=1;
	}
	if (!root_or_netadm) {
		printf(
			"The Linux kernel interfaces that iotop relies on now require root privileges\n"
			"or the NET_ADMIN capability. This change occurred because a security issue\n"
			"(CVE-2011-2494) was found that allows leakage of sensitive data across user\n"
			"boundaries. If you require the ability to run iotop as a non-root user, please\n"
			"configure sudo to allow you to run iotop as root.\n"
			"\n"
			"Alternatively to using sudo the NET_ADMIN capability can be set by\n"
			"\n"
			"\t$ sudo setcap 'cap_net_admin+eip' <path-to>/iotop\n"
			"\n"
			"Be warned that this will also allow other users to run it and get access to\n"
			"information that normally should not be available to them.\n"
			"\n"
			"Please do not file bugs on iotop about this.\n");

		return EACCES;
	}

	if (stat("/proc/self/io",&s))
		perror("Error in stat");
	else
		if (S_IFREG==(s.st_mode&S_IFMT))
			io_accounting=1;
	vm_event_counters=!get_vm_counters(&i,&o);
	if (!io_accounting||!vm_event_counters) {
		printf("Could not run iotop as some of the requirements are not met:\n");
		printf("- Linux >= 2.6.20 with\n");
		if (!io_accounting)
			printf("  - I/O accounting support (CONFIG_TASKSTATS, CONFIG_TASK_DELAY_ACCT, CONFIG_TASK_IO_ACCOUNTING)\n");
		if (!vm_event_counters)
			printf("  - VM event counters (CONFIG_VM_EVENT_COUNTERS)\n");
		return 1;
	}
	return 0;
}
