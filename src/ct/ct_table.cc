/*
 * ct_table.cc
 *
 * Copyright 2009-2024
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

#include "ct_table.h"
#include "ct_clipboard.h"
#include "ct_main_win.h"
#include "ct_actions.h"
#include "ct_storage_sqlite.h"
#include "ct_storage_xml.h"
#include "ct_logging.h"
#include "ct_misc_utils.h"
#include "ct_command_bridge.h"
#include "ct_list.h"

CtTableCommon::CtTableCommon(CtMainWin* pCtMainWin,
                             const int colWidthDefault,
                             const int charOffset,
                             const std::string& justification,
                             const CtTableColWidths& colWidths,
                             const size_t currRow,
                             const size_t currCol)
 : CtAnchoredWidget{pCtMainWin, charOffset, justification}
 , _colWidthDefault{colWidthDefault}
 , _colWidths{colWidths}
 , _currentRow{currRow}
 , _currentColumn{currCol}
{
}

bool CtTableCommon::get_is_light() const
{
    return dynamic_cast<const CtTableLight*>(this);
}

std::shared_ptr<CtAnchoredWidgetState_TableCommon> CtTableCommon::get_state_common() const
{
    // Command pattern uses XML snapshots instead of widget state objects
    return nullptr;
}

void CtTableCommon::row_move_down(const size_t rowIdx)
{
    if (rowIdx == get_num_rows()-1) {
        return;
    }
    const size_t rowIdxDown = rowIdx + 1;
    row_move_up(rowIdxDown, true/*from_move_down*/);
    _currentRow = rowIdxDown;
    grab_focus();
}

void CtTableCommon::set_current_row_column(const size_t rowIdx, const size_t colIdx)
{
    if (rowIdx < get_num_rows()) {
        _currentRow = rowIdx;
    }
    else {
        spdlog::warn("?? {} row {} >= {}", __FUNCTION__, rowIdx, get_num_rows());
    }
    if (colIdx < get_num_columns()) {
        _currentColumn = colIdx;
    }
    else {
        spdlog::warn("?? {} col {} >= {}", __FUNCTION__, colIdx, get_num_columns());
    }
}

bool CtTableCommon::on_table_button_press_event(GdkEventButton* event)
{
    _pCtMainWin->get_ct_actions()->curr_table_anchor = this;
    if (event->button != 3/*right button*/ and event->type != GDK_2BUTTON_PRESS and event->type != GDK_3BUTTON_PRESS) {
        _pCtMainWin->get_ct_actions()->object_set_selection(this);
    }
    return false;
}

void CtTableCommon::on_cell_populate_popup(Gtk::Menu* menu)
{
    if (not _pCtMainWin->user_active()) return;
    const size_t rowIdx = current_row();
    const size_t colIdx = current_column();
    _pCtMainWin->get_ct_actions()->curr_table_anchor = this;
    const bool first_row = 0 == rowIdx;
    const bool first_col = 0 == colIdx;
    const bool last_row = get_num_rows()-1 == rowIdx;
    const bool last_col = get_num_rows() and get_num_columns()-1 == colIdx;
    const bool is_rich = (get_type() == CtAnchWidgType::TableRich);
    _pCtMainWin->get_ct_menu().build_popup_menu_table_cell(menu, first_row, first_col, last_row, last_col, is_rich);
}

bool CtTableCommon::on_cell_key_press_event(GdkEventKey* event)
{
    if (not _pCtMainWin->user_active()) return false;

    // For rich cells: flush the undo session BEFORE the keystroke so the preceding
    // word gets its own undo entry (mirrors main-page key-press behaviour).
    if (get_type() == CtAnchWidgType::TableRich) {
        if (GDK_KEY_Return == event->keyval || GDK_KEY_KP_Enter == event->keyval ||
            GDK_KEY_space == event->keyval ||
            GDK_KEY_BackSpace == event->keyval || GDK_KEY_Delete == event->keyval)
        {
            auto* bridge = _pCtMainWin->get_command_bridge();
            if (bridge && bridge->isActive() && bridge->isTrackingRichCell()) {
                bridge->flushRichCellSession();
            }
        }
    }

    const size_t rowIdx = current_row();
    const size_t colIdx = current_column();
    _pCtMainWin->get_ct_actions()->curr_table_anchor = this;
    int index{-1};
    if (event->keyval == GDK_KEY_Tab or event->keyval == GDK_KEY_ISO_Left_Tab) {
        // In rich cells, Tab/Shift+Tab indent/unindent list items; otherwise insert tab
        if (get_type() == CtAnchWidgType::TableRich) {
            if (auto* pTable = dynamic_cast<CtTableRich*>(this)) {
                CtTextView& cellTextView = pTable->curr_cell_text_view();
                auto cellBuffer = cellTextView.get_buffer();
                if (!cellBuffer->get_has_selection()) {
                    auto iter_insert = cellBuffer->get_insert()->get_iter();
                    CtListInfo list_info = CtList{_pCtConfig, cellBuffer}.get_paragraph_list_info(iter_insert);
                    if (list_info) {
                        bool level_increase = !(event->state & Gdk::SHIFT_MASK);
                        if (level_increase || list_info.level) {
                            cellTextView.list_change_level(iter_insert, list_info, level_increase);
                            return true;
                        }
                    }
                }
            }
            return false; // let GtkSourceView insert the tab character
        }
        if (event->state & Gdk::SHIFT_MASK) {
            index = rowIdx * get_num_columns() + colIdx - 1;
        }
        else {
            index = rowIdx * get_num_columns() + colIdx + 1;
        }
    }
    else if (event->state & Gdk::CONTROL_MASK) {
        if (not (event->state & Gdk::MOD1_MASK)) {
            if (event->keyval == GDK_KEY_space) {
                CtTextView& textView = _pCtMainWin->get_text_view();
                Gtk::TextIter text_iter = textView.get_buffer()->get_iter_at_child_anchor(getTextChildAnchor());
                text_iter.forward_char();
                textView.get_buffer()->place_cursor(text_iter);
                textView.mm().grab_focus();
                return true;
            }
        }
        if (event->keyval == GDK_KEY_backslash) {
            if (event->state & Gdk::MOD1_MASK) {
                if (rowIdx > 0) {
                    index = (rowIdx-1) * get_num_columns() + colIdx;
                }
            }
            else {
                if ((rowIdx+1) < get_num_rows()) {
                    index = (rowIdx+1) * get_num_columns() + colIdx;
                }
            }
        }
        if (GDK_KEY_Return == event->keyval or GDK_KEY_KP_Enter == event->keyval) {
            return _on_cell_key_press_alt_or_ctrl_enter();
        }
    }
    else if (event->state & Gdk::MOD1_MASK) {
        if (GDK_KEY_Return == event->keyval or GDK_KEY_KP_Enter == event->keyval) {
            return _on_cell_key_press_alt_or_ctrl_enter();
        }
    }
    else if (GDK_KEY_Up == event->keyval) {
        if (not get_is_light()) {
            const int curr_line_num = get_curr_cell_curr_line_num();
            if (0 == curr_line_num and rowIdx > 0) {
                index = (rowIdx - 1) * get_num_columns() + colIdx;
            }
        }
    }
    else if (GDK_KEY_Down == event->keyval) {
        if (not get_is_light()) {
            const int curr_line_num = get_curr_cell_curr_line_num();
            const int max_line_num = get_curr_cell_max_line_num();
            if (max_line_num == curr_line_num and rowIdx < (get_num_rows()-1)) {
                index = (rowIdx + 1) * get_num_columns() + colIdx;
            }
        }
    }
    else if (GDK_KEY_Left == event->keyval) {
        if (not get_is_light()) {
            const int curr_offset = get_curr_cell_curr_offset();
            if (0 == curr_offset) {
                const int curr_index = rowIdx * get_num_columns() + colIdx;
                if (curr_index > 0) {
                    index = curr_index - 1;
                }
            }
        }
    }
    else if (GDK_KEY_Right == event->keyval) {
        if (not get_is_light()) {
            const int curr_offset = get_curr_cell_curr_offset();
            const int max_offset = get_curr_cell_max_offset();
            if (max_offset == curr_offset) {
                const int curr_index = rowIdx * get_num_columns() + colIdx;
                const int max_index = get_num_rows() * get_num_columns() - 1;
                if (curr_index < max_index) {
                    index = curr_index + 1;
                }
            }
        }
    }
    if (index >= 0) {
        const size_t nextRowIdx = index / get_num_columns();
        const size_t nextColIdx = index % get_num_columns();
        if ( nextRowIdx < get_num_rows() and
             nextColIdx < get_num_columns() )
        {
            _currentRow = nextRowIdx;
            _currentColumn = nextColIdx;
            grab_focus();
        }
        else {
            _pCtMainWin->get_ct_actions()->table_row_add();
            if ( nextRowIdx < get_num_rows() and
                 nextColIdx < get_num_columns() )
            {
                _currentRow = nextRowIdx;
                _currentColumn = nextColIdx;
                grab_focus();
            }
        }
        return true;
    }
    return false;
}

