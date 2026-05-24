/*
 * ct_text_commands.cc
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

#include "ct_text_commands.h"
#include "ct_command_bridge.h"
#include "ct_node_content.h"
#include "ct_logging.h"
#include "ct_const.h"

// TextEditCommand implementation

TextEditCommand::TextEditCommand(
    std::shared_ptr<CtDocumentModel> docModel,
    gint64 nodeId,
    const Glib::ustring& oldContentXml,
    const Glib::ustring& newContentXml,
    int oldCursorPos,
    int newCursorPos,
    const std::string& description)
    : _docModel(docModel)
    , _nodeId(nodeId)
    , _oldContentXml(oldContentXml)
    , _newContentXml(newContentXml)
    , _oldCursorPos(oldCursorPos)
    , _newCursorPos(newCursorPos)
    , _description(description)
{
}

void TextEditCommand::execute()
{
    if (!_docModel) {
        spdlog::error("TextEditCommand: null document model");
        return;
    }

    auto node = _docModel->getNodeById(_nodeId);
    if (!node) {
        spdlog::error("TextEditCommand: node {} not found", _nodeId);
        return;
    }

    spdlog::debug("TextEditCommand: executing for node {}", _nodeId);
    node->setContentXml(_newContentXml);
    _docModel->notifyNodeChanged(_nodeId);
}

void TextEditCommand::undo()
{
    if (!_docModel) {
        spdlog::error("TextEditCommand: null document model");
        return;
    }

    auto node = _docModel->getNodeById(_nodeId);
    if (!node) {
        spdlog::error("TextEditCommand: node {} not found", _nodeId);
        return;
    }

    spdlog::debug("TextEditCommand: undoing for node {}", _nodeId);
    node->setContentXml(_oldContentXml);
    _docModel->notifyNodeChanged(_nodeId);
}

void TextEditCommand::redo()
{
    execute();
}

std::string TextEditCommand::getDescription() const
{
    if (!_description.empty()) {
        return "Node " + std::to_string(_nodeId) + ": " + _description;
    }

    std::string description = "Node " + std::to_string(_nodeId) + ": ";

    // Parse XML to get structured content and show what actually changed
    CtNodeContent oldContent = CtNodeContent::fromXml(_oldContentXml, nullptr);
    CtNodeContent newContent = CtNodeContent::fromXml(_newContentXml, nullptr);

    Glib::ustring oldText = oldContent.getText();
    Glib::ustring newText = newContent.getText();

    spdlog::debug("TextEditCommand::getDescription - oldXml length: {}, newXml length: {}, oldText length: {}, newText length: {}",
                  _oldContentXml.length(), _newContentXml.length(),
                  oldText.length(), newText.length());

    // Fallback to simple description if parsing failed (both texts empty but XMLs aren't)
    if (oldText.empty() && newText.empty() && (!_oldContentXml.empty() || !_newContentXml.empty())) {
        spdlog::warn("TextEditCommand::getDescription - XML parsing failed, using simple description");
        size_t oldSize = _oldContentXml.size();
        size_t newSize = _newContentXml.size();
        if (newSize > oldSize + 10) {
            description += "Type text";
        } else if (oldSize > newSize + 10) {
            description += "Delete text";
        } else {
            description += "Edit text";
        }
        return description;
    }

    if (newText.length() > oldText.length()) {
        // Text was added
        size_t addedLen = newText.length() - oldText.length();

        // Find where text was added by comparing from start
        size_t diffPos = 0;
        while (diffPos < oldText.length() && diffPos < newText.length() &&
               oldText[diffPos] == newText[diffPos]) {
            diffPos++;
        }

        // Extract the added text
        Glib::ustring addedText = newText.substr(diffPos, addedLen);

        // Replace newlines with readable text
        if (addedText == "\n") {
            description += "Type newline";
        } else if (addedText.find('\n') != Glib::ustring::npos) {
            // Contains newlines but has other text too
            description += "Type text with newlines";
        } else {
            // Strip trailing single space from display (it's a word separator, not content)
            Glib::ustring displayText = addedText;
            if (displayText.length() > 1 && displayText[displayText.length() - 1] == ' ') {
                // Check if only one trailing space (not multiple)
                if (displayText.length() < 2 || displayText[displayText.length() - 2] != ' ') {
                    displayText = displayText.substr(0, displayText.length() - 1);
                }
            }
            // Limit to 30 characters for display
            if (displayText.length() > 30) {
                description += "Type \"" + displayText.substr(0, 30).raw() + "...\"";
            } else {
                description += "Type \"" + displayText.raw() + "\"";
            }
        }
    }
    else if (oldText.length() > newText.length()) {
        // Text was deleted
        size_t deletedLen = oldText.length() - newText.length();
        description += "Delete " + std::to_string(deletedLen) + " chars";
    }
    else {
        // Same length - formatting or replacement
        description += "Edit text";
    }

    return description;
}

// CtTextEditSession implementation

CtTextEditSession::CtTextEditSession(std::shared_ptr<CtDocumentModel> docModel)
    : _docModel(docModel)
{
}

CtTextEditSession::~CtTextEditSession()
{
    cancel();
}

void CtTextEditSession::begin(gint64 nodeId, const Glib::RefPtr<Gtk::TextBuffer>& buffer, bool hasWidgets, CtTreeIter* /*treeIter*/)
{
    // End any existing session first
    if (_active) {
        spdlog::warn("CtTextEditSession: beginning new session while one is active");
        cancel();
    }

    _nodeId = nodeId;
    _initialCursorPos = buffer->get_insert()->get_iter().get_offset();
    _active = true;
    _hasWidgets = hasWidgets;
    _suppressCapture = false;
    _capturedCommands.clear();

    // Snapshot for deduplication — detect sessions where all edits cancel out
    auto node = _docModel->getNodeById(nodeId);
    _initialXml = node ? node->getContent().toXml() : Glib::ustring{};

    startSignalCapture(buffer);
}

