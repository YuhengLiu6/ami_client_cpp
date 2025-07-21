// RawAmiClient.cpp
#include "RawAmiClient.hpp"
#include "RawAmiClientListener.hpp"
#include <boost/asio.hpp>
#include <iostream>
#include <chrono>
#include <algorithm>
#include <sstream>
#include <cctype>
#include <map>
#include <variant>
#include <string>
#include <optional>
#include <vector>
#include <stdexcept>
#include <memory>

#include <nlohmann/json.hpp> 
#include <boost/beast/core/detail/base64.hpp>
#include <cppcodec/base64_rfc4648.hpp>
#include "AmiTypes.hpp"
const std::string RawAmiClient::DEFAULT_HOST = "localhost";
const int RawAmiClient::DEFAULT_PORT = 3289;
using base64 = cppcodec::base64_rfc4648;
using json = nlohmann::json;
RawAmiClient::RawAmiClient()
    : socket_(nullptr),
    connected_(false),
    receiving_(false),
    sending_(false),
    loggedIn_(false),
    seqnum_(0),
    autoFlush_(false) {
}

RawAmiClient::~RawAmiClient() {
    disconnect();
}

bool RawAmiClient::connect(const std::string& host,
    int port,
    bool /*logErrorOnRetries*/,
    bool autoFlush) {
    if (connected_) throw std::runtime_error("Already connected");
    try {
        boost::asio::ip::tcp::resolver resolver(ioCtx_);
        auto endpoints = resolver.resolve(host, std::to_string(port));
        socket_ = std::make_unique<boost::asio::ip::tcp::socket>(ioCtx_);
        boost::asio::connect(*socket_, endpoints);
        connected_ = true;
        autoFlush_ = autoFlush;
        startReader();
        fireConnect();
        
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Connect error: " << e.what() << std::endl;
        return false;
    }
}

void RawAmiClient::disconnect() {
    if (!connected_) return;
    connected_ = false;
    loggedIn_ = false;
    if (socket_) {
        boost::system::error_code ec;
        socket_->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        socket_->close(ec);
    }
    fireDisconnect();
}

bool RawAmiClient::isConnected() const {
    return connected_;
}

long RawAmiClient::getNow() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch())
        .count();
}


void RawAmiClient::startReader() {
    readerThread_ = std::thread([this]() {
        boost::asio::streambuf buf;
        while (connected_) {
            try {
                // 1) 读取到 '\n'（或已有 '\n'）并返回这次操作读入 buffer 的字节数
                std::size_t bytes = boost::asio::read_until(*socket_, buf, '\n');

                // 2) 用一个新的 std::istream （或在这里每次重建）去提取一行
                std::istream is(&buf);
                std::string line;
                std::getline(is, line);

                // 3) 把这次已经处理过的 bytes 从 buf 里丢掉
                buf.consume(bytes);

                // ---- 可选：去掉行首行尾所有空白 ----
                while (!line.empty() && std::isspace((unsigned char)line.back()))
                    line.pop_back();
                size_t start = 0;
                while (start < line.size() && std::isspace((unsigned char)line[start]))
                    ++start;
                if (start) line.erase(0, start);

                // 4) 再交给 processIncoming
                auto err = processIncoming(line);
                if (!err.empty()) {
                    std::cerr << "[startReader] Fatal error: " << err
                        << "  原始行: '" << line << "'" << std::endl;
                    break;
                }
                else {
                    std::cout << "[startReader] Processed incoming line ok" << std::endl;
                }
            }
            catch (...) {
                break;
            }
        }
        disconnect();
        });
    readerThread_.detach();
}