// Flush the rich cell undo session on word boundaries (Space, Enter, Backspace, Delete)
// so each word/action becomes a separate undo entry — matching main-page behaviour.
bool CtTableCommon::on_rich_cell_key_release_event(GdkEventKey* event)
{
    if (get_type() != CtAnchWidgType::TableRich) return false;
    auto* bridge = _pCtMainWin->get_command_bridge();
    if (!bridge || !bridge->isActive() || !bridge->isTrackingRichCell()) return false;

    if (GDK_KEY_Return == event->keyval || GDK_KEY_KP_Enter == event->keyval ||
        GDK_KEY_space == event->keyval ||
        GDK_KEY_BackSpace == event->keyval || GDK_KEY_Delete == event->keyval)
    {
        bridge->flushRichCellSession();
    }
    return false;
}

/*static*/void CtTableCommon::populate_table_matrix_from_csv(const std::string& filepath,
                                                             CtMainWin* main_win,
                                                             const bool is_light,
                                                             CtTableMatrix& tbl_matrix)
{
    CtCSV::CtStringTable str_tbl = CtCSV::table_from_csv(filepath);
    if (str_tbl.size() and str_tbl.front().size()) {
        const size_t numColumns = str_tbl.front().size();
        size_t currRow{0};
        void* pCell{nullptr};
        for (const auto& row : str_tbl) {
            ++currRow;
            CtTableRow tbl_row;
            size_t currCol{0};
            for (const auto& cell : row) {
                ++currCol;
                if (currCol > numColumns) {
                    spdlog::warn("{} row {} col {} > {}", __FUNCTION__, currRow, currCol, numColumns);
                    break;
                }
                if (is_light) {
                    pCell = new Glib::ustring{cell};
                }
                else {
                    pCell = new CtTextCell{main_win, cell, CtConst::TABLE_CELL_TEXT_ID};
                }
                tbl_row.emplace_back(pCell);
            }
            while (currCol < numColumns) {
                ++currCol;
                if (is_light) {
                    pCell = new Glib::ustring{};
                }
                else {
                    pCell = new CtTextCell{main_win, "", CtConst::TABLE_CELL_TEXT_ID};
                }
                tbl_row.emplace_back(pCell);
            }
            tbl_matrix.emplace_back(tbl_row);
        }
    }
}

void CtTableCommon::to_xml(xmlpp::Element* p_node_parent, const int offset_adjustment, CtStorageCache*, const std::string&/*multifile_dir*/)
{
    std::vector<std::vector<Glib::ustring>> rows;
    write_strings_matrix(rows);
    CtXmlHelper::table_to_xml(p_node_parent,
                              rows,
                              _charOffset+offset_adjustment,
                              _justification,
                              _colWidthDefault,
                              str::join_numbers(_colWidths, ","),
                              CtAnchWidgType::TableLight == get_type());
}

bool CtTableCommon::to_sqlite(sqlite3* pDb, const gint64 node_id, const int offset_adjustment, CtStorageCache*)
{
    bool retVal{true};
    sqlite3_stmt *p_stmt;
    if (sqlite3_prepare_v2(pDb, CtStorageSqlite::TABLE_TABLE_INSERT, -1, &p_stmt, nullptr) != SQLITE_OK) {
        spdlog::error("{}: {}", CtStorageSqlite::ERR_SQLITE_PREPV2, sqlite3_errmsg(pDb));
        retVal = false;
    }
    else {
        xmlpp::Document xml_doc;
        xml_doc.create_root_node("table");
        xml_doc.get_root_node()->set_attribute("col_widths", str::join_numbers(_colWidths, ","));
        if (CtAnchWidgType::TableLight == get_type()) {
            xml_doc.get_root_node()->set_attribute("is_light", "1");
        } else if (CtAnchWidgType::TableRich == get_type()) {
            xml_doc.get_root_node()->set_attribute("is_rich", "1");
        }
        _populate_xml_rows_cells(xml_doc.get_root_node());
        const std::string table_txt = xml_doc.write_to_string();
        sqlite3_bind_int64(p_stmt, 1, node_id);
        sqlite3_bind_int64(p_stmt, 2, _charOffset+offset_adjustment);
        sqlite3_bind_text(p_stmt, 3, _justification.c_str(), _justification.size(), SQLITE_STATIC);
        sqlite3_bind_text(p_stmt, 4, table_txt.c_str(), table_txt.size(), SQLITE_STATIC);
        sqlite3_bind_int64(p_stmt, 5, _colWidthDefault); // todo get rid of column min
        sqlite3_bind_int64(p_stmt, 6, _colWidthDefault);
        if (sqlite3_step(p_stmt) != SQLITE_DONE) {
            spdlog::error("{}: {}", CtStorageSqlite::ERR_SQLITE_STEP, sqlite3_errmsg(pDb));
            retVal = false;
        }
        sqlite3_finalize(p_stmt);
    }
    return retVal;
}

std::pair<size_t, size_t> CtTableCommon::get_row_idx_col_idx(const size_t cell_idx) const
{
    const size_t num_columns = get_num_columns();
    const size_t rowIdx = cell_idx / num_columns;
    const size_t colIdx = cell_idx % num_columns;
    return std::make_pair(rowIdx, colIdx);
}

