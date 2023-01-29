/* SPDX-License-Identifier: GPL-2.0-or-later

Copyright (C) 2020-2023  Boian Bonev

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

*/

// {{{ includes

#include "ucell.h"

#include <errno.h>
#include <wchar.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>
#include <inttypes.h>

// }}}

// {{{ definitions

typedef enum { // utf8 sequence state machine
	U_NORM, // expect single byte or leading byte
	U_L2C1, // expect 1/1 continuation byte
	U_L3C1, // expect 1/2 continuation bytes
	U_L3C2, // expect 2/2 continuation bytes
	U_L4C1, // expect 1/3 continuation bytes
	U_L4C2, // expect 2/3 continuation bytes
	U_L4C3, // expect 3/3 continuation bytes
	U_L5C1, // expect 1/4 continuation bytes
	U_L5C2, // expect 2/4 continuation bytes
	U_L5C3, // expect 3/4 continuation bytes
	U_L5C4, // expect 4/4 continuation bytes
	U_L6C1, // expect 1/5 continuation bytes
	U_L6C2, // expect 2/5 continuation bytes
	U_L6C3, // expect 3/5 continuation bytes
	U_L6C4, // expect 4/5 continuation bytes
	U_L6C5, // expect 5/5 continuation bytes
} ucell_utf_state;

// size of string that can be stored immediately w/o allocation
#define PSIZE (2*sizeof(char *)-sizeof(uint8_t))

// cell contents are dynamically allocated
#define UC_ALLOC 1

typedef struct __attribute__((packed)) _cell {
	union __attribute__((packed)) {
		char *p; // allocated contents, aligned on 2x pointer size
		char d[PSIZE]; // inline contents
	};
	union __attribute__((packed)) {
		struct __attribute__((packed)) {
			uint8_t flags:1; // storage type 0=inline, 1=allocated
			uint8_t w:2; // terminal cells needed to represent this char
		};
		uint8_t pad;
	};
} cell;

struct _ucell {
	cell *cells; // cell array
	int len; // actual count
	int sz; // allocation size of cells
	int cursor; // cursor position
	ucell_utf_state utfst; // state of utf8 parsing state machine
	uint8_t utf[3]; // buffer for utf8 sequence parsing; last byte is not put here; it is never zero terminated
					// to be increased to 4 or 5 if some day unicode starts permitting 5 or 6 byte sequences
};

// }}}

inline ucell *ucell_init(int sz) { // {{{
	ucell *uc;

	if (sz<=0)
		sz=100;

	uc=calloc(1,sizeof *uc);
	if (!uc)
		return NULL;

	uc->cells=calloc(sz,sizeof *uc->cells);
	if (!uc->cells) {
		free(uc);
		return NULL;
	}

	uc->sz=sz;
	uc->utfst=U_NORM;
	return uc;
} // }}}

inline void ucell_free(ucell *uc) { // {{{
	int i;

	if (!uc)
		return;

	for (i=0;i<uc->sz;i++)
		if (uc->cells[i].flags&UC_ALLOC)
			free(uc->cells[i].p);
	free(uc->cells);
	free(uc);
} // }}}

inline int ucell_resize(ucell *uc,int newsz) { // {{{
	if (!uc)
		return -EINVAL;

	if (newsz<=0)
		return -EINVAL;

	if (newsz==uc->sz) // nothing to do
		return 0;

	if (newsz<uc->sz) { // shrink
		cell *nc;
		int i;

		for (i=newsz;i<uc->sz;i++)
			if (uc->cells[i].flags&UC_ALLOC) {
				free(uc->cells[i].p);
				uc->cells[i].flags=0;
				uc->cells[i].d[0]=0;
			}
		nc=reallocarray(uc->cells,newsz,sizeof *uc->cells);
		if (uc->len>newsz)
			uc->len=newsz;
		if (nc) {
			uc->cells=nc;
			uc->sz=newsz;
		}
		return 0;
	}

	if (newsz>uc->sz) { // grow
		cell *nc;
		int i;

		nc=reallocarray(uc->cells,newsz,sizeof *uc->cells);
		if (!nc)
			return -ENOMEM;

		uc->cells=nc;
		for (i=uc->sz;i<newsz;i++) {
			uc->cells[i].flags=0;
			uc->cells[i].d[0]=0;
		}
		uc->sz=newsz;
	}
	return 0;
} // }}}

