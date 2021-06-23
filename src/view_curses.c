/* SPDX-License-Identifer: GPL-2.0-or-later

Copyright (C) 2014  Vyacheslav Trushkin
Copyright (C) 2020,2021  Boian Bonev

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

*/

#include "iotop.h"

// allow ncurses printf-like arguments checking
#define GCC_PRINTF

#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curses.h>
#include <locale.h>
#include <langinfo.h>
#include <sys/types.h>

#define HEADER_XXS_FORMAT "%4.0f%s%s/%4.0f%s%s|%4.0f%s%s/%4.0f%s%s"
#define HEADER_XS_FORMAT "TR:%4.0f%s%sW:%4.0f%s%s|CR:%4.0f%s%sW:%4.0f%s%s"
#define HEADER_S_FORMAT "T R:%7.2f%s%s W:%7.2f%s%s|C R:%7.2f%s%s W:%7.2f%s%s"
#define HEADER_M_FORMAT "T Read:%7.2f%s%s Write:%7.2f%s%s|C Read:%7.2f%s%s Write:%7.2f%s%s"
#define HEADER_L_FORMAT "Total Read:%7.2f %s%s Write:%7.2f %s%s|Current Read:%7.2f %s%s Write:%7.2f %s%s"
#define HEADER_XL_FORMAT "Total Read: %7.2f %s%s Write: %7.2f %s%s | Current Read: %7.2f %s%s Write: %7.2f %s%s"

static int in_ionice=0; // ionice interface flag and state vars
static char ionice_id[50];
static int ionice_pos=-1;
static int ionice_cl=1; // select what to edit class(1) or prio(0) [digit pid enter mode only]
static int ionice_col=0; // select what to edit pid/tid(0), class(2) or prio(1) [arrow mode only]
static int ionice_class=IOPRIO_CLASS_RT;
static int ionice_prio=0;
static int ionice_id_changed=0;
static int ionice_line=1;
static int in_filter=0; // filter by pid/uid interface flag and state vars
static char filter_uid[50];
static char filter_pid[50];
static int filter_col=0; // select what to edit uid(0), pid(1)
static struct xxxid_stats *ionice_pos_data=NULL;
static int has_unicode=0;
static int unicode=1; // enable unicode (only valid if unicode is available)
static double hist_t_r[HISTORY_CNT]={0};
static double hist_t_w[HISTORY_CNT]={0};
static double hist_a_r[HISTORY_CNT]={0};
static double hist_a_w[HISTORY_CNT]={0};
static int scrollpos=0; // scroll view start position
static int viewsizey=0; // how many lines we can show on screen
static int dispcount=0; // how many lines we have after filters
static int lastvisible=0; // last visible screen line
static int showhelp=0; // flag if help window is shown
static WINDOW *whelp; // pop-up help window
static int hx=1,hy=1,hw=2+2+3,hh=2; // help window size and position
static size_t c1w=0,c2w=0,c3w=0,cdw=0; // help window column widths
static int dontrefresh=0; // flag to inhibit refresh of data

typedef struct {
	const char *descr;
	const char *k1;
	const char *k2;
	const char *k3;
} s_helpitem;

const s_helpitem thelp[]={
	{.descr="Exit",.k2="q",.k3="Q"},
	{.descr="Toggle sort order",.k1="<space>",.k2="r",.k3="R"},
	{.descr="Scroll to the top of the list",.k1="<home>"},
	{.descr="Scroll to the bottom of the list",.k1="<end>"},
	{.descr="Scroll one screen up",.k1="<page-up>"},
	{.descr="Scroll one screen down",.k1="<page-down>"},
	{.descr="Scroll one line up",.k1="<up>"},
	{.descr="Scroll one line down",.k1="<down>"},
	{.descr="Sort by next column",.k1="<right>"},
	{.descr="Sort by previous column",.k1="<left>"},
	{.descr="Cancel ionice/filter or close help window",.k1="<esc>"},
	{.descr="Toggle showing only processes with IO activity",.k2="o",.k3="O"},
	{.descr="Toggle showing processes/threads",.k2="p",.k3="P"},
	{.descr="Toggle showing accumulated/current values",.k2="a",.k3="A"},
	{.descr="Toggle showing this help",.k1="          ?",.k2="h",.k3="H"}, // padded to match <page-down>
	{.descr="Toggle showing full command line",.k2="c",.k3="C"},
	{.descr="Toggle showing TID",.k2="1"},
	{.descr="Toggle showing PRIO",.k2="2"},
	{.descr="Toggle showing USER",.k2="3"},
	{.descr="Toggle showing DISK READ",.k2="4"},
	{.descr="Toggle showing DISK WRITE",.k2="5"},
	{.descr="Toggle showing SWAPIN",.k2="6"},
	{.descr="Toggle showing IO",.k2="7"},
	{.descr="Toggle showing GRAPH",.k2="8"},
	{.descr="Toggle showing COMMAND",.k2="9"},
	{.descr="Show all columns",.k2="0"},
	{.descr="IOnice a process/thread",.k2="i",.k3="I"},
	{.descr="Change UID and PID filters",.k2="f",.k3="F"},
	{.descr="Toggle using Unicode/ASCII characters",.k2="u",.k3="U"},
	{.descr="Toggle exited processes x/inverse",.k2="x",.k3="X"},
	{.descr="Toggle data freeze",.k2="s",.k3="S"},
	{.descr=NULL},
};

static const char *column_name[]={
	"TID",
	" PRIO",
	"USER",
	"DISK READ",
	"DISK WRITE",
	"SWAPIN",
	"IO",
	"GRAPH",
	"COMMAND",
};

// Braille unicode pseudo graph - 2x5 levels graph per character
static const char *br_graph[5][5]={
	{"⠀","⢀","⢠","⢰","⢸",},
	{"⡀","⣀","⣠","⣰","⣸",},
	{"⡄","⣄","⣤","⣴","⣼",},
	{"⡆","⣆","⣦","⣶","⣾",},
	{"⡇","⣇","⣧","⣷","⣿",},
};