CtTableHeavy::CtTableHeavy(CtMainWin* pCtMainWin,
                 CtTableMatrix& tableMatrix,
                 const int colWidthDefault,
                 const int charOffset,
                 const std::string& justification,
                 const CtTableColWidths& colWidths,
                 const size_t currRow,
                 const size_t currCol)
 : CtTableCommon{pCtMainWin, colWidthDefault, charOffset, justification, colWidths, currRow, currCol}
 , _tableMatrix{tableMatrix}
{
    // enforce same number of columns per row
    size_t numCols{0u};
    const size_t numRows = get_num_rows();
    for (size_t r = 0u; r < numRows; ++r) {
        if (_tableMatrix[r].size() > numCols) { numCols = _tableMatrix[r].size(); }
    }
    for (size_t r = 0u; r < numRows; ++r) {
        while (_tableMatrix[r].size() < numCols) { _tableMatrix[r].push_back(new CtTextCell{pCtMainWin, "", CtConst::TABLE_CELL_TEXT_ID}); }
    }

    // column widths can be empty or wrong, trying to fix it
    // so we don't need to check it again and again
    while (_colWidths.size() < numCols) {
        _colWidths.push_back(0); // 0 means we use default width
    }
    for (size_t r = 0u; r < numRows; ++r) {
        for (size_t c = 0u; c < numCols; ++c) {
            _new_text_cell_attach(r, c, static_cast<CtTextCell*>(_tableMatrix.at(r).at(c)));
        }
    }

    _grid.set_column_spacing(1);
    _grid.set_row_spacing(1);
    _grid.signal_button_press_event().connect(sigc::mem_fun(*this, &CtTableCommon::on_table_button_press_event), false);
    _grid.signal_set_focus_child().connect(sigc::mem_fun(*this, &CtTableHeavy::_on_grid_set_focus_child));

    _frame.get_style_context()->add_class("ct-table");
    _frame.add(_grid);
    _frame.signal_size_allocate().connect(sigc::mem_fun(*this, &CtTableHeavy::_on_frame_size_allocate));
    show_all();
}

CtTableHeavy::~CtTableHeavy()
{
    for (CtTableRow& tableRow : _tableMatrix) {
        for (void* pTextCell : tableRow) {
            delete static_cast<CtTextCell*>(pTextCell);
        }
    }
}

void CtTableHeavy::write_strings_matrix(std::vector<std::vector<Glib::ustring>>& rows) const
{
    rows.reserve(get_num_rows());
    for (const auto& row : _tableMatrix) {
        rows.push_back(std::vector<Glib::ustring>{});
        rows.back().reserve(get_num_columns());
        for (void* cell : row) {
            rows.back().push_back(static_cast<CtTextCell*>(cell)->get_text_content());
        }
    }
}

void CtTableHeavy::_new_text_cell_attach(const size_t rowIdx, const size_t colIdx, CtTextCell* pTextCell)
{
    CtTextView& ctTextView = pTextCell->get_text_view();
    auto& textView = ctTextView.mm();
    const bool is_header = 0 == rowIdx;
    textView.set_size_request(get_col_width(colIdx), -1);
    gtk_source_view_set_highlight_current_line(GTK_SOURCE_VIEW(ctTextView.gobj()), false);
    if (is_header) {
        _apply_remove_header_style(true/*isApply*/, ctTextView);
    }
    textView.signal_populate_popup().connect(sigc::mem_fun(*this, &CtTableCommon::on_cell_populate_popup));
    textView.signal_key_press_event().connect(sigc::mem_fun(*this, &CtTableCommon::on_cell_key_press_event), false);

    _grid.attach(pTextCell->get_text_view().mm(), colIdx, rowIdx, 1/*# cell horiz*/, 1/*# cell vert*/);

    _pCtMainWin->apply_syntax_highlighting(pTextCell->get_buffer(), pTextCell->get_syntax_highlighting(), false/*forceReApply*/);
    textView.show();
}

void CtTableHeavy::_apply_styles_to_cells(const bool forceReApply)
{
    for (CtTableRow& tableRow : _tableMatrix) {
        for (void* pTextCell : tableRow) {
            _pCtMainWin->apply_syntax_highlighting(static_cast<CtTextCell*>(pTextCell)->get_buffer(),
                                                   static_cast<CtTextCell*>(pTextCell)->get_syntax_highlighting(), forceReApply);
        }
    }
}

void CtTableHeavy::apply_syntax_highlighting(const bool forceReApply)
{
    _apply_styles_to_cells(forceReApply);
}

void CtTableHeavy::_populate_xml_rows_cells(xmlpp::Element* p_table_node) const
{
    auto row_to_xml = [&](const CtTableRow& tableRow) {
        xmlpp::Element* p_row_node = p_table_node->add_child("row");
        for (void* pTextCell : tableRow) {
            xmlpp::Element* p_cell_node = p_row_node->add_child("cell");
            p_cell_node->add_child_text(static_cast<CtTextCell*>(pTextCell)->get_text_content());
        }
    };

    // put header at the end
    bool is_header{true};
    for (const CtTableRow& tableRow : _tableMatrix) {
        if (is_header) { is_header = false; continue; }
        row_to_xml(tableRow);
    }
    row_to_xml(_tableMatrix.front());
}

std::string CtTableHeavy::to_csv() const
{
    CtCSV::CtStringTable tbl;
    tbl.reserve(get_num_rows());
    const size_t numColumns = get_num_columns();
    for (const CtTableRow& ct_row : _tableMatrix) {
        std::vector<std::string> row;
        row.reserve(numColumns);
        for (void* ct_cell : ct_row) {
            row.emplace_back(static_cast<CtTextCell*>(ct_cell)->get_text_content());
        }
        tbl.emplace_back(row);
    }
    return CtCSV::table_to_csv(tbl);
}

std::shared_ptr<CtAnchoredWidgetState> CtTableHeavy::get_state()
{
    // Command pattern uses XML snapshots instead of widget state objects
    return nullptr;
}

void CtTableHeavy::set_modified_false()
{
    for (CtTableRow& tableRow : _tableMatrix) {
        for (void* pTextCell : tableRow) {
            static_cast<CtTextCell*>(pTextCell)->set_text_buffer_modified_false();
        }
    }
}

void CtTableHeavy::column_add(const size_t afterColIdx, const std::vector<Glib::ustring>* pNewColumn/*= nullptr*/)
{
    const size_t newColIdx = afterColIdx + 1;
    _grid.insert_column(newColIdx);
    _colWidths.insert(_colWidths.begin()+newColIdx, 0);
    const Glib::ustring emptyCell;
    const size_t num_rows = get_num_rows();
    for (size_t rowIdx = 0u; rowIdx < num_rows; ++rowIdx) {
        const Glib::ustring* pStr = not pNewColumn or pNewColumn->size() <= rowIdx ? &emptyCell : &pNewColumn->at(rowIdx);
        auto pTextCell = new CtTextCell{_pCtMainWin, *pStr, CtConst::TABLE_CELL_TEXT_ID};
        _tableMatrix.at(rowIdx).insert(_tableMatrix.at(rowIdx).begin()+newColIdx, pTextCell);
        _new_text_cell_attach(rowIdx, newColIdx, pTextCell);
    }
}

void CtTableHeavy::column_delete(const size_t colIdx)
{
    if (1 == get_num_columns() or colIdx >= get_num_columns()) {
        return;
    }
    _grid.remove_column(colIdx);
    _colWidths.erase(_colWidths.begin()+colIdx);
    for (CtTableRow& tableRow : _tableMatrix) {
        delete static_cast<CtTextCell*>(tableRow.at(colIdx));
        tableRow.erase(tableRow.begin()+colIdx);
    }
    if (_currentColumn == get_num_columns()) {
        --_currentColumn;
    }
    grab_focus();
}

