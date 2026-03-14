/*
 * ct_gtk_compat.h
 *
 * Copyright 2009-2025
 * Giuseppe Penone <giuspen@gmail.com>
 * Evgenii Gurianov <https://github.com/txe>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#pragma once

#include <gtksourceview/gtksource.h>

// GtkSourceView 5 removed begin/end_not_undoable_action
// Define compatibility macros based on version
#if defined(GTK_SOURCE_MAJOR_VERSION) && (GTK_SOURCE_MAJOR_VERSION >= 5)
#define CT_SOURCE_BUFFER_BEGIN_NOT_UNDOABLE(buf) /* no-op */
#define CT_SOURCE_BUFFER_END_NOT_UNDOABLE(buf)   /* no-op */
#define CT_HAS_UNDO_REDO 0
#else
#define CT_SOURCE_BUFFER_BEGIN_NOT_UNDOABLE(buf) gtk_source_buffer_begin_not_undoable_action(buf)
#define CT_SOURCE_BUFFER_END_NOT_UNDOABLE(buf)   gtk_source_buffer_end_not_undoable_action(buf)
#define CT_HAS_UNDO_REDO 1
#endif