inline int ucell_app_cchar(ucell *uc,int pos,const char *c) { // {{{
	// append combining character
	if (!uc||!c)
		return -EINVAL;

	if (pos<0) // snap to start
		pos=0;

	if (pos>uc->len) // snap to end
		pos=uc->len;

	if (pos>=uc->len)
		return -ERANGE;

	if (!(uc->cells[pos].flags&UC_ALLOC)&&strlen(uc->cells[pos].d)+strlen(c)<PSIZE)
		strcat(uc->cells[pos].d,c);
	else {
		char *s=calloc(1,strlen((uc->cells[pos].flags&UC_ALLOC)?uc->cells[pos].p:uc->cells[pos].d)+strlen(c)+1);

		if (!s)
			return -ENOMEM;

		strcpy(s,(uc->cells[pos].flags&UC_ALLOC)?uc->cells[pos].p:uc->cells[pos].d);
		strcat(s,c);
		uc->cells[pos].flags=UC_ALLOC;
		uc->cells[pos].p=s;
	}
	return 0;
} // }}}

inline int ucell_ins_char(ucell *uc,int pos,const char *c,uint8_t w) { // {{{
	// this assumes that c contains a single cell utf sequence
	// e.g. a single utf character and 0+ combining characters
	// w is the precalculated terminal cell size
	if (!uc)
		return -EINVAL;

	if (!c)
		return -EINVAL;

	if (pos<0) // snap to start
		pos=0;

	if (pos>uc->len) // snap to end
		pos=uc->len;

	if (!w) // append combining char
		return ucell_app_cchar(uc,pos?pos-1:0,c);

	// insert non-combining char
	if (uc->len==uc->sz) { // grow
		int rv=ucell_resize(uc,uc->sz+100);

		if (rv)
			return rv;
	}
	if (pos<uc->len) // shift tail
		memmove(uc->cells+pos+1,uc->cells+pos,(uc->len-pos)*sizeof *uc->cells);
	uc->cells[pos].flags=(strlen(c)<PSIZE)?0:UC_ALLOC;
	if (uc->cells[pos].flags&UC_ALLOC) {
		uc->cells[pos].p=strdup(c);
		if (!uc->cells[pos].p) {
			if (pos<uc->len) // shift back
				memmove(uc->cells+pos,uc->cells+pos+1,(uc->len-pos)*sizeof *uc->cells);
			// clear last cell to avoid duplicate pointer or data
			uc->cells[uc->len].flags=0;
			uc->cells[uc->len].d[0]=0;
			return -ENOMEM;
		}
	} else
		strcpy(uc->cells[pos].d,c);
	uc->cells[pos].w=w;
	uc->len++;
	return 0;
} // }}}

inline int ucell_del_char_at(ucell *uc,int pos) { // {{{
	if (!uc)
		return -EINVAL;

	if (pos<0||pos>=uc->len)
		return -EINVAL;

	if (!uc->len)
		return -EINVAL;

	if (uc->cells[pos].flags&UC_ALLOC)
		free(uc->cells[pos].p);
	if (pos<uc->len-1) // shift tail
		memmove(uc->cells+pos,uc->cells+pos+1,(uc->len-1-pos)*sizeof *uc->cells);
	uc->cells[uc->len-1].d[0]=0;
	uc->cells[uc->len-1].flags=0;
	uc->len--;
	if (uc->cursor<0)
		uc->cursor=0;
	if (uc->cursor>uc->len)
		uc->cursor=uc->len;
	return 0;
} // }}}

