// http_server.h
// ---------------------------------------------------------------------------
// A very small HTTP server so the C++ program can talk to the browser.
// It only understands what we need: one request at a time, GET/POST with a
// query string (no request body), and it always closes the connection after
// replying. It listens on 127.0.0.1 only, so nobody else on the network can reach it.
//
// Works on Linux/macOS (POSIX sockets) and Windows (Winsock).
// On Windows with MinGW compile with  -lws2_32.
// ---------------------------------------------------------------------------
#pragma once

#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <string>

#ifdef _WIN32
  #ifndef NOMINMAX
    #define NOMINMAX            // stop windows.h from defining min/max macros that clash with std::min/std::max
  #endif
  #define WIN32_LEAN_AND_MEAN
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef SOCKET socket_t;
  #define CLOSE_SOCKET closesocket
  #define INVALID_SOCK INVALID_SOCKET
#else
  #include <arpa/inet.h>
  #include <csignal>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <unistd.h>
  typedef int socket_t;
  #define CLOSE_SOCKET close
  #define INVALID_SOCK (-1)
#endif

#ifndef MSG_NOSIGNAL
  #define MSG_NOSIGNAL 0
#endif

struct Request {
    std::string                        method;   // "GET" or "POST"
    std::string                        path;     // e.g. "/api/analyze"
    std::map<std::string, std::string> query;    // e.g. {"from": "6", "algo": "astar"}

    std::string get(const std::string& key, const std::string& fallback = "") const {
        auto it = query.find(key);
        return it == query.end() ? fallback : it->second;
    }
};

struct Response {
    int         status = 200;
    std::string contentType = "text/plain; charset=utf-8";
    std::string body;
    std::string extraHeaders;                    // e.g. "Content-Disposition: attachment\r\n"
};

class HttpServer {
public:
    typedef std::function<Response(const Request&)> Handler;

    // Starts listening and handles requests forever. Returns false if it cannot start.
    bool run(int port, Handler handler) {
#ifdef _WIN32
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#else
        std::signal(SIGPIPE, SIG_IGN);           // do not crash if the browser hangs up early
#endif
        socket_t server = socket(AF_INET, SOCK_STREAM, 0);
        if (server == INVALID_SOCK) return false;

        int yes = 1;
        setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // 127.0.0.1 only
        addr.sin_port = htons((unsigned short)port);
        if (bind(server, (sockaddr*)&addr, sizeof(addr)) < 0) return false;
        if (listen(server, 16) < 0) return false;

        while (true) {
            socket_t client = accept(server, nullptr, nullptr);
            if (client == INVALID_SOCK) continue;
            setReceiveTimeout(client, 500);              // ignore idle connections quickly

            std::string raw = readHeaders(client);
            Request req;
            Response res;
            if (parseRequest(raw, req)) {
                res = handler(req);
            } else {
                res.status = 400;
                res.body = "Bad request";
            }
            sendResponse(client, res);
            CLOSE_SOCKET(client);
        }
        return true;
    }

private:
    static void setReceiveTimeout(socket_t s, int ms) {
#ifdef _WIN32
        DWORD t = ms;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&t, sizeof(t));
#else
        timeval tv;
        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#endif
    }

    // Read until the blank line that ends the HTTP headers.
    static std::string readHeaders(socket_t client) {
        std::string data;
        char buf[2048];
        while (data.find("\r\n\r\n") == std::string::npos && data.size() < 16384) {
            int got = (int)recv(client, buf, sizeof(buf), 0);
            if (got <= 0) break;
            data.append(buf, got);
        }
        return data;
    }

    static std::string urlDecode(const std::string& s) {
        std::string out;
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '%' && i + 2 < s.size()) {
                out += (char)std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
                i += 2;
            } else if (s[i] == '+') {
                out += ' ';
            } else {
                out += s[i];
            }
        }
        return out;
    }

    // First line looks like:  GET /api/analyze?from=6&algo=astar HTTP/1.1
    static bool parseRequest(const std::string& raw, Request& req) {
        size_t lineEnd = raw.find("\r\n");
        if (lineEnd == std::string::npos) return false;
        std::string line = raw.substr(0, lineEnd);

        size_t sp1 = line.find(' ');
        size_t sp2 = line.find(' ', sp1 + 1);
        if (sp1 == std::string::npos || sp2 == std::string::npos) return false;

        req.method = line.substr(0, sp1);
        std::string target = line.substr(sp1 + 1, sp2 - sp1 - 1);

        size_t q = target.find('?');
        req.path = urlDecode(target.substr(0, q));
        if (q != std::string::npos) {
            std::string qs = target.substr(q + 1);
            size_t pos = 0;
            while (pos <= qs.size()) {
                size_t amp = qs.find('&', pos);
                if (amp == std::string::npos) amp = qs.size();
                std::string pair = qs.substr(pos, amp - pos);
                size_t eq = pair.find('=');
                if (eq != std::string::npos)
                    req.query[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
                pos = amp + 1;
            }
        }
        return true;
    }

    static void sendResponse(socket_t client, const Response& res) {
        const char* reason = res.status == 200 ? "OK" : res.status == 404 ? "Not Found" : "Error";
        std::string head = "HTTP/1.1 " + std::to_string(res.status) + " " + reason + "\r\n"
                         + "Content-Type: " + res.contentType + "\r\n"
                         + "Content-Length: " + std::to_string(res.body.size()) + "\r\n"
                         + "Cache-Control: no-store\r\n"
                         + res.extraHeaders
                         + "Connection: close\r\n\r\n";
        std::string all = head + res.body;
        size_t sent = 0;
        while (sent < all.size()) {
            int n = (int)send(client, all.data() + sent, (int)(all.size() - sent), MSG_NOSIGNAL);
            if (n <= 0) break;
            sent += n;
        }
    }
};
