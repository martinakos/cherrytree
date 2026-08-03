/*
 * ct_delta_engine.cc
 *
 * Copyright 2009-2026
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

#include "ct_delta_engine.h"
#include "ct_logging.h"
#include <glibmm.h>
#include <sstream>
#include <vector>

namespace {

std::vector<std::string> splitPipe(const std::string& s)
{
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= s.size()) {
        size_t pos = s.find('|', start);
        if (pos == std::string::npos) {
            parts.push_back(s.substr(start));
            break;
        }
        parts.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

std::map<std::string, std::string> parseAttrs(const std::string& s)
{
    std::map<std::string, std::string> attrs;
    if (s.empty()) return attrs;
    size_t start = 0;
    while (start < s.size()) {
        size_t semi = s.find(';', start);
        std::string token = (semi == std::string::npos) ? s.substr(start) : s.substr(start, semi - start);
        size_t eq = token.find('=');
        if (eq != std::string::npos) {
            attrs[token.substr(0, eq)] = token.substr(eq + 1);
        }
        if (semi == std::string::npos) break;
        start = semi + 1;
    }
    return attrs;
}

CtFormatChange parseFormatChange(const std::vector<std::string>& parts, size_t startIdx)
{
    CtFormatChange change;
    for (size_t i = startIdx; i < parts.size(); ++i) {
        const auto& sc = parts[i];
        size_t c1 = sc.find(',');
        if (c1 == std::string::npos) continue;
        size_t c2 = sc.find(',', c1 + 1);
        if (c2 == std::string::npos) continue;

        int offset = std::stoi(sc.substr(0, c1));
        int length = std::stoi(sc.substr(c1 + 1, c2 - c1 - 1));
        std::string oldVal = sc.substr(c2 + 1);
        bool had = !oldVal.empty();
        change.changes.emplace_back(offset, length, oldVal, had);
    }
    return change;
}

bool reverseINS(CtNodeContent& content, const std::vector<std::string>& parts)
{
    if (parts.size() < 3) return false;
    int offset = std::stoi(parts[1]);
    Glib::ustring text = Glib::Base64::decode(parts[2]);
    content.deleteRange(offset, text.size());
    return true;
}

bool forwardINS(CtNodeContent& content, const std::vector<std::string>& parts)
{
    if (parts.size() < 3) return false;
    int offset = std::stoi(parts[1]);
    Glib::ustring text = Glib::Base64::decode(parts[2]);
    auto attrs = (parts.size() > 3) ? parseAttrs(parts[3]) : std::map<std::string, std::string>{};
    content.insertText(offset, text, attrs);
    return true;
}

bool reverseDEL(CtNodeContent& content, const std::vector<std::string>& parts)
{
    if (parts.size() < 2) return false;
    int start = std::stoi(parts[1]);
    CtDeletedContent deleted(start);

    for (size_t i = 2; i < parts.size(); ++i) {
        if (parts[i].empty()) continue;
        char prefix = parts[i][0];
        std::string payload = parts[i].substr(1);

        if (prefix == 'T') {
            Glib::ustring text = Glib::Base64::decode(payload);
            auto attrs = (i + 1 < parts.size()) ? parseAttrs(parts[i + 1]) : std::map<std::string, std::string>{};
            deleted.elements.emplace_back(CtTextSpan(text, attrs));
            ++i;
        } else if (prefix == 'W') {
            std::string widgetXml = Glib::Base64::decode(payload);
            CtWidgetDesc wd = CtWidgetDesc::fromXml(widgetXml);
            deleted.elements.emplace_back(wd);
        }
    }

    content.reinsertContent(deleted);
    return true;
}

bool forwardDEL(CtNodeContent& content, const std::vector<std::string>& parts)
{
    if (parts.size() < 2) return false;
    int start = std::stoi(parts[1]);

    int totalLen = 0;
    for (size_t i = 2; i < parts.size(); ++i) {
        if (parts[i].empty()) continue;
        char prefix = parts[i][0];
        std::string payload = parts[i].substr(1);

        if (prefix == 'T') {
            Glib::ustring text = Glib::Base64::decode(payload);
            totalLen += text.size();
            ++i;
        } else if (prefix == 'W') {
            totalLen += 1;
        }
    }

    content.deleteRange(start, totalLen);
    return true;
}

bool reverseFMT(CtNodeContent& content, const std::vector<std::string>& parts)
{
    if (parts.size() < 5) return false;
    int start = std::stoi(parts[1]);
    int length = std::stoi(parts[2]);
    const std::string& attr = parts[3];
    auto change = parseFormatChange(parts, 5);
    content.restoreFormat(start, length, attr, change);
    return true;
}

bool forwardFMT(CtNodeContent& content, const std::vector<std::string>& parts)
{
    if (parts.size() < 5) return false;
    int start = std::stoi(parts[1]);
    int length = std::stoi(parts[2]);
    const std::string& attr = parts[3];
    const std::string& value = parts[4];
    content.applyFormat(start, length, attr, value);
    return true;
}

bool reverseRFM(CtNodeContent& content, const std::vector<std::string>& parts)
{
    if (parts.size() < 5) return false;
    int start = std::stoi(parts[1]);
    int length = std::stoi(parts[2]);
    const std::string& attr = parts[3];
    auto change = parseFormatChange(parts, 5);
    content.restoreFormat(start, length, attr, change);
    return true;
}

bool forwardRFM(CtNodeContent& content, const std::vector<std::string>& parts)
{
    if (parts.size() < 5) return false;
    int start = std::stoi(parts[1]);
    int length = std::stoi(parts[2]);
    const std::string& attr = parts[3];
    content.removeFormat(start, length, attr);
    return true;
}

bool reverseTED(CtNodeContent& content, const std::vector<std::string>& parts)
{
    if (parts.size() < 3) return false;
    std::string oldXml = Glib::Base64::decode(parts[1]);
    content = CtNodeContent::fromXml(oldXml, nullptr);
    return true;
}

bool forwardTED(CtNodeContent& content, const std::vector<std::string>& parts)
{
    if (parts.size() < 3) return false;
    std::string newXml = Glib::Base64::decode(parts[2]);
    content = CtNodeContent::fromXml(newXml, nullptr);
    return true;
}

bool reverseWIns(CtNodeContent& content, const std::vector<std::string>& parts)
{
    if (parts.size() < 3) return false;
    int offset = std::stoi(parts[1]);
    content.removeWidget(offset);
    return true;
}

bool forwardWIns(CtNodeContent& content, const std::vector<std::string>& parts)
{
    if (parts.size() < 3) return false;
    int offset = std::stoi(parts[1]);
    std::string widgetXml = Glib::Base64::decode(parts[2]);
    CtWidgetDesc wd = CtWidgetDesc::fromXml(widgetXml);
    content.insertWidget(offset, wd);
    return true;
}

bool reverseWMod(CtNodeContent& content, const std::vector<std::string>& parts)
{
    if (parts.size() < 4) return false;
    int offset = std::stoi(parts[1]);
    std::string oldXml = Glib::Base64::decode(parts[2]);
    CtWidgetDesc oldDesc = CtWidgetDesc::fromXml(oldXml);
    content.replaceWidget(offset, oldDesc);
    return true;
}

bool forwardWMod(CtNodeContent& content, const std::vector<std::string>& parts)
{
    if (parts.size() < 4) return false;
    int offset = std::stoi(parts[1]);
    std::string newXml = Glib::Base64::decode(parts[3]);
    CtWidgetDesc newDesc = CtWidgetDesc::fromXml(newXml);
    content.replaceWidget(offset, newDesc);
    return true;
}

bool reverseTCel(CtNodeContent& content, const std::vector<std::string>& parts)
{
    if (parts.size() < 6) return false;
    int offset = std::stoi(parts[1]);
    size_t row = std::stoul(parts[2]);
    size_t col = std::stoul(parts[3]);
    Glib::ustring oldText = Glib::Base64::decode(parts[4]);
    content.setWidgetTableCell(offset, row, col, oldText);
    return true;
}

bool forwardTCel(CtNodeContent& content, const std::vector<std::string>& parts)
{
    if (parts.size() < 6) return false;
    int offset = std::stoi(parts[1]);
    size_t row = std::stoul(parts[2]);
    size_t col = std::stoul(parts[3]);
    Glib::ustring newText = Glib::Base64::decode(parts[5]);
    content.setWidgetTableCell(offset, row, col, newText);
    return true;
}

bool reverseCBed(CtNodeContent& content, const std::vector<std::string>& parts)
{
    if (parts.size() < 4) return false;
    int offset = std::stoi(parts[1]);
    std::string oldContent = Glib::Base64::decode(parts[2]);
    content.setWidgetContentData(offset, oldContent);
    return true;
}

bool forwardCBed(CtNodeContent& content, const std::vector<std::string>& parts)
{
    if (parts.size() < 4) return false;
    int offset = std::stoi(parts[1]);
    std::string newContent = Glib::Base64::decode(parts[3]);
    content.setWidgetContentData(offset, newContent);
    return true;
}

bool reverseRCel(CtNodeContent& content, const std::vector<std::string>& parts)
{
    if (parts.size() < 6) return false;
    int offset = std::stoi(parts[1]);
    size_t row = std::stoul(parts[2]);
    size_t col = std::stoul(parts[3]);
    std::string oldXml = Glib::Base64::decode(parts[4]);
    CtCellContent oldCell = CtCellContent::fromXml(oldXml);
    content.setWidgetRichTableCell(offset, row, col, oldCell);
    return true;
}

bool forwardRCel(CtNodeContent& content, const std::vector<std::string>& parts)
{
    if (parts.size() < 6) return false;
    int offset = std::stoi(parts[1]);
    size_t row = std::stoul(parts[2]);
    size_t col = std::stoul(parts[3]);
    std::string newXml = Glib::Base64::decode(parts[5]);
    CtCellContent newCell = CtCellContent::fromXml(newXml);
    content.setWidgetRichTableCell(offset, row, col, newCell);
    return true;
}

bool applySingleReverse(CtNodeContent& content, const std::string& line)
{
    auto parts = splitPipe(line);
    if (parts.empty()) return false;

    const std::string& type = parts[0];
    if (type == "INS") return reverseINS(content, parts);
    if (type == "DEL") return reverseDEL(content, parts);
    if (type == "FMT") return reverseFMT(content, parts);
    if (type == "RFM") return reverseRFM(content, parts);
    if (type == "TED") return reverseTED(content, parts);
    if (type == "WIns") return reverseWIns(content, parts);
    if (type == "WMod") return reverseWMod(content, parts);
    if (type == "TCel") return reverseTCel(content, parts);
    if (type == "CBed") return reverseCBed(content, parts);
    if (type == "RCel") return reverseRCel(content, parts);
    return false;
}

bool applySingleForward(CtNodeContent& content, const std::string& line)
{
    auto parts = splitPipe(line);
    if (parts.empty()) return false;

    const std::string& type = parts[0];
    if (type == "INS") return forwardINS(content, parts);
    if (type == "DEL") return forwardDEL(content, parts);
    if (type == "FMT") return forwardFMT(content, parts);
    if (type == "RFM") return forwardRFM(content, parts);
    if (type == "TED") return forwardTED(content, parts);
    if (type == "WIns") return forwardWIns(content, parts);
    if (type == "WMod") return forwardWMod(content, parts);
    if (type == "TCel") return forwardTCel(content, parts);
    if (type == "CBed") return forwardCBed(content, parts);
    if (type == "RCel") return forwardRCel(content, parts);
    return false;
}

std::vector<std::string> splitLines(const std::string& s)
{
    std::vector<std::string> lines;
    std::istringstream stream(s);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

std::string serializePoints(const std::vector<CtDrawingPoint>& pts)
{
    std::string result;
    for (const auto& p : pts) {
        if (!result.empty()) result += ';';
        result += std::to_string(p.x) + ',' + std::to_string(p.y);
    }
    return result;
}

std::vector<CtDrawingPoint> deserializePoints(const std::string& data)
{
    std::vector<CtDrawingPoint> pts;
    size_t pos = 0;
    while (pos < data.size()) {
        size_t commaPos = data.find(',', pos);
        if (commaPos == std::string::npos) break;
        size_t semiPos = data.find(';', commaPos);
        if (semiPos == std::string::npos) semiPos = data.size();
        double px = std::stod(data.substr(pos, commaPos - pos));
        double py = std::stod(data.substr(commaPos + 1, semiPos - commaPos - 1));
        pts.push_back({px, py});
        pos = semiPos + 1;
    }
    return pts;
}

bool reverseDSt(std::vector<CtDrawingCanvas>& canvases, const std::vector<std::string>& parts)
{
    if (parts.size() < 3) return false;
    size_t ci = std::stoul(parts[1]);
    if (ci >= canvases.size() || canvases[ci].strokes.empty()) return false;
    canvases[ci].strokes.pop_back();
    return true;
}

bool forwardDSt(std::vector<CtDrawingCanvas>& canvases, const std::vector<std::string>& parts)
{
    if (parts.size() < 3) return false;
    size_t ci = std::stoul(parts[1]);
    if (ci >= canvases.size()) return false;
    std::string strokeData = Glib::Base64::decode(parts[2]);
    canvases[ci].strokes.push_back(CtDeltaEngine::deserializeStroke(strokeData));
    return true;
}

bool reverseESt(std::vector<CtDrawingCanvas>& canvases, const std::vector<std::string>& parts)
{
    if (parts.size() < 4) return false;
    size_t ci = std::stoul(parts[1]);
    size_t si = std::stoul(parts[2]);
    if (ci >= canvases.size()) return false;
    std::string strokeData = Glib::Base64::decode(parts[3]);
    auto stroke = CtDeltaEngine::deserializeStroke(strokeData);
    auto& strokes = canvases[ci].strokes;
    if (si <= strokes.size()) {
        strokes.insert(strokes.begin() + si, std::move(stroke));
    } else {
        strokes.push_back(std::move(stroke));
    }
    return true;
}

bool forwardESt(std::vector<CtDrawingCanvas>& canvases, const std::vector<std::string>& parts)
{
    if (parts.size() < 4) return false;
    size_t ci = std::stoul(parts[1]);
    size_t si = std::stoul(parts[2]);
    if (ci >= canvases.size() || si >= canvases[ci].strokes.size()) return false;
    canvases[ci].strokes.erase(canvases[ci].strokes.begin() + si);
    return true;
}

bool reverseRSt(std::vector<CtDrawingCanvas>& canvases, const std::vector<std::string>& parts)
{
    if (parts.size() < 5) return false;
    size_t ci = std::stoul(parts[1]);
    size_t si = std::stoul(parts[2]);
    double oldRot = std::stod(parts[3]);
    if (ci >= canvases.size() || si >= canvases[ci].strokes.size()) return false;
    canvases[ci].strokes[si].rotation = oldRot;
    return true;
}

bool forwardRSt(std::vector<CtDrawingCanvas>& canvases, const std::vector<std::string>& parts)
{
    if (parts.size() < 5) return false;
    size_t ci = std::stoul(parts[1]);
    size_t si = std::stoul(parts[2]);
    double newRot = std::stod(parts[4]);
    if (ci >= canvases.size() || si >= canvases[ci].strokes.size()) return false;
    canvases[ci].strokes[si].rotation = newRot;
    return true;
}

bool reverseMSt(std::vector<CtDrawingCanvas>& canvases, const std::vector<std::string>& parts)
{
    if (parts.size() < 5) return false;
    size_t ci = std::stoul(parts[1]);
    size_t si = std::stoul(parts[2]);
    if (ci >= canvases.size() || si >= canvases[ci].strokes.size()) return false;
    std::string oldPtsData = Glib::Base64::decode(parts[3]);
    canvases[ci].strokes[si].points = deserializePoints(oldPtsData);
    return true;
}

bool forwardMSt(std::vector<CtDrawingCanvas>& canvases, const std::vector<std::string>& parts)
{
    if (parts.size() < 5) return false;
    size_t ci = std::stoul(parts[1]);
    size_t si = std::stoul(parts[2]);
    if (ci >= canvases.size() || si >= canvases[ci].strokes.size()) return false;
    std::string newPtsData = Glib::Base64::decode(parts[4]);
    canvases[ci].strokes[si].points = deserializePoints(newPtsData);
    return true;
}

bool reverseACv(std::vector<CtDrawingCanvas>& canvases, const std::vector<std::string>& parts)
{
    if (parts.size() < 3) return false;
    size_t ci = std::stoul(parts[1]);
    if (ci >= canvases.size()) return false;
    canvases.erase(canvases.begin() + ci);
    return true;
}

bool forwardACv(std::vector<CtDrawingCanvas>& canvases, const std::vector<std::string>& parts)
{
    if (parts.size() < 3) return false;
    size_t ci = std::stoul(parts[1]);
    std::string canvasData = Glib::Base64::decode(parts[2]);
    auto canvas = CtDeltaEngine::deserializeCanvas(canvasData);
    if (ci <= canvases.size()) {
        canvases.insert(canvases.begin() + ci, std::move(canvas));
    } else {
        canvases.push_back(std::move(canvas));
    }
    return true;
}

bool reverseDCv(std::vector<CtDrawingCanvas>& canvases, const std::vector<std::string>& parts)
{
    if (parts.size() < 3) return false;
    size_t ci = std::stoul(parts[1]);
    std::string canvasData = Glib::Base64::decode(parts[2]);
    auto canvas = CtDeltaEngine::deserializeCanvas(canvasData);
    if (ci <= canvases.size()) {
        canvases.insert(canvases.begin() + ci, std::move(canvas));
    } else {
        canvases.push_back(std::move(canvas));
    }
    return true;
}

bool forwardDCv(std::vector<CtDrawingCanvas>& canvases, const std::vector<std::string>& parts)
{
    if (parts.size() < 3) return false;
    size_t ci = std::stoul(parts[1]);
    if (ci >= canvases.size()) return false;
    canvases.erase(canvases.begin() + ci);
    return true;
}

bool reverseMCv(std::vector<CtDrawingCanvas>& canvases, const std::vector<std::string>& parts)
{
    if (parts.size() < 6) return false;
    size_t ci = std::stoul(parts[1]);
    if (ci >= canvases.size()) return false;
    canvases[ci].x = std::stod(parts[2]);
    canvases[ci].y = std::stod(parts[3]);
    return true;
}

bool forwardMCv(std::vector<CtDrawingCanvas>& canvases, const std::vector<std::string>& parts)
{
    if (parts.size() < 6) return false;
    size_t ci = std::stoul(parts[1]);
    if (ci >= canvases.size()) return false;
    canvases[ci].x = std::stod(parts[4]);
    canvases[ci].y = std::stod(parts[5]);
    return true;
}

bool reverseRCv(std::vector<CtDrawingCanvas>& canvases, const std::vector<std::string>& parts)
{
    if (parts.size() < 10) return false;
    size_t ci = std::stoul(parts[1]);
    if (ci >= canvases.size()) return false;
    canvases[ci].x = std::stod(parts[2]);
    canvases[ci].y = std::stod(parts[3]);
    canvases[ci].width = std::stod(parts[4]);
    canvases[ci].height = std::stod(parts[5]);
    return true;
}

bool forwardRCv(std::vector<CtDrawingCanvas>& canvases, const std::vector<std::string>& parts)
{
    if (parts.size() < 10) return false;
    size_t ci = std::stoul(parts[1]);
    if (ci >= canvases.size()) return false;
    canvases[ci].x = std::stod(parts[6]);
    canvases[ci].y = std::stod(parts[7]);
    canvases[ci].width = std::stod(parts[8]);
    canvases[ci].height = std::stod(parts[9]);
    return true;
}

bool reversePCv(std::vector<CtDrawingCanvas>& canvases, const std::vector<std::string>& parts)
{
    if (parts.size() < 12) return false;
    size_t ci = std::stoul(parts[1]);
    if (ci >= canvases.size()) return false;
    canvases[ci].name = Glib::Base64::decode(parts[2]);
    canvases[ci].bgColor = parts[4];
    canvases[ci].bgOpacity = std::stod(parts[6]);
    canvases[ci].cornerRadius = std::stod(parts[8]);
    canvases[ci].showBorderWhenInactive = (parts[10] == "1");
    return true;
}

bool forwardPCv(std::vector<CtDrawingCanvas>& canvases, const std::vector<std::string>& parts)
{
    if (parts.size() < 12) return false;
    size_t ci = std::stoul(parts[1]);
    if (ci >= canvases.size()) return false;
    canvases[ci].name = Glib::Base64::decode(parts[3]);
    canvases[ci].bgColor = parts[5];
    canvases[ci].bgOpacity = std::stod(parts[7]);
    canvases[ci].cornerRadius = std::stod(parts[9]);
    canvases[ci].showBorderWhenInactive = (parts[11] == "1");
    return true;
}

bool applySingleReverseDrawing(std::vector<CtDrawingCanvas>& canvases, const std::string& line)
{
    auto parts = splitPipe(line);
    if (parts.empty()) return false;
    const std::string& type = parts[0];
    if (type == "DSt") return reverseDSt(canvases, parts);
    if (type == "ESt") return reverseESt(canvases, parts);
    if (type == "RSt") return reverseRSt(canvases, parts);
    if (type == "MSt") return reverseMSt(canvases, parts);
    if (type == "ACv") return reverseACv(canvases, parts);
    if (type == "DCv") return reverseDCv(canvases, parts);
    if (type == "MCv") return reverseMCv(canvases, parts);
    if (type == "RCv") return reverseRCv(canvases, parts);
    if (type == "PCv") return reversePCv(canvases, parts);
    return false;
}

bool applySingleForwardDrawing(std::vector<CtDrawingCanvas>& canvases, const std::string& line)
{
    auto parts = splitPipe(line);
    if (parts.empty()) return false;
    const std::string& type = parts[0];
    if (type == "DSt") return forwardDSt(canvases, parts);
    if (type == "ESt") return forwardESt(canvases, parts);
    if (type == "RSt") return forwardRSt(canvases, parts);
    if (type == "MSt") return forwardMSt(canvases, parts);
    if (type == "ACv") return forwardACv(canvases, parts);
    if (type == "DCv") return forwardDCv(canvases, parts);
    if (type == "MCv") return forwardMCv(canvases, parts);
    if (type == "RCv") return forwardRCv(canvases, parts);
    if (type == "PCv") return forwardPCv(canvases, parts);
    return false;
}

} // anonymous namespace

bool CtDeltaEngine::applyReverse(CtNodeContent& content, const std::string& deltaData)
{
    if (deltaData.empty()) return false;

    try {
        auto lines = splitLines(deltaData);
        if (lines.empty()) return false;

        if (lines.size() == 1) {
            return applySingleReverse(content, lines[0]);
        }

        // Compound: reverse sub-deltas in reverse order
        for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
            if (!applySingleReverse(content, *it)) return false;
        }
        return true;
    }
    catch (const std::exception& e) {
        spdlog::error("CtDeltaEngine::applyReverse: {}", e.what());
        return false;
    }
}

bool CtDeltaEngine::applyForward(CtNodeContent& content, const std::string& deltaData)
{
    if (deltaData.empty()) return false;

    try {
        auto lines = splitLines(deltaData);
        if (lines.empty()) return false;

        for (const auto& line : lines) {
            if (!applySingleForward(content, line)) return false;
        }
        return true;
    }
    catch (const std::exception& e) {
        spdlog::error("CtDeltaEngine::applyForward: {}", e.what());
        return false;
    }
}

bool CtDeltaEngine::isReplayable(const std::string& deltaData)
{
    if (deltaData.empty()) return false;

    auto lines = splitLines(deltaData);
    for (const auto& line : lines) {
        auto parts = splitPipe(line);
        if (parts.empty()) return false;
        const std::string& type = parts[0];
        if (type != "INS" && type != "DEL" && type != "FMT" && type != "RFM" &&
            type != "TED" && type != "WIns" && type != "WMod" &&
            type != "TCel" && type != "CBed" && type != "RCel") {
            return false;
        }
    }
    return true;
}

bool CtDeltaEngine::isDrawingDelta(const std::string& deltaData)
{
    if (deltaData.empty()) return false;
    auto parts = splitPipe(deltaData);
    if (parts.empty()) return false;
    const std::string& type = parts[0];
    return type == "DSt" || type == "ESt" || type == "RSt" || type == "MSt" ||
           type == "ACv" || type == "DCv" || type == "MCv" || type == "RCv" || type == "PCv";
}

bool CtDeltaEngine::applyReverseDrawing(std::vector<CtDrawingCanvas>& canvases, const std::string& deltaData)
{
    if (deltaData.empty()) return false;
    try {
        return applySingleReverseDrawing(canvases, deltaData);
    }
    catch (const std::exception& e) {
        spdlog::error("CtDeltaEngine::applyReverseDrawing: {}", e.what());
        return false;
    }
}

bool CtDeltaEngine::applyForwardDrawing(std::vector<CtDrawingCanvas>& canvases, const std::string& deltaData)
{
    if (deltaData.empty()) return false;
    try {
        return applySingleForwardDrawing(canvases, deltaData);
    }
    catch (const std::exception& e) {
        spdlog::error("CtDeltaEngine::applyForwardDrawing: {}", e.what());
        return false;
    }
}

std::string CtDeltaEngine::serializePoints(const std::vector<CtDrawingPoint>& pts)
{
    return ::serializePoints(pts);
}

std::string CtDeltaEngine::serializeStroke(const CtDrawingStroke& s)
{
    std::string pts = serializePoints(s.points);
    return s.color + "\t" + std::to_string(s.lineWidth) + "\t" + std::to_string(s.opacity) + "\t"
         + std::to_string(static_cast<int>(s.type)) + "\t" + std::to_string(static_cast<int>(s.lineStyle)) + "\t"
         + (s.filled ? "1" : "0") + "\t" + std::to_string(s.rotation) + "\t"
         + std::to_string(static_cast<int>(s.arrowHead)) + "\t" + std::to_string(static_cast<int>(s.arrowStyle)) + "\t"
         + s.fontFamily + "\t" + std::to_string(s.fontSize) + "\t"
         + Glib::Base64::encode(s.textContent) + "\t" + pts;
}

CtDrawingStroke CtDeltaEngine::deserializeStroke(const std::string& data)
{
    CtDrawingStroke s;
    std::vector<std::string> fields;
    size_t start = 0;
    while (start <= data.size()) {
        size_t pos = data.find('\t', start);
        if (pos == std::string::npos) {
            fields.push_back(data.substr(start));
            break;
        }
        fields.push_back(data.substr(start, pos - start));
        start = pos + 1;
    }
    if (fields.size() < 13) return s;
    s.color = fields[0];
    s.lineWidth = std::stod(fields[1]);
    s.opacity = std::stod(fields[2]);
    s.type = static_cast<CtDrawingElementType>(std::stoi(fields[3]));
    s.lineStyle = static_cast<CtDrawingLineStyle>(std::stoi(fields[4]));
    s.filled = (fields[5] == "1");
    s.rotation = std::stod(fields[6]);
    s.arrowHead = static_cast<CtDrawingArrowHead>(std::stoi(fields[7]));
    s.arrowStyle = static_cast<CtDrawingArrowStyle>(std::stoi(fields[8]));
    s.fontFamily = fields[9];
    s.fontSize = std::stod(fields[10]);
    s.textContent = Glib::Base64::decode(fields[11]);
    s.points = deserializePoints(fields[12]);
    return s;
}

std::string CtDeltaEngine::serializeCanvas(const CtDrawingCanvas& c)
{
    std::string result = std::to_string(c.x) + "\t" + std::to_string(c.y) + "\t"
        + std::to_string(c.width) + "\t" + std::to_string(c.height) + "\t"
        + std::to_string(c.cornerRadius) + "\t" + Glib::Base64::encode(c.name) + "\t"
        + c.bgColor + "\t" + std::to_string(c.bgOpacity) + "\t"
        + (c.showBorderWhenInactive ? "1" : "0") + "\t"
        + std::to_string(c.tsCreation) + "\t" + std::to_string(c.tsLastSave) + "\t"
        + std::to_string(c.strokes.size());
    for (const auto& stroke : c.strokes) {
        result += "\n" + serializeStroke(stroke);
    }
    return result;
}

CtDrawingCanvas CtDeltaEngine::deserializeCanvas(const std::string& data)
{
    CtDrawingCanvas c;
    std::istringstream stream(data);
    std::string headerLine;
    if (!std::getline(stream, headerLine)) return c;

    std::vector<std::string> fields;
    size_t start = 0;
    while (start <= headerLine.size()) {
        size_t pos = headerLine.find('\t', start);
        if (pos == std::string::npos) {
            fields.push_back(headerLine.substr(start));
            break;
        }
        fields.push_back(headerLine.substr(start, pos - start));
        start = pos + 1;
    }
    if (fields.size() < 12) return c;
    c.x = std::stod(fields[0]);
    c.y = std::stod(fields[1]);
    c.width = std::stod(fields[2]);
    c.height = std::stod(fields[3]);
    c.cornerRadius = std::stod(fields[4]);
    c.name = Glib::Base64::decode(fields[5]);
    c.bgColor = fields[6];
    c.bgOpacity = std::stod(fields[7]);
    c.showBorderWhenInactive = (fields[8] == "1");
    c.tsCreation = std::stoll(fields[9]);
    c.tsLastSave = std::stoll(fields[10]);
    size_t numStrokes = std::stoul(fields[11]);

    for (size_t i = 0; i < numStrokes; ++i) {
        std::string strokeLine;
        if (!std::getline(stream, strokeLine)) break;
        c.strokes.push_back(deserializeStroke(strokeLine));
    }
    return c;
}