void RawAmiClient::parseIncomingParams(
    const std::string& str, size_t pos,
    std::map<std::string, AmiValue>& out) {

    while (pos < str.size()) {
        if (str[pos] == '|') ++pos;

        size_t eq = str.find('=', pos);
        if (eq == std::string::npos) break;

        std::string key = str.substr(pos, eq - pos);
        pos = eq + 1;

        AmiValue val;

        // ---------- 引号字符串 ----------
        if (str[pos] == '"') {
            ++pos;
            std::string quoted;
            if (!readUntilSkipEscaped(str, pos, '"', quoted)) break;

            if (pos < str.size()) {
                char suffix = str[pos];
                if (suffix == 'J') {
                    ++pos;
                    val = json::parse(quoted);
                }
                else if (suffix == 'U') {
                    ++pos;
                    std::string padded = quoted + std::string((4 - quoted.size() % 4) % 4, '=');
                    std::vector<uint8_t> decoded = base64::decode(padded);
                    val = decoded;
                }
                else {
                    val = quoted;
                }
            }
            else {
                val = quoted;
            }

            // ---------- 单引号字符串 ----------
        }
        else if (str[pos] == '\'') {
            ++pos;
            std::string quoted;
            if (!readUntilSkipEscaped(str, pos, '\'', quoted)) break;
            val = quoted;

            // ---------- true / false ----------
        }
        else if (str.compare(pos, 4, "true") == 0) {
            val = true;
            pos += 4;
        }
        else if (str.compare(pos, 5, "false") == 0) {
            val = false;
            pos += 5;

            // ---------- null ----------
        }
        else if (str.compare(pos, 4, "null") == 0) {
            val = nullptr;
            pos += 4;

            // ---------- 数字 ----------
        }
        else {
            size_t end = str.find('|', pos);
            if (end == std::string::npos) end = str.size();
            std::string raw = str.substr(pos, end - pos);
            pos = end;

            try {
                if (raw.find('.') != std::string::npos) {
                    if (!raw.empty() && raw.back() == 'D') raw.pop_back();
                    val = std::stod(raw);
                }
                else if (!raw.empty() && raw.back() == 'L') {
                    raw.pop_back();
                    val = std::stoll(raw);
                }
                else {
                    val = std::stoi(raw);
                }
            }
            catch (...) {
                val = raw;
            }
        }

        out[key] = val;
    }
}




std::string RawAmiClient::processIncoming(const std::string& line) {
    if (line.find("Welcome to 3forge AMI") != std::string::npos ||
                line.find("logged in") != std::string::npos ||
                line.find("|Q=0|S=0|") != std::string::npos) {
                fireOnLogin();  // ✅ 合适触发点
            }

    std::string s = line;
    //if (!s.empty() && s.back() == '\r') s.pop_back();
     while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();

    std::cout << "[DEBUG] Incoming type: '" << s[0] << "' | Raw: " << s << std::endl;

    if (s.size() < 3 || s[1] != '@') return "Invalid header";

    size_t pipe1 = s.find('|', 2);
    if (pipe1 == std::string::npos) return "Missing | after timestamp";

    long long ts = 0;
    try {
        ts = std::stoll(s.substr(2, pipe1 - 2));
    }
    catch (...) {
        return "Invalid timestamp";
    }

    size_t pos = pipe1 + 1;

    

    try {
        switch (s[0]) {
        case 'M': {
           
            // 解析 MQ=..., S=..., M="..."
            auto require = [&](char expected, const char* msg) {
                if (pos >= s.size() || s[pos] != expected)
                    throw std::runtime_error(msg);
                ++pos;
                };

            require('Q', "Expecting Q");
            require('=', "Expecting =");

            size_t pipe2 = s.find('|', pos);
            if (pipe2 == std::string::npos) return "Missing | after Q";
            long seqNum = std::stol(s.substr(pos, pipe2 - pos));
            pos = pipe2 + 1;

            require('S', "Expecting S");
            require('=', "Expecting =");
            size_t pipe3 = s.find('|', pos);
            if (pipe3 == std::string::npos) return "Missing | after S";
            int status = std::stoi(s.substr(pos, pipe3 - pos));
            pos = pipe3 + 1;

            require('M', "Expecting M");
            require('=', "Expecting =");
            require('"', "Expecting opening quote");

            std::string msg;
            if (!readUntilSkipEscaped(s, pos, '"', msg)) {
                std::cout << "[DEBUG] readUntilSkipEscaped Failed! " << msg << std::endl;
                return "Malformed message string";
            }
            /*while (pos < s.size() && (s[pos] == '\r' || s[pos] == '\n' || s[pos] == ' '))
                ++pos;*/

            while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos])))
                ++pos;

            std::cout << "[DEBUG] Will call fireMessageReceived(...) with msg before return: " << msg << std::endl;
            /*if (pos != s.size()) return "Trailing garbage after message";*/
            std::cout << "[DEBUG] Will call fireMessageReceived(...) with msg after return: " << msg << std::endl;
            fireMessageReceived(ts, seqNum, status, msg);

          

            break;
        }

        case 'E': {
            std::map<std::string, AmiValue> params;
            parseIncomingParams(s, pos, params);

            auto getStr = [&](const std::string& key) -> std::string {
                auto it = params.find(key);
                if (it != params.end() && std::holds_alternative<std::string>(it->second))
                    return std::get<std::string>(it->second);
                return {};
                };

            std::string requestId = getStr("I"); params.erase("I");
            std::string userName = getStr("U"); params.erase("U");
            std::string cmd = getStr("C"); params.erase("C");
            std::string type = getStr("T"); params.erase("T");
            std::string objectId = getStr("O"); params.erase("O");

            fireCommand(requestId, cmd, userName, type, objectId, params);
            break;
        }

                // 如果是 ack 或 ping 也可以处理（根据 AMI 文档）
        //case 'X':
        //case 'P': {
        //    // 可以处理 heartbeat/ping/ack（可选）
        //    std::cout << "[DEBUG] Heartbeat or ack received." << std::endl;
        //    break;
        //}

        default:
            std::cout << "[DEBUG] Unknown message type '" << s[0] << "', forwarding raw message.\n";
            fireMessageReceived(ts, 0, 0, s);
            break;
        }
    }
    catch (const std::exception& ex) {
        return std::string("Exception during parse: ") + ex.what();
    }

    return {}; // success
}









