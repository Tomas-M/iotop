/* SPDX-License-Identifer: GPL-2.0-or-later

Copyright (C) 2014  Vyacheslav Trushkin
Copyright (C) 2020,2021  Boian Bonev

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

*/

/*

iotop can only run on Linux
libc may vary, check if old glibc
require -lrt for clock_gettime

*/

#include <stdio.h>
#ifdef __GLIBC__
#include <gnu/libc-version.h>
int min=__GLIBC__,maj=__GLIBC_MINOR__;
#if __GLIBC__>2
char *IOTOP_NEED_LRT="no";
#else // __GLIBC__>2
#if __GLIBC__<2
char *IOTOP_NEED_LRT="yes";
#else // __GLIBC__<2
#if __GLIBC__==2
#if __GLIBC_MINOR__<=17
char *IOTOP_NEED_LRT="yes";
#else // __GLIBC_MINOR__<=17
char *IOTOP_NEED_LRT="no";
#endif // __GLIBC_MINOR__<=17
#else // __GLIBC__==2
char *IOTOP_NEED_LRT="no";
#endif // __GLIBC__==2
#endif // __GLIBC__<2
#endif // __GLIBC__>2
#else // def __GLIBC__
char *IOTOP_NEED_LRT="no";
#endif // def __GLIBC__
