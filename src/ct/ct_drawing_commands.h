/*
 * ct_drawing_commands.h
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

#pragma once

#include "ct_command.h"
#include "ct_drawing.h"
#include "ct_document_model.h"
#include <memory>

class DrawStrokeCommand : public CtCommand {
public:
    DrawStrokeCommand(std::shared_ptr<CtDocumentModel> docModel,
                      gint64 nodeId, size_t canvasIdx,
                      CtDrawingStroke stroke)
        : _docModel(std::move(docModel))
        , _nodeId(nodeId)
        , _canvasIdx(canvasIdx)
        , _stroke(std::move(stroke))
    {}

    void execute() override {
        auto node = _docModel->getNodeById(_nodeId);
        if (!node) return;
        auto& canvases = node->getDrawingCanvasesMut();
        if (_canvasIdx < canvases.size()) {
            canvases[_canvasIdx].strokes.push_back(_stroke);
            _docModel->notifyNodeDrawingChanged(_nodeId);
        }
    }

    void undo() override {
        auto node = _docModel->getNodeById(_nodeId);
        if (!node) return;
        auto& canvases = node->getDrawingCanvasesMut();
        if (_canvasIdx < canvases.size() && !canvases[_canvasIdx].strokes.empty()) {
            canvases[_canvasIdx].strokes.pop_back();
            _docModel->notifyNodeDrawingChanged(_nodeId);
        }
    }

    std::string getDescription() const override { return "Draw stroke"; }
    gint64 getNodeId() const override { return _nodeId; }

private:
    std::shared_ptr<CtDocumentModel> _docModel;
    gint64 _nodeId;
    size_t _canvasIdx;
    CtDrawingStroke _stroke;
};

class EraseStrokeCommand : public CtCommand {
public:
    EraseStrokeCommand(std::shared_ptr<CtDocumentModel> docModel,
                       gint64 nodeId, size_t canvasIdx,
                       CtDrawingStroke stroke, size_t strokeIdx)
        : _docModel(std::move(docModel))
        , _nodeId(nodeId)
        , _canvasIdx(canvasIdx)
        , _stroke(std::move(stroke))
        , _strokeIdx(strokeIdx)
    {}

    void execute() override {
        auto node = _docModel->getNodeById(_nodeId);
        if (!node) return;
        auto& canvases = node->getDrawingCanvasesMut();
        if (_canvasIdx < canvases.size() && _strokeIdx < canvases[_canvasIdx].strokes.size()) {
            canvases[_canvasIdx].strokes.erase(canvases[_canvasIdx].strokes.begin() + _strokeIdx);
            _docModel->notifyNodeDrawingChanged(_nodeId);
        }
    }

    void undo() override {
        auto node = _docModel->getNodeById(_nodeId);
        if (!node) return;
        auto& canvases = node->getDrawingCanvasesMut();
        if (_canvasIdx < canvases.size()) {
            auto& strokes = canvases[_canvasIdx].strokes;
            if (_strokeIdx <= strokes.size()) {
                strokes.insert(strokes.begin() + _strokeIdx, _stroke);
            } else {
                strokes.push_back(_stroke);
            }
            _docModel->notifyNodeDrawingChanged(_nodeId);
        }
    }

    std::string getDescription() const override { return "Erase stroke"; }
    gint64 getNodeId() const override { return _nodeId; }

private:
    std::shared_ptr<CtDocumentModel> _docModel;
    gint64 _nodeId;
    size_t _canvasIdx;
    CtDrawingStroke _stroke;
    size_t _strokeIdx;
};

class RotateStrokeCommand : public CtCommand {
public:
    RotateStrokeCommand(std::shared_ptr<CtDocumentModel> docModel,
                        gint64 nodeId, size_t canvasIdx, size_t strokeIdx,
                        double oldRotation, double newRotation)
        : _docModel(std::move(docModel))
        , _nodeId(nodeId)
        , _canvasIdx(canvasIdx)
        , _strokeIdx(strokeIdx)
        , _oldRotation(oldRotation)
        , _newRotation(newRotation)
    {}

    void execute() override {
        auto node = _docModel->getNodeById(_nodeId);
        if (!node) return;
        auto& canvases = node->getDrawingCanvasesMut();
        if (_canvasIdx < canvases.size() && _strokeIdx < canvases[_canvasIdx].strokes.size()) {
            canvases[_canvasIdx].strokes[_strokeIdx].rotation = _newRotation;
            _docModel->notifyNodeDrawingChanged(_nodeId);
        }
    }

    void undo() override {
        auto node = _docModel->getNodeById(_nodeId);
        if (!node) return;
        auto& canvases = node->getDrawingCanvasesMut();
        if (_canvasIdx < canvases.size() && _strokeIdx < canvases[_canvasIdx].strokes.size()) {
            canvases[_canvasIdx].strokes[_strokeIdx].rotation = _oldRotation;
            _docModel->notifyNodeDrawingChanged(_nodeId);
        }
    }

    std::string getDescription() const override { return "Rotate stroke"; }
    gint64 getNodeId() const override { return _nodeId; }

private:
    std::shared_ptr<CtDocumentModel> _docModel;
    gint64 _nodeId;
    size_t _canvasIdx;
    size_t _strokeIdx;
    double _oldRotation;
    double _newRotation;
};

class MoveStrokeCommand : public CtCommand {
public:
    MoveStrokeCommand(std::shared_ptr<CtDocumentModel> docModel,
                      gint64 nodeId, size_t canvasIdx, size_t strokeIdx,
                      std::vector<CtDrawingPoint> oldPoints,
                      std::vector<CtDrawingPoint> newPoints)
        : _docModel(std::move(docModel))
        , _nodeId(nodeId)
        , _canvasIdx(canvasIdx)
        , _strokeIdx(strokeIdx)
        , _oldPoints(std::move(oldPoints))
        , _newPoints(std::move(newPoints))
    {}

    void execute() override {
        auto node = _docModel->getNodeById(_nodeId);
        if (!node) return;
        auto& canvases = node->getDrawingCanvasesMut();
        if (_canvasIdx < canvases.size() && _strokeIdx < canvases[_canvasIdx].strokes.size()) {
            canvases[_canvasIdx].strokes[_strokeIdx].points = _newPoints;
            _docModel->notifyNodeDrawingChanged(_nodeId);
        }
    }

    void undo() override {
        auto node = _docModel->getNodeById(_nodeId);
        if (!node) return;
        auto& canvases = node->getDrawingCanvasesMut();
        if (_canvasIdx < canvases.size() && _strokeIdx < canvases[_canvasIdx].strokes.size()) {
            canvases[_canvasIdx].strokes[_strokeIdx].points = _oldPoints;
            _docModel->notifyNodeDrawingChanged(_nodeId);
        }
    }

    std::string getDescription() const override { return "Move stroke"; }
    gint64 getNodeId() const override { return _nodeId; }

private:
    std::shared_ptr<CtDocumentModel> _docModel;
    gint64 _nodeId;
    size_t _canvasIdx;
    size_t _strokeIdx;
    std::vector<CtDrawingPoint> _oldPoints;
    std::vector<CtDrawingPoint> _newPoints;
};

class AddCanvasCommand : public CtCommand {
public:
    AddCanvasCommand(std::shared_ptr<CtDocumentModel> docModel,
                     gint64 nodeId, CtDrawingCanvas canvas)
        : _docModel(std::move(docModel))
        , _nodeId(nodeId)
        , _canvas(std::move(canvas))
    {}

    void execute() override {
        auto node = _docModel->getNodeById(_nodeId);
        if (!node) return;
        auto& canvases = node->getDrawingCanvasesMut();
        canvases.push_back(_canvas);
        _canvasIdx = canvases.size() - 1;
        _docModel->notifyNodeDrawingChanged(_nodeId);
    }

    void undo() override {
        auto node = _docModel->getNodeById(_nodeId);
        if (!node) return;
        auto& canvases = node->getDrawingCanvasesMut();
        if (_canvasIdx < canvases.size()) {
            canvases.erase(canvases.begin() + _canvasIdx);
            _docModel->notifyNodeDrawingChanged(_nodeId);
        }
    }

    std::string getDescription() const override { return "Add drawing canvas"; }
    gint64 getNodeId() const override { return _nodeId; }
    size_t getCanvasIdx() const { return _canvasIdx; }

private:
    std::shared_ptr<CtDocumentModel> _docModel;
    gint64 _nodeId;
    CtDrawingCanvas _canvas;
    size_t _canvasIdx{0};
};

class DeleteCanvasCommand : public CtCommand {
public:
    DeleteCanvasCommand(std::shared_ptr<CtDocumentModel> docModel,
                        gint64 nodeId, CtDrawingCanvas canvas, size_t canvasIdx)
        : _docModel(std::move(docModel))
        , _nodeId(nodeId)
        , _canvas(std::move(canvas))
        , _canvasIdx(canvasIdx)
    {}

    void execute() override {
        auto node = _docModel->getNodeById(_nodeId);
        if (!node) return;
        auto& canvases = node->getDrawingCanvasesMut();
        if (_canvasIdx < canvases.size()) {
            canvases.erase(canvases.begin() + _canvasIdx);
            _docModel->notifyNodeDrawingChanged(_nodeId);
        }
    }

    void undo() override {
        auto node = _docModel->getNodeById(_nodeId);
        if (!node) return;
        auto& canvases = node->getDrawingCanvasesMut();
        if (_canvasIdx <= canvases.size()) {
            canvases.insert(canvases.begin() + _canvasIdx, _canvas);
        } else {
            canvases.push_back(_canvas);
        }
        _docModel->notifyNodeDrawingChanged(_nodeId);
    }

    std::string getDescription() const override { return "Delete drawing canvas"; }
    gint64 getNodeId() const override { return _nodeId; }

private:
    std::shared_ptr<CtDocumentModel> _docModel;
    gint64 _nodeId;
    CtDrawingCanvas _canvas;
    size_t _canvasIdx;
};

class MoveCanvasCommand : public CtCommand {
public:
    MoveCanvasCommand(std::shared_ptr<CtDocumentModel> docModel,
                      gint64 nodeId, size_t canvasIdx,
                      double oldX, double oldY, double newX, double newY)
        : _docModel(std::move(docModel))
        , _nodeId(nodeId)
        , _canvasIdx(canvasIdx)
        , _oldX(oldX), _oldY(oldY)
        , _newX(newX), _newY(newY)
    {}

    void execute() override {
        auto node = _docModel->getNodeById(_nodeId);
        if (!node) return;
        auto& canvases = node->getDrawingCanvasesMut();
        if (_canvasIdx < canvases.size()) {
            canvases[_canvasIdx].x = _newX;
            canvases[_canvasIdx].y = _newY;
            _docModel->notifyNodeDrawingChanged(_nodeId);
        }
    }

    void undo() override {
        auto node = _docModel->getNodeById(_nodeId);
        if (!node) return;
        auto& canvases = node->getDrawingCanvasesMut();
        if (_canvasIdx < canvases.size()) {
            canvases[_canvasIdx].x = _oldX;
            canvases[_canvasIdx].y = _oldY;
            _docModel->notifyNodeDrawingChanged(_nodeId);
        }
    }

    std::string getDescription() const override { return "Move drawing canvas"; }
    gint64 getNodeId() const override { return _nodeId; }

private:
    std::shared_ptr<CtDocumentModel> _docModel;
    gint64 _nodeId;
    size_t _canvasIdx;
    double _oldX, _oldY, _newX, _newY;
};

class ResizeCanvasCommand : public CtCommand {
public:
    ResizeCanvasCommand(std::shared_ptr<CtDocumentModel> docModel,
                        gint64 nodeId, size_t canvasIdx,
                        double oldX, double oldY, double oldW, double oldH,
                        double newX, double newY, double newW, double newH)
        : _docModel(std::move(docModel))
        , _nodeId(nodeId)
        , _canvasIdx(canvasIdx)
        , _oldX(oldX), _oldY(oldY), _oldW(oldW), _oldH(oldH)
        , _newX(newX), _newY(newY), _newW(newW), _newH(newH)
    {}

    void execute() override {
        auto node = _docModel->getNodeById(_nodeId);
        if (!node) return;
        auto& canvases = node->getDrawingCanvasesMut();
        if (_canvasIdx < canvases.size()) {
            canvases[_canvasIdx].x = _newX;
            canvases[_canvasIdx].y = _newY;
            canvases[_canvasIdx].width = _newW;
            canvases[_canvasIdx].height = _newH;
            _docModel->notifyNodeDrawingChanged(_nodeId);
        }
    }

    void undo() override {
        auto node = _docModel->getNodeById(_nodeId);
        if (!node) return;
        auto& canvases = node->getDrawingCanvasesMut();
        if (_canvasIdx < canvases.size()) {
            canvases[_canvasIdx].x = _oldX;
            canvases[_canvasIdx].y = _oldY;
            canvases[_canvasIdx].width = _oldW;
            canvases[_canvasIdx].height = _oldH;
            _docModel->notifyNodeDrawingChanged(_nodeId);
        }
    }

    std::string getDescription() const override { return "Resize drawing canvas"; }
    gint64 getNodeId() const override { return _nodeId; }

private:
    std::shared_ptr<CtDocumentModel> _docModel;
    gint64 _nodeId;
    size_t _canvasIdx;
    double _oldX, _oldY, _oldW, _oldH;
    double _newX, _newY, _newW, _newH;
};

class CanvasPropertiesCommand : public CtCommand {
public:
    CanvasPropertiesCommand(std::shared_ptr<CtDocumentModel> docModel,
                            gint64 nodeId, size_t canvasIdx,
                            std::string oldName, std::string newName,
                            std::string oldBgColor, std::string newBgColor,
                            double oldBgOpacity, double newBgOpacity,
                            double oldCornerRadius, double newCornerRadius,
                            bool oldShowBorder, bool newShowBorder)
        : _docModel(std::move(docModel))
        , _nodeId(nodeId)
        , _canvasIdx(canvasIdx)
        , _oldName(std::move(oldName)), _newName(std::move(newName))
        , _oldBgColor(std::move(oldBgColor)), _newBgColor(std::move(newBgColor))
        , _oldBgOpacity(oldBgOpacity), _newBgOpacity(newBgOpacity)
        , _oldCornerRadius(oldCornerRadius), _newCornerRadius(newCornerRadius)
        , _oldShowBorder(oldShowBorder), _newShowBorder(newShowBorder)
    {}

    void execute() override {
        auto node = _docModel->getNodeById(_nodeId);
        if (!node) return;
        auto& canvases = node->getDrawingCanvasesMut();
        if (_canvasIdx < canvases.size()) {
            canvases[_canvasIdx].name = _newName;
            canvases[_canvasIdx].bgColor = _newBgColor;
            canvases[_canvasIdx].bgOpacity = _newBgOpacity;
            canvases[_canvasIdx].cornerRadius = _newCornerRadius;
            canvases[_canvasIdx].showBorderWhenInactive = _newShowBorder;
            _docModel->notifyNodeDrawingChanged(_nodeId);
        }
    }

    void undo() override {
        auto node = _docModel->getNodeById(_nodeId);
        if (!node) return;
        auto& canvases = node->getDrawingCanvasesMut();
        if (_canvasIdx < canvases.size()) {
            canvases[_canvasIdx].name = _oldName;
            canvases[_canvasIdx].bgColor = _oldBgColor;
            canvases[_canvasIdx].bgOpacity = _oldBgOpacity;
            canvases[_canvasIdx].cornerRadius = _oldCornerRadius;
            canvases[_canvasIdx].showBorderWhenInactive = _oldShowBorder;
            _docModel->notifyNodeDrawingChanged(_nodeId);
        }
    }

    std::string getDescription() const override { return "Canvas properties"; }
    gint64 getNodeId() const override { return _nodeId; }

private:
    std::shared_ptr<CtDocumentModel> _docModel;
    gint64 _nodeId;
    size_t _canvasIdx;
    std::string _oldName, _newName;
    std::string _oldBgColor, _newBgColor;
    double _oldBgOpacity, _newBgOpacity;
    double _oldCornerRadius, _newCornerRadius;
    bool _oldShowBorder, _newShowBorder;
};