std::unique_ptr<CtCommand> CtTextEditSession::end(const Glib::RefPtr<Gtk::TextBuffer>& /*buffer*/,
                                                   const std::list<CtAnchoredWidget*>& /*widgets*/,
                                                   int cursorPos)
{
    if (!_active) {
        spdlog::debug("CtTextEditSession: end called but no active session");
        return nullptr;
    }

    stopSignalCapture();

    if (_capturedCommands.empty()) {
        _active = false;
        return nullptr;
    }

    // Deduplication: if all edits cancel out (e.g. type 'a' then backspace),
    // the model is unchanged — discard the session rather than pushing a no-op.
    // Skip this check when model sync is disabled (rich cell sessions) because
    // the model wasn't updated, so it would always appear unchanged.
    if (!_skipModelSync) {
        auto node = _docModel->getNodeById(_nodeId);
        const Glib::ustring currentXml = node ? node->getContent().toXml() : Glib::ustring{};
        if (currentXml == _initialXml) {
            _active = false;
            _capturedCommands.clear();
            return nullptr;
        }
    }

    // If all captured commands have empty descriptions (e.g., only single spaces),
    // don't create a command - the space is already in the buffer
    bool allEmpty = true;
    for (const auto& cmd : _capturedCommands) {
        if (!cmd->getDescription().empty()) {
            allEmpty = false;
            break;
        }
    }
    if (allEmpty) {
        _active = false;
        _capturedCommands.clear();
        return nullptr;
    }

    // Model was kept in sync via onBufferInsert/onBufferErase for all node types

    // Build description from sub-commands
    std::string description;
    if (_capturedCommands.size() == 1) {
        description = _capturedCommands[0]->getDescription();
        // Handle single command with empty description (e.g., single space)
        if (description.empty()) {
            description = "Node " + std::to_string(_nodeId) + ": Edit text";
        }
    } else {
        // Multiple commands - build combined description
        std::vector<std::string> parts;
        for (const auto& cmd : _capturedCommands) {
            std::string desc = cmd->getDescription();
            // Skip empty descriptions (single spaces used as separators)
            if (desc.empty()) continue;
            // Strip "Node X: " prefix
            std::string prefix = "Node " + std::to_string(_nodeId) + ": ";
            if (desc.find(prefix) == 0) {
                desc = desc.substr(prefix.length());
            }
            // Skip if part is now empty
            if (!desc.empty()) {
                parts.push_back(desc);
            }
        }

        // Combine all parts with " + "
        description = "Node " + std::to_string(_nodeId) + ": ";
        if (parts.empty()) {
            description += "Edit text";
        } else {
            for (size_t i = 0; i < parts.size(); ++i) {
                description += parts[i];
                if (i < parts.size() - 1) {
                    description += " + ";
                }
            }
        }
    }

    _active = false;

    // Build compound command from captured delta commands
    auto compound = std::make_unique<CompoundCommand>(description);
    compound->setNodeId(_nodeId);
    compound->setDocumentModel(_docModel);
    compound->setOldCursorPos(_initialCursorPos);
    compound->setNewCursorPos(cursorPos);
    for (auto& cmd : _capturedCommands) {
        compound->addCommand(std::move(cmd));
    }
    _capturedCommands.clear();
    return compound;
}