inline void ucell_utf_feed_s(ucell *uc,const char *s) { // {{{
	char d[(s&&strlen(s))?strlen(s)+1:1];
	size_t si=0;
	size_t di=0;
	size_t tl=0;
	size_t sl;
	wchar_t w;

	if (!uc||!s||!strlen(s))
		return;

	*d=0;
	sl=strlen(s);
	mbtowc(NULL,NULL,0);
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
		tw=wcwidth(w);
		if (tw<0) {
			si+=cl;
			continue;
		}
		if (strlen(d)&&tw) { // appending non-combining characters is not allowed, split them
			if (!ucell_ins_char(uc,uc->cursor,d,tl)&&tl) // send d
				uc->cursor++;
			*d=0;
			di=0;
			tl=0;
		}
		memcpy(d+di,s+si,cl);
		di+=cl;
		d[di]=0;
		si+=cl;
		tl+=tw;
	}
	if (strlen(d))
		if (!ucell_ins_char(uc,uc->cursor,d,tl)&&tl) // send d
			uc->cursor++;
} // }}}

inline void ucell_utf_feed1(ucell *uc,uint8_t c1) { // {{{
	char s[]={(char)c1,0};

	if (!uc)
		return;

	ucell_utf_feed_s(uc,s);
} // }}}

inline void ucell_utf_feed2(ucell *uc,uint8_t c1,uint8_t c2) { // {{{
	char s[]={(char)c1,(char)c2,0};

	if (!uc)
		return;

	ucell_utf_feed_s(uc,s);
} // }}}

inline void ucell_utf_feed3(ucell *uc,uint8_t c1,uint8_t c2,uint8_t c3) { // {{{
	char s[]={(char)c1,(char)c2,(char)c3,0};

	if (!uc)
		return;

	ucell_utf_feed_s(uc,s);
} // }}}

inline void ucell_utf_feed4(ucell *uc,uint8_t c1,uint8_t c2,uint8_t c3,uint8_t c4) { // {{{
	char s[]={(char)c1,(char)c2,(char)c3,(char)c4,0};

	if (!uc)
		return;

	ucell_utf_feed_s(uc,s);
} // }}}