// ASCII pseudo graph - 1x5 levels graph per character
static const char *as_graph[5]={" ","_",".",":","|",};

// process and threads grouping characters
static const char *th_lines_u[8]={" ","►","┌","│","└","╭","┊","╰",};
static const char *th_lines_a[8]={" ",">",",","|","`",",",":","`",};

// sort order characters
static const char *sort_dir_u[3]={" ","△","▽",};
static const char *sort_dir_a[3]={" ","<",">",};

// vertical scroller characters
static const char *scroll_u[12]={" ","▲","▼","⬍","▁","▂","▃","▄","▅","▆","▇","█",};
static const char *scroll_a[4]={" ","^","v",":",};

static const int column_width[]={
	0,  // TID
	6,  // PRIO
	10, // USER
	12, // READ
	12, // WRITE
	9,  // SWAPIN
	9,  // IO
	0,  // GRAPH
	0,  // COMMAND
};

#define __COLUMN_NAME(i) (column_name[(i)])
#define __SAFE_INDEX(i) ((((i)%SORT_BY_MAX)+SORT_BY_MAX)%SORT_BY_MAX)
#define COLUMN_NAME(i) __COLUMN_NAME(__SAFE_INDEX(i))
#define COLUMN_L(i) COLUMN_NAME((i)-1)
#define COLUMN_R(i) COLUMN_NAME((i)+1)
#define SORT_CHAR_IND(x) ((config.f.sort_by==x)?(config.f.sort_order==SORT_ASC?1:2):0)
#define SORT_CHAR(x) (((has_unicode&&unicode)?sort_dir_u:sort_dir_a)[SORT_CHAR_IND(x)])

#define TIMEDIFF_IN_S(sta,end) ((((sta)==(end))||(sta)==0)?0.0001:(((end)-(sta))/1000.0))

static inline int filter_view(struct xxxid_stats *s,int gr_width) {
	static const uint8_t iohist_z[HISTORY_CNT]={0};

	// apply uid/pid filter
	if (filter1(s))
		return 1;
	// visible history is non-zero
	if (config.f.only) {
		if (!config.f.hidegraph&&!memcmp(s->iohist,iohist_z,gr_width))
			return 1;
		if (config.f.hidegraph&&s->blkio_val<=0)
			return 1;
	}
	if (config.f.processes&&s->tid!=s->pid)
		return 1;
	if (config.f.hidegraph) {
		if (s->exited>=3)
			return 2;
	} else {
		if (s->exited>=gr_width)
			return 2;
	}

	return 0;
}

static inline void draw_vscroll(int xpos,int from,int to,int items,int pos) {
	if (!items) // avoid div by 0
		items++;

	attron(A_REVERSE);
	if (from==to) {
		if (unicode&&has_unicode) {
			mvprintw(from,xpos,"%s",scroll_u[3]);
		} else {
			attron(A_REVERSE);
			mvprintw(from,xpos,"%s",scroll_a[3]);
			attroff(A_REVERSE);
		}
	} else {
		int i;
		int visible=to-from+1; // count of visible items
		int linecnt=visible-2; // count of lines usable by scroller
		int drscale=(unicode&&has_unicode)?8:1; // draw scale
		int poitems=(items>visible)?items-visible:1; // positionable items
		int scrols0=(drscale*linecnt*visible)/poitems; // scroller size (may be smaller than scale, even 0)
		int scrolsz=(scrols0<drscale)?drscale:scrols0; // scroller size, not less than scale
		int begpos=(pos*(linecnt*drscale-scrolsz))/poitems+(from+1)*drscale;
		int endpos=begpos+scrolsz;

		for (i=from;i<=to;i++) {
			if (i==from||i==to) {
				attron(A_REVERSE);
				mvprintw(i,xpos,"%s",(unicode&&has_unicode)?scroll_u[i==from?1:2]:scroll_a[i==from?1:2]);
				attroff(A_REVERSE);
			}
			if (i!=from&&i!=to) {
				if (unicode&&has_unicode) {
					if (items<=to-from+1)
						mvprintw(i,xpos,"%s",scroll_u[11]);
					else {
						if (i<begpos/8||endpos/8<i)
							mvprintw(i,xpos,"%s",scroll_u[0]);
						if (i==begpos/8)
							mvprintw(i,xpos,"%s",scroll_u[4+7-begpos%8]);
						if (i==endpos/8&&i!=begpos/8) {
							attron(A_REVERSE);
							mvprintw(i,xpos,"%s",scroll_u[4+7-endpos%8]);
							attroff(A_REVERSE);
						}
						if (begpos/8<i&&i<endpos/8)
							mvprintw(i,xpos,"%s",scroll_u[11]);
					}
				} else {
					if (items<=to-from+1) {
						attron(A_REVERSE);
						mvprintw(i,xpos,"%s",scroll_a[0]);
						attroff(A_REVERSE);
					} else {
						if (i<begpos||endpos<i)
							mvprintw(i,xpos,"%s",scroll_a[0]);
						else {
							attron(A_REVERSE);
							mvprintw(i,xpos,"%s",scroll_a[0]);
							attroff(A_REVERSE);
						}
					}
				}
			}
		}
	}
}

static inline void view_help(void) {
	int i,a=c1w,b=c2w,c=c3w,d=cdw;
	const s_helpitem *p;

	mvwprintw(whelp,0,0,"%s",(has_unicode&&unicode)?"─":"_");
	wattron(whelp,A_REVERSE);
	wprintw(whelp," help ");
	wattroff(whelp,A_REVERSE);
	for (i=1+strlen(" help ");i<hw;i++)
		wprintw(whelp,"%s",(has_unicode&&unicode)?"─":"_");
	for (p=thelp,i=1;i<hh-1;i++,p++)
		mvwprintw(whelp,i,0," %-*.*s %-*.*s %-*.*s - %-*.*s ",a,a,p->k1?p->k1:"",b,b,p->k2?p->k2:"",c,c,p->k3?p->k3:"",d,d,p->descr);
	mvwprintw(whelp,hh-1,0,"%s",(has_unicode&&unicode)?"─":"_");
	for (i=1;i<hw;i++)
		wprintw(whelp,"%s",(has_unicode&&unicode)?"─":"_");
}