void CtTableHeavy::column_move_left(const size_t colIdx, const bool/*from_move_right*/)
{
    if (0u == colIdx) {
        return;
    }
    const size_t colIdxLeft = colIdx - 1u;
    std::swap(_colWidths[colIdxLeft], _colWidths[colIdx]);
    _grid.remove_column(colIdxLeft);
    _grid.insert_column(colIdx);
    const size_t num_rows = get_num_rows();
    for (size_t rowIdx = 0u; rowIdx < num_rows; ++rowIdx) {
        std::swap(_tableMatrix[rowIdx][colIdxLeft], _tableMatrix[rowIdx][colIdx]);
        CtTextView& ctTextView = static_cast<CtTextCell*>(_tableMatrix.at(rowIdx).at(colIdx))->get_text_view();
        _grid.attach(ctTextView.mm(), colIdx, rowIdx, 1/*# cell horiz*/, 1/*# cell vert*/);
    }
    _currentColumn = colIdxLeft;
}

void CtTableHeavy::column_move_right(const size_t colIdx)
{
    if (colIdx == get_num_columns()-1) {
        return;
    }
    const size_t colIdxRight = colIdx + 1;
    column_move_left(colIdxRight, true/*from_move_right*/);
    _currentColumn = colIdxRight;
    grab_focus();
}

void CtTableHeavy::row_add(const size_t afterRowIdx, const std::vector<Glib::ustring>* pNewRow/*= nullptr*/)
{
    const size_t newRowIdx = afterRowIdx + 1;
    _tableMatrix.insert(_tableMatrix.begin()+newRowIdx, CtTableRow{});
    _grid.insert_row(newRowIdx);
    const Glib::ustring emptyCell;
    const size_t num_columns = get_num_columns();
    for (size_t colIdx = 0u; colIdx < num_columns; ++colIdx) {
        const Glib::ustring* pStr = not pNewRow or pNewRow->size() <= colIdx ? &emptyCell : &pNewRow->at(colIdx);
        auto pTextCell = new CtTextCell{_pCtMainWin, *pStr, CtConst::TABLE_CELL_TEXT_ID};
        _tableMatrix.at(newRowIdx).push_back(pTextCell);
        _new_text_cell_attach(newRowIdx, colIdx, pTextCell);
    }
}

void CtTableHeavy::row_delete(const size_t rowIdx)
{
    if (1 == get_num_rows() or rowIdx >= get_num_rows()) {
        return;
    }
    _grid.remove_row(rowIdx);
    for (void* pTextCell : _tableMatrix.at(rowIdx)) {
        delete static_cast<CtTextCell*>(pTextCell);
    }
    _tableMatrix.erase(_tableMatrix.begin()+rowIdx);
    if (_currentRow == get_num_rows()) {
        --_currentRow;
    }
    grab_focus();
}

void CtTableHeavy::_apply_remove_header_style(const bool isApply, CtTextView& textView)
{
    const char headerStyle[] = "ct-table-header-cell";
    auto rStyleContext = textView.mm().get_style_context();
    if (isApply) {
        if (not rStyleContext->has_class(headerStyle)) {
            rStyleContext->add_class(headerStyle);
            textView.mm().set_wrap_mode(Gtk::WrapMode::WRAP_NONE);
        }
    }
    else {
        if (rStyleContext->has_class(headerStyle)) {
            rStyleContext->remove_class(headerStyle);
            textView.mm().set_wrap_mode(_pCtMainWin->get_ct_config()->lineWrapping ?
                                        Gtk::WrapMode::WRAP_WORD_CHAR : Gtk::WrapMode::WRAP_NONE);
        }
    }
}

void CtTableHeavy::row_move_up(const size_t rowIdx, const bool/*from_move_down*/)
{
    if (0 == rowIdx) {
        return;
    }
    const size_t rowIdxUp = rowIdx - 1;
    _grid.remove_row(rowIdxUp);
    _grid.insert_row(rowIdx);
    std::swap(_tableMatrix[rowIdxUp], _tableMatrix[rowIdx]);
    const size_t num_cols = get_num_columns();
    for (size_t colIdx = 0u; colIdx < num_cols; ++colIdx) {
        CtTextView& textView = static_cast<CtTextCell*>(_tableMatrix.at(rowIdx).at(colIdx))->get_text_view();
        _grid.attach(textView.mm(), colIdx, rowIdx, 1/*# cell horiz*/, 1/*# cell vert*/);
        if (0 == rowIdxUp) {
            // we swapped header
            CtTextView& textViewUp = static_cast<CtTextCell*>(_tableMatrix.at(rowIdxUp).at(colIdx))->get_text_view();
            _apply_remove_header_style(true/*isApply*/, textViewUp);
            _apply_remove_header_style(false/*isApply*/, textView);
        }
    }
    _currentRow = rowIdxUp;
}

bool CtTableHeavy::_row_sort(const bool sortAsc)
{
    auto f_need_swap = [sortAsc](const CtTableRow& l, const CtTableRow& r)->bool{
        const size_t minCols = std::min(l.size(), r.size());
        for (size_t i = 0; i < minCols; ++i) {
            const int cmpResult = CtStrUtil::natural_compare(static_cast<CtTextCell*>(l.at(i))->get_text_content(),
                                                             static_cast<CtTextCell*>(r.at(i))->get_text_content());
            if (0 != cmpResult) {
                return sortAsc ? cmpResult < 0 : cmpResult > 0;
            }
        }
        return false; // no swap needed as equal
    };
    // Sort the table rows (skip header row at index 0)
    std::sort(_tableMatrix.begin()+1, _tableMatrix.end(), f_need_swap);

    // Rebuild all rows in grid (command pattern uses XML snapshots, not state comparison)
    const size_t num_rows = get_num_rows();
    for (size_t rowIdx = 1; rowIdx < num_rows; ++rowIdx) {
        _grid.remove_row(rowIdx);
        _grid.insert_row(rowIdx);
        for (size_t colIdx = 0; colIdx < _tableMatrix.at(rowIdx).size(); ++colIdx) {
            CtTextView& textView = static_cast<CtTextCell*>(_tableMatrix.at(rowIdx).at(colIdx))->get_text_view();
            _grid.attach(textView.mm(), colIdx, rowIdx, 1/*# cell horiz*/, 1/*# cell vert*/);
        }
    }
    return true;
}

void CtTableHeavy::set_col_width_default(const int colWidthDefault)
{
    _colWidthDefault = colWidthDefault;
    bool has_default_widths = vec::exists(_colWidths, 0);
    if (has_default_widths) {
        const size_t numRows = get_num_rows();
        const size_t numColumns = get_num_columns();
        for (size_t r = 0u; r < numRows; ++r) {
            for (size_t c = 0u; c < numColumns; ++c) {
                if (0u == _colWidths.at(c)) {
                    CtTextCell* pTextCell = static_cast<CtTextCell*>(_tableMatrix[r][c]);
                    CtTextView& textView = pTextCell->get_text_view();
                    textView.mm().set_size_request(colWidthDefault, -1);
                }
            }
        }
    }
}

