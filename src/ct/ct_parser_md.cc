/*
 * ct_parser_md.cc
 *
 * Copyright 2009-2022
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

#include "ct_parser.h"
#include "ct_const.h"
#include "ct_misc_utils.h"
#include "ct_logging.h"
#include "ct_config.h"
#include <cctype>

CtMDParser::CtMDParser(CtConfig* config) : CtDocBuildingParser{config}, _pCtConfig{config}, _text_parser{std::make_unique<CtTextParser>(_token_schemas())} {}

void CtMDParser::_add_scale_to_last(int level) {
    doc_builder().with_last_element([this, level]{ doc_builder().add_scale_tag(level, std::nullopt); });
}

std::vector<CtTextParser::token_schema> CtMDParser::_token_schemas()
{
    auto add_codebox = [this](const std::string& data) {
        auto data_iter = data.begin();
        while (data_iter != data.end() && *data_iter != '\n') {
            ++data_iter;
        }
        std::string text(data_iter, data.end());
        std::string lang;
        if (data_iter != data.begin()) {
            lang.append(data.begin(), data_iter);
        }
        spdlog::debug("CODEBOX: {}, lang: {}", text, lang);
        doc_builder().add_codebox(lang, text);

    };
    auto add_h3 = [this](const std::string& text) {
        doc_builder().close_current_tag();
        doc_builder().add_scale_tag(3, text + "\n");
        doc_builder().close_current_tag();
    };
    auto add_list = [this](const std::string& text) {
        doc_builder().add_list(_list_level, "");
        feed(text);
        doc_builder().add_newline();
        _list_level = 0;
    };

    return {
        // Bold
        {"__", true,  true,  [this](const std::string &data) {
            doc_builder().add_weight_tag(CtConst::TAG_PROP_VAL_HEAVY, data);
        }},
        // Italic
        {"_", true, true, [this](const std::string& data){
            doc_builder().add_italic_tag(data);
        }},
        // Italic
        {"*", true, true, [this](const std::string& data){
            doc_builder().add_italic_tag(data);
        }},
        // Bold and italic
        {"***", true, true, [this](const std::string& data){
            doc_builder().add_italic_tag(data);
            doc_builder().add_weight_tag(CtConst::TAG_PROP_VAL_HEAVY, std::nullopt);
        }},
        // Bold
        {"**", true,  true,  [this](const std::string &data) {
            doc_builder().add_weight_tag(CtConst::TAG_PROP_VAL_HEAVY, data);
        }},
        // First part of a link
        {"[",  true,  false, [this](const std::string &data) {
            // Parse for end of display
            auto last_pos = data.find_last_of(']');
            if (last_pos == std::string::npos) {
                spdlog::warn("Unknown data captured: {}; printing as plaintext", data);
                doc_builder().add_text(data);
                return;
            }

            std::string title(data.begin(), data.begin() + last_pos);
            std::string url(data.begin() + last_pos + 2, data.end());

            doc_builder().add_text(title, false);
            doc_builder().add_link(url);
        }, ")", true},
        // Monospace
        {"`", true, true, [this](const std::string& data){
            doc_builder().add_monospace_tag(data);
        }, "`", true},
        // Footnote
        {"[^", true, false, [this](const std::string& data){
            // Todo: Implement footnotes
            doc_builder().add_text("[^" + data + "]");
        }, "]"},
        // Codebox(s)
        {"~~~", true, true, add_codebox, "~~~", true},
        {"```", true, true, add_codebox, "```", true},
        // List
        {"* ", true, false, add_list, "\n", true},
        // Also list
        {"- ", true, false, add_list, "\n", true},

        // Passthroughs for lists
        {" -", true, false, [](const std::string&){}, " "},
        {"-", true, true, [](const std::string&){}},
        // Strikethrough
        {"~~", true,  true,  [this](const std::string &data) {
            doc_builder().add_strikethrough_tag(data);
        }},
        // Passthrough for ``` and `
        {"``", true, true, [this](const std::string& data){ doc_builder().add_text("``" + data + "``"); }},
        // Headers (h1, h2, etc)
        {"# ",  true, false, [this](const std::string &data) {
            doc_builder().close_current_tag();
            doc_builder().add_scale_tag(1, data + "\n");
            doc_builder().close_current_tag();
        }, "\n"},
        {"## ",  true, false, [this](const std::string &data) {
            doc_builder().close_current_tag();
            doc_builder().add_scale_tag(2, data + "\n");
            doc_builder().close_current_tag();
        }, "\n"},
        {"### ",  true, false,  add_h3, "\n"},
        {"#### ",  true, false,  add_h3, "\n"},
        {"##### ",  true, false,  add_h3, "\n"},
        {"###### ",  true, false,  add_h3, "\n"},

        // H1
        {"\n==", true, false, [this](const std::string&){
            _add_scale_to_last(1);
            doc_builder().add_newline();
        }, "\n"},
        // H2
        {"\n----", true, false, [this](const std::string&){
            _add_scale_to_last(2);
            doc_builder().add_newline();
        }, "\n"},
        // Horizontal divider
        {"***\n", true, false, [this](const std::string&){
            doc_builder().add_hrule();
        }, " "},
        // Tables

        // Table row
        {"|", true, false, [this](const std::string& data){
            //spdlog::debug("Got end: {}", data);
            _add_table_cell(data);
        }, "\n"},
        // Table header divider
        {"| -", true, false, [](const std::string&){
            // Since cherrytree tables don't use headers, this is not needed
        }, "- |\n"},
        // Image link
        {"![", true, false, [this](const std::string& data){
            auto last_pos = data.find_last_of(']');
            if (last_pos == std::string::npos) {
                spdlog::warn("Image captured unknown data: <{}>; printing as plaintext", data);
                doc_builder().add_text(data);
                return;
            }

            std::string title(data.begin(), data.begin() + last_pos);
            std::string uri(data.begin() + last_pos + 2, data.end());

            doc_builder().add_text(title, false);
            doc_builder().add_image(uri);
        }, ")", true},
        // Link
        {"<", true, false, [this](const std::string& data){
            doc_builder().add_text(data, false);
            doc_builder().add_link(data);
        }, ">", true}
    };
}

namespace {

// A run of text with one formatting
struct CtMdSpan {
    std::string text;
    bool        bold{false};
    bool        italic{false};
    bool        strike{false};
    bool        mono{false};
    std::string link;
    std::string image; // the span is an image with this path or URL, text is its title
};

bool md_is_space(const char ch) { return ' ' == ch or '\t' == ch or '\n' == ch or '\r' == ch; }
bool md_is_punct(const char ch) { return ch > 0 and std::ispunct(static_cast<unsigned char>(ch)); }

std::string md_trim(const std::string& text)
{
    const size_t begin = text.find_first_not_of(" \t");
    if (std::string::npos == begin) return "";
    return text.substr(begin, text.find_last_not_of(" \t") - begin + 1);
}

// Leading whitespace in columns (a tab goes to the next multiple of 4) and where the text starts
size_t md_indent(const std::string& line, size_t& rTextStart)
{
    size_t cols{0};
    for (rTextStart = 0; rTextStart < line.size(); ++rTextStart) {
        if (' ' == line[rTextStart]) ++cols;
        else if ('\t' == line[rTextStart]) cols += 4 - cols % 4;
        else break;
    }
    return cols;
}

// Finds "label](destination)" with the label starting at labelStart, just after the '['
bool md_find_link(const std::string& text, const size_t labelStart, std::string& rLabel, std::string& rDest, size_t& rEnd)
{
    int depth{1};
    size_t j = labelStart;
    for (; j < text.size(); ++j) {
        if ('\\' == text[j]) ++j;
        else if ('[' == text[j]) ++depth;
        else if (']' == text[j] and 0 == --depth) break;
    }
    if (j + 1 >= text.size() or '(' != text[j + 1]) return false;
    int parens{1};
    size_t k = j + 2;
    for (; k < text.size(); ++k) {
        if ('\\' == text[k]) ++k;
        else if ('(' == text[k]) ++parens;
        else if (')' == text[k] and 0 == --parens) break;
    }
    if (k >= text.size()) return false;
    std::string dest = md_trim(text.substr(j + 2, k - j - 2));
    if (not dest.empty() and '<' == dest.front()) {
        const size_t close = dest.find('>');
        if (std::string::npos == close) return false;
        dest = dest.substr(1, close - 1);
    }
    else {
        dest = dest.substr(0, dest.find_first_of(" \t")); // drops a title
    }
    if (dest.empty()) return false;
    rLabel = text.substr(labelStart, j - labelStart);
    rDest = dest;
    rEnd = k + 1;
    return true;
}

// One piece of a line: literal text, a run of emphasis delimiters, or spans already resolved
struct CtMdItem {
    std::string           text;
    char                  delim{0}; // '*', '_' or '~' for a delimiter run
    int                   count{0}; // delimiter characters not matched yet
    int                   origCount{0};
    bool                  canOpen{false};
    bool                  canClose{false};
    bool                  active{true};
    int                   boldOpen{0}, italicOpen{0}, strikeOpen{0};
    int                   boldClose{0}, italicClose{0}, strikeClose{0};
    std::vector<CtMdSpan> spans; // a code span, link or image
};

void md_inline(const std::string& text, const CtMdSpan& base, std::vector<CtMdSpan>& rSpans);

// Matches emphasis delimiters as CommonMark does
void md_match_emphasis(std::vector<CtMdItem>& items)
{
    for (size_t c = 0; c < items.size(); ++c) {
        CtMdItem& closer = items[c];
        if (not closer.delim or not closer.canClose) continue;
        while (closer.count > 0) {
            size_t o = c;
            bool found{false};
            while (o-- > 0) {
                const CtMdItem& opener = items[o];
                if (opener.delim != closer.delim or not opener.canOpen or not opener.active or opener.count <= 0) continue;
                // "rule of 3": a run that can both open and close does not pair across a multiple of 3
                const bool ruleOf3 = (opener.canClose or closer.canOpen) and
                                     0 == (opener.origCount + closer.origCount) % 3 and
                                     not (0 == opener.origCount % 3 and 0 == closer.origCount % 3);
                if ('~' == closer.delim or not ruleOf3) {
                    found = true;
                    break;
                }
            }
            if (not found) break;
            CtMdItem& opener = items[o];
            int use{2};
            if ('~' == closer.delim) {
                ++opener.strikeOpen;
                ++closer.strikeClose;
            }
            else if (opener.count >= 2 and closer.count >= 2) {
                ++opener.boldOpen;
                ++closer.boldClose;
            }
            else {
                use = 1;
                ++opener.italicOpen;
                ++closer.italicClose;
            }
            opener.count -= use;
            closer.count -= use;
            for (size_t k = o + 1; k < c; ++k) {
                items[k].active = false;
            }
        }
    }
}

void md_inline(const std::string& text, const CtMdSpan& base, std::vector<CtMdSpan>& rSpans)
{
    std::vector<CtMdItem> items;
    std::string buf;
    auto flush = [&]() {
        if (buf.empty()) return;
        items.emplace_back();
        items.back().text = std::move(buf);
        buf.clear();
    };
    const size_t n = text.size();
    size_t i{0};
    while (i < n) {
        const char ch = text[i];
        if ('\\' == ch and i + 1 < n and md_is_punct(text[i + 1])) {
            buf += text[i + 1];
            i += 2;
            continue;
        }
        if ('`' == ch) {
            size_t runEnd = i;
            while (runEnd < n and '`' == text[runEnd]) ++runEnd;
            const size_t len = runEnd - i;
            size_t close{std::string::npos};
            for (size_t j = runEnd; j < n;) {
                if ('`' != text[j]) {
                    ++j;
                    continue;
                }
                size_t k = j;
                while (k < n and '`' == text[k]) ++k;
                if (k - j == len) {
                    close = j;
                    break;
                }
                j = k;
            }
            if (std::string::npos == close) {
                buf.append(text, i, len);
                i = runEnd;
                continue;
            }
            CtMdSpan span{base};
            span.text = text.substr(runEnd, close - runEnd);
            if (span.text.size() >= 2 and ' ' == span.text.front() and ' ' == span.text.back() and
                std::string::npos != span.text.find_first_not_of(' '))
            {
                span.text = span.text.substr(1, span.text.size() - 2);
            }
            span.mono = true;
            flush();
            items.emplace_back();
            items.back().spans.push_back(std::move(span));
            i = close + len;
            continue;
        }
        if ('[' == ch or ('!' == ch and i + 1 < n and '[' == text[i + 1])) {
            const bool isImage = '!' == ch;
            std::string label, dest;
            size_t end{0};
            if (md_find_link(text, i + (isImage ? 2 : 1), label, dest, end)) {
                flush();
                items.emplace_back();
                if (isImage) {
                    CtMdSpan span{base};
                    span.text = label;
                    span.image = dest;
                    items.back().spans.push_back(std::move(span));
                }
                else {
                    CtMdSpan linkBase{base};
                    linkBase.link = dest;
                    md_inline(label, linkBase, items.back().spans);
                }
                i = end;
                continue;
            }
        }
        if ('<' == ch) {
            const size_t close = text.find('>', i + 1);
            if (std::string::npos != close) {
                const std::string url = text.substr(i + 1, close - i - 1);
                if (std::string::npos == url.find_first_of(" \t<") and
                    (std::string::npos != url.find("://") or 0 == url.rfind("mailto:", 0)))
                {
                    CtMdSpan span{base};
                    span.text = url;
                    span.link = url;
                    flush();
                    items.emplace_back();
                    items.back().spans.push_back(std::move(span));
                    i = close + 1;
                    continue;
                }
            }
        }
        if ('*' == ch or '_' == ch or '~' == ch) {
            size_t runEnd = i;
            while (runEnd < n and ch == text[runEnd]) ++runEnd;
            const int len = static_cast<int>(runEnd - i);
            if ('~' != ch or 2 == len) {
                const char prev = i > 0 ? text[i - 1] : ' ';
                const char next = runEnd < n ? text[runEnd] : ' ';
                const bool leftFlanking = not md_is_space(next) and
                                          (not md_is_punct(next) or md_is_space(prev) or md_is_punct(prev));
                const bool rightFlanking = not md_is_space(prev) and
                                           (not md_is_punct(prev) or md_is_space(next) or md_is_punct(next));
                flush();
                items.emplace_back();
                CtMdItem& item = items.back();
                item.delim = ch;
                item.count = item.origCount = len;
                if ('_' == ch) {
                    // no emphasis inside a word, e.g. snake_case
                    item.canOpen = leftFlanking and (not rightFlanking or md_is_punct(prev));
                    item.canClose = rightFlanking and (not leftFlanking or md_is_punct(next));
                }
                else {
                    item.canOpen = leftFlanking;
                    item.canClose = rightFlanking;
                }
                i = runEnd;
                continue;
            }
            buf.append(text, i, runEnd - i);
            i = runEnd;
            continue;
        }
        buf += ch;
        ++i;
    }
    flush();

    md_match_emphasis(items);

    int bold{0}, italic{0}, strike{0};
    auto emit = [&](CtMdSpan span) {
        if (span.text.empty() and span.image.empty()) return;
        span.bold = span.bold or bold > 0;
        span.italic = span.italic or italic > 0;
        span.strike = span.strike or strike > 0;
        if (not rSpans.empty()) {
            CtMdSpan& last = rSpans.back();
            if (last.image.empty() and span.image.empty() and last.bold == span.bold and last.italic == span.italic and
                last.strike == span.strike and last.mono == span.mono and last.link == span.link)
            {
                last.text += span.text;
                return;
            }
        }
        rSpans.push_back(std::move(span));
    };
    for (const CtMdItem& item : items) {
        if (item.delim) {
            bold -= item.boldClose;
            italic -= item.italicClose;
            strike -= item.strikeClose;
            if (item.count > 0) {
                CtMdSpan span{base};
                span.text = std::string(static_cast<size_t>(item.count), item.delim);
                emit(std::move(span));
            }
            bold += item.boldOpen;
            italic += item.italicOpen;
            strike += item.strikeOpen;
        }
        else if (not item.spans.empty()) {
            for (const CtMdSpan& span : item.spans) emit(span);
        }
        else {
            CtMdSpan span{base};
            span.text = item.text;
            emit(std::move(span));
        }
    }
}

std::string md_plain_text(const std::string& text)
{
    std::vector<CtMdSpan> spans;
    md_inline(text, CtMdSpan{}, spans);
    std::string plain;
    for (const CtMdSpan& span : spans) plain += span.text;
    return plain;
}

// The cells of a table row, without the outer pipes
std::vector<std::string> md_table_cells(const std::string& line)
{
    std::string row = md_trim(line);
    if (not row.empty() and '|' == row.front()) row.erase(0, 1);
    if (row.size() >= 2 and '|' == row.back() and '\\' != row[row.size() - 2]) row.pop_back();
    else if (row.size() == 1 and '|' == row.back()) row.pop_back();
    std::vector<std::string> cells;
    std::string cell;
    bool inCode{false};
    for (size_t i = 0; i < row.size(); ++i) {
        if ('\\' == row[i] and i + 1 < row.size() and '|' == row[i + 1]) {
            cell += '|';
            ++i;
            continue;
        }
        if ('`' == row[i]) inCode = not inCode;
        if ('|' == row[i] and not inCode) {
            cells.push_back(md_trim(cell));
            cell.clear();
            continue;
        }
        cell += row[i];
    }
    cells.push_back(md_trim(cell));
    return cells;
}

bool md_is_table_separator(const std::string& line)
{
    if (std::string::npos == line.find('-')) return false;
    for (const std::string& cell : md_table_cells(line)) {
        size_t i{0};
        if (i < cell.size() and ':' == cell[i]) ++i;
        const size_t dashes = i;
        while (i < cell.size() and '-' == cell[i]) ++i;
        if (i == dashes) return false;
        if (i < cell.size() and ':' == cell[i]) ++i;
        if (i != cell.size()) return false;
    }
    return true;
}

bool md_is_hrule(const std::string& line)
{
    size_t start{0};
    if (md_indent(line, start) > 3 or start >= line.size()) return false;
    const char ch = line[start];
    if ('-' != ch and '*' != ch and '_' != ch) return false;
    int count{0};
    for (size_t i = start; i < line.size(); ++i) {
        if (ch == line[i]) ++count;
        else if (' ' != line[i] and '\t' != line[i]) return false;
    }
    return count >= 3;
}

// "===" or "---" under a line of text makes it a heading of level 1 or 2
int md_setext_level(const std::string& line)
{
    size_t start{0};
    if (md_indent(line, start) > 3 or start >= line.size()) return 0;
    const char ch = line[start];
    if ('=' != ch and '-' != ch) return 0;
    const std::string rest = md_trim(line.substr(start));
    if (std::string::npos != rest.find_first_not_of(ch)) return 0;
    return '=' == ch ? 1 : 2;
}

int md_atx_level(const std::string& line, std::string& rText)
{
    size_t start{0};
    if (md_indent(line, start) > 3) return 0;
    size_t i = start;
    while (i < line.size() and '#' == line[i]) ++i;
    const int level = static_cast<int>(i - start);
    if (level < 1 or level > 6) return 0;
    if (i < line.size() and ' ' != line[i] and '\t' != line[i]) return 0;
    rText = md_trim(line.substr(i));
    // closing "#"s
    const size_t hashes = rText.find_last_not_of('#');
    if (std::string::npos == hashes) rText.clear();
    else if (hashes + 1 < rText.size() and (' ' == rText[hashes] or '\t' == rText[hashes])) rText = md_trim(rText.substr(0, hashes));
    return level;
}

struct CtMdFence {
    size_t      indent{0};
    char        ch{0};
    size_t      len{0};
    std::string lang;
};

bool md_fence_open(const std::string& line, CtMdFence& rFence)
{
    size_t start{0};
    md_indent(line, start);
    if (start >= line.size() or ('`' != line[start] and '~' != line[start])) return false;
    size_t i = start;
    while (i < line.size() and line[start] == line[i]) ++i;
    if (i - start < 3) return false;
    const std::string info = md_trim(line.substr(i));
    if ('`' == line[start] and std::string::npos != info.find('`')) return false;
    rFence.indent = start;
    rFence.ch = line[start];
    rFence.len = i - start;
    rFence.lang = info.substr(0, info.find_first_of(" \t{"));
    return true;
}

bool md_fence_close(const std::string& line, const CtMdFence& fence)
{
    const std::string trimmed = md_trim(line);
    return trimmed.size() >= fence.len and std::string::npos == trimmed.find_first_not_of(fence.ch);
}

struct CtMdListItem {
    size_t      indent{0};
    std::string marker; // "" for a bullet, else the number and its '.' or ')'
    int         todo{-1}; // checkbox state, -1 if not a to-do item
    std::string text;
};

bool md_list_item(const std::string& line, CtMdListItem& rItem)
{
    size_t start{0};
    rItem.indent = md_indent(line, start);
    if (start >= line.size()) return false;
    size_t i = start;
    if ('-' == line[i] or '*' == line[i] or '+' == line[i]) {
        rItem.marker.clear();
        ++i;
    }
    else {
        while (i < line.size() and i - start < 9 and std::isdigit(static_cast<unsigned char>(line[i]))) ++i;
        if (i == start or i >= line.size() or ('.' != line[i] and ')' != line[i])) return false;
        ++i;
        rItem.marker = line.substr(start, i - start);
    }
    if (i < line.size() and ' ' != line[i] and '\t' != line[i]) return false;
    rItem.text = i < line.size() ? line.substr(i + 1) : "";
    rItem.todo = -1;
    if (rItem.marker.empty() and rItem.text.size() >= 3 and '[' == rItem.text[0] and ']' == rItem.text[2] and
        (rItem.text.size() == 3 or ' ' == rItem.text[3]))
    {
        const char state = rItem.text[1];
        if (' ' == state) rItem.todo = static_cast<int>(CtDocumentBuilder::checkbox_state::unchecked);
        else if ('x' == state or 'X' == state) rItem.todo = static_cast<int>(CtDocumentBuilder::checkbox_state::ticked);
        if (rItem.todo >= 0) rItem.text = rItem.text.size() > 4 ? rItem.text.substr(4) : "";
    }
    return true;
}

} // namespace (anonymous)

void CtMDParser::_add_spans(const std::string& text, const int scaleLevel)
{
    std::vector<CtMdSpan> spans;
    md_inline(text, CtMdSpan{}, spans);
    CtDocumentBuilder& builder = doc_builder();
    for (const CtMdSpan& span : spans) {
        if (not span.image.empty()) {
            builder.add_text(span.text);
            builder.add_image(span.image);
            continue;
        }
        if (not span.bold and not span.italic and not span.strike and not span.mono and span.link.empty() and 0 == scaleLevel) {
            builder.add_text(span.text);
            continue;
        }
        builder.close_current_tag();
        if (span.bold) builder.add_weight_tag(CtConst::TAG_PROP_VAL_HEAVY, std::nullopt);
        if (span.italic) builder.add_italic_tag(std::nullopt);
        if (span.strike) builder.add_strikethrough_tag(std::nullopt);
        if (span.mono) builder.add_monospace_tag(std::nullopt);
        if (scaleLevel > 0) builder.add_scale_tag(scaleLevel, std::nullopt);
        if (not span.link.empty()) builder.add_link(span.link);
        builder.add_text(span.text, false);
        builder.close_current_tag();
    }
}

void CtMDParser::feed(const Glib::ustring& buffer)
{
    _lastError.clear();
    try {
        std::vector<std::string> lines;
        const std::string& raw = buffer.raw();
        for (size_t start = 0; start <= raw.size();) {
            size_t end = raw.find('\n', start);
            if (std::string::npos == end) end = raw.size();
            std::string line = raw.substr(start, end - start);
            if (not line.empty() and '\r' == line.back()) line.pop_back();
            lines.push_back(std::move(line));
            start = end + 1;
        }
        // text ending with a newline gives an empty last line: the newline goes after the line before it
        const bool endsWithNewline = lines.size() > 1 and lines.back().empty();
        if (endsWithNewline) lines.pop_back();

        CtDocumentBuilder& builder = doc_builder();
        std::vector<size_t> listIndents;
        for (size_t i = 0; i < lines.size(); ++i) {
            const std::string& line = lines[i];
            const bool hasNext = i + 1 < lines.size();
            CtMdFence fence;
            CtMdListItem listItem;
            std::string headingText;
            if (not md_list_item(line, listItem) and not md_trim(line).empty()) {
                listIndents.clear();
            }
            if (md_fence_open(line, fence)) {
                std::string code;
                size_t j = i + 1;
                for (; j < lines.size() and not md_fence_close(lines[j], fence); ++j) {
                    std::string codeLine = lines[j];
                    size_t start{0};
                    md_indent(codeLine, start);
                    codeLine.erase(0, std::min(start, fence.indent));
                    if (j > i + 1) code += '\n';
                    code += codeLine;
                }
                builder.add_codebox(fence.lang.empty() ? CtConst::PLAIN_TEXT_ID : fence.lang, code);
                i = std::min(j, lines.size() - 1);
            }
            else if (const int level = md_atx_level(line, headingText)) {
                _add_spans(headingText, std::min(level, 3));
            }
            else if (hasNext and not md_trim(line).empty() and not md_is_hrule(line) and
                     not md_list_item(line, listItem) and md_setext_level(lines[i + 1]))
            {
                _add_spans(md_trim(line), md_setext_level(lines[i + 1]));
                ++i;
            }
            else if (md_is_hrule(line)) {
                builder.add_text(_pCtConfig->hRule);
            }
            else if (hasNext and std::string::npos != line.find('|') and md_is_table_separator(lines[i + 1])) {
                std::vector<std::vector<Glib::ustring>> table;
                const size_t numCols = md_table_cells(line).size();
                size_t j = i;
                for (; j < lines.size() and not md_trim(lines[j]).empty() and std::string::npos != lines[j].find('|'); ++j) {
                    if (j == i + 1) continue;
                    std::vector<Glib::ustring> row;
                    for (const std::string& cell : md_table_cells(lines[j])) {
                        row.push_back(md_plain_text(cell));
                    }
                    row.resize(numCols);
                    table.push_back(std::move(row));
                }
                builder.add_table(table);
                i = j - 1;
            }
            else if (md_list_item(line, listItem)) {
                while (not listIndents.empty() and listIndents.back() > listItem.indent) listIndents.pop_back();
                if (listIndents.empty() or listIndents.back() < listItem.indent) listIndents.push_back(listItem.indent);
                const size_t level = listIndents.size() - 1;
                if (listItem.todo >= 0) {
                    builder.add_text(std::string(level, '\t'));
                    builder.add_todo_list(static_cast<CtDocumentBuilder::checkbox_state>(listItem.todo), "");
                }
                else if (listItem.marker.empty()) {
                    builder.add_list(static_cast<uint8_t>(std::min<size_t>(level, UINT8_MAX)), "");
                }
                else {
                    builder.add_text(std::string(level, '\t') + listItem.marker + " ");
                }
                _add_spans(listItem.text, 0);
            }
            else {
                _add_spans(line, 0);
            }
            if (i + 1 < lines.size() or endsWithNewline) {
                builder.add_text(CtConst::CHAR_NEWLINE);
            }
        }
    }
    catch (std::exception& e) {
        _lastError = e.what();
        spdlog::error("Exception while parsing '{}': {}", buffer.c_str(), e.what());
    }
}

void CtMDParser::_add_table_cell(const std::string& text)
{
    if (!text.empty()) {
        // Parse it to see if a cell or end of row
        char last_ch = text.back();
        if (last_ch == '|') {
            // End of row
            _current_table_row.emplace_back(text.begin(), text.end() - 1);
            _pop_table_row();
        } else {
            // Just a cell
            _current_table_row.emplace_back(text);
        }
    } else {
        if (not _current_table_row.empty()) { _pop_table_row(); }
        //spdlog::warn("_add_table_cell called without text, the document may contain invalid or unknown formatting");
    }
}

void CtMDParser::_pop_table()
{
    doc_builder().add_table(_current_table);
    _current_table.clear();
}

void CtMDParser::_pop_table_row()
{
    _current_table.emplace_back(_current_table_row);
    _current_table_row.clear();
}
