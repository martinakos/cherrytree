/*
 * tests_types.cpp
 *
 * Copyright 2009-2023
 * Giuseppe Penone <giuspen@gmail.com>
 * Evgenii Gurianov <https://github.com/txe>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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

#include "ct_types.h"
#include "ct_filesystem.h"
#include "tests_common.h"
#include <thread>

TEST(TestTypesGroup, ctMaxSizedList)
{
    CtMaxSizedList<int> maxSizedList{3};
    maxSizedList.push_back(1);
    maxSizedList.push_back(2);
    maxSizedList.push_back(3);
    ASSERT_EQ(3, maxSizedList.size());
    ASSERT_EQ(1, maxSizedList.front());
    ASSERT_EQ(3, maxSizedList.back());
    maxSizedList.move_or_push_back(1);
    ASSERT_EQ(3, maxSizedList.size());
    ASSERT_EQ(2, maxSizedList.front());
    ASSERT_EQ(1, maxSizedList.back());
    maxSizedList.move_or_push_front(3);
    ASSERT_EQ(3, maxSizedList.size());
    ASSERT_EQ(3, maxSizedList.front());
    ASSERT_EQ(1, maxSizedList.back());
    maxSizedList.move_or_push_front(4);
    ASSERT_EQ(3, maxSizedList.size());
    ASSERT_EQ(4, maxSizedList.front());
    ASSERT_EQ(2, maxSizedList.back());
}

TEST(TestTypesGroup, scope_guard)
{
    int var = -1;
    {
        auto on_scope_exit = scope_guard([&](void*) {
            var = 0;
        });
        ASSERT_EQ(-1, var);
    }
    ASSERT_EQ(0, var);
}

TEST(TestTypesGroup, ThreadSafeDEQueue_TwoThreadsHandshake)
{
    ThreadSafeDEQueue<int,500> threadSafeDEQueue;
    auto f_first = [&threadSafeDEQueue](){
        ASSERT_EQ(1, threadSafeDEQueue.pop_front());
        threadSafeDEQueue.push_back(2);
        while (true) {
            g_usleep(1);
            std::optional<int> peeked = threadSafeDEQueue.peek();
            if (not peeked.has_value() or 3 == peeked.value()) {
                break;
            }
        }
        ASSERT_EQ(3, threadSafeDEQueue.pop_front());
    };
    auto f_second = [&threadSafeDEQueue](){
        ASSERT_TRUE(threadSafeDEQueue.empty());
        threadSafeDEQueue.push_back(1);
        while (true) {
            g_usleep(1);
            std::optional<int> peeked = threadSafeDEQueue.peek();
            if (not peeked.has_value() or 2 == peeked.value()) {
                break;
            }
        }
        ASSERT_EQ(2, threadSafeDEQueue.pop_front());
        threadSafeDEQueue.push_back(3);
    };
    std::thread first(f_first);
    std::thread second(f_second);
    first.join();
    second.join();
}

TEST(TestTypesGroup, ThreadSafeDEQueue_TwoThreadsPushOneThreadPop)
{
    ThreadSafeDEQueue<int,500> threadSafeDEQueue;
    auto f_push = [&threadSafeDEQueue](){
        for (int i = 0; i < 100; ++i) {
            g_usleep(1);
            threadSafeDEQueue.push_back(i);
        }
    };
    auto f_pop = [&threadSafeDEQueue](){
        for (int i = 0; i < 200; ++i) {
            threadSafeDEQueue.pop_front();
        }
    };
    std::thread first(f_push);
    std::thread second(f_pop);
    std::thread third(f_push);
    first.join();
    second.join();
    third.join();
    ASSERT_TRUE(threadSafeDEQueue.empty());
}

TEST(TestTypesGroup, ThreadSafeDEQueue_MaxElements)
{
    ThreadSafeDEQueue<int,3> threadSafeDEQueue;
    for (int i = 0; i < 5; ++i) {
        threadSafeDEQueue.push_back(i);
    }
    ASSERT_EQ(3, threadSafeDEQueue.size());
}

TEST(TestTypesGroup, ctScalableTag)
{
    {
        char serialised[]{"1.728000;;;0;0;0"};
        char fallback[]{"1728;;;0;0;0"};
        CtScalableTag scalableTag{serialised, fallback};
        ASSERT_DOUBLE_EQ(1.728, scalableTag.scale);
        ASSERT_TRUE(scalableTag.foreground.empty());
        ASSERT_TRUE(scalableTag.background.empty());
        ASSERT_FALSE(scalableTag.bold);
        ASSERT_FALSE(scalableTag.italic);
        ASSERT_FALSE(scalableTag.underline);
        ASSERT_STREQ(fallback, scalableTag.serialise().c_str());
    }
    {
        char serialised[]{"1,000000;;;0;0;0"};
        char fallback[]{"1728;;;0;0;0"};
        CtScalableTag scalableTag{serialised, fallback};
        ASSERT_DOUBLE_EQ(1.728, scalableTag.scale);
        ASSERT_STREQ(fallback, scalableTag.serialise().c_str());
    }
    {
        char serialised[]{"678;#000000;#ffffff;1;1;1"};
        CtScalableTag scalableTag{serialised};
        ASSERT_DOUBLE_EQ(0.678, scalableTag.scale);
        ASSERT_STREQ("#000000", scalableTag.foreground.c_str());
        ASSERT_STREQ("#ffffff", scalableTag.background.c_str());
        ASSERT_TRUE(scalableTag.bold);
        ASSERT_TRUE(scalableTag.italic);
        ASSERT_TRUE(scalableTag.underline);
        ASSERT_STREQ(serialised, scalableTag.serialise().c_str());

        scalableTag.deserialise("1728;;;0;0;0");
        ASSERT_DOUBLE_EQ(1.728, scalableTag.scale);
        ASSERT_TRUE(scalableTag.foreground.empty());
        ASSERT_TRUE(scalableTag.background.empty());
        ASSERT_FALSE(scalableTag.bold);
        ASSERT_FALSE(scalableTag.italic);
        ASSERT_FALSE(scalableTag.underline);
    }
}

TEST(TestTypesGroup, CtStockIcon)
{
    // non existing ID does not crash but returns 'no icon' icon
    ASSERT_STREQ(CtStockIcon::at(0u),     "ct_node_no_icon");
    ASSERT_STREQ(CtStockIcon::at(10000u), "ct_node_no_icon");
    ASSERT_STREQ(CtStockIcon::at(10001u), "ct_node_no_icon");

    // valid ID
    ASSERT_STREQ(CtStockIcon::at(14u), "ct_home");

    ASSERT_EQ(CtStockIcon::size(), CtConst::_NODE_CUSTOM_ICONS.size());
}

// Navigation history helpers that mirror the production logic
namespace {

struct NavHistory {
    std::deque<gint64> visited;
    size_t idx{0};
    bool navigating{false};

    void visit(gint64 nodeId) {
        if (navigating) return;
        if (!visited.empty() && idx < visited.size()) {
            size_t last_index = visited.size() - 1;
            if (idx != last_index) {
                visited.erase(visited.begin() + idx + 1, visited.end());
            }
        }
        for (auto it = visited.begin(); it != visited.end(); ) {
            if (*it == nodeId) it = visited.erase(it);
            else ++it;
        }
        visited.push_back(nodeId);
        idx = visited.size() - 1;
    }

    bool go_back() {
        if (visited.empty() || idx == 0) return false;
        idx--;
        navigating = true;
        visit(visited[idx]); // simulates the cursor-change event
        navigating = false;
        return true;
    }

    bool go_forward() {
        if (visited.empty() || idx >= visited.size() - 1) return false;
        idx++;
        navigating = true;
        visit(visited[idx]);
        navigating = false;
        return true;
    }

    gint64 current() const { return visited[idx]; }
};

} // anonymous namespace

TEST(NavigationHistoryGroup, BackAndForward)
{
    NavHistory h;
    h.visit(1);
    h.visit(2);
    h.visit(3);
    ASSERT_EQ(3, h.current());

    ASSERT_TRUE(h.go_back());
    ASSERT_EQ(2, h.current());

    ASSERT_TRUE(h.go_forward());
    ASSERT_EQ(3, h.current());
}

TEST(NavigationHistoryGroup, ForwardAfterMultipleBack)
{
    NavHistory h;
    h.visit(1);
    h.visit(2);
    h.visit(3);
    h.visit(4);

    ASSERT_TRUE(h.go_back());
    ASSERT_TRUE(h.go_back());
    ASSERT_EQ(2, h.current());

    ASSERT_TRUE(h.go_forward());
    ASSERT_EQ(3, h.current());
    ASSERT_TRUE(h.go_forward());
    ASSERT_EQ(4, h.current());

    ASSERT_FALSE(h.go_forward());
}

TEST(NavigationHistoryGroup, BackAtStartReturnsFalse)
{
    NavHistory h;
    h.visit(1);
    ASSERT_FALSE(h.go_back());
    ASSERT_EQ(1, h.current());
}

TEST(NavigationHistoryGroup, NewVisitTruncatesForwardHistory)
{
    NavHistory h;
    h.visit(1);
    h.visit(2);
    h.visit(3);

    h.go_back();
    ASSERT_EQ(2, h.current());

    h.visit(5);
    ASSERT_EQ(5, h.current());
    ASSERT_FALSE(h.go_forward());
}

TEST(NavigationHistoryGroup, BackForwardBackForward)
{
    NavHistory h;
    h.visit(1);
    h.visit(2);

    ASSERT_TRUE(h.go_back());
    ASSERT_EQ(1, h.current());
    ASSERT_TRUE(h.go_forward());
    ASSERT_EQ(2, h.current());
    ASSERT_TRUE(h.go_back());
    ASSERT_EQ(1, h.current());
    ASSERT_TRUE(h.go_forward());
    ASSERT_EQ(2, h.current());
}

TEST(NavigationHistoryGroup, EmptyHistoryNoOps)
{
    NavHistory h;
    ASSERT_FALSE(h.go_back());
    ASSERT_FALSE(h.go_forward());
}