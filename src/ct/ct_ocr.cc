/*
 * ct_ocr.cc
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

#include "ct_ocr.h"

#ifdef HAVE_NCNN

#include "ct_logging.h"
#include "ct_treestore.h"
#include "ct_image.h"
#include "ct_filesystem.h"
#include "config.h"

#include "ocr_engine.h"

#include <opencv2/core/mat.hpp>
#include <opencv2/imgcodecs.hpp>

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <thread>
#include <chrono>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <fcntl.h>

CtOcrManager::CtOcrManager(const std::string& modelDir)
 : _modelDir{modelDir}
{
    spdlog::info("CtOcrManager: loading models from {}", _modelDir);
    _workerThread = std::thread(&CtOcrManager::_worker_func, this);
}

CtOcrManager::~CtOcrManager()
{
    _shutDown = true;
    _workQueue.push_back(nullptr);
    if (_workerThread.joinable()) {
        _workerThread.join();
    }
}

bool CtOcrManager::enqueue(const std::string& pngBlob, gint64 nodeId, CtImagePng* pImagePng, bool priority)
{
    auto* pItem = new CtOcrWorkItem{pngBlob, nodeId, pImagePng, priority};
    const bool pushed = priority ? _workQueue.push_front(pItem) : _workQueue.push_back(pItem);
    if (pushed) {
        _pendingCount++;
        _totalEnqueued++;
        return true;
    }
    delete pItem;
    return false;
}

void CtOcrManager::enqueue_all_missing(CtTreeStore& treeStore)
{
    size_t count = 0;
    treeStore.get_store()->foreach_iter([&](const Gtk::TreeModel::iterator& iter) -> bool {
        CtTreeIter ctIter = treeStore.to_ct_tree_iter(iter);
        std::list<CtAnchoredWidget*> widgets = ctIter.get_anchored_widgets_fast();
        for (auto* pWidget : widgets) {
            if (pWidget->get_type() != CtAnchWidgType::ImagePng) continue;
            auto* pImagePng = dynamic_cast<CtImagePng*>(pWidget);
            if (not pImagePng or not pImagePng->get_ocr_boxes().empty()) continue;
            enqueue(pImagePng->get_raw_blob(), ctIter.get_node_id(), pImagePng);
            ++count;
        }
        return false; // continue iteration
    });
    spdlog::info("CtOcrManager: enqueued {} images for OCR", count);
}

void CtOcrManager::enqueue_all(CtTreeStore& treeStore)
{
    size_t count = 0;
    treeStore.get_store()->foreach_iter([&](const Gtk::TreeModel::iterator& iter) -> bool {
        CtTreeIter ctIter = treeStore.to_ct_tree_iter(iter);
        std::list<CtAnchoredWidget*> widgets = ctIter.get_anchored_widgets_fast();
        for (auto* pWidget : widgets) {
            if (pWidget->get_type() != CtAnchWidgType::ImagePng) continue;
            auto* pImagePng = dynamic_cast<CtImagePng*>(pWidget);
            if (not pImagePng) continue;
            enqueue(pImagePng->get_raw_blob(), ctIter.get_node_id(), pImagePng);
            ++count;
        }
        return false;
    });
    spdlog::info("CtOcrManager: re-enqueued {} images for OCR", count);
}

/*static*/bool CtOcrManager::is_available()
{
    const std::string modelDir = fs::get_cherrytree_datadir().string() + "/data/ocr_models";
    namespace stdfs = std::filesystem;
    bool isDir = stdfs::is_directory(modelDir);
    bool hasDet = stdfs::exists(modelDir + "/PP_OCRv6_tiny_det.param")
              and stdfs::exists(modelDir + "/PP_OCRv6_tiny_det.bin");
    bool hasRec = stdfs::exists(modelDir + "/PP_OCRv6_tiny_rec.param")
              and stdfs::exists(modelDir + "/PP_OCRv6_tiny_rec.bin");
    bool hasKeys = stdfs::exists(modelDir + "/ppocr_keys_v6_tiny.txt");
    bool result = isDir and hasDet and hasRec and hasKeys;
    return result;
}

size_t CtOcrManager::pending_count() const
{
    return _pendingCount;
}

size_t CtOcrManager::total_enqueued() const
{
    return _totalEnqueued;
}