void CtTableHeavy::set_col_width(const int colWidth, std::optional<size_t> optColIdx/*= std::nullopt*/)
{
    const size_t c = optColIdx.value_or(_currentColumn);
    _colWidths[c] = colWidth;
    const size_t numRows = get_num_rows();
    for (size_t r = 0u; r < numRows; ++r) {
        CtTextCell* pTextCell = static_cast<CtTextCell*>(_tableMatrix[r][c]);
        CtTextView& textView = pTextCell->get_text_view();
        textView.mm().set_size_request(colWidth, -1);
    }
}

void CtTableHeavy::grab_focus() const
{
    static_cast<CtTextCell*>(_tableMatrix.at(current_row()).at(current_column()))->get_text_view().mm().grab_focus();
}

void CtTableHeavy::set_selection_at_offset_n_delta(const int offset, const int delta) const
{
    curr_cell_text_view().set_selection_at_offset_n_delta(offset, delta);
}

int CtTableHeavy::get_curr_cell_curr_line_num() const
{
    Glib::RefPtr<Gtk::TextBuffer> pCurrCellBuffer = get_buffer(current_row(), current_column());
    Gtk::TextIter iter_insert = pCurrCellBuffer->get_insert()->get_iter();
    return iter_insert.get_line();
}

int CtTableHeavy::get_curr_cell_max_line_num() const
{
    Glib::RefPtr<Gtk::TextBuffer> pCurrCellBuffer = get_buffer(current_row(), current_column());
    Gtk::TextIter iter_end = pCurrCellBuffer->end();
    return iter_end.get_line();
}

int CtTableHeavy::get_curr_cell_curr_offset() const
{
    Glib::RefPtr<Gtk::TextBuffer> pCurrCellBuffer = get_buffer(current_row(), current_column());
    Gtk::TextIter iter_insert = pCurrCellBuffer->get_insert()->get_iter();
    return iter_insert.get_offset();
}

int CtTableHeavy::get_curr_cell_max_offset() const
{
    Glib::RefPtr<Gtk::TextBuffer> pCurrCellBuffer = get_buffer(current_row(), current_column());
    Gtk::TextIter iter_end = pCurrCellBuffer->end();
    return iter_end.get_offset();
}

CtTextView& CtTableHeavy::curr_cell_text_view() const
{
    return static_cast<CtTextCell*>(_tableMatrix.at(current_row()).at(current_column()))->get_text_view();
}

Glib::RefPtr<Gtk::TextBuffer> CtTableHeavy::get_buffer(const size_t rowIdx, const size_t colIdx) const
{
    if (rowIdx < get_num_rows() and colIdx < get_num_columns()) {
        return static_cast<CtTextCell*>(_tableMatrix.at(rowIdx).at(colIdx))->get_buffer();
    }
    return Glib::RefPtr<Gtk::TextBuffer>{};
}

Glib::ustring CtTableHeavy::get_line_content(size_t rowIdx, size_t colIdx, int match_end_offset) const
{
    Glib::RefPtr<Gtk::TextBuffer> pBuffer = get_buffer(rowIdx, colIdx);
    if (pBuffer) {
        return CtTextIterUtil::get_line_content(pBuffer, match_end_offset);
    }
    return "!?";
}

void CtTableHeavy::_on_grid_set_focus_child(Gtk::Widget* pWidget)
{
    auto* bridge = _pCtMainWin->get_command_bridge();

    if (pWidget == nullptr) {
        // Focus left the table — flush any pending widget edit
        if (bridge && bridge->isActive()) {
            bridge->endWidgetEdit();
        }
        return;
    }

    const size_t num_rows = get_num_rows();
    for (size_t rowIdx = 0u; rowIdx < num_rows; ++rowIdx) {
        for (size_t colIdx = 0; colIdx < _tableMatrix[rowIdx].size(); ++colIdx) {
            if (pWidget == &static_cast<CtTextCell*>(_tableMatrix.at(rowIdx).at(colIdx))->get_text_view().mm()) {
                _currentRow = rowIdx;
                _currentColumn = colIdx;
                // Focus entered a cell — begin widget edit tracking
                if (bridge && bridge->isActive()) {
                    CtTreeIter currTreeIter = _pCtMainWin->curr_tree_iter();
                    if (currTreeIter) {
                        bridge->beginWidgetEdit(currTreeIter.get_node_id(), this, (int)rowIdx, (int)colIdx);
                    }
                }
                return;
            }
        }
    }
}

// ─── CtRichCell ──────────────────────────────────────────────────────────────

CtRichCell::CtRichCell(CtMainWin* pCtMainWin, const CtCellContent& content)
 : CtTextCell{pCtMainWin, "", CtConst::RICH_TEXT_ID}
 , _pCtMainWin{pCtMainWin}
{
    populateFromContent(content);
    // Hook custom clipboard handling so copy/paste of images and rich text works
    // in the cell (same pattern as CtCodebox; pCodebox=nullptr means "rich text mode").
    _uClipboardPair = std::make_unique<CtPairCodeboxMainWin>(nullptr, pCtMainWin);
    auto& tv = get_text_view().mm();
    g_signal_connect(G_OBJECT(tv.gobj()), "cut-clipboard",   G_CALLBACK(CtClipboard::on_cut_clipboard),   _uClipboardPair.get());
    g_signal_connect(G_OBJECT(tv.gobj()), "copy-clipboard",  G_CALLBACK(CtClipboard::on_copy_clipboard),  _uClipboardPair.get());
    g_signal_connect(G_OBJECT(tv.gobj()), "paste-clipboard", G_CALLBACK(CtClipboard::on_paste_clipboard), _uClipboardPair.get());
}

CtRichCell::~CtRichCell()
{
    for (auto* w : _embeddedWidgets) { delete w; }
}

void CtRichCell::addEmbeddedWidget(CtAnchoredWidget* pWidget)
{
    _embeddedWidgets.push_back(pWidget);
    auto anchor = pWidget->getTextChildAnchor();
    if (anchor && anchor->get_widgets().empty()) {
        _ctTextview.mm().add_child_at_anchor(*pWidget, anchor);
        pWidget->apply_width_height(_ctTextview.mm().get_allocation().get_width());
        pWidget->apply_syntax_highlighting(false);
    }
}

CtAnchoredWidget* CtRichCell::_createWidgetFromDesc(const CtWidgetDesc& desc, int charOffset) const
{
    const std::string justification = desc.getJustification();
    try {
        if (desc.type == CtAnchWidgType::ImagePng) {
            std::string rawBlob;
            const std::string encodedBlob = desc.getContent();
            if (!encodedBlob.empty()) rawBlob = Glib::Base64::decode(encodedBlob);
            return new CtImagePng{_pCtMainWin, rawBlob, desc.getLink(), charOffset, justification};
        }
        if (desc.type == CtAnchWidgType::ImageAnchor) {
            return new CtImageAnchor{_pCtMainWin, desc.getAnchorName(),
                                     CtAnchorExpCollState::None, charOffset, justification};
        }
        if (desc.type == CtAnchWidgType::ImageLatex) {
            return new CtImageLatex{_pCtMainWin, desc.getContent(), charOffset, justification,
                                    CtImageEmbFile::get_next_unique_id()};
        }
    }
    catch (const std::exception& e) {
        spdlog::error("CtRichCell::_createWidgetFromDesc: {}", e.what());
    }
    return nullptr;
}

