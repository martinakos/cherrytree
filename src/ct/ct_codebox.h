/*
 * ct_codebox.h
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

#include <gtkmm.h>
#include <gtksourceview/gtksource.h>
#include "ct_const.h"
#include "ct_widgets.h"
#include "ct_text_view.h"

class CtTextCell
{
public:
    CtTextCell(CtMainWin* pCtMainWin,
               const Glib::ustring& textContent,
               const std::string& syntaxHighlighting);
    virtual ~CtTextCell() {}

    Glib::ustring get_text_content() const;
    Glib::RefPtr<Gtk::TextBuffer> get_buffer() const { return _rTextBuffer; }
    CtTextView& get_text_view() { return _ctTextview; }
    const std::string& get_syntax_highlighting() const { return _syntaxHighlighting; }
    void set_syntax_highlighting(const std::string& syntaxHighlighting, GtkSourceLanguageManager* pGtkSourceLanguageManager);
    void set_text_buffer_modified_false() { _rTextBuffer->set_modified(false); }

    // Apply (or clear) a background color to this cell's text view.
    // Pass empty string to remove the override.
    void applyCellBgColor(const std::string& hexColor);

    // Apply (or clear) per-side borders on this cell's text view.
    // Each side width <= 0 means no border on that side.
    // Per-side colors allow different colors on shared edges (collapsed-border model).
    // The four cornerColor* args fill the corner gaps independently: each corner
    // is determined by the two edges meeting there (highest sequence wins).
    void applyCellBorder(int wTop, int wRight, int wBottom, int wLeft,
                         const std::string& colorTop, const std::string& colorRight,
                         const std::string& colorBottom, const std::string& colorLeft,
                         const std::string& cornerColorTL, const std::string& cornerColorTR,
                         const std::string& cornerColorBL, const std::string& cornerColorBR);

    // Returns the BR corner color last set by applyCellBorder (for testing).
    const std::string& getCornerColor() const { return _cornerColorBR; }
    const std::string& getCornerColorTL() const { return _cornerColorTL; }
    const std::string& getCornerColorTR() const { return _cornerColorTR; }
    const std::string& getCornerColorBL() const { return _cornerColorBL; }
    const std::string& getCornerColorBR() const { return _cornerColorBR; }

protected:
    std::string _syntaxHighlighting;
    Glib::RefPtr<Gtk::TextBuffer> _rTextBuffer{};
    CtTextView _ctTextview;
    std::unique_ptr<CtPairCodeboxMainWin> _uCtPairCodeboxMainWin;
    Glib::RefPtr<Gtk::CssProvider> _rCssProviderCellBg;
    Glib::RefPtr<Gtk::CssProvider> _rCssProviderCellBorder;
    std::string _cornerColorTL;
    std::string _cornerColorTR;
    std::string _cornerColorBL;
    std::string _cornerColorBR;
    std::string _borderColorTop;
    std::string _borderColorRight;
    std::string _borderColorBottom;
    std::string _borderColorLeft;
    int _borderWTop{0};
    int _borderWRight{0};
    int _borderWBottom{0};
    int _borderWLeft{0};
    sigc::connection _drawConn;
    sigc::connection _sizeAllocConn;
};

class CtCodebox : public CtAnchoredWidget, public CtTextCell
{
public:
    enum { CB_WIDTH_HEIGHT_STEP_PIX = 15,
           CB_WIDTH_HEIGHT_STEP_PERC = 9,
           CB_WIDTH_LIMIT_MIN = 40,
           CB_HEIGHT_LIMIT_MIN = 30
         };

public:
    CtCodebox(CtMainWin* pCtMainWin,
              const Glib::ustring& textContent,
              const std::string& syntaxHighlighting,
              const int frameWidth,
              const int frameHeight,
              const int charOffset,
              const std::string& justification,
              const bool widthInPixels,
              const bool highlightBrackets,
              const bool showLineNumbers);

    void apply_width_height(const int parentTextWidth) override;
    void apply_syntax_highlighting(const bool forceReApply) override;
    void to_xml(xmlpp::Element* p_node_parent, const int offset_adjustment, CtStorageCache* cache, const std::string& multifile_dir) override;
    bool to_sqlite(sqlite3* pDb, const gint64 node_id, const int offset_adjustment, CtStorageCache* cache) override;
    void set_modified_false() override { set_text_buffer_modified_false(); }
    CtAnchWidgType get_type() const override { return CtAnchWidgType::CodeBox; }
    std::shared_ptr<CtAnchoredWidgetState> get_state() override;
    CtWidgetDesc to_widget_desc(int charOffset) override;

    void apply_zoom(const double scaleFactor);
    void set_width_height(int newWidth, int newHeight);
    void set_width_in_pixels(const bool widthInPixels) { _widthInPixels = widthInPixels; }
    void set_highlight_brackets(const bool highlightBrackets);
    void set_show_line_numbers(const bool showLineNumbers);
    void apply_cursor_pos(const int cursorPos);
    void update_toolbar_buttons();

    bool get_width_in_pixels() const { return _widthInPixels; }
    int  get_frame_width() const {
        if (_widthInPixels and _ctTextview.mm().get_allocated_width() > _frameWidth) {
            return _ctTextview.mm().get_allocated_width();
        }
        return _frameWidth;
    }
    int  get_frame_height() const { return _frameHeight; }
    bool get_highlight_brackets() const { return _highlightBrackets; }
    bool get_show_line_numbers() const { return _showLineNumbers; }

private:
#if GTKMM_MAJOR_VERSION < 4 && !defined(GTKMM_DISABLE_DEPRECATED)
    bool _on_key_press_event(GdkEventKey* event);
#endif
    void _set_scrollbars_policies();

private:
    int    _frameWidth;
    int    _frameHeight;
    double _zoomFactor{1.0};
    bool   _widthInPixels{true};
    Glib::RefPtr<Gtk::CssProvider> _rCssProviderZoom;
    // CSS provider scoped to the scrolled window that scales the scrollbar
    // dimensions (slider, trough) so the gutter shrinks/grows with zoom.
    Glib::RefPtr<Gtk::CssProvider> _rCssProviderScrollbarZoom;
    bool _highlightBrackets{true};
    bool _showLineNumbers{false};
    Gtk::ScrolledWindow _scrolledwindow;
#if GTKMM_MAJOR_VERSION >= 4
    Gtk::Box _hbox{Gtk::Orientation::HORIZONTAL};
#else
    Gtk::Box _hbox{Gtk::ORIENTATION_HORIZONTAL};
#endif
    #if GTKMM_MAJOR_VERSION < 4 && !defined(GTKMM_DISABLE_DEPRECATED)
    Gtk::Toolbar _toolbar;
    Gtk::ToolButton _toolButtonPlay;
    Gtk::ToolButton _toolButtonCopy;
    Gtk::ToolButton _toolButtonProp;
#endif /* GTKMM_MAJOR_VERSION < 4 && !defined(GTKMM_DISABLE_DEPRECATED) */
};
