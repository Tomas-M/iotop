/* SPDX-License-Identifier: GPL-2.0-or-later

Copyright (C) 2014  Vyacheslav Trushkin
Copyright (C) 2020-2023  Boian Bonev

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

*/

#include "iotop.h"
#include "ucell.h"

// allow ncurses printf-like arguments checking
#define GCC_PRINTF

#include <pwd.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curses.h>
#include <locale.h>
#include <langinfo.h>
#include <sys/types.h>

// key definitions
#ifndef KEY_CTRL_A
#define KEY_CTRL_A 0x01
#endif
#ifndef KEY_CTRL_B
#define KEY_CTRL_B 0x02
#endif
#ifndef KEY_CTRL_D
#define KEY_CTRL_D 0x04
#endif
#ifndef KEY_CTRL_E
#define KEY_CTRL_E 0x05
#endif
#ifndef KEY_CTRL_F
#define KEY_CTRL_F 0x06
#endif
#ifndef KEY_CTRL_H
#define KEY_CTRL_H 0x08
#endif
#ifndef KEY_CTRL_I
#define KEY_CTRL_I 0x09
#endif
#ifndef KEY_TAB
#define KEY_TAB KEY_CTRL_I
#endif
#ifndef KEY_CTRL_K
#define KEY_CTRL_K 0x0b
#endif
#ifndef KEY_CTRL_L
#define KEY_CTRL_L 0x0c
#endif
#ifndef KEY_CTRL_M
#define KEY_CTRL_M 0x0d
#endif
#ifndef KEY_RET
#define KEY_RET KEY_CTRL_M
#endif
#ifndef KEY_CTRL_R
#define KEY_CTRL_R 0x12
#endif
#ifndef KEY_CTRL_T
#define KEY_CTRL_T 0x14
#endif
#ifndef KEY_CTRL_U
#define KEY_CTRL_U 0x15
#endif
#ifndef KEY_CTRL_W
#define KEY_CTRL_W 0x17
#endif
#ifndef KEY_ESCAPE
#define KEY_ESCAPE 0x1b
#endif

// fix for old ncurses that does not implement A_ITALIC
#ifndef A_ITALIC
#define A_ITALIC A_BOLD
#endif

#define HEADER_XXS_FORMAT "%4.0f%s%s/%4.0f%s%s|%4.0f%s%s/%4.0f%s%s"
#define HEADER_XS_FORMAT "TR:%4.0f%s%sW:%4.0f%s%s|CR:%4.0f%s%sW:%4.0f%s%s"
#define HEADER_S_FORMAT "T R:%7.2f%s%s W:%7.2f%s%s|C R:%7.2f%s%s W:%7.2f%s%s"
#define HEADER_M_FORMAT "T Read:%7.2f%s%s Write:%7.2f%s%s|C Read:%7.2f%s%s Write:%7.2f%s%s"
#define HEADER_L_FORMAT "Total Read:%7.2f %s%s Write:%7.2f %s%s|Current Read:%7.2f %s%s Write:%7.2f %s%s"
#define HEADER_XL_FORMAT "Total Read: %7.2f %s%s Write: %7.2f %s%s | Current Read: %7.2f %s%s Write: %7.2f %s%s"

#define RED_PAIR 1
#define CYAN_PAIR 2
#define GREEN_PAIR 3
#define MAGENTA_PAIR 4

#define mymax(a,b) (((a)>(b))?(a):(b))

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
static int in_search=0; // search by regex interface flag
static char *search_str=NULL; // search regex string
static regex_t search_regx; // search regex
static int search_regx_ok=0; // search regex compiles ok
static ucell *search_uc=NULL; // utf cell array
static struct xxxid_stats *ionice_pos_data=NULL;
static int has_unicode=0;
static double hist_t_r[HISTORY_CNT]={0};
static double hist_t_w[HISTORY_CNT]={0};
static double hist_a_r[HISTORY_CNT]={0};
static double hist_a_w[HISTORY_CNT]={0};
static int scrollpos=0; // scroll view start position
static int viewsizey=0; // how many lines we can show on screen
static int dispcount=0; // how many lines we have after filters
static int lastvisible=0; // last visible screen line
static int noinlinehelp=0; // should inline help be allowed
static int showtda=0; // flag if delayacct warning window is shown
static int has_tda=1; // flag if delayacct kernel support is enabled
static WINDOW *whelp; // pop-up help window
static int hx=1,hy=1,hw=2+2+3,hh=2; // help window size and position
static size_t c1w=0,c2w=0,c3w=0,cdw=0; // help window column widths
static int helppos=0; // help window scroll position
static WINDOW *wtda; // pop-up warning window
static int whx=1,why=1,whw=2+2+20,whh=6; // warning window size and position
static int dontrefresh=0; // flag to inhibit refresh of data
static int initial_delayacct=0; // initial state of task_delayacct

typedef struct {
	const char *descr;
	const char *k1;
	const char *k2;
	const char *k3;
} s_helpitem;

static char units[100]="";
static char unitt[100]="";

const s_helpitem thelp[]={
	{.descr="Exit",.k2="q",.k3="Q"},
	{.descr="Toggle sort order",.k1="<space>",.k2="r"},
	{.descr="Scroll to the top of the list",.k1="<home>"},
	{.descr="Scroll to the bottom of the list",.k1="<end>"},
	{.descr="Scroll one screen up",.k1="<page-up>"},
	{.descr="Scroll one screen down",.k1="<page-down>"},
	{.descr="Scroll one line up",.k1="<up>"},
	{.descr="Scroll one line down",.k1="<down>"},
	{.descr="Sort by next column",.k1="<right>"},
	{.descr="Sort by previous column",.k1="<left>"},
	{.descr="Cancel ionice/filter/search or close help window",.k1="<esc>"},
	{.descr="Toggle showing only processes with IO activity",.k2="o",.k3="O"},
	{.descr="Toggle showing processes/threads",.k2="p",.k3="P"},
	{.descr="Cycle accumulated/accum-bw/current values",.k2="a",.k3="A"},
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
	{.descr="Cycle GRAPH source (IO, R, W, R+W, SW)",.k2="g",.k3="G"},
	{.descr="Toggle reverse GRAPH direction",.k2="R"},
	{.descr="Toggle showing inline help",.k2="?"},
	{.descr="Toggle showing this help",.k2="h",.k3="H"},
	{.descr="IOnice a process/thread",.k2="i",.k3="I"},
	{.descr="Change UID and PID filters",.k2="f",.k3="F"},
	{.descr="Search cmdline by regex",.k2="/"},
	{.descr="Toggle using Unicode/ASCII characters",.k2="u",.k3="U"},
	{.descr="Toggle colorizing values",.k2="l",.k3="L"},
	{.descr="Toggle exited processes xxx/inverse",.k2="x",.k3="X"},
	{.descr="Toggle showing exited processes",.k2="e",.k3="E"},
	{.descr="Toggle data freeze",.k2="s",.k3="S"},
	{.descr=units,.k1="<Ctrl-B>",.k2="",.k3=""},
	{.descr=unitt,.k1="<Ctrl-R>",.k2="",.k3=""},
	{.descr="Toggle task_delayacct (if available)",.k1="<Ctrl-T>",.k2="",.k3=""},
	{.descr="Redraw screen",.k1="<Ctrl-L>",.k2="",.k3=""},
	{.descr="Reset all settings to their defaults",.k2="D"},
	{.descr="Save current setting in config file",.k2="W"},
	{.descr=NULL},
};