void CtRichCell::populateFromContent(const CtCellContent& content)
{
    // Suppress signal handlers during population to avoid recursive lazy-loading
    bool prevUserActive = _pCtMainWin->user_active();
    _pCtMainWin->user_active() = false;
    auto restoreGuard = scope_guard([&](void*){ _pCtMainWin->user_active() = prevUserActive; });

    // Delete old embedded widgets before clearing buffer — same pattern as buildBufferForNode.
    for (auto* w : _embeddedWidgets) { delete w; }
    _embeddedWidgets.clear();

    _rTextBuffer->erase(_rTextBuffer->begin(), _rTextBuffer->end());

    // Reusable lambda to insert one text span with its formatting tags.
    auto insertSpan = [&](const CtTextSpan& span) {
        auto insert_iter = _rTextBuffer->end();
        if (span.attributes.empty()) {
            _rTextBuffer->insert(insert_iter, span.text);
        }
        else {
            std::vector<Glib::ustring> tag_names;
            for (const auto& tag_prop : CtConst::TAG_PROPERTIES) {
                auto it = span.attributes.find(std::string(tag_prop));
                if (it != span.attributes.end() && !it->second.empty()) {
                    tag_names.push_back(_pCtMainWin->get_text_tag_name_exist_or_create(
                        it->first, it->second));
                }
            }
            if (!tag_names.empty()) {
                _rTextBuffer->insert_with_tags_by_name(insert_iter, span.text, tag_names);
            }
            else {
                _rTextBuffer->insert(insert_iter, span.text);
            }
        }
    };

    if (content.embeddedWidgets.empty()) {
        for (const auto& span : content.textSpans) { insertSpan(span); }
        _rTextBuffer->set_modified(false);
        return;
    }

    // Build offset-keyed index for widget descs so we can interleave them with text spans.
    std::map<int, size_t> widgetByOffset; // charOffset → index in content.embeddedWidgets
    for (size_t i = 0; i < content.embeddedWidgets.size(); ++i) {
        widgetByOffset[content.embeddedWidgets[i].getCharOffset()] = i;
    }

    int curOffset = 0;

    // Insert any widget(s) scheduled at curOffset before advancing.
    auto insertPendingWidgets = [&]() {
        for (;;) {
            auto it = widgetByOffset.find(curOffset);
            if (it == widgetByOffset.end()) break;
            const auto& wd = content.embeddedWidgets[it->second];
            auto* pWidget = _createWidgetFromDesc(wd, curOffset);
            if (pWidget) {
                pWidget->insertInTextBuffer(_rTextBuffer);
                addEmbeddedWidget(pWidget);
            }
            ++curOffset;
        }
    };

    for (const auto& span : content.textSpans) {
        insertPendingWidgets();
        insertSpan(span);
        curOffset += static_cast<int>(span.text.length());
    }
    insertPendingWidgets(); // any trailing widgets after all text

    _rTextBuffer->set_modified(false);
}

CtCellContent CtRichCell::extractContent() const
{
    CtCellContent result;

    Gtk::TextIter startIter = _rTextBuffer->begin();
    Gtk::TextIter endIter = _rTextBuffer->end();

    if (startIter == endIter) {
        return result;
    }

    // Build offset→widget lookup from the tracked list.
    std::map<int, CtAnchoredWidget*> widgetByOffset;
    for (auto* w : _embeddedWidgets) {
        widgetByOffset[w->getOffset()] = w;
    }

    std::map<std::string, std::string> currentAttrs = extractAttributesFromIter(startIter);
    Glib::ustring currentText;

    auto finalizeSpan = [&]() {
        if (!currentText.empty()) {
            CtTextSpan span;
            span.text = currentText;
            span.attributes = currentAttrs;
            result.textSpans.push_back(std::move(span));
            currentText.clear();
        }
    };

    Gtk::TextIter iter = startIter;
    while (iter != endIter) {
        // Detect embedded widget anchors.
        auto pAnchor = iter.get_child_anchor();
        if (pAnchor) {
            finalizeSpan();
            auto wIt = widgetByOffset.find(iter.get_offset());
            if (wIt != widgetByOffset.end()) {
                result.embeddedWidgets.push_back(
                    extractWidgetDesc(wIt->second, iter.get_offset()));
            }
            iter.forward_char();
            continue;
        }

        auto iterAttrs = extractAttributesFromIter(iter);
        if (currentAttrs != iterAttrs) {
            finalizeSpan();
            currentAttrs = iterAttrs;
        }
        currentText += iter.get_char();
        iter.forward_char();
    }

    finalizeSpan();
    return result;
}

// ─── CtTableRich ─────────────────────────────────────────────────────────────

CtTableRich::CtTableRich(CtMainWin* pCtMainWin,
                         const std::vector<std::vector<CtCellContent>>& richData,
                         const int colWidthDefault,
                         const int charOffset,
                         const std::string& justification,
                         const CtTableColWidths& colWidths,
                         const size_t currRow,
                         const size_t currCol)
 : CtTableCommon{pCtMainWin, colWidthDefault, charOffset, justification, colWidths, currRow, currCol}
{
    for (const auto& row : richData) {
        _tableMatrix.push_back(CtTableRow{});
        for (const auto& cell : row) {
            _tableMatrix.back().push_back(new CtRichCell{pCtMainWin, cell});
        }
    }

    // Enforce equal column count across rows
    size_t numCols{0u};
    for (const auto& row : _tableMatrix) {
        if (row.size() > numCols) numCols = row.size();
    }
    for (auto& row : _tableMatrix) {
        while (row.size() < numCols) {
            row.push_back(new CtRichCell{pCtMainWin, CtCellContent{}});
        }
    }

    while (_colWidths.size() < numCols) {
        _colWidths.push_back(0);
    }

    const size_t numRows = get_num_rows();
    for (size_t r = 0u; r < numRows; ++r) {
        for (size_t c = 0u; c < numCols; ++c) {
            _new_rich_cell_attach(r, c, static_cast<CtRichCell*>(_tableMatrix.at(r).at(c)));
        }
    }

    _grid.set_column_spacing(1);
    _grid.set_row_spacing(1);
    _grid.signal_button_press_event().connect(sigc::mem_fun(*this, &CtTableCommon::on_table_button_press_event), false);
    _grid.signal_set_focus_child().connect(sigc::mem_fun(*this, &CtTableRich::_on_grid_set_focus_child));

    _frame.get_style_context()->add_class("ct-table");
    _frame.add(_grid);
    _frame.signal_size_allocate().connect(sigc::mem_fun(*this, &CtTableRich::_on_frame_size_allocate));
    show_all();
}

CtTableRich::~CtTableRich()
{
    for (CtTableRow& row : _tableMatrix) {
        for (void* pCell : row) {
            delete static_cast<CtRichCell*>(pCell);
        }
    }
}

void CtTableRich::_new_rich_cell_attach(const size_t rowIdx, const size_t colIdx, CtRichCell* pCell)
{
    CtTextView& ctTextView = pCell->get_text_view();
    auto& textView = ctTextView.mm();
    textView.set_size_request(get_col_width(colIdx), -1);
    gtk_source_view_set_highlight_current_line(GTK_SOURCE_VIEW(ctTextView.gobj()), false);
    textView.signal_populate_popup().connect(sigc::mem_fun(*this, &CtTableCommon::on_cell_populate_popup));
    textView.signal_key_press_event().connect(sigc::mem_fun(*this, &CtTableCommon::on_cell_key_press_event), false);
    textView.signal_key_release_event().connect(sigc::mem_fun(*this, &CtTableCommon::on_rich_cell_key_release_event), false);
    // Clear selection when this cell loses focus so the highlighted text doesn't persist visually
    textView.signal_focus_out_event().connect([pCell](GdkEventFocus*) {
        auto buffer = pCell->get_buffer();
        buffer->place_cursor(buffer->get_iter_at_mark(buffer->get_insert()));
        return false;
    });

    _grid.attach(ctTextView.mm(), colIdx, rowIdx, 1, 1);
    _pCtMainWin->apply_syntax_highlighting(pCell->get_buffer(), pCell->get_syntax_highlighting(), false);
    textView.show();
}