void CtOcrManager::_worker_func()
{
    // Pin this thread to the last CPU core to avoid contention with the UI thread
    unsigned numCpus = std::thread::hardware_concurrency();
    if (numCpus > 1) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(numCpus - 1, &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    }

    const std::string configPath = _modelDir + "/ct_ocr_config.json";
    if (not std::filesystem::exists(configPath)) {
        std::ofstream f(configPath);
        f << "{\n"
          << R"(  "save": false,)" << "\n"
          << R"(  "det": {)" << "\n"
          << R"(    "infer_threads": 1,)" << "\n"
          << R"(    "model_path": ")" << _modelDir << R"(/PP_OCRv6_tiny_det",)" << "\n"
          << R"(    "padding": 0,)" << "\n"
          << R"(    "max_side_len": 768,)" << "\n"
          << R"(    "box_thres": 0.45,)" << "\n"
          << R"(    "bitmap_thres": 0.2,)" << "\n"
          << R"(    "unclip_ratio": 1.4,)" << "\n"
          << R"(    "fp16": false)" << "\n"
          << "  },\n"
          << R"(  "cls": {)" << "\n"
          << R"(    "infer_threads": 1,)" << "\n"
          << R"(    "reco_threads": 1,)" << "\n"
          << R"(    "model_path": ")" << _modelDir << R"(/PP_LCNet_x0_25_textline_ori",)" << "\n"
          << R"(    "enable": true,)" << "\n"
          << R"(    "most_angle": true,)" << "\n"
          << R"(    "fp16": false)" << "\n"
          << "  },\n"
          << R"(  "rec": {)" << "\n"
          << R"(    "infer_threads": 1,)" << "\n"
          << R"(    "reco_threads": 1,)" << "\n"
          << R"(    "model_path": ")" << _modelDir << R"(/PP_OCRv6_tiny_rec",)" << "\n"
          << R"(    "keys_path": ")" << _modelDir << R"(/ppocr_keys_v6_tiny.txt",)" << "\n"
          << R"(    "fp16": false)" << "\n"
          << "  }\n"
          << "}\n";
    }

    _pOcrEngine = std::make_unique<OCR::OCREngine>();
    {
        int savedStderr = dup(STDERR_FILENO);
        int devNull = open("/dev/null", O_WRONLY);
        if (devNull >= 0) { dup2(devNull, STDERR_FILENO); close(devNull); }
        bool ok = _pOcrEngine->Initialize(configPath);
        if (savedStderr >= 0) { dup2(savedStderr, STDERR_FILENO); close(savedStderr); }
        if (not ok) {
            spdlog::error("CtOcrManager: failed to initialize OCR engine from {}", _modelDir);
            _pOcrEngine.reset();
        } else {
            spdlog::info("CtOcrManager: OCR engine initialized");
        }
    }

    while (not _shutDown) {
        CtOcrWorkItem* pItem = _workQueue.pop_front();
        if (not pItem) {
            break; // nullptr sentinel → shut down
        }
        const bool isPriority = pItem->priority;
        OcrData ocrData = _run_ocr(pItem->pngBlob);
        CtOcrResult result;
        result.nodeId = pItem->nodeId;
        result.pImagePng = pItem->pImagePng;
        result.ocrText = ocrData.text;
        result.ocrBoxes = ocrData.boxes;
        resultQueue.push_back(result);
        _pendingCount--;
        delete pItem;
        dispatcherOcrResult.emit();
        if (not isPriority and not _shutDown) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
    spdlog::info("CtOcrManager: worker thread stopped");
}

CtOcrManager::OcrData CtOcrManager::_run_ocr(const std::string& pngBlob)
{
    if (not _pOcrEngine or pngBlob.empty()) {
        return {};
    }

    std::vector<uchar> buf(pngBlob.begin(), pngBlob.end());
    cv::Mat image = cv::imdecode(buf, cv::IMREAD_COLOR);
    if (image.empty()) {
        spdlog::warn("CtOcrManager: failed to decode image blob ({} bytes)", pngBlob.size());
        return {};
    }

    int savedStderr = dup(STDERR_FILENO);
    int devNull = open("/dev/null", O_WRONLY);
    if (devNull >= 0) { dup2(devNull, STDERR_FILENO); close(devNull); }
    std::vector<OCR::OCRResult> results = _pOcrEngine->Run(image);
    if (savedStderr >= 0) { dup2(savedStderr, STDERR_FILENO); close(savedStderr); }

    OcrData data;
    std::string boxes = std::to_string(image.cols) + "," + std::to_string(image.rows);
    int pos = 0;
    for (const auto& res : results) {
        if (not data.text.empty()) {
            data.text += " ";
            pos++;
        }
        int startPos = pos;
        data.text += res.line.text;
        int endPos = pos + static_cast<int>(Glib::ustring{res.line.text}.size());
        pos = endPos;
        if (res.box.points.size() == 4) {
            boxes += ";";
            boxes += std::to_string(startPos) + "," + std::to_string(endPos);
            for (const auto& pt : res.box.points) {
                boxes += "," + std::to_string(pt.x) + "," + std::to_string(pt.y);
            }
        }
    }
    data.boxes = boxes;
    return data;
}

#endif // HAVE_NCNN