inline int ucell_utf_feed(ucell *uc,uint8_t c) { // {{{
	if (!uc)
		return -1;

	switch (uc->utfst) {
		case U_NORM:
			if (c&0x80) {
				if ((c&0xc0)==0x80) // unexpected continuation byte - ignore
					break;
			startbyte:
				if ((c&0xe0)==0xc0) { // 2 byte seq
					uc->utf[0]=c;
					uc->utfst=U_L2C1;
					break;
				}
				if ((c&0xf0)==0xe0) { // 3 byte seq
					uc->utf[0]=c;
					uc->utfst=U_L3C1;
					break;
				}
				if ((c&0xf8)==0xf0) { // 4 byte seq
					uc->utf[0]=c;
					uc->utfst=U_L4C1;
					break;
				}
				if ((c&0xfc)==0xf8) { // 5 byte seq
					//uc->utf[0]=c;
					uc->utfst=U_L5C1;
					break;
				}
				if ((c&0xfe)==0xfc) { // 6 byte seq
					//uc->utf[0]=c;
					uc->utfst=U_L6C1;
					break;
				}
				// pass 0xff and 0xfe - violates rfc
				ucell_utf_feed1(uc,c);
				uc->utfst=U_NORM; // in case we come from unexpected start byte
			} else
				ucell_utf_feed1(uc,c);
			return 1; // both paths feed 1 byte
		case U_L2C1:
			if ((c&0xc0)==0x80) { // continuation byte
				ucell_utf_feed2(uc,uc->utf[0],c);
				uc->utfst=U_NORM;
				return 2;
			}
			if (c&0x80) // start another sequence
				goto startbyte;
			uc->utfst=U_NORM; // normal byte kills current sequence and is processed
			ucell_utf_feed1(uc,c);
			return 1;
		case U_L3C1:
			if ((c&0xc0)==0x80) { // continuation byte
				uc->utf[1]=c;
				uc->utfst=U_L3C2;
				break;
			}
			if (c&0x80) // start another sequence
				goto startbyte;
			uc->utfst=U_NORM; // normal byte kills current sequence and is processed
			ucell_utf_feed1(uc,c);
			return 1;
		case U_L3C2:
			if ((c&0xc0)==0x80) { // continuation byte
				ucell_utf_feed3(uc,uc->utf[0],uc->utf[1],c);
				uc->utfst=U_NORM;
				return 3;
			}
			if (c&0x80) // start another sequence
				goto startbyte;
			uc->utfst=U_NORM; // normal byte kills current sequence and is processed
			ucell_utf_feed1(uc,c);
			return 1;
		case U_L4C1:
			if ((c&0xc0)==0x80) { // continuation byte
				uc->utf[1]=c;
				uc->utfst=U_L4C2;
				break;
			}
			if (c&0x80) // start another sequence
				goto startbyte;
			uc->utfst=U_NORM; // normal byte kills current sequence and is processed
			ucell_utf_feed1(uc,c);
			return 1;
		case U_L4C2:
			if ((c&0xc0)==0x80) { // continuation byte
				uc->utf[2]=c;
				uc->utfst=U_L4C3;
				break;
			}
			if (c&0x80) // start another sequence
				goto startbyte;
			uc->utfst=U_NORM; // normal byte kills current sequence and is processed
			ucell_utf_feed1(uc,c);
			return 1;
		case U_L4C3:
			if ((c&0xc0)==0x80) { // continuation byte
				ucell_utf_feed4(uc,uc->utf[0],uc->utf[1],uc->utf[2],c);
				uc->utfst=U_NORM;
				return 4;
			}
			if (c&0x80) // start another sequence
				goto startbyte;
			uc->utfst=U_NORM; // normal byte kills current sequence and is processed
			ucell_utf_feed1(uc,c);
			return 1;
		case U_L5C1:
			if ((c&0xc0)==0x80) { // continuation byte
				//uc->utf[1]=c;
				uc->utfst=U_L5C2;
				break;
			}
			if (c&0x80) // start another sequence
				goto startbyte;
			uc->utfst=U_NORM; // normal byte kills current sequence and is processed
			ucell_utf_feed1(uc,c);
			return 1;
		case U_L5C2:
			if ((c&0xc0)==0x80) { // continuation byte
				//uc->utf[2]=c;
				uc->utfst=U_L5C3;
				break;
			}
			if (c&0x80) // start another sequence
				goto startbyte;
			uc->utfst=U_NORM; // normal byte kills current sequence and is processed
			ucell_utf_feed1(uc,c);
			return 1;
		case U_L5C3:
			if ((c&0xc0)==0x80) { // continuation byte
				//uc->utf[3]=c;
				uc->utfst=U_L5C4;
				break;
			}
			if (c&0x80) // start another sequence
				goto startbyte;
			uc->utfst=U_NORM; // normal byte kills current sequence and is processed
			ucell_utf_feed1(uc,c);
			return 1;
		case U_L5C4:
			if ((c&0xc0)==0x80) { // continuation byte
				//ucell_utf_feed5(uc,uc->utf[0],uc->utf[1],uc->utf[2],uc->utf[3],c); // sequence is parsed but ignored
				uc->utfst=U_NORM;
				break;
			}
			if (c&0x80) // start another sequence
				goto startbyte;
			uc->utfst=U_NORM; // normal byte kills current sequence and is processed
			ucell_utf_feed1(uc,c);
			return 1;
		case U_L6C1:
			if ((c&0xc0)==0x80) { // continuation byte
				//uc->utf[1]=c;
				uc->utfst=U_L6C2;
				break;
			}
			if (c&0x80) // start another sequence
				goto startbyte;
			uc->utfst=U_NORM; // normal byte kills current sequence and is processed
			ucell_utf_feed1(uc,c);
			return 1;
		case U_L6C2:
			if ((c&0xc0)==0x80) { // continuation byte
				//uc->utf[2]=c;
				uc->utfst=U_L6C3;
				break;
			}
			if (c&0x80) // start another sequence
				goto startbyte;
			uc->utfst=U_NORM; // normal byte kills current sequence and is processed
			ucell_utf_feed1(uc,c);
			return 1;
		case U_L6C3:
			if ((c&0xc0)==0x80) { // continuation byte
				//uc->utf[3]=c;
				uc->utfst=U_L6C4;
				break;
			}
			if (c&0x80) // start another sequence
				goto startbyte;
			uc->utfst=U_NORM; // normal byte kills current sequence and is processed
			ucell_utf_feed1(uc,c);
			return 1;
		case U_L6C4:
			if ((c&0xc0)==0x80) { // continuation byte
				//uc->utf[3]=c;
				uc->utfst=U_L6C5;
				break;
			}
			if (c&0x80) // start another sequence
				goto startbyte;
			uc->utfst=U_NORM; // normal byte kills current sequence and is processed
			ucell_utf_feed1(uc,c);
			return 1;
		case U_L6C5:
			if ((c&0xc0)==0x80) { // continuation byte
				//ucell_utf_feed6(uc,uc->utf[0],uc->utf[1],uc->utf[2],uc->utf[3],uc->utf[4],c); // sequence is parsed but ignored
				uc->utfst=U_NORM;
				break;
			}
			if (c&0x80) // start another sequence
				goto startbyte;
			uc->utfst=U_NORM; // normal byte kills current sequence and is processed
			ucell_utf_feed1(uc,c);
			return 1;
	}
	return 0; // 0 bytes pushed
} // }}}