static inline void view_curses(struct xxxid_stats_arr *cs,struct xxxid_stats_arr *ps,struct act_stats *act,int roll) {
	double time_s=TIMEDIFF_IN_S(act->ts_o,act->ts_c);
	double total_read,total_write;
	double total_a_read,total_a_write;
	char pg_t_r[HISTORY_POS*5]={0};
	char pg_t_w[HISTORY_POS*5]={0};
	char pg_a_r[HISTORY_POS*5]={0};
	char pg_a_w[HISTORY_POS*5]={0};
	char str_read[4],str_write[4];
	char str_a_read[4],str_a_write[4];
	char *head1row_format="";
	int promptx=0,prompty=0,show;
	double mx_t_r=1000.0;
	double mx_t_w=1000.0;
	double mx_a_r=1000.0;
	double mx_a_w=1000.0;
	int line,lastline;
	int shrink_dm=0;
	int head1row=0;
	int maxcmdline;
	int gr_width_h;
	int gr_width;
	int diff_len;
	int saveskip;
	int i,j,k;
	int maxy;
	int maxx;
	int skip;

	ionice_pos_data=NULL;

	maxy=getmaxy(stdscr);
	maxx=getmaxx(stdscr);

	maxcmdline=maxx;
	if (!config.f.hidepid)
		maxcmdline-=maxpidlen+2;
	if (!config.f.hideprio)
		maxcmdline-=column_width[SORT_BY_PRIO];
	if (!config.f.hideuser)
		maxcmdline-=column_width[SORT_BY_USER];
	if (!config.f.hideread)
		maxcmdline-=column_width[SORT_BY_READ];
	if (!config.f.hidewrite)
		maxcmdline-=column_width[SORT_BY_WRITE];
	if (!config.f.hideswapin)
		maxcmdline-=column_width[SORT_BY_SWAPIN];
	if (!config.f.hideio)
		maxcmdline-=column_width[SORT_BY_IO];
	gr_width=maxcmdline/4;
	if (gr_width<5)
		gr_width=5;
	if (gr_width>HISTORY_POS)
		gr_width=HISTORY_POS;
	if (!config.f.hidegraph)
		maxcmdline-=gr_width+1;
	if (maxcmdline<0)
		maxcmdline=0;

	diff_len=create_diff(cs,ps,time_s,filter_view,(has_unicode&&unicode)?gr_width*2:gr_width,&dispcount);

	calc_total(cs,&total_read,&total_write);
	calc_a_total(act,&total_a_read,&total_a_write,time_s);

	if (roll) {
		memmove(hist_t_r+1,hist_t_r,sizeof hist_t_r-sizeof *hist_t_r);
		memmove(hist_t_w+1,hist_t_w,sizeof hist_t_w-sizeof *hist_t_w);
		memmove(hist_a_r+1,hist_a_r,sizeof hist_a_r-sizeof *hist_a_r);
		memmove(hist_a_w+1,hist_a_w,sizeof hist_a_w-sizeof *hist_a_w);
		hist_t_r[0]=total_read;
		hist_t_w[0]=total_write;
		hist_a_r[0]=total_a_read;
		hist_a_w[0]=total_a_write;
	}

	humanize_val(&total_read,str_read,1);
	humanize_val(&total_write,str_write,1);
	humanize_val(&total_a_read,str_a_read,0);
	humanize_val(&total_a_write,str_a_write,0);

	gr_width_h=gr_width;
	if (maxy<10||maxx<(int)strlen(HEADER1_FORMAT)+2*(7-5+3-2+(!config.f.hidegraph?gr_width_h+1:0)-2)) {
		int size_off;

		head1row=1;

		gr_width_h/=2;
		if (gr_width_h<3)
			gr_width_h=3;

		size_off=7-5+3-2+(!config.f.hidegraph?gr_width_h+1:0)-2;

		if (maxx>=(int)strlen(HEADER_XL_FORMAT)+4*size_off)
			head1row_format=HEADER_XL_FORMAT;
		else
			if (maxx>=(int)strlen(HEADER_L_FORMAT)+4*size_off)
				head1row_format=HEADER_L_FORMAT;
			else
				if (maxx>=(int)strlen(HEADER_M_FORMAT)+4*size_off)
					head1row_format=HEADER_M_FORMAT;
				else
					if (maxx>=(int)strlen(HEADER_S_FORMAT)+4*(size_off-2)) {
						head1row_format=HEADER_S_FORMAT;
						shrink_dm=1;
						size_off-=2;
					} else
						if (maxx>=(int)strlen(HEADER_XS_FORMAT)+4*(size_off-5)) {
							head1row_format=HEADER_XS_FORMAT;
							shrink_dm=1;
							size_off-=5;
						} else {
							head1row_format=HEADER_XXS_FORMAT;
							shrink_dm=1;
							size_off-=5;
						}
		if (!config.f.hidegraph)
			while (gr_width_h<gr_width&&maxx>=(int)strlen(head1row_format)+4*(size_off+1)) {
				size_off++;
				gr_width_h++;
			}
	}

	for (i=0;i<((has_unicode&&unicode)?gr_width_h*2:gr_width_h);i++) {
		if (mx_t_r<hist_t_r[i])
			mx_t_r=hist_t_r[i];
		if (mx_t_w<hist_t_w[i])
			mx_t_w=hist_t_w[i];
		if (mx_a_r<hist_a_r[i])
			mx_a_r=hist_a_r[i];
		if (mx_a_w<hist_a_w[i])
			mx_a_w=hist_a_w[i];
	}
	strcpy(pg_t_r," ");
	strcpy(pg_t_w," ");
	strcpy(pg_a_r," ");
	strcpy(pg_a_w," ");
	for (j=0;j<gr_width_h;j++) {
		if (has_unicode&&unicode) {
			strcat(pg_t_r,br_graph[value2scale(hist_t_r[j*2],mx_t_r)][value2scale(hist_t_r[j*2+1],mx_t_r)]);
			strcat(pg_t_w,br_graph[value2scale(hist_t_w[j*2],mx_t_w)][value2scale(hist_t_w[j*2+1],mx_t_w)]);
			strcat(pg_a_r,br_graph[value2scale(hist_a_r[j*2],mx_a_r)][value2scale(hist_a_r[j*2+1],mx_a_r)]);
			strcat(pg_a_w,br_graph[value2scale(hist_a_w[j*2],mx_a_w)][value2scale(hist_a_w[j*2+1],mx_a_w)]);
		} else {
			strcat(pg_t_r,as_graph[value2scale(hist_t_r[j],mx_t_r)]);
			strcat(pg_t_w,as_graph[value2scale(hist_t_w[j],mx_t_w)]);
			strcat(pg_a_r,as_graph[value2scale(hist_a_r[j],mx_a_r)]);
			strcat(pg_a_w,as_graph[value2scale(hist_a_w[j],mx_a_w)]);
		}
	}

	ionice_line=1;
	if (head1row) {
		ionice_line=0;
		if (!in_ionice&&!in_filter) {
			if (shrink_dm) {
				str_read[1]=0;
				str_write[1]=0;
				str_a_read[1]=0;
				str_a_write[1]=0;
			}
			mvhline(0,0,' ',maxx);
			mvprintw(0,0,head1row_format,total_read,str_read,!config.f.hidegraph?pg_t_r:"",total_write,str_write,!config.f.hidegraph?pg_t_w:"",total_a_read,str_a_read,!config.f.hidegraph?pg_a_r:"",total_a_write,str_a_write,!config.f.hidegraph?pg_a_w:"");
			show=FALSE;
		}
	} else {
		mvhline(0,0,' ',maxx);
		mvprintw(0,0,HEADER1_FORMAT,total_read,str_read,!config.f.hidegraph?pg_t_r:"",total_write,str_write,!config.f.hidegraph?pg_t_w:"");

		if (!in_ionice&&!in_filter) {
			mvhline(1,0,' ',maxx);
			mvprintw(1,0,HEADER2_FORMAT,total_a_read,str_a_read,!config.f.hidegraph?pg_a_r:"",total_a_write,str_a_write,!config.f.hidegraph?pg_a_w:"");
			show=FALSE;
		}
	}

	attron(A_REVERSE);
	mvhline(ionice_line+1,0,' ',maxx);
	move(ionice_line+1,0);

	for (i=0;i<SORT_BY_MAX;i++) {
		int wt,wi=column_width[i];
		char t[50];
		char *ts;

		if (i==SORT_BY_PID)
			wi=maxpidlen+2;
		if (i==SORT_BY_GRAPH)
			wi=gr_width+1;
		if (i==SORT_BY_COMMAND)
			wi=maxcmdline;

		if (config.opts[&config.f.hidepid-config.opts+i])
			continue;

		wt=strlen(COLUMN_NAME(i));
		if (wt>wi-1)
			wt=wi-1;
		if (config.f.sort_by==i)
			attron(A_BOLD);
		snprintf(t,sizeof t,"%-*.*s%s",wt,wt,COLUMN_NAME(i),SORT_CHAR(i));
		ts=u8strpadt(t,wi);
		if (ts) {
			printw("%s",ts);
			free(ts);
		} else
			printw("%-*.*s",wi,wi,t);
		if (config.f.sort_by==i)
			attroff(A_BOLD);
	}
	attroff(A_REVERSE);

	if (dontrefresh&&(maxx-maxcmdline+(config.f.hidecmd?0:strlen(COLUMN_L(0))+1)<(size_t)maxx)) {
		size_t xpos=maxx-strlen("[freezed]");

		// don't step on column descriptions
		if (xpos<maxx-maxcmdline+(config.f.hidecmd?0:strlen(COLUMN_L(0))+1))
			xpos=maxx-maxcmdline+(config.f.hidecmd?0:strlen(COLUMN_L(0))+1);
		mvprintw(ionice_line+1,xpos,"[freezed]");
	}
	// easiest place to print debug info
	//mvprintw(ionice_line+1,maxx-maxcmdline+strlen(COLUMN_L(0))+1," ... ",...);

	maxcmdline--; // vertical scroller

	iotop_sort_cb(NULL,(void *)(long)((has_unicode&&unicode)?gr_width*2:gr_width));
	arr_sort(cs,iotop_sort_cb);

	line=ionice_line+2;
	lastline=line;
	viewsizey=maxy-1-ionice_line;
	if (viewsizey<0)
		viewsizey=0;
	skip=scrollpos;
	if (skip>dispcount-viewsizey)
		skip=dispcount-viewsizey;
	if (skip<0)
		skip=0;
	saveskip=skip;
	if (ionice_pos!=-1) { // have some selected position
		if (ionice_pos<ionice_line+2)
			ionice_pos=ionice_line+2;
		if (ionice_pos>=lastvisible&&lastvisible>ionice_line+2)
			ionice_pos=lastvisible-1;
	}
	for (i=0;cs->sor&&i<diff_len;i++) {
		int th_prio_diff,th_first,th_have_filtered,th_first_id,th_last_id;
		struct xxxid_stats *ms=cs->sor[i],*s;
		char read_str[4],write_str[4];
		char iohist[HISTORY_POS*5];
		double read_val,write_val;
		char *pw_name,*cmdline;
		char *pwt,*cmdt;
		int hrevpos;

		// always start showing from processes, threads are kept on the main list for easier search
		if (ms->pid!=ms->tid)
			continue;
		if (ms->threads)
			arr_sort(ms->threads,iotop_sort_cb);
		// check if threads use the same prio as the main process
		// scan for hidden threads
		th_first=1;
		th_prio_diff=0;
		th_have_filtered=0;
		th_first_id=th_last_id=-2;
		for (k=-1;ms->threads&&ms->threads->arr&&k<ms->threads->length;k++) {
			if (k<0)
				s=ms;
			else {
				if (!ms->threads||!ms->threads->sor)
					break;
				s=ms->threads->sor[k];
				if (ms->io_prio!=s->io_prio) {
					th_prio_diff=1;
					if (config.f.processes)
						break;
				}
			}
			if (!config.f.processes) {
				int fres=filter_view(s,(has_unicode&&unicode)?gr_width*2:gr_width);

				if (fres==2) // exited process that is no longer visible; do not count as filtered
					continue;
				if (fres) {
					th_have_filtered=1;
					continue;
				}
				if (th_first) {
					th_first=0;
					th_first_id=k;
				}
				th_last_id=k;
			}
		}
		// show only processes, if configured
		for (k=-1;k<(config.f.processes?0:(ms->threads?ms->threads->length:0));k++) {
			if (k<0)
				s=ms;
			else {
				if (!ms->threads||!ms->threads->sor)
					break;
				s=ms->threads->sor[k];
			}
			// apply filters
			if (filter_view(s,(has_unicode&&unicode)?gr_width*2:gr_width))
				continue;
			if (skip) {
				skip--;
				continue;
			}

			read_val=config.f.accumulated?s->read_val_acc:s->read_val;
			write_val=config.f.accumulated?s->write_val_acc:s->write_val;

			humanize_val(&read_val,read_str,1);
			humanize_val(&write_val,write_str,1);

			pwt=esc_low_ascii(s->pw_name);
			pw_name=u8strpadt(pwt,9);
			if (pwt)
				free(pwt);
			cmdt=esc_low_ascii(config.f.fullcmdline?s->cmdline2:s->cmdline1);
			cmdline=u8strpadt(cmdt,maxcmdline-1); // -1 for thread/process link chars
			if (cmdt)
				free(cmdt);

			hrevpos=-1;
			if (!config.f.hidegraph) {
				*iohist=0;
				for (j=0;j<gr_width;j++) {
					if (config.f.deadx) {
						// +1 avoids stepping on a char with one valid and one invalid value
						if (((has_unicode&&unicode)?j*2+1:j)<s->exited)
							strcat(iohist,"x");
						else {
							if (has_unicode&&unicode)
								strcat(iohist,br_graph[s->iohist[j*2]][s->iohist[j*2+1]]);
							else
								strcat(iohist,as_graph[s->iohist[j]]);
						}
					} else {
						// stepping on a char with one valid and one invalid value is not a problem with background
						if (has_unicode&&unicode)
							strcat(iohist,br_graph[s->iohist[j*2]][s->iohist[j*2+1]]);
						else
							strcat(iohist,as_graph[s->iohist[j]]);
						if (((has_unicode&&unicode)?j*2:j)<s->exited)
							hrevpos=strlen(iohist);
					}
				}
				strcat(iohist," ");
			}

			if (in_ionice&&ionice_pos==line) {
				attron(A_UNDERLINE);
				ionice_pos_data=s;
			}
			if (s->exited)
				attron(A_DIM);
			mvhline(line,0,' ',maxx);
			move(line,0);
			if (!config.f.hidepid)
				printw("%*i  ",maxpidlen,s->tid);
			if (!config.f.hideprio) {
				char c=' ';

				if (k==-1&&th_prio_diff)
					c='!';
				printw("%c%4s ",c,str_ioprio(s->io_prio));
			}
			if (!config.f.hideuser)
				printw("%s ",pw_name?pw_name:"(null)");
			if (!config.f.hideread)
				printw("%7.2f %-3.3s ",read_val,read_str);
			if (!config.f.hidewrite)
				printw("%7.2f %-3.3s ",write_val,write_str);
			if (!config.f.hideswapin)
				printw("%6.2f %% ",s->swapin_val);
			if (!config.f.hideio)
				printw("%6.2f %% ",s->blkio_val);
			if (!config.f.hidegraph&&hrevpos>0) {
				attron(A_REVERSE);
				printw("%*.*s",hrevpos,hrevpos,iohist);
				attroff(A_REVERSE);
				printw("%s",iohist+hrevpos);
			} else
				printw("%s",!config.f.hidegraph?iohist:"");
			if (!config.f.hidecmd) {
				const char *ss=(has_unicode&&unicode)?th_lines_u[0]:th_lines_a[0];

				if (ms->threads) {
					if (config.f.processes) {
						if (k==-1&&ms->threads->length)
							ss=(has_unicode&&unicode)?th_lines_u[1]:th_lines_a[1];
					} else
						if (th_first_id!=th_last_id) {
							if (k==th_first_id)
								ss=(has_unicode&&unicode)?th_lines_u[2+3*th_have_filtered]:th_lines_a[2+3*th_have_filtered];
							if (k!=th_first_id&&k!=th_last_id)
								ss=(has_unicode&&unicode)?th_lines_u[3+3*th_have_filtered]:th_lines_a[3+3*th_have_filtered];
							if (k==th_last_id)
								ss=(has_unicode&&unicode)?th_lines_u[4+3*th_have_filtered]:th_lines_a[4+3*th_have_filtered];
						}
				}
				printw("%s%s",ss,cmdline?cmdline:"(null)");
			}
			if (in_ionice&&ionice_pos==line)
				attroff(A_UNDERLINE);

			if (pw_name)
				free(pw_name);
			if (cmdline)
				free(cmdline);
			if (s->exited)
				attroff(A_DIM);

			line++;
			lastline=line;
			if (line>maxy-1) // do not draw out of screen
				goto donedraw;
		}
	}
donedraw:
	lastvisible=lastline; // last selectable screen line
	for (line=lastline;line<=maxy-1;line++) // always draw empty lines
		mvhline(line,0,' ',maxx);

	if (in_ionice) {
		mvhline(ionice_line,0,' ',maxx);
		mvprintw(ionice_line,0,"%s: ",COLUMN_NAME(0));

		if (strlen(ionice_id)) {
			struct xxxid_stats *p=NULL;
			pid_t id=atoi(ionice_id);

			attron(A_BOLD);
			printw("%s",ionice_id);
			attroff(A_BOLD);
			getyx(stdscr,promptx,prompty);
			show=TRUE;
			if (id&&(p=arr_find(cs,id))&&!p->exited) {
				printw(" Current: ");
				attron(A_BOLD);
				printw("%s",str_ioprio(p->io_prio));
				attroff(A_BOLD);
				printw(" Change to: ");

				if (ionice_id_changed) {
					ionice_id_changed=0;
					ionice_class=ioprio2class(p->io_prio);
					ionice_prio=ioprio2prio(p->io_prio);
				}

				attron(A_BOLD);
				if (ionice_cl)
					attron(A_REVERSE);
				printw("%s",str_ioprio_class[ionice_class]);
				if (ionice_cl)
					attroff(A_REVERSE);
				printw("/");
				if (!ionice_cl)
					attron(A_REVERSE);
				printw("%d",ionice_prio);
				if (!ionice_cl)
					attroff(A_REVERSE);
				attroff(A_BOLD);
			} else
				printw(" (invalid %s)",COLUMN_NAME(0));
			printw(" ");
			attron(A_REVERSE);
			printw("[use 0-9/bksp for %s, tab and arrows for prio]",COLUMN_NAME(0));
			attroff(A_REVERSE);
		} else {
			if (ionice_pos==-1||ionice_pos_data==NULL||ionice_pos_data->exited)
				printw(" (select %s by arrows or enter by 0-9/bksp)",COLUMN_NAME(0));
			else {
				attron(A_BOLD);
				if (ionice_col==0)
					attron(A_REVERSE);
				printw("%d",ionice_pos_data->tid);
				if (ionice_col==0)
					attroff(A_REVERSE);
				attroff(A_BOLD);

				printw(" Current: ");
				attron(A_BOLD);
				printw("%s",str_ioprio(ionice_pos_data->io_prio));
				attroff(A_BOLD);
				printw(" Change to: ");

				if (ionice_id_changed) {
					ionice_id_changed=0;
					ionice_class=ioprio2class(ionice_pos_data->io_prio);
					ionice_prio=ioprio2prio(ionice_pos_data->io_prio);
				}

				attron(A_BOLD);
				if (ionice_col==1)
					attron(A_REVERSE);
				printw("%s",str_ioprio_class[ionice_class]);
				if (ionice_col==1)
					attroff(A_REVERSE);
				printw("/");
				if (ionice_col==2)
					attron(A_REVERSE);
				printw("%d",ionice_prio);
				if (ionice_col==2)
					attroff(A_REVERSE);
				attroff(A_BOLD);
				printw(" ");
				attron(A_REVERSE);
				printw("[use arrows and tab for %s and prio]",COLUMN_NAME(0));
				attroff(A_REVERSE);
			}
		}
	}
	if (in_filter) {
		mvhline(ionice_line,0,' ',maxx);
		mvprintw(ionice_line,0,"UID: ");
		attron(A_BOLD);
		if (params.user_id<0)
			printw("none");
		else {
			struct passwd *pwd;

			pwd=getpwuid(params.user_id);
			printw("%d [%s]",params.user_id,pwd&&pwd->pw_name?pwd->pw_name:"n/a");
		}
		attroff(A_BOLD);
		printw(" Change to: ");
		attron(A_BOLD);
		printw("%s",filter_uid);
		if (filter_col==0) {
			getyx(stdscr,promptx,prompty);
			show=TRUE;
		}
		if (strlen(filter_uid)&&strcmp(filter_uid,"none")) {
			struct passwd *pwd;

			pwd=getpwuid(atoi(filter_uid));
			printw(" [%s]",pwd&&pwd->pw_name?pwd->pw_name:"n/a");
		}
		attroff(A_BOLD);

		printw(" TID: ");
		attron(A_BOLD);
		if (params.pid<0)
			printw("none");
		else
			printw("%d",params.pid);
		attroff(A_BOLD);
		printw(" Change to: ");
		attron(A_BOLD);
		printw("%s",filter_pid);
		attroff(A_BOLD);
		if (filter_col==1) {
			getyx(stdscr,promptx,prompty);
			show=TRUE;
		}

		printw("  ");
		attron(A_REVERSE);
		printw("[use 0-9/n/bksp for UID/TID, tab to switch UID/TID]");
		attroff(A_REVERSE);
	}
	if (show)
		move(promptx,prompty);
	curs_set(show);
	draw_vscroll(maxx-1,head1row?2:3,maxy-1,dispcount,saveskip);
	wnoutrefresh(stdscr);
	if (showhelp) {
		int rhh,rhw;

		if (hw+2>=maxx)
			hx=1;
		else
			hx=1+(maxx-2-hw)/2;
		if (hh+2>=maxy)
			hy=1;
		else
			hy=1+(maxy-2-hh)/2;

		// all this madness is to keep all parts of the window inside screen
		if (hx+hw>maxx)
			rhw=maxx-hx;
		else
			rhw=hw;
		if (hy+hh>maxy)
			rhh=maxy-hy;
		else
			rhh=hh;
		if (rhw<=0) {
			rhw=1;
			hx=0;
		}
		if (rhh<=0) {
			rhh=1;
			hy=0;
		}
		wresize(whelp,rhh,rhw);
		mvwin(whelp,hy,hx);
		view_help();
		wnoutrefresh(whelp);
	}
	doupdate();
}

