/* SPDX-License-Identifier: GPL-2.0-or-later

Copyright (C) 2020-2025  Boian Bonev

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

*/

#ifndef ___UCELL_H___
#define ___UCELL_H___

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <inttypes.h>

struct _ucell; // opaque internal state
typedef struct _ucell ucell;

inline ucell *ucell_init(int sz); // allocate internal state
inline void ucell_free(ucell *uc); // free internal state
inline int ucell_ins_char(ucell *uc,int pos,const char *c,uint8_t w); // add a combining character to an existing one
inline int ucell_del_char_at(ucell *uc,int pos); // remove a character
inline int ucell_utf_feed(ucell *uc,uint8_t c); // utf8 parser, returns count of generated bytes, negative on error
inline void ucell_cursor_set(ucell *uc,int c); // change cursor position
inline char *ucell_substr(ucell *uc,int skip,int maxc); // get substring for visualization; skip/maxc are in cells
inline int ucell_len(ucell *uc); // character count
inline int ucell_cursor(ucell *uc); // cursor position as character index
inline int ucell_cursor_c(ucell *uc); // cursor position in cells
inline void ucell_del_char(ucell *uc); // delete the character at cursor position
inline void ucell_del_char_prev(ucell *uc); // delete the character before cursor position
inline void ucell_del_to_end(ucell *uc); // delete from current character to the end
inline void ucell_del_all(ucell *uc); // delete everything
inline void ucell_del_word(ucell *uc); // delete word from cursor
inline void ucell_del_word_prev(ucell *uc); // delete previous word
inline void ucell_move_home(ucell *uc); // move to home
inline void ucell_move_end(ucell *uc); // move to end
inline void ucell_move(ucell *uc); // move right
inline void ucell_move_back(ucell *uc); // move left
inline void ucell_move_word(ucell *uc); // move word forward
inline void ucell_move_word_back(ucell *uc); // move word forward back

#endif // ___UCELL_H___