void CtTableRich::_apply_remove_header_style(const bool isApply, CtTextView& textView)
{
    const char headerStyle[] = "ct-table-header-cell";
    auto rStyleContext = textView.mm().get_style_context();
    if (isApply) {
        if (not rStyleContext->has_class(headerStyle)) {
            rStyleContext->add_class(headerStyle);
            textView.mm().set_wrap_mode(Gtk::WrapMode::WRAP_NONE);
        }
    }
    else {
        if (rStyleContext->has_class(headerStyle)) {
            rStyleContext->remove_class(headerStyle);
            textView.mm().set_wrap_mode(_pCtMainWin->get_ct_config()->lineWrapping ?
                                        Gtk::WrapMode::WRAP_WORD_CHAR : Gtk::WrapMode::WRAP_NONE);
        }
    }
}

void CtTableRich::apply_syntax_highlighting(const bool forceReApply)
{
    for (CtTableRow& row : _tableMatrix) {
        for (void* pCell : row) {
            auto* rc = static_cast<CtRichCell*>(pCell);
            _pCtMainWin->apply_syntax_highlighting(rc->get_buffer(), rc->get_syntax_highlighting(), forceReApply);
        }
    }
}

void CtTableRich::to_xml(xmlpp::Element* p_node_parent, const int offset_adjustment,
                         CtStorageCache*, const std::string&)
{
    xmlpp::Element* p_table_node = p_node_parent->add_child("table");
    p_table_node->set_attribute("char_offset", std::to_string(_charOffset + offset_adjustment));
    p_table_node->set_attribute(CtConst::TAG_JUSTIFICATION, _justification);
    p_table_node->set_attribute("col_min", std::to_string(_colWidthDefault));
    p_table_node->set_attribute("col_max", std::to_string(_colWidthDefault));
    p_table_node->set_attribute("col_widths", str::join_numbers(_colWidths, ","));
    p_table_node->set_attribute("is_rich", "1");
    _populate_xml_rows_cells(p_table_node);
}

void CtTableRich::_populate_xml_rows_cells(xmlpp::Element* p_table_node) const
{
    auto row_to_xml = [&](const CtTableRow& tableRow) {
        xmlpp::Element* p_row_node = p_table_node->add_child("row");
        for (void* pCell : tableRow) {
            xmlpp::Element* p_cell_node = p_row_node->add_child("cell");
            auto* richCell = static_cast<CtRichCell*>(pCell);
            CtCellContent content = richCell->extractContent();
            if (content.textSpans.empty() && content.embeddedWidgets.empty()) {
                p_cell_node->add_child("rich_text");
            }
            else {
                for (const auto& span : content.textSpans) {
                    xmlpp::Element* p_rt = p_cell_node->add_child("rich_text");
                    for (const auto& attr : span.attributes) {
                        if (!attr.second.empty()) {
                            p_rt->set_attribute(attr.first, attr.second);
                        }
                    }
                    p_rt->add_child_text(span.text);
                }
                // Serialize embedded widgets (images, anchors, LaTeX)
                for (auto* widget : richCell->getEmbeddedWidgets()) {
                    widget->to_xml(p_cell_node, 0, nullptr, "");
                }
            }
        }
    };

    bool is_header{true};
    for (const CtTableRow& row : _tableMatrix) {
        if (is_header) { is_header = false; continue; }
        row_to_xml(row);
    }
    row_to_xml(_tableMatrix.front());
}

std::string CtTableRich::to_csv() const
{
    CtCSV::CtStringTable tbl;
    tbl.reserve(get_num_rows());
    for (const CtTableRow& row : _tableMatrix) {
        std::vector<std::string> csvRow;
        csvRow.reserve(get_num_columns());
        for (void* pCell : row) {
            csvRow.emplace_back(static_cast<CtRichCell*>(pCell)->get_text_content());
        }
        tbl.emplace_back(std::move(csvRow));
    }
    return CtCSV::table_to_csv(tbl);
}

Glib::ustring CtTableRich::get_line_content(size_t rowIdx, size_t colIdx, int match_end_offset) const
{
    auto buf = get_buffer(rowIdx, colIdx);
    if (buf) {
        return CtTextIterUtil::get_line_content(buf, match_end_offset);
    }
    return "!?";
}

void CtTableRich::set_modified_false()
{
    for (CtTableRow& row : _tableMatrix) {
        for (void* pCell : row) {
            static_cast<CtRichCell*>(pCell)->set_text_buffer_modified_false();
        }
    }
}

CtTextView& CtTableRich::curr_cell_text_view() const
{
    return static_cast<CtRichCell*>(_tableMatrix.at(current_row()).at(current_column()))->get_text_view();
}

Glib::RefPtr<Gtk::TextBuffer> CtTableRich::get_buffer(const size_t rowIdx, const size_t colIdx) const
{
    if (rowIdx < get_num_rows() && colIdx < get_num_columns()) {
        return static_cast<CtRichCell*>(_tableMatrix.at(rowIdx).at(colIdx))->get_buffer();
    }
    return Glib::RefPtr<Gtk::TextBuffer>{};
}

CtRichCell* CtTableRich::getRichCell(size_t row, size_t col) const
{
    return static_cast<CtRichCell*>(_tableMatrix.at(row).at(col));
}

void CtTableRich::write_strings_matrix(std::vector<std::vector<Glib::ustring>>& rows) const
{
    rows.reserve(get_num_rows());
    for (const auto& row : _tableMatrix) {
        rows.push_back({});
        rows.back().reserve(get_num_columns());
        for (void* pCell : row) {
            rows.back().push_back(static_cast<CtRichCell*>(pCell)->get_text_content());
        }
    }
}

void CtTableRich::column_add(const size_t afterColIdx, const std::vector<Glib::ustring>* /*pNewColumn*/)
{
    const size_t newColIdx = afterColIdx + 1;
    _grid.insert_column(newColIdx);
    _colWidths.insert(_colWidths.begin() + newColIdx, 0);
    const size_t numRows = get_num_rows();
    for (size_t r = 0u; r < numRows; ++r) {
        auto* pCell = new CtRichCell{_pCtMainWin, CtCellContent{}};
        _tableMatrix.at(r).insert(_tableMatrix.at(r).begin() + newColIdx, pCell);
        _new_rich_cell_attach(r, newColIdx, pCell);
    }
}

void CtTableRich::column_delete(const size_t colIdx)
{
    if (1 == get_num_columns() || colIdx >= get_num_columns()) return;
    _grid.remove_column(colIdx);
    _colWidths.erase(_colWidths.begin() + colIdx);
    for (CtTableRow& row : _tableMatrix) {
        delete static_cast<CtRichCell*>(row.at(colIdx));
        row.erase(row.begin() + colIdx);
    }
    if (_currentColumn == get_num_columns()) --_currentColumn;
    grab_focus();
}