static inline int curses_key(int ch) {
	switch (ch) {
		case 's':
		case 'S':
			dontrefresh^=1;
			break;
		case 'q':
		case 'Q':
			if (in_ionice) {
				in_ionice=0;
				break;
			}
			if (in_filter) {
				in_filter=0;
				break;
			}
			return 1;
		case ' ':
		case 'r':
		case 'R':
			config.f.sort_order=(config.f.sort_order==SORT_ASC)?SORT_DESC:SORT_ASC;
			break;
		case KEY_HOME:
			scrollpos=0;
			break;
		case KEY_END:
			scrollpos=dispcount-viewsizey;
			if (scrollpos<0)
				scrollpos=0;
			break;
		case KEY_PPAGE:
			scrollpos-=viewsizey;
			if (scrollpos<0)
				scrollpos=0;
			break;
		case KEY_NPAGE:
			scrollpos+=viewsizey;
			if (scrollpos>dispcount-viewsizey)
				scrollpos=dispcount-viewsizey;
			if (scrollpos<0)
				scrollpos=0;
			break;
		case KEY_RIGHT:
			if (in_ionice) {
				if (strlen(ionice_id))
					ionice_cl=!ionice_cl;
				else
					if (ionice_pos!=-1)
						ionice_col=(ionice_col+1)%3;
			}
			if (!in_ionice&&!in_filter)
				if (++config.f.sort_by==SORT_BY_MAX)
					config.f.sort_by=SORT_BY_PID;
			break;
		case KEY_LEFT:
			if (in_ionice) {
				if (strlen(ionice_id))
					ionice_cl=!ionice_cl;
				else
					if (ionice_pos!=-1)
						ionice_col=(ionice_col+3-1)%3;
			}
			if (!in_ionice&&!in_filter)
				if (--config.f.sort_by==-1)
					config.f.sort_by=SORT_BY_MAX-1;
			break;
		case KEY_UP:
			if (in_ionice) {
				if (strlen(ionice_id)) {
					if (ionice_cl) {
						ionice_class++;
						if (ionice_class>=IOPRIO_CLASS_MAX)
							ionice_class=IOPRIO_CLASS_MIN;
					} else {
						ionice_prio++;
						if (ionice_prio>7)
							ionice_prio=0;
					}
				} else {
					if (ionice_pos_data==NULL||ionice_pos_data->exited)
						ionice_col=0;
					switch (ionice_col) {
						case 0:
							if (ionice_pos>ionice_line+2) {
								ionice_id_changed=1;
								ionice_pos--;
							}
							break;
						case 1:
							ionice_class++;
							if (ionice_class>=IOPRIO_CLASS_MAX)
								ionice_class=IOPRIO_CLASS_MIN;
							break;
						case 2:
							ionice_prio++;
							if (ionice_prio>7)
								ionice_prio=0;
							break;
					}
				}
			}
			if (!in_ionice&&!in_filter) {
				scrollpos--;
				if (scrollpos<0)
					scrollpos=0;
			}
			break;
		case KEY_DOWN:
			if (in_ionice) {
				if (strlen(ionice_id)) {
					if (ionice_cl) {
						ionice_class--;
						if (ionice_class<IOPRIO_CLASS_MIN)
							ionice_class=IOPRIO_CLASS_MAX-1;
					} else {
						ionice_prio--;
						if (ionice_prio<0)
							ionice_prio=7;
					}
				} else {
					if (ionice_pos_data==NULL||ionice_pos_data->exited)
						ionice_col=0;
					switch (ionice_col) {
						case 0:
							ionice_id_changed=1;
							if (ionice_pos==-1)
								ionice_pos=ionice_line+2;
							else
								if (ionice_pos+1<lastvisible)
									ionice_pos++;
							break;
						case 1:
							ionice_class--;
							if (ionice_class<IOPRIO_CLASS_MIN)
								ionice_class=IOPRIO_CLASS_MAX-1;
							break;
						case 2:
							ionice_prio--;
							if (ionice_prio<0)
								ionice_prio=7;
							break;
					}
				}
			}
			if (!in_ionice&&!in_filter) {
				scrollpos++;
				if (scrollpos>dispcount-viewsizey)
					scrollpos=dispcount-viewsizey;
				if (scrollpos<0)
					scrollpos=0;
			}
			break;
		case 'o':
		case 'O':
			config.f.only=!config.f.only;
			break;
		case 'p':
		case 'P':
			config.f.processes=!config.f.processes;
			break;
		case 'a':
		case 'A':
			config.f.accumulated=!config.f.accumulated;
			break;
		case '?':
		case 'h':
		case 'H':
			showhelp=!showhelp;
			break;
		case 'c':
		case 'C':
			config.f.fullcmdline=!config.f.fullcmdline;
			break;
		case 'i':
		case 'I':
			if (!in_filter) {
				in_ionice=1;
				ionice_id[0]=0;
				ionice_cl=1;
				ionice_id_changed=1;
				ionice_pos=-1;
				ionice_col=0;
			}
			break;
		case 'f':
		case 'F':
			if (!in_ionice) {
				in_filter=1;
				if (params.user_id==-1)
					strcpy(filter_uid,"none");
				else
					sprintf(filter_uid,"%d",params.user_id);
				if (params.pid==-1)
					strcpy(filter_pid,"none");
				else
					sprintf(filter_pid,"%d",params.pid);
				filter_col=0;
			}
			break;
		case 'n':
		case 'N':
			if (in_filter)
				strcpy(filter_col?filter_pid:filter_uid,"none");
			break;
		case 'u':
		case 'U':
			unicode=!unicode;
			break;
		case 'x':
		case 'X':
			config.f.deadx=!config.f.deadx;
			break;
		case 27: // ESC
			if (showhelp&&!in_ionice&&!in_filter)
				showhelp=0;
			// unlike help window these cannot happen at the same time
			if (in_ionice)
				in_ionice=0;
			if (in_filter)
				in_filter=0;
			break;
		case '\r': // CR
		case KEY_ENTER:
			if (in_ionice&&strlen(ionice_id)) {
				pid_t pgid=atoi(ionice_id);
				int who=IOPRIO_WHO_PROCESS;

				if (config.f.processes) {
					pgid=getpgid(pgid);
					who=IOPRIO_WHO_PGRP;
				}
				in_ionice=0;
				set_ioprio(who,pgid,ionice_class,ionice_prio);
			}
			if (in_ionice&&ionice_pos_data&&!ionice_pos_data->exited) {
				pid_t pgid=ionice_pos_data->tid;
				int who=IOPRIO_WHO_PROCESS;

				if (config.f.processes) {
					pgid=getpgid(pgid);
					who=IOPRIO_WHO_PGRP;
				}
				in_ionice=0;
				set_ioprio(who,pgid,ionice_class,ionice_prio);
			}
			if (in_filter) {
				if (!strcmp(filter_pid,"")||!strcmp(filter_pid,"none"))
					params.pid=-1;
				else
					params.pid=atoi(filter_pid);
				if (!strcmp(filter_uid,"")||!strcmp(filter_uid,"none"))
					params.user_id=-1;
				else
					params.user_id=atoi(filter_uid);
				in_filter=0;
			}
			break;
		case '\t': // TAB
			if (in_ionice) {
				if (strlen(ionice_id))
					ionice_cl=!ionice_cl;
				else
					if (ionice_pos!=-1)
						ionice_col=(ionice_col+1)%3;
			}
			if (in_filter)
				filter_col^=1;
			break;
		case 0x08: // Ctrl-H, Backspace on some terminals
		case KEY_BACKSPACE:
			if (in_ionice) {
				int idlen=strlen(ionice_id);

				if (idlen) {
					ionice_id[idlen-1]=0;
					ionice_id_changed=1;
					ionice_pos=-1;
					ionice_col=0;
				} else {
					ionice_pos=-1;
					ionice_col=0;
				}
			}
			if (in_filter) {
				int idlen=strlen(filter_col?filter_pid:filter_uid);

				if (!strcmp(filter_col?filter_pid:filter_uid,"none"))
					(filter_col?filter_pid:filter_uid)[0]=0;
				else
					if (idlen)
						(filter_col?filter_pid:filter_uid)[idlen-1]=0;
			}
			break;
		case '0'...'9':
			if (in_ionice) {
				size_t idlen=strlen(ionice_id);

				if (idlen<sizeof ionice_id-1) {
					ionice_id[idlen++]=ch;
					ionice_id[idlen]=0;
					ionice_id_changed=1;
					ionice_pos=-1;
					ionice_col=0;
				}
			}
			if (in_filter) {
				size_t idlen;

				if (!strcmp(filter_col?filter_pid:filter_uid,"none"))
					(filter_col?filter_pid:filter_uid)[0]=0;
				idlen=strlen(filter_col?filter_pid:filter_uid);

				if (idlen<(filter_col?sizeof filter_pid:sizeof filter_uid)-1) {
					(filter_col?filter_pid:filter_uid)[idlen++]=ch;
					(filter_col?filter_pid:filter_uid)[idlen]=0;
				}
			}
			if (!in_ionice&&!in_filter) {
				if (ch>='1'&&ch<='9')
					config.opts[&config.f.hidepid-config.opts+ch-'1']^=1;
				if (ch=='0') { // show all columns
					int i;

					for (i=1;i<=9;i++)
						config.opts[&config.f.hidepid-config.opts+i-1]=0;
				}
			}
			break;
		case KEY_CTRL_L:
			redrawwin(stdscr);
		case KEY_REFRESH:
		case KEY_RESIZE:
			break;
		default:
			return -1;
	}
	return 0;
}