static const char *grtype_text[]={
	"GRAPH[IO]",
	"GRAPH[R]",
	"GRAPH[W]",
	"GRAPH[R+W]",
	"GRAPH[SW]",
};

static const char *column_name[]={
	"TID",
	" PRIO",
	"USER",
	"DISK READ",
	"DISK WRITE",
	"SWAPIN",
	"IO",
	"xxxxx[xxx]",
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

#define __COLUMN_NAME(i) (((i)==SORT_BY_GRAPH)?grtype_text[masked_grtype(0)]:column_name[(i)])
#define __SAFE_INDEX(i) ((((i)%SORT_BY_MAX)+SORT_BY_MAX)%SORT_BY_MAX)
#define COLUMN_NAME(i) __COLUMN_NAME(__SAFE_INDEX(i))
#define COLUMN_L(i) COLUMN_NAME((i)-1)
#define COLUMN_R(i) COLUMN_NAME((i)+1)
#define SORT_CHAR_IND(x) ((masked_sort_by(0)==x)?(config.f.sort_order==SORT_ASC?1:2):0)
#define SORT_CHAR(x) (((has_unicode&&config.f.unicode)?sort_dir_u:sort_dir_a)[SORT_CHAR_IND(x)])

inline e_grtype masked_grtype(int isforward) {
	if (!has_tda)
		if (config.f.grtype==E_GR_IO||config.f.grtype==E_GR_SW)
			return isforward?E_GR_R:E_GR_RW;
	return config.f.grtype;
}

inline int masked_sort_by(int isforward) {
	if (!has_tda)
		if (config.f.sort_by==SORT_BY_IO||config.f.sort_by==SORT_BY_SWAPIN)
			return isforward?SORT_BY_GRAPH:SORT_BY_WRITE;
	return config.f.sort_by;
}

static inline int filter_view(struct xxxid_stats *s,int gr_width) {
	static const uint8_t iohist_z[HISTORY_CNT]={0};

	// apply uid/pid filter
	if (filter1(s))
		return 1;
	if (search_regx_ok)
		if (regexec(&search_regx,s->cmdline1,0,NULL,0)&&regexec(&search_regx,s->cmdline2,0,NULL,0))
			return 1;
	// visible history is non-zero
	if (config.f.only) {
		if (config.f.hidegraph) {
			if (has_tda) {
				if (s->blkio_val<=0)
					return 1;
			} else {
				if (s->read_val+s->write_val<=0)
					return 1;
			}
		} else {
			double su=0;
			int i;

			switch (masked_grtype(0)) {
				case E_GR_IO:
					if (!memcmp(s->iohist,iohist_z,gr_width))
						return 1;
					break;
				case E_GR_R:
					for (i=0;i<gr_width;i++)
						su+=s->readhist[i];
					if (su<=0)
						return 1;
					break;
				case E_GR_W:
					for (i=0;i<gr_width;i++)
						su+=s->writehist[i];
					if (su<=0)
						return 1;
					break;
				case E_GR_RW:
					for (i=0;i<gr_width;i++)
						su+=s->readhist[i]+s->writehist[i];
					if (su<=0)
						return 1;
					break;
				case E_GR_SW:
					if (!memcmp(s->sihist,iohist_z,gr_width))
						return 1;
					break;
			}
		}
	}
	if (config.f.hideexited&&s->exited)
		return 1;
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
		if (config.f.unicode&&has_unicode) {
			mvprintw(from,xpos,"%s",scroll_u[3]);
		} else {
			attron(A_REVERSE);
			mvprintw(from,xpos,"%s",scroll_a[3]);
			attroff(A_REVERSE);
		}
	} else {
		int visible=to-from+1; // count of visible items
		int begpos;
		int endpos;
		int i;

		if (items<=visible) {
			begpos=from;
			endpos=to;
		} else {
			int u=config.f.unicode&&has_unicode;
			int linecnt=visible-2; // count of lines usable by scroller
			int drscale=u?8:1; // draw scale
			int y=drscale*linecnt; // all scroll space
			int ss=y*visible/items; // scroller size in scroll space
			int min_ss=u?8:1; // minimum size of scroll bar in draw units
			int adjss=(ss<min_ss)?min_ss:ss; // adjusted scroller size
			int vss=y-adjss+1; // available scroll space without scroller size

			begpos=((from+1)*drscale)+vss*pos/(items-visible);
			endpos=begpos+adjss-1*(!u);
		}

		for (i=from;i<=to;i++) {
			if (i==from||i==to) {
				attron(A_REVERSE);
				mvprintw(i,xpos,"%s",(config.f.unicode&&has_unicode)?scroll_u[i==from?1:2]:scroll_a[i==from?1:2]);
				attroff(A_REVERSE);
			}
			if (i!=from&&i!=to) {
				if (config.f.unicode&&has_unicode) {
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
	int hh=getmaxy(whelp);
	int hw=getmaxx(whelp);
	static int helpcnt=0;
	const s_helpitem *p;
	int can_scroll;

	if (!helpcnt) // count thelp items once
		for (p=thelp;p->descr;p++)
			helpcnt++;

	// adjust scroll position
	if (hh-2>=helpcnt) { // all fits, no scroll
		can_scroll=0;
		helppos=0;
	} else
		can_scroll=1;
	if (helpcnt-helppos<hh-2)
		helppos=helpcnt-(hh-2);
	if (helppos<0) // can't go before start
		helppos=0;

	snprintf(units,sizeof units,"Toggle SI units [now: %d]",config.f.base);
	snprintf(unitt,sizeof unitt,"Cycle unit threshold [now: %d]",config.f.threshold);

	mvwprintw(whelp,0,0,"%s",(has_unicode&&config.f.unicode)?"─":"_");
	wattron(whelp,A_REVERSE);
	wprintw(whelp," help ");
	wattroff(whelp,A_REVERSE);
	for (i=1+strlen(" help ");i<hw;i++)
		wprintw(whelp,"%s",(has_unicode&&config.f.unicode)?"─":"_");
	for (p=thelp+helppos,i=1;i<hh-1&&p->descr;i++,p++)
		mvwprintw(whelp,i,0," %-*.*s %-*.*s %-*.*s - %-*.*s ",a,a,p->k1?p->k1:"",b,b,p->k2?p->k2:"",c,c,p->k3?p->k3:"",d,d,p->descr);
	mvwprintw(whelp,hh-1,0,"%s",(has_unicode&&config.f.unicode)?"─":"_");
	wattron(whelp,A_REVERSE|A_DIM);
	for (i=1;i<hw&&i<1+(int)strlen(" iotop "VERSION" ");i++)
		mvwprintw(whelp,hh-1,i,"%c",(" iotop "VERSION" ")[i-1]);
	wattroff(whelp,A_REVERSE|A_DIM);
	if (can_scroll) {
		int vp=1+strlen(" iotop "VERSION" ");

		for (i=vp;i<hw&&i<vp+2;i++)
			wprintw(whelp,"%s",(has_unicode&&config.f.unicode)?"─":"_");
		wattron(whelp,A_REVERSE);
		for (i=vp+2;i<hw&&i<vp+2+(int)strlen(" < > scroll ");i++)
			mvwprintw(whelp,hh-1,i,"%c",(" < > scroll ")[i-vp-2]);
		wattroff(whelp,A_REVERSE);
		vp+=strlen(" < > scroll ");
		for (i=vp;i<hw;i++)
			wprintw(whelp,"%s",(has_unicode&&config.f.unicode)?"─":"_");
	} else
		for (i=1+strlen(" iotop "VERSION" ");i<hw;i++)
			wprintw(whelp,"%s",(has_unicode&&config.f.unicode)?"─":"_");
}

static inline void view_warning(void) {
	int i;

	mvwprintw(wtda,0,0,"%s",(has_unicode&&config.f.unicode)?"─":"_");
	wattron(wtda,A_REVERSE);
	wattron(wtda,config.f.nocolor?A_BOLD:COLOR_PAIR(RED_PAIR));
	wprintw(wtda," warning ");
	wattroff(wtda,config.f.nocolor?A_BOLD:COLOR_PAIR(RED_PAIR));
	wattroff(wtda,A_REVERSE);
	for (i=1+strlen(" warning ");i<whw;i++)
		wprintw(wtda,"%s",(has_unicode&&config.f.unicode)?"─":"_");
	mvwprintw(wtda,1,0,"%*.*s",whw,whw,"");
	mvwprintw(wtda,2,0," task_delayacct is %s ",read_task_delayacct()?"ON ":"OFF");
	mvwprintw(wtda,3,0," press Ctrl-T to toggle ");
	mvwprintw(wtda,4,0,"%*.*s",whw,whw,"");
	mvwprintw(wtda,whh-1,0,"%s",(has_unicode&&config.f.unicode)?"─":"_");
	for (i=1;i<whw;i++)
		wprintw(wtda,"%s",(has_unicode&&config.f.unicode)?"─":"_");
	wattron(wtda,A_REVERSE|A_DIM);
	mvwprintw(wtda,whh-1,1," press a key to hide ");
	wattroff(wtda,A_REVERSE|A_DIM);
}

static inline void color_print_pc(double v) {
	int cp=0;

	if (v<=10)
		cp=0;
	else if (v<=40)
		cp=COLOR_PAIR(GREEN_PAIR);
	else if (v<=80)
		cp=COLOR_PAIR(MAGENTA_PAIR);
	else // 80-100
		cp=COLOR_PAIR(RED_PAIR);
	if (config.f.nocolor)
		cp=0;
	attron(cp);
	printw("%6.2f",v);
	attroff(cp);
	printw(" %% ");
}

static inline void view_curses(struct xxxid_stats_arr *cs,struct xxxid_stats_arr *ps,struct act_stats *act,int roll) {
	double time_s=timediff_in_s(act->ts_o,act->ts_c);
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
	double maxvisible=0.0;
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
	int gs,ge,gi;
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
	if (!config.f.hideswapin&&has_tda)
		maxcmdline-=column_width[SORT_BY_SWAPIN];
	if (!config.f.hideio&&has_tda)
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

	diff_len=create_diff(cs,ps,time_s,act->ts_c,filter_view,(has_unicode&&config.f.unicode)?gr_width*2:gr_width,&dispcount);

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

	for (i=0;i<((has_unicode&&config.f.unicode)?gr_width_h*2:gr_width_h);i++) {
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
	gs=config.f.reverse_graph?gr_width_h-1:0;
	ge=config.f.reverse_graph?0:gr_width_h-1;
	gi=config.f.reverse_graph?-1:1;
	for (j=gs;ge<gs?j>=ge:j<=ge;j+=gi) {
		if (has_unicode&&config.f.unicode) {
			strcat(pg_t_r,br_graph[value2scale(hist_t_r[j*2],mx_t_r)][value2scale(hist_t_r[j*2+gi],mx_t_r)]);
			strcat(pg_t_w,br_graph[value2scale(hist_t_w[j*2],mx_t_w)][value2scale(hist_t_w[j*2+gi],mx_t_w)]);
			strcat(pg_a_r,br_graph[value2scale(hist_a_r[j*2],mx_a_r)][value2scale(hist_a_r[j*2+gi],mx_a_r)]);
			strcat(pg_a_w,br_graph[value2scale(hist_a_w[j*2],mx_a_w)][value2scale(hist_a_w[j*2+gi],mx_a_w)]);
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
		if (!in_ionice&&!in_filter&&!in_search) {
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

		if (!in_ionice&&!in_filter&&!in_search) {
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

		if (i==SORT_BY_TID)
			wi=maxpidlen+2;
		if (i==SORT_BY_GRAPH)
			wi=gr_width+1;
		if (i==SORT_BY_COMMAND)
			wi=maxcmdline;

		if (config.opts[&config.f.hidepid-config.opts+i])
			continue;
		// mask swapin and io columns if there is no task_delayacct
		if ((&config.f.hidepid-config.opts+i==&config.f.hideswapin-config.opts)&&!has_tda)
			continue;
		if ((&config.f.hidepid-config.opts+i==&config.f.hideio-config.opts)&&!has_tda)
			continue;

		wt=strlen(COLUMN_NAME(i));
		if (wt>wi-1)
			wt=wi-1;
		if (masked_sort_by(0)==i)
			attron(A_BOLD);
		snprintf(t,sizeof t,"%-*.*s%s",wt,wt,COLUMN_NAME(i),SORT_CHAR(i));
		ts=u8strpadt(t,wi);
		if (ts) {
			printw("%s",ts);
			free(ts);
		} else
			printw("%-*.*s",wi,wi,t);
		if (masked_sort_by(0)==i)
			attroff(A_BOLD);
	}
	attroff(A_REVERSE);

	if ((dontrefresh||!has_tda)&&(maxx-maxcmdline+(config.f.hidecmd?0:strlen(COLUMN_L(0))+1)<(size_t)maxx)) {
		size_t xpos=maxx;

		if (!has_tda)
			xpos-=strlen("[T]");
		if (dontrefresh)
			xpos-=strlen("[frozen]");

		// don't step on column descriptions
		if (xpos<maxx-maxcmdline+(config.f.hidecmd?0:strlen(COLUMN_L(0))+1))
			xpos=maxx-maxcmdline+(config.f.hidecmd?0:strlen(COLUMN_L(0))+1);
		if (!has_tda) {
			attron(A_REVERSE);
			attron(config.f.nocolor?A_BOLD:COLOR_PAIR(RED_PAIR));
			mvprintw(ionice_line+1,xpos,"[T]");
			attroff(config.f.nocolor?A_BOLD:COLOR_PAIR(RED_PAIR));
			attroff(A_REVERSE);
		}
		if (dontrefresh)
			mvprintw(ionice_line+1,xpos+(has_tda?0:strlen("[T]")),"[frozen]");
	}
	// easiest place to print debug info
	//mvprintw(ionice_line+1,maxx-maxcmdline+strlen(COLUMN_L(0))+1," ... ",...);

	maxcmdline--; // vertical scroller

	iotop_sort_cb(NULL,(void *)(long)((has_unicode&&config.f.unicode)?gr_width*2:gr_width));
	arr_sort(cs,iotop_sort_cb);

	if (maxy<10)
		noinlinehelp=1;
	else
		noinlinehelp=0;
	line=ionice_line+2;
	lastline=line;
	viewsizey=maxy-1-ionice_line-(noinlinehelp==0&&config.f.helptype==2?2:0);
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
	// get the maximum visible value, normalize all graphs according to that (R, W and R+W only)
	if (masked_grtype(0)!=E_GR_IO&&masked_grtype(0)!=E_GR_SW&&!config.f.hidegraph) {
		int saveline=line;

		for (i=0;cs->sor&&i<diff_len;i++) {
			struct xxxid_stats *ms=cs->sor[i],*s;

			if (ms->pid!=ms->tid)
				continue;
			if (ms->threads)
				arr_sort(ms->threads,iotop_sort_cb);
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
				if (filter_view(s,(has_unicode&&config.f.unicode)?gr_width*2:gr_width))
					continue;
				if (skip) {
					skip--;
					continue;
				}

				for (j=0;j<gr_width;j++) {
					if (masked_grtype(0)==E_GR_R) {
						if (has_unicode&&config.f.unicode) {
							maxvisible=mymax(maxvisible,s->readhist[j*2]);
							maxvisible=mymax(maxvisible,s->readhist[j*2+1]);
						} else
							maxvisible=mymax(maxvisible,s->readhist[j]);
					}
					if (masked_grtype(0)==E_GR_W) {
						if (has_unicode&&config.f.unicode) {
							maxvisible=mymax(maxvisible,s->writehist[j*2]);
							maxvisible=mymax(maxvisible,s->writehist[j*2+1]);
						} else
							maxvisible=mymax(maxvisible,s->writehist[j]);
					}
					if (masked_grtype(0)==E_GR_RW) {
						if (has_unicode&&config.f.unicode) {
							maxvisible=mymax(maxvisible,s->readhist[j*2]+s->writehist[j*2]);
							maxvisible=mymax(maxvisible,s->readhist[j*2+1]+s->writehist[j*2+1]);
						} else
							maxvisible=mymax(maxvisible,s->readhist[j]+s->writehist[j]);
					}
				}

				if (line>maxy-1-(noinlinehelp==0&&config.f.helptype==2?2:0)) // do not draw out of screen
					goto donemax;
			}
		}
	donemax:
		line=saveline;
		skip=saveskip;
	}
	for (i=0;cs->sor&&i<diff_len;i++) {
		int th_prio_diff,th_first,th_have_filtered,th_first_id,th_last_id;
		struct xxxid_stats *ms=cs->sor[i],*s;
		char read_str[4],write_str[4];
		char graphstr[HISTORY_POS*5];
		double read_val,write_val;
		char *pw_name,*cmdline;
		char *pwt,*cmdt;
		int hrevpos;

		// always start showing from processes, threads are kept on the main list for easier search
		if (ms->pid!=ms->tid)
			continue;
		if (ms->threads)
			if (!(masked_grtype(0)!=E_GR_IO&&masked_grtype(0)!=E_GR_SW&&!config.f.hidegraph))
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
				int fres=filter_view(s,(has_unicode&&config.f.unicode)?gr_width*2:gr_width);

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
			if (filter_view(s,(has_unicode&&config.f.unicode)?gr_width*2:gr_width))
				continue;
			if (skip) {
				skip--;
				continue;
			}

			if (config.f.accumbw) {
				read_val=s->read_val_abw;
				write_val=s->write_val_abw;
			} else if (config.f.accumulated) {
				read_val=s->read_val_acc;
				write_val=s->write_val_acc;
			} else {
				read_val=s->read_val;
				write_val=s->write_val;
			}

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
				*graphstr=0;
				gs=config.f.reverse_graph?gr_width-1:0;
				ge=config.f.reverse_graph?0:gr_width-1;
				gi=config.f.reverse_graph?-1:1;
				for (j=gs;ge<gs?j>=ge:j<=ge;j+=gi) {
					uint8_t v1=0,v2=0;

					switch (masked_grtype(0)) {
						case E_GR_IO:
							if (has_unicode&&config.f.unicode) {
								v1=s->iohist[j*2];
								v2=s->iohist[j*2+gi];
							} else
								v1=s->iohist[j];
							break;
						case E_GR_R:
							if (has_unicode&&config.f.unicode) {
								v1=value2scale(s->readhist[j*2],maxvisible);
								v2=value2scale(s->readhist[j*2+gi],maxvisible);
							} else
								v1=value2scale(s->readhist[j*2],maxvisible);
							break;
						case E_GR_W:
							if (has_unicode&&config.f.unicode) {
								v1=value2scale(s->writehist[j*2],maxvisible);
								v2=value2scale(s->writehist[j*2+gi],maxvisible);
							} else
								v1=value2scale(s->writehist[j*2],maxvisible);
							break;
						case E_GR_RW:
							if (has_unicode&&config.f.unicode) {
								v1=value2scale(s->readhist[j*2]+s->writehist[j*2],maxvisible);
								v2=value2scale(s->readhist[j*2+gi]+s->writehist[j*2+gi],maxvisible);
							} else
								v1=value2scale(s->readhist[j*2]+s->writehist[j*2],maxvisible);
							break;
						case E_GR_SW:
							if (has_unicode&&config.f.unicode) {
								v1=s->sihist[j*2];
								v2=s->sihist[j*2+gi];
							} else
								v1=s->sihist[j];
							break;
					}
					if (config.f.deadx) {
						// +1 avoids stepping on a char with one valid and one invalid value
						if (((has_unicode&&config.f.unicode)?j*2+1:j)<s->exited)
							strcat(graphstr,"x");
						else {
							if (has_unicode&&config.f.unicode)
								strcat(graphstr,br_graph[v1][v2]);
							else
								strcat(graphstr,as_graph[v1]);
						}
					} else {
						// stepping on a char with one valid and one invalid value is not a problem with background
						if (has_unicode&&config.f.unicode)
							strcat(graphstr,br_graph[v1][v2]);
						else
							strcat(graphstr,as_graph[v1]);
						if (config.f.reverse_graph) {
							if (((has_unicode&&config.f.unicode)?j*2:j)>=s->exited&&s->exited)
								hrevpos=strlen(graphstr);
						} else {
							if (((has_unicode&&config.f.unicode)?j*2:j)<s->exited&&s->exited)
								hrevpos=strlen(graphstr);
						}
					}
				}
				strcat(graphstr," ");
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
				if (s->error_i) {
					attron(config.f.nocolor?A_ITALIC:COLOR_PAIR(RED_PAIR));
					printw("Error ");
					attroff(config.f.nocolor?A_ITALIC:COLOR_PAIR(RED_PAIR));
				} else
					printw("%c%4s ",c,str_ioprio(s->io_prio));
			}
			if (!config.f.hideuser)
				printw("%s ",pw_name?pw_name:"(null)");
			if (!config.f.hideread) {
				if (s->error_x) {
					attron(config.f.nocolor?A_ITALIC:COLOR_PAIR(RED_PAIR));
					printw("   Error    ");
					attroff(config.f.nocolor?A_ITALIC:COLOR_PAIR(RED_PAIR));
				} else
					printw("%7.2f %-3.3s ",read_val,read_str);
			}
			if (!config.f.hidewrite) {
				if (s->error_x) {
					attron(config.f.nocolor?A_ITALIC:COLOR_PAIR(RED_PAIR));
					printw("   Error    ");
					attroff(config.f.nocolor?A_ITALIC:COLOR_PAIR(RED_PAIR));
				} else
					printw("%7.2f %-3.3s ",write_val,write_str);
			}
			if (!config.f.hideswapin&&has_tda) {
				if (s->error_x) {
					attron(config.f.nocolor?A_ITALIC:COLOR_PAIR(RED_PAIR));
					printw("  Error  ");
					attroff(config.f.nocolor?A_ITALIC:COLOR_PAIR(RED_PAIR));
				} else
					color_print_pc(s->swapin_val);
			}
			if (!config.f.hideio&&has_tda) {
				if (s->error_x) {
					attron(config.f.nocolor?A_ITALIC:COLOR_PAIR(RED_PAIR));
					printw("  Error  ");
					attroff(config.f.nocolor?A_ITALIC:COLOR_PAIR(RED_PAIR));
				} else
					color_print_pc(s->blkio_val);
			}
			if (!config.f.hidegraph&&hrevpos>0) {
				if (config.f.reverse_graph) {
					graphstr[strlen(graphstr)-1]=0; // remove last space
					printw("%*.*s",hrevpos,hrevpos,graphstr);
					attron(A_REVERSE);
					printw("%s",graphstr+hrevpos);
					attroff(A_REVERSE);
					printw(" ");
				} else {
					attron(A_REVERSE);
					printw("%*.*s",hrevpos,hrevpos,graphstr);
					attroff(A_REVERSE);
					printw("%s",graphstr+hrevpos);
				}
			} else
				printw("%s",!config.f.hidegraph?graphstr:"");
			if (!config.f.hidecmd) {
				const char *ss=(has_unicode&&config.f.unicode)?th_lines_u[0]:th_lines_a[0];

				if (ms->threads) {
					if (config.f.processes) {
						if (k==-1&&ms->threads->length)
							ss=(has_unicode&&config.f.unicode)?th_lines_u[1]:th_lines_a[1];
					} else
						if (th_first_id!=th_last_id) {
							if (k==th_first_id)
								ss=(has_unicode&&config.f.unicode)?th_lines_u[2+3*th_have_filtered]:th_lines_a[2+3*th_have_filtered];
							if (k!=th_first_id&&k!=th_last_id)
								ss=(has_unicode&&config.f.unicode)?th_lines_u[3+3*th_have_filtered]:th_lines_a[3+3*th_have_filtered];
							if (k==th_last_id)
								ss=(has_unicode&&config.f.unicode)?th_lines_u[4+3*th_have_filtered]:th_lines_a[4+3*th_have_filtered];
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
			if (line>maxy-1-(noinlinehelp==0&&config.f.helptype==2?2:0)) // do not draw out of screen
				goto donedraw;
		}
	}
donedraw:
	lastvisible=lastline; // last selectable screen line
	for (line=lastline;line<=maxy-1-(noinlinehelp==0&&config.f.helptype==2?2:0);line++) // always draw empty lines
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
			getyx(stdscr,prompty,promptx);
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
			getyx(stdscr,prompty,promptx);
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
			getyx(stdscr,prompty,promptx);
			show=TRUE;
		}

		printw("  ");
		attron(A_REVERSE);
		printw("[use 0-9/n/bksp for UID/TID, tab to switch UID/TID]");
		attroff(A_REVERSE);
	}
	if (in_search) {
		int ssize=maxx-strlen("Search: ")-10;

		mvhline(ionice_line,0,' ',maxx);
		mvprintw(ionice_line,0,"Search: ");
		if (ssize>2) {
			int toskip=ssize-1<ucell_cursor_c(search_uc)?ucell_cursor_c(search_uc)-ssize+1:0;
			char *ss=ucell_substr(search_uc,toskip,ssize);
			char *ps=u8strpadt(ss,ssize);

			attron(A_REVERSE);
			if (ps)
				printw("%s",ps);
			else
				printw("%*.*s",ssize,ssize,"");
			attroff(A_REVERSE);
			printw(" [");
			if (!config.f.nocolor)
				attron(search_regx_ok?COLOR_PAIR(GREEN_PAIR):COLOR_PAIR(RED_PAIR));
			attron(A_BOLD);
			printw("%s",search_regx_ok?"  OK  ":"Error!");
			if (!config.f.nocolor)
				attroff(search_regx_ok?COLOR_PAIR(GREEN_PAIR):COLOR_PAIR(RED_PAIR));
			attroff(A_BOLD);
			printw("]");
			promptx=strlen("Search: ")+ucell_cursor_c(search_uc)-toskip;
			prompty=ionice_line;
			show=TRUE;
			if (ps)
				free(ps);
			if (ss)
				free(ss);
		}
	}
	draw_vscroll(maxx-1,head1row?2:3,maxy-1-(noinlinehelp==0&&config.f.helptype==2?2:0),dispcount,saveskip);
	if (config.f.helptype==2) {
		attron(A_REVERSE);

		mvhline(maxy-2,0,' ',maxx);
		mvhline(maxy-1,0,' ',maxx);

		attron(A_BOLD);
		mvprintw(maxy-2,0,"keys: ");
		attroff(A_BOLD);

		attron(A_UNDERLINE);
		attron(config.f.nocolor?A_ITALIC:COLOR_PAIR(CYAN_PAIR));
		printw("^L");
		attroff(config.f.nocolor?A_ITALIC:COLOR_PAIR(CYAN_PAIR));
		attroff(A_UNDERLINE);
		printw(": redraw ");

		attron(A_UNDERLINE);
		attron(config.f.nocolor?A_ITALIC:COLOR_PAIR(CYAN_PAIR));
		printw("q");
		attroff(config.f.nocolor?A_ITALIC:COLOR_PAIR(CYAN_PAIR));
		attroff(A_UNDERLINE);
		printw(": quit ");

		attron(A_UNDERLINE);
		attron(config.f.nocolor?A_ITALIC:COLOR_PAIR(CYAN_PAIR));
		printw("i");
		attroff(config.f.nocolor?A_ITALIC:COLOR_PAIR(CYAN_PAIR));
		attroff(A_UNDERLINE);
		printw(": ionice ");

		attron(A_UNDERLINE);
		attron(config.f.nocolor?A_ITALIC:COLOR_PAIR(CYAN_PAIR));
		printw("f");
		attroff(config.f.nocolor?A_ITALIC:COLOR_PAIR(CYAN_PAIR));
		attroff(A_UNDERLINE);
		printw(": uid/pid ");

		attron(A_UNDERLINE);
		attron(config.f.nocolor?A_ITALIC:COLOR_PAIR(CYAN_PAIR));
		printw("o");
		attroff(config.f.nocolor?A_ITALIC:COLOR_PAIR(CYAN_PAIR));
		attroff(A_UNDERLINE);
		printw(": %s ",config.f.only?"all":"active");

		attron(A_UNDERLINE);
		attron(config.f.nocolor?A_ITALIC:COLOR_PAIR(CYAN_PAIR));
		printw("p");
		attroff(config.f.nocolor?A_ITALIC:COLOR_PAIR(CYAN_PAIR));
		attroff(A_UNDERLINE);
		printw(": %s ",config.f.processes?"threads":"procs");

		attron(A_UNDERLINE);
		attron(config.f.nocolor?A_ITALIC:COLOR_PAIR(CYAN_PAIR));
		printw("a");
		attroff(config.f.nocolor?A_ITALIC:COLOR_PAIR(CYAN_PAIR));
		attroff(A_UNDERLINE);
		printw(": %s ",config.f.accumbw?"bandwidth":config.f.accumulated?"accum-bw":"accum");

		attron(A_UNDERLINE);
		attron(config.f.nocolor?A_ITALIC:COLOR_PAIR(CYAN_PAIR));
		printw("g");
		attroff(config.f.nocolor?A_ITALIC:COLOR_PAIR(CYAN_PAIR));
		attroff(A_UNDERLINE);
		printw(": graph src ");

		if (has_unicode) {
			attron(A_UNDERLINE);
			attron(config.f.nocolor?A_ITALIC:COLOR_PAIR(CYAN_PAIR));
			printw("u");
			attroff(config.f.nocolor?A_ITALIC:COLOR_PAIR(CYAN_PAIR));
			attroff(A_UNDERLINE);
			printw(": %s ",config.f.unicode?"ASCII":"UTF");
		}

		attron(A_UNDERLINE);
		attron(config.f.nocolor?A_ITALIC:COLOR_PAIR(CYAN_PAIR));
		printw("h/?");
		attroff(config.f.nocolor?A_ITALIC:COLOR_PAIR(CYAN_PAIR));
		attroff(A_UNDERLINE);
		printw(": help");

		attron(A_BOLD);
		mvprintw(maxy-1,0,"sort: ");
		attroff(A_BOLD);

		attron(A_UNDERLINE);
		attron(config.f.nocolor?A_ITALIC:COLOR_PAIR(CYAN_PAIR));
		printw("r");
		attroff(config.f.nocolor?A_ITALIC:COLOR_PAIR(CYAN_PAIR));
		attroff(A_UNDERLINE);
		printw(": %s ",config.f.sort_order==SORT_ASC?"desc":"asc");

		attron(A_UNDERLINE);
		attron(config.f.nocolor?A_ITALIC:COLOR_PAIR(CYAN_PAIR));
		printw("left/right");
		attroff(config.f.nocolor?A_ITALIC:COLOR_PAIR(CYAN_PAIR));
		attroff(A_UNDERLINE);
		printw(": select ");

		attron(A_BOLD);
		printw("column: ");
		attroff(A_BOLD);

		attron(A_UNDERLINE);
		attron(config.f.nocolor?A_ITALIC:COLOR_PAIR(CYAN_PAIR));
		printw("1-9");
		attroff(config.f.nocolor?A_ITALIC:COLOR_PAIR(CYAN_PAIR));
		attroff(A_UNDERLINE);
		printw(": toggle ");

		attron(A_UNDERLINE);
		attron(config.f.nocolor?A_ITALIC:COLOR_PAIR(CYAN_PAIR));
		printw("0");
		attroff(config.f.nocolor?A_ITALIC:COLOR_PAIR(CYAN_PAIR));
		attroff(A_UNDERLINE);
		printw(": show all ");

		attron(A_UNDERLINE);
		attron(config.f.nocolor?A_ITALIC:COLOR_PAIR(CYAN_PAIR));
		printw("(pg)up/dn/home/end");
		attroff(config.f.nocolor?A_ITALIC:COLOR_PAIR(CYAN_PAIR));
		attroff(A_UNDERLINE);
		printw(": scroll ");

		attron(A_UNDERLINE);
		attron(config.f.nocolor?A_ITALIC:COLOR_PAIR(CYAN_PAIR));
		printw("x");
		attroff(config.f.nocolor?A_ITALIC:COLOR_PAIR(CYAN_PAIR));
		attroff(A_UNDERLINE);
		printw(": %s ",config.f.deadx?"bkg":"xxx");

		attron(A_UNDERLINE);
		attron(config.f.nocolor?A_ITALIC:COLOR_PAIR(CYAN_PAIR));
		printw("s");
		attroff(config.f.nocolor?A_ITALIC:COLOR_PAIR(CYAN_PAIR));
		attroff(A_UNDERLINE);
		printw(": %s ",dontrefresh?"unfreeze":"freeze");

		attroff(A_REVERSE);
	}
	wnoutrefresh(stdscr);
	if (config.f.helptype==1) {
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
	if (showtda) {
		int rhh,rhw;

		if (whw+2>=maxx)
			whx=1;
		else
			whx=1+(maxx-2-whw)/2;
		if (whh+2>=maxy)
			why=1;
		else
			why=1+(maxy-2-whh)/2;

		// all this madness is to keep all parts of the window inside screen
		if (whx+whw>maxx)
			rhw=maxx-whx;
		else
			rhw=whw;
		if (why+whh>maxy)
			rhh=maxy-why;
		else
			rhh=whh;
		if (rhw<=0) {
			rhw=1;
			whx=0;
		}
		if (rhh<=0) {
			rhh=1;
			why=0;
		}
		wresize(wtda,rhh,rhw);
		mvwin(wtda,why,whx);
		view_warning();
		wnoutrefresh(wtda);
	}
	if (show)
		move(prompty,promptx);
	curs_set(show);
	doupdate();
}

static inline void update_search(void) {
	char *fs;

	if (!search_uc)
		return;

	fs=ucell_substr(search_uc,0,0); // regex to compile
	if (!fs)
		return;
	if (search_str)
		free(search_str);
	search_str=fs;
	if (search_regx_ok) {
		regfree(&search_regx);
		search_regx_ok=0;
	}
	if (search_str)
		search_regx_ok=regcomp(&search_regx,search_str,REG_EXTENDED)==0;
}

static inline void key_log(int ch __attribute__((unused)),int issecond __attribute__((unused))) {
#if 0 // debug key logging
	const char *kn=keyname(ch);
	FILE *f=fopen("iotop-key.log","a+");

	if (f) {
		fprintf(f,"[%u] key: %8x name: %s\n",issecond,ch,kn);
		fclose(f);
	}
#endif
}

static inline int curses_key_search(int ch) {
	int k2;

	switch (ch) {
		case KEY_ESCAPE: // ESC
			nocbreak();
			k2=getch();
			cbreak();
			key_log(k2,1);
			if (k2!=ERR) {
				switch (k2) {
					// add alt-/meta- key handling here
					case KEY_CTRL_H: // Ctrl-H, Backspace on some terminals
					case KEY_BACKSPACE:
						goto case_Alt_Backspace;
					case 'b':
					case 'B':
						goto case_Alt_b;
					case 'd':
					case 'D':
						goto case_Alt_d;
					case 'f':
					case 'F':
						goto case_Alt_f;
					default:
						break;
				}
				break;
			}
			in_search=0;
			if (search_str) {
				free(search_str);
				search_str=0;
			}
			if (search_regx_ok) {
				regfree(&search_regx);
				search_regx_ok=0;
			}
			if (search_uc) {
				ucell_free(search_uc);
				search_uc=NULL;
			}
			break;
		case KEY_RET: // CR
		case KEY_ENTER:
			in_search=0;
			if (search_regx_ok&&search_str&&!strlen(search_str)) { // empty string=cancel search
				regfree(&search_regx);
				search_regx_ok=0;
			}
			if (!search_regx_ok) {
				if (search_str) {
					free(search_str);
					search_str=0;
				}
				if (search_uc) {
					ucell_free(search_uc);
					search_uc=NULL;
				}
			}
			break;
		case KEY_HOME:
		case KEY_CTRL_A:
			ucell_move_home(search_uc);
			break;
		case KEY_END:
		case KEY_CTRL_E:
			ucell_move_end(search_uc);
			break;
		case KEY_RIGHT:
		case KEY_CTRL_F:
			ucell_move(search_uc);
			break;
		case KEY_LEFT:
		case KEY_CTRL_B:
			ucell_move_back(search_uc);
			break;
		case KEY_CTRL_H: // Ctrl-H, Backspace on some terminals
		case KEY_BACKSPACE: // del prev char
			ucell_del_char_prev(search_uc);
			update_search();
			break;
		case KEY_DC:
		case KEY_CTRL_D: // del current char
			ucell_del_char(search_uc);
			update_search();
			break;
		case KEY_CTRL_K: // Ctrl-K, del to end of line
			ucell_del_to_end(search_uc);
			update_search();
			break;
		case KEY_CTRL_U: // Ctrl-U, del all
			ucell_del_all(search_uc);
			update_search();
			break;
		case KEY_CTRL_W: // Ctrl-W, del prev word
		case_Alt_Backspace:
			ucell_del_word_prev(search_uc);
			update_search();
			break;
		case_Alt_f: // word forward
		case_Ctrl_Right:
			ucell_move_word(search_uc);
			update_search();
			break;
		case_Alt_b: // word backward
		case_Ctrl_Left:
			ucell_move_word_back(search_uc);
			update_search();
			break;
		case_Alt_d: // del word at cursor
			ucell_del_word(search_uc);
			update_search();
			break;
		case KEY_CTRL_L: // Ctrl-L
			redrawwin(stdscr);
		case KEY_REFRESH:
		case KEY_RESIZE:
			break;
		default:
			if (ch>=' '&&ch<=0xff) {
				if (ucell_utf_feed(search_uc,ch)>0) {
					update_search();
					return 0; // refresh screen
				}
			} else if (ch>0xff) {
				const char *kn=keyname(ch);

				if (kn&&!strcmp(kn,"kLFT5")) // CTRL-Left
					goto case_Ctrl_Left;
				if (kn&&!strcmp(kn,"kRIT5")) // CTRL-Right
					goto case_Ctrl_Right;
				if (kn&&!strcmp(kn,"M-b")) // Alt-b
					goto case_Alt_b;
				if (kn&&!strcmp(kn,"M-B")) // Alt-B
					goto case_Alt_b;
				if (kn&&!strcmp(kn,"M-d")) // Alt-d
					goto case_Alt_d;
				if (kn&&!strcmp(kn,"M-D")) // Alt-D
					goto case_Alt_d;
				if (kn&&!strcmp(kn,"M-f")) // Alt-f
					goto case_Alt_f;
				if (kn&&!strcmp(kn,"M-F")) // Alt-F
					goto case_Alt_f;
				if (kn&&!strcmp(kn,"M-^H")) // Alt-Backspace
					goto case_Alt_Backspace;
				if (kn&&!strcmp(kn,"M-^?")) // Alt-Backspace
					goto case_Alt_Backspace;
			}
			return -1;
	}
	return 0;
}

static inline int curses_key(int ch) {
	int k2;

	key_log(ch,0);
	if (in_search)
		return curses_key_search(ch);
	switch (ch) {
		case 'D':
			params.delay=1;
			memset(&config,0,sizeof(config));
			config.f.sort_by=SORT_BY_GRAPH;
			config.f.sort_order=SORT_DESC;
			config.f.base=1024; // use non-SI units by default
			config.f.threshold=2; // default threshold is 2*base
			config.f.unicode=1; // default is unicode
			break;
		case 'W':
			config_file_save();
			break;
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
			config.f.sort_order=(config.f.sort_order==SORT_ASC)?SORT_DESC:SORT_ASC;
			break;
		case 'R':
			config.f.reverse_graph=!config.f.reverse_graph;
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
			if (!in_ionice&&!in_filter) {
				if (++config.f.sort_by==SORT_BY_MAX)
					config.f.sort_by=SORT_BY_TID;
				config.f.sort_by=masked_sort_by(1);
			}
			break;
		case KEY_LEFT:
			if (in_ionice) {
				if (strlen(ionice_id))
					ionice_cl=!ionice_cl;
				else
					if (ionice_pos!=-1)
						ionice_col=(ionice_col+3-1)%3;
			}
			if (!in_ionice&&!in_filter) {
				if (--config.f.sort_by==-1)
					config.f.sort_by=SORT_BY_MAX-1;
				config.f.sort_by=masked_sort_by(0);
			}
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
			if (!config.f.accumbw&&!config.f.accumulated)
				config.f.accumulated=1;
			else if (!config.f.accumbw&&config.f.accumulated) {
				config.f.accumulated=0;
				config.f.accumbw=1;
			} else {
				config.f.accumulated=0;
				config.f.accumbw=0;
			}
			break;
		case 'l':
		case 'L':
			config.f.nocolor=!config.f.nocolor;
			break;
		case '?':
			if (config.f.helptype!=2)
				config.f.helptype=2;
			else
				config.f.helptype=0;
			if (noinlinehelp&&config.f.helptype==2)
				config.f.helptype=0;
			break;
		case 'h':
		case 'H':
			if (config.f.helptype!=1) {
				config.f.helptype=1;
				helppos=0;
			} else
				config.f.helptype=0;
			break;
		case '<':
			if (config.f.helptype==1) // out of bounds checks are in view_help
				helppos--;
			break;
		case '>':
			if (config.f.helptype==1)
				helppos++;
			break;
		case 'c':
		case 'C':
			config.f.fullcmdline=!config.f.fullcmdline;
			break;
		case 'g': // roll grtype forward
			config.f.grtype++;
			if (config.f.grtype>E_GR_MAX)
				config.f.grtype=E_GR_MIN;
			config.f.grtype=masked_grtype(1);
			break;
		case 'G': // roll grtype backward
			if (config.f.grtype>E_GR_MIN)
				config.f.grtype--;
			else
				config.f.grtype=E_GR_MAX;
			config.f.grtype=masked_grtype(0);
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
		case 'e':
		case 'E':
			config.f.hideexited=!config.f.hideexited;
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
			config.f.unicode=!config.f.unicode;
			break;
		case 'x':
		case 'X':
			config.f.deadx=!config.f.deadx;
			break;
		case KEY_ESCAPE: // ESC
			nocbreak();
			k2=getch();
			cbreak();
			key_log(k2,1);
			if (k2!=ERR) {
				switch (k2) {
					// add alt-/meta- key handling here
					default:
						break;
				}
				break;
			}
			if (config.f.helptype==1&&!in_ionice&&!in_filter)
				config.f.helptype=0;
			// unlike help window these cannot happen at the same time
			if (in_ionice)
				in_ionice=0;
			if (in_filter)
				in_filter=0;
			break;
		case KEY_RET: // CR
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
		case KEY_TAB: // TAB
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
		case KEY_CTRL_H: // Ctrl-H, Backspace on some terminals
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
		case '/':
			if (!in_ionice&&!in_filter) {
				in_search=1;
				if (!search_regx_ok) {
					if (search_uc) {
						ucell_free(search_uc);
						search_uc=NULL;
					}
				}
				if (!search_uc)
					search_uc=ucell_init(0);
				if (search_str) {
					free(search_str);
					search_str=NULL;
				}
				if (!search_uc)
					in_search=0;
				update_search();
			}
			break;
		case KEY_CTRL_B:
			config.f.base=config.f.base==1000?1024:1000;
			break;
		case KEY_CTRL_R:
			config.f.threshold++;
			if (config.f.threshold>10)
				config.f.threshold=1;
			break;
		case KEY_CTRL_T:
			write_task_delayacct(!read_task_delayacct());
			break;
		case KEY_CTRL_L: // Ctrl-L
			redrawwin(stdscr);
		case KEY_REFRESH:
		case KEY_RESIZE:
			break;
		default:
			return -1;
	}
	if (ch!=KEY_REFRESH&&ch!=KEY_RESIZE&&showtda)
		showtda=0;
	return 0;
}

inline void view_curses_init(void) {
	char *term=getenv("TERM");
	const s_helpitem *p;

	// keep the state of the toggle at startup
	// warn at exit, if it is left enabled but initially was not
	initial_delayacct=read_task_delayacct();

	if (term&&strcmp(term,"linux")) {
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
	start_color();
	use_default_colors();
	init_pair(RED_PAIR,COLOR_RED,-1);
	init_pair(CYAN_PAIR,COLOR_CYAN,-1);
	init_pair(GREEN_PAIR,COLOR_GREEN,-1);
	init_pair(MAGENTA_PAIR,COLOR_MAGENTA,-1);

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
	wtda=newwin(whh,whw,whx,why);
	if (!whelp) {
		view_curses_fini();
		nl_fini();
		fprintf(stderr,"Error: can not allocate warning window\n");
		exit(1);
	}
}

inline void view_curses_fini(void) {
	if (whelp)
		delwin(whelp);
	if (wtda)
		delwin(wtda);
	endwin();
	if (search_str) {
		free(search_str);
		search_str=NULL;
	}
	if (search_regx_ok) {
		regfree(&search_regx);
		search_regx_ok=0;
	}
	if (search_uc) {
		ucell_free(search_uc);
		search_uc=NULL;
	}

	if (has_task_delayacct())
		if (!initial_delayacct&&read_task_delayacct()) {
			printf(
				"WARNING:\n"
				"\tThis kernel supports controlling task_delayacct at runtime.\n"
				"\tAt program startup it was OFF and now it is ON; use:\n"
				"\t\tsysctl kernel.task_delayacct=0\n"
				"\tto restore it to its previous value and save some CPU cycles.\n"
			);
		}
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

		if (!read_task_delayacct()) {
			if (has_tda)
				showtda=1;
			has_tda=0;
		} else {
			showtda=0;
			has_tda=1;
		}
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