void CtTextEditSession::cancel()
{
    if (!_active) {
        return;
    }

    stopSignalCapture();

    _active = false;
    _nodeId = 0;
    _initialCursorPos = -1;
    _capturedCommands.clear();
}

void CtTextEditSession::startSignalCapture(const Glib::RefPtr<Gtk::TextBuffer>& buffer)
{
    if (!buffer) return;

    _insertConnection = buffer->signal_insert().connect(
        sigc::mem_fun(*this, &CtTextEditSession::onBufferInsert), false);

    _eraseConnection = buffer->signal_erase().connect(
        sigc::mem_fun(*this, &CtTextEditSession::onBufferErase), false);
}

void CtTextEditSession::stopSignalCapture()
{
    if (_insertConnection.connected()) {
        _insertConnection.disconnect();
    }
    if (_eraseConnection.connected()) {
        _eraseConnection.disconnect();
    }
}

// Returns true if tagName starts with prefix (replacement for str::startswith without ct_misc_utils.h).
static bool startsWith(const std::string& s, const Glib::ustring& prefix)
{
    return s.rfind(std::string(prefix), 0) == 0;
}

// Returns true if tagName is a user-applied CherryTree format tag (not syntax/internal).
static bool isUserFormattingTag(const std::string& tagName)
{
    return startsWith(tagName, CtConst::TAG_WEIGHT_PREFIX)
        || startsWith(tagName, CtConst::TAG_FOREGROUND_PREFIX)
        || startsWith(tagName, CtConst::TAG_BACKGROUND_PREFIX)
        || startsWith(tagName, CtConst::TAG_SCALE_PREFIX)
        || startsWith(tagName, CtConst::TAG_JUSTIFICATION_PREFIX)
        || startsWith(tagName, CtConst::TAG_STYLE_PREFIX)
        || startsWith(tagName, CtConst::TAG_UNDERLINE_PREFIX)
        || startsWith(tagName, CtConst::TAG_STRIKETHROUGH_PREFIX)
        || startsWith(tagName, CtConst::TAG_INDENT_PREFIX)
        || startsWith(tagName, CtConst::TAG_FAMILY_PREFIX);
}

void CtTextEditSession::startTagCapture(const Glib::RefPtr<Gtk::TextBuffer>& buffer)
{
    if (!buffer) return;
    stopTagCapture();
    _pendingTagChanges.clear();
    _applyTagConnection = buffer->signal_apply_tag().connect(
        sigc::mem_fun(*this, &CtTextEditSession::onTagApplied));
    _removeTagConnection = buffer->signal_remove_tag().connect(
        sigc::mem_fun(*this, &CtTextEditSession::onTagRemoved));
}

void CtTextEditSession::stopTagCapture()
{
    if (_applyTagConnection.connected()) _applyTagConnection.disconnect();
    if (_removeTagConnection.connected()) _removeTagConnection.disconnect();
}