inline void view_curses_init(void) {
	const s_helpitem *p;

	if (strcmp(getenv("TERM"),"linux")) {
		if (setlocale(LC_CTYPE,"C.UTF-8"))
			has_unicode=1;
		else
			if (setlocale(LC_CTYPE,""))
				if (!strcmp("UTF-8",nl_langinfo(CODESET)))
					has_unicode=1;
	}
	initscr();
	keypad(stdscr,TRUE);
	nonl();
	cbreak();
	halfdelay(2);
	noecho();
	curs_set(FALSE);
	nodelay(stdscr,TRUE);

	for (p=thelp;p->descr;p++) {
		if (p->k1&&strlen(p->k1)>c1w)
			c1w=strlen(p->k1);
		if (p->k2&&strlen(p->k2)>c2w)
			c2w=strlen(p->k2);
		if (p->k3&&strlen(p->k3)>c3w)
			c3w=strlen(p->k3);
		if (strlen(p->descr)>cdw)
			cdw=strlen(p->descr);
		hh++;
	}
	hw+=c1w+c2w+c3w+cdw;
	whelp=newwin(hh,hw,hx,hy);
	if (!whelp) {
		view_curses_fini();
		nl_fini();
		fprintf(stderr,"Error: can not allocate help window\n");
		exit(1);
	}
}