inline void ucell_cursor_set(ucell *uc,int c) { // {{{
	if (!uc)
		return;

	if (c<0)
		c=0;

	if (c>uc->len)
		c=uc->len;

	uc->cursor=c;
} // }}}

inline char *ucell_substr(ucell *uc,int skip,int maxc) { // {{{
	char *ress;
	int blen=0;
	int clen=0;
	int skipw;
	int stp;
	int enp;
	int i;

	if (!uc)
		return NULL;

	if (skip<0)
		skip=0;

	if (maxc<0)
		maxc=0;

	// pass 1 calculate size
	stp=0;
	enp=0;
	skipw=skip;
	for (i=0;i<uc->len;i++) {
		if (skipw) {
			if (uc->cells[i].w<=skipw)
				skipw-=uc->cells[i].w;
			else
				skipw=0;
			stp=i+1;
			continue;
		}
		if (!maxc||clen+uc->cells[i].w<=maxc) { // maxc==0 means no limit
			clen+=uc->cells[i].w;
			blen+=strlen((uc->cells[i].flags&UC_ALLOC)?uc->cells[i].p:uc->cells[i].d);
			enp=i+1;
		} else { // end of string
			enp=i;
			break;
		}
	}
	ress=calloc(blen+1,1);
	if (!ress)
		return NULL;

	for (i=stp;i<enp;i++)
		strcat(ress,(uc->cells[i].flags&UC_ALLOC)?uc->cells[i].p:uc->cells[i].d);
	return ress;
} // }}}

inline int ucell_len(ucell *uc) { // {{{
	if (!uc)
		return 0;

	return uc->len;
} // }}}

inline int ucell_cursor(ucell *uc) { // {{{
	if (!uc)
		return 0;

	return uc->cursor;
} // }}}

inline int ucell_cursor_c(ucell *uc) { // {{{
	int pos=0;
	int i;

	if (!uc)
		return 0;

	for (i=0;i<uc->len&&i<uc->cursor;i++)
		pos+=uc->cells[i].w;
	return pos;
} // }}}

inline void ucell_del_char(ucell *uc) { // {{{
	if (!uc)
		return;

	if (uc->cursor>=uc->len) // nothing to do
		return;

	ucell_del_char_at(uc,uc->cursor);
} // }}}

inline void ucell_del_char_prev(ucell *uc) { // {{{
	if (!uc)
		return;

	if (!uc->cursor) // nothing to do
		return;

	uc->cursor--;
	ucell_del_char_at(uc,uc->cursor);
} // }}}

inline void ucell_del_to_end(ucell *uc) { // {{{
	int i;

	if (!uc)
		return;

	if (uc->cursor<0)
		uc->cursor=0;

	for (i=uc->len-1;i>=uc->cursor;i--)
		ucell_del_char_at(uc,i);
} // }}}

inline void ucell_del_all(ucell *uc) { // {{{
	int i;

	if (!uc)
		return;

	uc->cursor=0;
	for (i=uc->len-1;i>=0;i--)
		ucell_del_char_at(uc,i);
} // }}}