void CtTextEditSession::onTagApplied(const Glib::RefPtr<Gtk::TextTag>& tag,
                                      const Gtk::TextIter& start,
                                      const Gtk::TextIter& end)
{
    const Glib::ustring tagNameU = tag->property_name();
    const std::string tagName = std::string(tagNameU);
    if (tagName.empty() || !isUserFormattingTag(tagName)) return;
    _pendingTagChanges.push_back({tagName, start.get_offset(), end.get_offset(), true});
}

void CtTextEditSession::onTagRemoved(const Glib::RefPtr<Gtk::TextTag>& tag,
                                      const Gtk::TextIter& start,
                                      const Gtk::TextIter& end)
{
    const Glib::ustring tagNameU = tag->property_name();
    const std::string tagName = std::string(tagNameU);
    if (tagName.empty() || !isUserFormattingTag(tagName)) return;
    _pendingTagChanges.push_back({tagName, start.get_offset(), end.get_offset(), false});
}

std::vector<CtTextEditSession::TagChange> CtTextEditSession::drainAndCoalesceTagChanges()
{
    std::vector<TagChange> result;
    for (const auto& ch : _pendingTagChanges) {
        if (!result.empty()
                && result.back().tagName == ch.tagName
                && result.back().isApply == ch.isApply
                && result.back().end == ch.start) {
            // Adjacent range for same tag+direction — extend.
            result.back().end = ch.end;
        } else {
            result.push_back(ch);
        }
    }
    _pendingTagChanges.clear();
    return result;
}

void CtTextEditSession::onBufferInsert(const Gtk::TextBuffer::iterator& pos,
                                        const Glib::ustring& text, int /*bytes*/)
{
    if (_suppressCapture) {
        return;
    }

    // Note: The insert signal passes 'pos' pointing to the START of the newly inserted text
    // This is the offset we need for undo
    int offset = pos.get_offset();
    auto attrs = extractAttributesFromIter(pos);

    // Helper to check if text is a special character that should be a separate command
    auto isSpecialChar = [](const Glib::ustring& txt) {
        return txt == " " || txt == "\n" || txt == "\t";
    };

    // Keep model in sync with buffer during session (needed for delta-based undo/redo).
    // Skip when this session tracks a rich cell buffer (independent of node model).
    if (!_skipModelSync) {
        auto node = _docModel->getNodeById(_nodeId);
        if (node) {
            try {
                node->getContent().insertText(offset, text, attrs);
            } catch (const std::out_of_range& e) {
                // Model is out of sync with buffer - this can happen after widget
                // insertions or other buffer modifications outside a session.
                // Log and skip; the next beginTextEditSession will re-sync.
                spdlog::warn("onBufferInsert: model out of sync at offset {} (model length={}): {}",
                             offset, node->getContent().length(), e.what());
            }
        }
    }

    // Coalesce: if last command is an adjacent insert with same attrs, extend it
    // BUT: don't coalesce special characters (space, newline, tab) - keep them separate
    if (!_capturedCommands.empty()) {
        auto* last = dynamic_cast<InsertTextCommand*>(_capturedCommands.back().get());
        // Check if this insert is immediately after the previous insert
        // The new text starts at 'offset', so check if previous insert ends at 'offset'
        if (last &&
            last->getOffset() + static_cast<int>(last->getText().length()) == offset &&
            last->getAttributes() == attrs &&
            !isSpecialChar(text) &&
            !isSpecialChar(last->getText()))
        {
            last->appendText(text);
            return;
        }
    }

    // Create new InsertTextCommand
    _capturedCommands.push_back(
        std::make_unique<InsertTextCommand>(_docModel, _nodeId, offset, text, attrs));
}