inline void view_curses_fini(void) {
	if (whelp)
		delwin(whelp);
	endwin();
}

inline void view_curses_loop(void) {
	struct xxxid_stats_arr *ps=NULL;
	struct xxxid_stats_arr *cs=NULL;
	struct act_stats act={0};
	uint64_t bef=0;
	int refresh=0;
	int k=ERR;

	for (;;) {
		uint64_t now=monotime();

		if (bef+1000*params.delay<now&&!dontrefresh) {
			bef=now;
			if (ps)
				arr_free(ps);

			ps=cs;
			act.read_bytes_o=act.read_bytes;
			act.write_bytes_o=act.write_bytes;
			if (act.ts_c)
				act.have_o=1;
			act.ts_o=act.ts_c;

			cs=fetch_data(NULL);
			if (!ps) {
				ps=cs;
				cs=fetch_data(NULL);
			}
			get_vm_counters(&act.read_bytes,&act.write_bytes);
			act.ts_c=now;
			refresh=1;
		}
		if (refresh&&k==ERR)
			k=KEY_REFRESH;
		if (k!=ERR) {
			int kres;

			if ((kres=curses_key(k))>0)
				break;
			if (kres==0) {
				view_curses(cs,ps,&act,refresh);
				refresh=0;
			}
		}
		if ((params.iter>-1)&&((--params.iter)==0))
			break;
		k=getch();
	}
}