void CtTableRich::column_move_left(const size_t colIdx, const bool/*from_move_right*/)
{
    if (0u == colIdx) return;
    const size_t colIdxLeft = colIdx - 1u;
    std::swap(_colWidths[colIdxLeft], _colWidths[colIdx]);
    _grid.remove_column(colIdxLeft);
    _grid.insert_column(colIdx);
    const size_t numRows = get_num_rows();
    for (size_t r = 0u; r < numRows; ++r) {
        std::swap(_tableMatrix[r][colIdxLeft], _tableMatrix[r][colIdx]);
        CtTextView& tv = static_cast<CtRichCell*>(_tableMatrix.at(r).at(colIdx))->get_text_view();
        _grid.attach(tv.mm(), colIdx, r, 1, 1);
    }
    _currentColumn = colIdxLeft;
}

void CtTableRich::column_move_right(const size_t colIdx)
{
    if (colIdx == get_num_columns() - 1) return;
    column_move_left(colIdx + 1, true/*from_move_right*/);
    _currentColumn = colIdx + 1;
    grab_focus();
}

void CtTableRich::row_add(const size_t afterRowIdx, const std::vector<Glib::ustring>* /*pNewRow*/)
{
    const size_t newRowIdx = afterRowIdx + 1;
    _tableMatrix.insert(_tableMatrix.begin() + newRowIdx, CtTableRow{});
    _grid.insert_row(newRowIdx);
    const size_t numCols = get_num_columns();
    for (size_t c = 0u; c < numCols; ++c) {
        auto* pCell = new CtRichCell{_pCtMainWin, CtCellContent{}};
        _tableMatrix.at(newRowIdx).push_back(pCell);
        _new_rich_cell_attach(newRowIdx, c, pCell);
    }
}

void CtTableRich::row_delete(const size_t rowIdx)
{
    if (1 == get_num_rows() || rowIdx >= get_num_rows()) return;
    _grid.remove_row(rowIdx);
    for (void* pCell : _tableMatrix.at(rowIdx)) {
        delete static_cast<CtRichCell*>(pCell);
    }
    _tableMatrix.erase(_tableMatrix.begin() + rowIdx);
    if (_currentRow == get_num_rows()) --_currentRow;
    grab_focus();
}

void CtTableRich::row_move_up(const size_t rowIdx, const bool/*from_move_down*/)
{
    if (0 == rowIdx) return;
    const size_t rowIdxUp = rowIdx - 1;
    _grid.remove_row(rowIdxUp);
    _grid.insert_row(rowIdx);
    std::swap(_tableMatrix[rowIdxUp], _tableMatrix[rowIdx]);
    const size_t numCols = get_num_columns();
    for (size_t c = 0u; c < numCols; ++c) {
        CtTextView& tv = static_cast<CtRichCell*>(_tableMatrix.at(rowIdx).at(c))->get_text_view();
        _grid.attach(tv.mm(), c, rowIdx, 1, 1);
    }
    _currentRow = rowIdxUp;
}

bool CtTableRich::_row_sort(const bool sortAsc)
{
    auto f_need_swap = [sortAsc](const CtTableRow& l, const CtTableRow& r) -> bool {
        const size_t minCols = std::min(l.size(), r.size());
        for (size_t i = 0; i < minCols; ++i) {
            const int cmp = CtStrUtil::natural_compare(
                static_cast<CtRichCell*>(l.at(i))->get_text_content(),
                static_cast<CtRichCell*>(r.at(i))->get_text_content());
            if (0 != cmp) return sortAsc ? cmp < 0 : cmp > 0;
        }
        return false;
    };
    std::sort(_tableMatrix.begin() + 1, _tableMatrix.end(), f_need_swap);

    const size_t numRows = get_num_rows();
    for (size_t r = 1; r < numRows; ++r) {
        _grid.remove_row(r);
        _grid.insert_row(r);
        for (size_t c = 0; c < _tableMatrix.at(r).size(); ++c) {
            CtTextView& tv = static_cast<CtRichCell*>(_tableMatrix.at(r).at(c))->get_text_view();
            _grid.attach(tv.mm(), c, r, 1, 1);
        }
    }
    return true;
}

void CtTableRich::set_col_width_default(const int colWidthDefault)
{
    _colWidthDefault = colWidthDefault;
    if (vec::exists(_colWidths, 0)) {
        const size_t numRows = get_num_rows();
        const size_t numCols = get_num_columns();
        for (size_t r = 0u; r < numRows; ++r) {
            for (size_t c = 0u; c < numCols; ++c) {
                if (0u == _colWidths.at(c)) {
                    static_cast<CtRichCell*>(_tableMatrix[r][c])->get_text_view().mm()
                        .set_size_request(colWidthDefault, -1);
                }
            }
        }
    }
}

void CtTableRich::set_col_width(const int colWidth, std::optional<size_t> optColIdx)
{
    const size_t c = optColIdx.value_or(_currentColumn);
    _colWidths[c] = colWidth;
    const size_t numRows = get_num_rows();
    for (size_t r = 0u; r < numRows; ++r) {
        static_cast<CtRichCell*>(_tableMatrix[r][c])->get_text_view().mm()
            .set_size_request(colWidth, -1);
    }
}

void CtTableRich::grab_focus() const
{
    static_cast<CtRichCell*>(_tableMatrix.at(current_row()).at(current_column()))
        ->get_text_view().mm().grab_focus();
}

void CtTableRich::set_selection_at_offset_n_delta(const int offset, const int delta) const
{
    curr_cell_text_view().set_selection_at_offset_n_delta(offset, delta);
}

int CtTableRich::get_curr_cell_curr_line_num() const
{
    auto buf = get_buffer(current_row(), current_column());
    return buf ? buf->get_insert()->get_iter().get_line() : 0;
}

int CtTableRich::get_curr_cell_max_line_num() const
{
    auto buf = get_buffer(current_row(), current_column());
    return buf ? buf->end().get_line() : 0;
}

int CtTableRich::get_curr_cell_curr_offset() const
{
    auto buf = get_buffer(current_row(), current_column());
    return buf ? buf->get_insert()->get_iter().get_offset() : 0;
}

int CtTableRich::get_curr_cell_max_offset() const
{
    auto buf = get_buffer(current_row(), current_column());
    return buf ? buf->end().get_offset() : 0;
}

void CtTableRich::_on_grid_set_focus_child(Gtk::Widget* pWidget)
{
    auto* bridge = _pCtMainWin->get_command_bridge();

    if (pWidget == nullptr) {
        if (bridge && bridge->isActive()) {
            bridge->endWidgetEdit();
        }
        return;
    }

    const size_t numRows = get_num_rows();
    for (size_t r = 0u; r < numRows; ++r) {
        for (size_t c = 0; c < _tableMatrix[r].size(); ++c) {
            if (pWidget == &static_cast<CtRichCell*>(_tableMatrix.at(r).at(c))->get_text_view().mm()) {
                _currentRow = r;
                _currentColumn = c;
                if (bridge && bridge->isActive()) {
                    CtTreeIter currTreeIter = _pCtMainWin->curr_tree_iter();
                    if (currTreeIter) {
                        bridge->beginWidgetEdit(currTreeIter.get_node_id(), this, (int)r, (int)c);
                    }
                }
                return;
            }
        }
    }
}