void CtTextEditSession::onBufferErase(const Gtk::TextBuffer::iterator& start,
                                       const Gtk::TextBuffer::iterator& end)
{
    if (_suppressCapture) {
        return;
    }

    int startOffset = start.get_offset();
    int length = end.get_offset() - startOffset;

    // Create DeleteRangeCommand (constructor captures deleted content from model via extractRange)
    auto deleteCmd = std::make_unique<DeleteRangeCommand>(_docModel, _nodeId, startOffset, length);

    // Keep model in sync with buffer during session (needed for delta-based undo/redo).
    // Skip when this session tracks a rich cell buffer (independent of node model).
    if (!_skipModelSync) {
        auto node = _docModel->getNodeById(_nodeId);
        if (node) {
            node->getContent().deleteRange(startOffset, length);
        }
    }

    _capturedCommands.push_back(std::move(deleteCmd));
}

// InsertTextCommand implementation

InsertTextCommand::InsertTextCommand(
    std::shared_ptr<CtDocumentModel> docModel,
    gint64 nodeId,
    int offset,
    const Glib::ustring& text,
    const std::map<std::string, std::string>& attributes,
    int oldCursorPos,
    int newCursorPos)
    : _docModel(docModel)
    , _nodeId(nodeId)
    , _offset(offset)
    , _text(text)
    , _attributes(attributes)
    , _oldCursorPos(oldCursorPos)
    , _newCursorPos(newCursorPos)
    , _insertedLength(static_cast<int>(text.length()))
{
}

void InsertTextCommand::execute()
{
    auto node = _docModel->getNodeById(_nodeId);
    if (!node) {
        spdlog::error("InsertTextCommand: node {} not found", _nodeId);
        return;
    }

    // Insert text into the content model
    int endOffset = node->getContent().insertText(_offset, _text, _attributes);
    _insertedLength = endOffset - _offset;

    // Notify observers that node changed
    _docModel->notifyNodeChanged(_nodeId);

    spdlog::debug("InsertTextCommand: inserted {} chars at offset {} in node {}",
                  _insertedLength, _offset, _nodeId);
}

void InsertTextCommand::undo()
{
    auto node = _docModel->getNodeById(_nodeId);
    if (!node) {
        spdlog::error("InsertTextCommand: node {} not found for undo", _nodeId);
        return;
    }

    spdlog::debug("InsertTextCommand::undo() - deleting {} chars at offset {}",
                  static_cast<int>(_text.length()), _offset);

    // Use delta-based undo
    int lengthToDelete = static_cast<int>(_text.length());
    node->getContent().deleteRange(_offset, lengthToDelete);

    // Notify observers
    spdlog::info("InsertTextCommand::undo - about to call notifyNodeChanged for node {}", _nodeId);
    _docModel->notifyNodeChanged(_nodeId);
    spdlog::info("InsertTextCommand::undo - notifyNodeChanged completed for node {}", _nodeId);

    spdlog::debug("InsertTextCommand: undone - deleted {} chars at offset {} in node {}",
                  static_cast<int>(_text.length()), _offset, _nodeId);
}

void InsertTextCommand::redo()
{
    // Re-execute the insert
    execute();
}

std::string InsertTextCommand::getDescription() const
{
    std::string description = "Node " + std::to_string(_nodeId) + ": ";

    // Handle special characters for readable single-line display
    if (_text == " ") {
        // Single space only - this is a word separator, hide it from undo list
        // Return empty description so it can be filtered out
        return "";
    }
    else if (_text == "\n") {
        description += "Newline";
    }
    else if (_text == "\t") {
        description += "Tab";
    }
    else {
        // Check if text is only whitespace
        bool onlySpaces = true;
        bool onlyNewlines = true;
        for (auto c : _text) {
            if (c != ' ') onlySpaces = false;
            if (c != '\n') onlyNewlines = false;
        }
        if (onlySpaces && _text.length() > 1) {
            description += "Space × " + std::to_string(_text.length());
        }
        else if (onlyNewlines && _text.length() > 1) {
            description += "Newline × " + std::to_string(_text.length());
        }
        else {
            // Replace newlines with visible representation for display
            Glib::ustring displayText = _text;
            size_t pos = 0;
            while ((pos = displayText.find('\n', pos)) != Glib::ustring::npos) {
                displayText.replace(pos, 1, "\\n");
                pos += 2;
            }

            // Strip trailing single space from display (word separator)
            if (displayText.length() > 1 && displayText[displayText.length() - 1] == ' ') {
                // Check if only one trailing space (not multiple)
                if (displayText.length() < 2 || displayText[displayText.length() - 2] != ' ') {
                    displayText = displayText.substr(0, displayText.length() - 1);
                }
            }

            if (displayText.length() > 20) {
                description += "Type \"" + displayText.substr(0, 20).raw() + "...\"";
            } else {
                description += "Type \"" + displayText.raw() + "\"";
            }
        }
    }

    return description;
}

