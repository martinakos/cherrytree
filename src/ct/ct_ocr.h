/*
 * ct_ocr.h
 *
 * Copyright 2009-2026
 * Giuseppe Penone <giuspen@gmail.com>
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

#ifdef HAVE_NCNN

#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <glibmm/ustring.h>
#include <glibmm/dispatcher.h>
#include "ct_types.h"

class CtTreeStore;
class CtImagePng;

namespace OCR { class OCREngine; }

struct CtOcrWorkItem {
    std::string pngBlob;
    gint64      nodeId;
    CtImagePng* pImagePng;
    int         charOffset{0};
    bool        priority{false};
};

struct CtOcrResult {
    gint64         nodeId;
    CtImagePng*    pImagePng;
    Glib::ustring  ocrText;
    std::string    ocrBoxes;
};

class CtOcrManager
{
public:
    CtOcrManager(const std::string& modelDir);
    ~CtOcrManager();

    bool enqueue(const std::string& pngBlob, gint64 nodeId, CtImagePng* pImagePng, bool priority = false);
    void enqueue_all_missing(CtTreeStore& treeStore);
    void enqueue_all(CtTreeStore& treeStore);

    static bool is_available();
    size_t pending_count() const;
    size_t total_enqueued() const;

    Glib::Dispatcher dispatcherOcrResult;
    ThreadSafeDEQueue<CtOcrResult, 256> resultQueue;

    struct OcrData {
        Glib::ustring text;
        std::string boxes;
    };

private:
    void _worker_func();
    OcrData _run_ocr(const std::string& pngBlob);

    ThreadSafeDEQueue<CtOcrWorkItem*, 1024> _workQueue;
    std::thread _workerThread;
    std::atomic<bool> _shutDown{false};
    std::atomic<bool> _hasWork{false};
    std::mutex _workerMutex;
    std::condition_variable _workerCv;
    std::atomic<size_t> _pendingCount{0};
    std::atomic<size_t> _totalEnqueued{0};
    std::string _modelDir;
    std::string _runtimeConfigPath;
    std::unique_ptr<OCR::OCREngine> _pOcrEngine;
};

#endif // HAVE_NCNN