inline int ucell_isalnum(const char *s) { // {{{
	wchar_t ws[1];
	size_t l,p=0;
	int c=-1;

	if (!s)
		return 0;

	mbtowc(NULL,NULL,0); // reset state
	l=strlen(s);
	while (p<l&&(c=mbtowc(ws,s+p,l-p))>0) {
		if (!iswalnum(ws[0]))
			return 0;
		p+=c;
	}
	if (c<=0)
		return 0;
	return 1;
} // }}}

inline int ucell_isalnum_c(ucell *uc,int pos) { // {{{
	if (!uc)
		return 0;

	if (pos<0||pos>=uc->len)
		return 0;

	return ucell_isalnum((uc->cells[pos].flags&UC_ALLOC)?uc->cells[pos].p:uc->cells[pos].d);
} // }}}

inline void ucell_del_word(ucell *uc) { // {{{
	if (!uc)
		return;

	while (uc->cursor>=0&&uc->cursor<uc->len&&!ucell_isalnum_c(uc,uc->cursor))
		ucell_del_char_at(uc,uc->cursor);
	while (uc->cursor>=0&&uc->cursor<uc->len&&ucell_isalnum_c(uc,uc->cursor))
		ucell_del_char_at(uc,uc->cursor);
} // }}}

inline void ucell_del_word_prev(ucell *uc) { // {{{
	if (!uc)
		return;

	while (uc->cursor>0&&uc->cursor<=uc->len&&!ucell_isalnum_c(uc,uc->cursor-1)) {
		uc->cursor--;
		ucell_del_char_at(uc,uc->cursor);
	}
	while (uc->cursor>0&&uc->cursor<=uc->len&&ucell_isalnum_c(uc,uc->cursor-1)) {
		uc->cursor--;
		ucell_del_char_at(uc,uc->cursor);
	}
} // }}}

inline void ucell_move_home(ucell *uc) { // {{{
	if (!uc)
		return;

	ucell_cursor_set(uc,0);
} // }}}

inline void ucell_move_end(ucell *uc) { // {{{
	if (!uc)
		return;

	ucell_cursor_set(uc,ucell_len(uc));
} // }}}

inline void ucell_move(ucell *uc) { // {{{
	if (!uc)
		return;

	ucell_cursor_set(uc,ucell_cursor(uc)+1);
} // }}}

inline void ucell_move_back(ucell *uc) { // {{{
	if (!uc)
		return;

	ucell_cursor_set(uc,ucell_cursor(uc)-1);
} // }}}

inline void ucell_move_word(ucell *uc) { // {{{
	if (!uc)
		return;

	while (uc->cursor>=0&&uc->cursor<uc->len&&!ucell_isalnum_c(uc,uc->cursor))
		uc->cursor++;
	while (uc->cursor>=0&&uc->cursor<uc->len&&ucell_isalnum_c(uc,uc->cursor))
		uc->cursor++;
} // }}}

inline void ucell_move_word_back(ucell *uc) { // {{{
	if (!uc)
		return;

	while (uc->cursor>0&&uc->cursor<=uc->len&&!ucell_isalnum_c(uc,uc->cursor-1))
		uc->cursor--;
	while (uc->cursor>0&&uc->cursor<=uc->len&&ucell_isalnum_c(uc,uc->cursor-1))
		uc->cursor--;
} // }}}

// {{{ struct alignment test

#ifdef STRUCT_TEST

#include <stdio.h>

int main(void) {
	cell c;

	printf("PSIZE: %lu\n",PSIZE);
	printf("sizeof(cell): %lu\n",sizeof(cell));
	printf("sizeof(c.p): %lu\n",sizeof(c.p));
	printf("sizeof(c.d): %lu\n",sizeof(c.d));
	printf("off(c.p): %lu\n",__builtin_offsetof(cell,p));
	printf("off(c.d): %lu\n",__builtin_offsetof(cell,d));
	printf("off(c.pad): %lu\n",__builtin_offsetof(cell,pad));
	return 0;
}

#endif

// }}}