bool RawAmiClient::readUntilSkipEscaped(const std::string& input, size_t& pos, char endChar, std::string& out) {
    while (pos < input.size()) {
        char c = input[pos++];
        if (c == endChar) return true;
        if (c == '\\') {
            if (pos >= input.size()) return false;
            char next = input[pos++];
            switch (next) {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case '\\': out += '\\'; break;
            case '\'': out += '\''; break;
            case '\"': out += '"'; break;
            case 'u': {
                if (pos + 4 > input.size()) return false;
                int code = std::stoi(input.substr(pos, 4), nullptr, 16);
                out += static_cast<char>(code);
                pos += 4;
                break;
            }
            default:
                out += next;
            }
        }
        else {
            out += c;
        }
    }
    return false;
}




bool RawAmiClient::pumpIncomingEvent() {
    // 留空或返回 connected_
    return connected_;
}





bool RawAmiClient::sendMessage(const std::string& msg, bool /*flush*/) {
    if (!connected_) throw std::runtime_error("Not connected");
    boost::asio::write(*socket_, boost::asio::buffer(msg + "\n"));
    fireMessageSent(msg);
    return true;
}

void RawAmiClient::addListener(std::shared_ptr<RawAmiClientListener> listener) {
    std::lock_guard<std::mutex> lock(listenersMutex_);
    listeners_.push_back(listener);
}

bool RawAmiClient::removeListener(std::shared_ptr<RawAmiClientListener> listener) {
    std::lock_guard<std::mutex> lock(listenersMutex_);
    auto it = std::find(listeners_.begin(), listeners_.end(), listener);
    if (it != listeners_.end()) {
        listeners_.erase(it);
        return true;
    }
    return false;
}

void RawAmiClient::fireConnect() {
    std::lock_guard<std::mutex> lock(listenersMutex_);
    for (auto& l : listeners_) l->onConnect(this);
}

void RawAmiClient::fireDisconnect() {
    std::lock_guard<std::mutex> lock(listenersMutex_);
    for (auto& l : listeners_) l->onDisconnect(this);
}

void RawAmiClient::fireMessageReceived(long ts, long seq, int status, const std::string& msg) {
    std::lock_guard<std::mutex> lock(listenersMutex_);
    for (auto& l : listeners_) l->onMessageReceived(this, ts, seq, status, msg);
}

void RawAmiClient::fireMessageSent(const std::string& msg) {
    std::lock_guard<std::mutex> lock(listenersMutex_);
    for (auto& l : listeners_) l->onMessageSent(this, msg);
}

void RawAmiClient::fireOnLogin() {
    if (loggedIn_.exchange(true)) return;  // 已经登录则直接返回

    std::lock_guard<std::mutex> lock(listenersMutex_);
    for (auto& listener : listeners_) {
        if (listener) listener->onLoggedIn(this);
    }
}

void RawAmiClient::fireCommand(const std::string& requestId,
    const std::string& cmd,
    const std::string& userName,
    const std::string& objectType,
    const std::string& objectId,
    const std::map<std::string, AmiValue>& params) {
    std::lock_guard<std::mutex> lock(listenersMutex_);
    for (auto& l : listeners_)
        l->onCommand(this, requestId, cmd, userName, objectType, objectId, params);
}



long RawAmiClient::resetSeqNum(long seqnum) {
    std::lock_guard<std::mutex> lock(seqnumMutex_);  // optional thread safety
    long old = seqnum_;
    seqnum_ = seqnum;
    return old;
}