/*
 * ct_agent_server.h
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

#include <glib.h>
#include <map>
#include <string>

class CtMainWin;

// Lets an AI agent (Claude Code or any other MCP client) work on the open document.
//
// While access is allowed, the app listens on a Unix socket that only the current
// user can open. The agent harness starts "cherrytree --mcp-stdio", which relays
// MCP JSON-RPC messages (one per line) between its stdin/stdout and that socket.
// Requests are served on the GTK main loop. Every change goes through the command
// system, so it can be undone and shows in the timeline like an edit by the user.
class CtAgentServer
{
public:
    static CtAgentServer& instance();

    // Starts listening; returns false and sets rError when the socket cannot be opened
    bool start(std::string& rError);
    void stop();
    bool is_running() const { return _listenFd >= 0; }

    // Socket path for the application id postfix ("" for the main session, "_2" for -S)
    static std::string socket_path(const std::string& appIdPostfix);
    // The command an MCP client runs to reach the main session
    static std::string relay_command();

    // Handles one JSON-RPC message, returns the reply (empty for a notification)
    std::string handle_message(const std::string& message);

    // Runs one tool on a window. argsJson is a JSON object, the result a JSON document;
    // rIsError is set when the tool refused, the result is then {"error": "..."}
    static std::string call_tool(CtMainWin* pCtMainWin,
                                 const std::string& toolName,
                                 const std::string& argsJson,
                                 bool& rIsError);

    // Body of "cherrytree --mcp-stdio [-S]": pipes stdin/stdout to the socket, returns the exit code
    static int run_stdio_relay(int argc, char* argv[]);

private:
    CtAgentServer() = default;
    ~CtAgentServer();

    struct Client {
        int         fd{-1};
        guint       sourceId{0};
        std::string inBuf;
    };

    static gboolean _on_listen_ready(gint fd, GIOCondition condition, gpointer user_data);
    static gboolean _on_client_ready(gint fd, GIOCondition condition, gpointer user_data);
    // false when the client went away and must be dropped
    bool _read_client(Client& client);

    int                   _listenFd{-1};
    guint                 _listenSourceId{0};
    std::string           _socketPath;
    std::map<int, Client> _clients;
};