// DeleteRangeCommand implementation

DeleteRangeCommand::DeleteRangeCommand(
    std::shared_ptr<CtDocumentModel> docModel,
    gint64 nodeId,
    int start,
    int length,
    int oldCursorPos,
    int newCursorPos)
    : _docModel(docModel)
    , _nodeId(nodeId)
    , _start(start)
    , _length(length)
    , _oldCursorPos(oldCursorPos)
    , _newCursorPos(newCursorPos)
    , _deletedContent(start)
{
    // Capture deleted content from model at construction time
    // The model still has pre-edit state when signals fire (buffer-first architecture)
    auto node = _docModel->getNodeById(_nodeId);
    if (node) {
        _deletedContent = node->getContent().extractRange(_start, _length);
        spdlog::debug("DeleteRangeCommand: captured {} elements from range [{}, {})",
                      _deletedContent.elements.size(), _start, _start + _length);
    }
}

void DeleteRangeCommand::execute()
{
    auto node = _docModel->getNodeById(_nodeId);
    if (!node) {
        spdlog::error("DeleteRangeCommand: node {} not found", _nodeId);
        return;
    }

    // Delete range (content already captured in constructor for buffer-first flow)
    // If executed normally (model-first flow), capture content now
    if (_deletedContent.isEmpty()) {
        _deletedContent = node->getContent().deleteRange(_start, _length);
    } else {
        // Content already captured, just delete
        node->getContent().deleteRange(_start, _length);
    }

    // Notify observers
    _docModel->notifyNodeChanged(_nodeId);

    spdlog::debug("DeleteRangeCommand: deleted {} chars at offset {} in node {}",
                  _length, _start, _nodeId);
}

void DeleteRangeCommand::undo()
{
    auto node = _docModel->getNodeById(_nodeId);
    if (!node) {
        spdlog::error("DeleteRangeCommand: node {} not found for undo", _nodeId);
        return;
    }

    // Re-insert the deleted content
    node->getContent().reinsertContent(_deletedContent);

    // Notify observers
    _docModel->notifyNodeChanged(_nodeId);

    spdlog::debug("DeleteRangeCommand: undone - reinserted {} chars at offset {} in node {}",
                  _length, _start, _nodeId);
}

void DeleteRangeCommand::redo()
{
    auto node = _docModel->getNodeById(_nodeId);
    if (!node) {
        spdlog::error("DeleteRangeCommand: node {} not found for redo", _nodeId);
        return;
    }

    // Delete again (don't call execute() because we don't want to overwrite _deletedContent)
    node->getContent().deleteRange(_start, _length);

    // Notify observers
    _docModel->notifyNodeChanged(_nodeId);
}

std::string DeleteRangeCommand::getDescription() const
{
    return "Node " + std::to_string(_nodeId) + ": Delete " + std::to_string(_length) + " chars";
}

// ApplyFormatCommand implementation

ApplyFormatCommand::ApplyFormatCommand(
    std::shared_ptr<CtDocumentModel> docModel,
    gint64 nodeId,
    int start,
    int length,
    const std::string& attribute,
    const std::string& value,
    int oldCursorPos,
    int newCursorPos)
    : _docModel(docModel)
    , _nodeId(nodeId)
    , _start(start)
    , _length(length)
    , _attribute(attribute)
    , _value(value)
    , _oldCursorPos(oldCursorPos)
    , _newCursorPos(newCursorPos)
{
}

void ApplyFormatCommand::execute()
{
    auto node = _docModel->getNodeById(_nodeId);
    if (!node) {
        spdlog::error("ApplyFormatCommand: node {} not found", _nodeId);
        return;
    }

    // Apply format and capture old values for undo
    _formatChange = node->getContent().applyFormat(_start, _length, _attribute, _value);

    // Notify observers
    _docModel->notifyNodeChanged(_nodeId);

    spdlog::debug("ApplyFormatCommand: applied {}={} to {} chars at offset {} in node {}",
                  _attribute, _value, _length, _start, _nodeId);
}

void ApplyFormatCommand::undo()
{
    auto node = _docModel->getNodeById(_nodeId);
    if (!node) {
        spdlog::error("ApplyFormatCommand: node {} not found for undo", _nodeId);
        return;
    }

    // Restore old format values
    node->getContent().restoreFormat(_start, _length, _attribute, _formatChange);

    // Notify observers
    _docModel->notifyNodeChanged(_nodeId);

    spdlog::debug("ApplyFormatCommand: undone format {}={} in node {}", _attribute, _value, _nodeId);
}

void ApplyFormatCommand::redo()
{
    auto node = _docModel->getNodeById(_nodeId);
    if (!node) {
        spdlog::error("ApplyFormatCommand: node {} not found for redo", _nodeId);
        return;
    }

    // Re-apply format (don't call execute() to avoid overwriting _formatChange)
    node->getContent().applyFormat(_start, _length, _attribute, _value);

    // Notify observers
    _docModel->notifyNodeChanged(_nodeId);
}

std::string ApplyFormatCommand::getDescription() const
{
    return "Node " + std::to_string(_nodeId) + ": Format " + _attribute + "=" + _value;
}

// RemoveFormatCommand implementation

RemoveFormatCommand::RemoveFormatCommand(
    std::shared_ptr<CtDocumentModel> docModel,
    gint64 nodeId,
    int start,
    int length,
    const std::string& attribute,
    int oldCursorPos,
    int newCursorPos)
    : _docModel(docModel)
    , _nodeId(nodeId)
    , _start(start)
    , _length(length)
    , _attribute(attribute)
    , _oldCursorPos(oldCursorPos)
    , _newCursorPos(newCursorPos)
{
}

void RemoveFormatCommand::execute()
{
    auto node = _docModel->getNodeById(_nodeId);
    if (!node) {
        spdlog::error("RemoveFormatCommand: node {} not found", _nodeId);
        return;
    }

    // Remove format and capture old values for undo
    _formatChange = node->getContent().removeFormat(_start, _length, _attribute);

    // Notify observers
    _docModel->notifyNodeChanged(_nodeId);

    spdlog::debug("RemoveFormatCommand: removed {} from {} chars at offset {} in node {}",
                  _attribute, _length, _start, _nodeId);
}

void RemoveFormatCommand::undo()
{
    auto node = _docModel->getNodeById(_nodeId);
    if (!node) {
        spdlog::error("RemoveFormatCommand: node {} not found for undo", _nodeId);
        return;
    }

    // Restore old format values
    node->getContent().restoreFormat(_start, _length, _attribute, _formatChange);

    // Notify observers
    _docModel->notifyNodeChanged(_nodeId);

    spdlog::debug("RemoveFormatCommand: undone removal of {} in node {}", _attribute, _nodeId);
}

void RemoveFormatCommand::redo()
{
    auto node = _docModel->getNodeById(_nodeId);
    if (!node) {
        spdlog::error("RemoveFormatCommand: node {} not found for redo", _nodeId);
        return;
    }

    // Re-remove format (don't call execute() to avoid overwriting _formatChange)
    node->getContent().removeFormat(_start, _length, _attribute);

    // Notify observers
    _docModel->notifyNodeChanged(_nodeId);
}

std::string RemoveFormatCommand::getDescription() const
{
    return "Node " + std::to_string(_nodeId) + ": Remove format " + _attribute;
}

