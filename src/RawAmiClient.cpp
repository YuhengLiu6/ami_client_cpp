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

const std::string RawAmiClient::DEFAULT_HOST = "localhost";
const int RawAmiClient::DEFAULT_PORT = 3289;

RawAmiClient::RawAmiClient()
    : socket_(nullptr),
    connected_(false),
    receiving_(false),
    sending_(false),
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
    // 启动后台线程，不要使用 this->startReader()
    readerThread_ = std::thread([this]() {
        boost::asio::streambuf buf;
        std::istream is(&buf);
        while (connected_) {
            try {
                boost::asio::read_until(*socket_, buf, '\n');
                std::string line;
                std::getline(is, line);
                if (!processIncoming(line).empty()) break;
            }
            catch (...) {
                break;
            }
        }
        // 退出时自动断开
        disconnect();
        });
    readerThread_.detach();
}

//std::string RawAmiClient::processIncoming(const std::string& line) {
//    // TODO: 按 AMI 协议解析
//    fireMessageReceived(getNow(), seqnum_++, 0, line);
//    return std::string();
//}

std::string RawAmiClient::processIncoming(const std::string& line) {
    if (line.length() < 2) return "Too short";
    if (line[1] != '@') return "missing @";

    size_t pos = line.find('|', 2);
    if (pos == std::string::npos) return "Missing | after timestamp";

    long ts = std::stol(line.substr(2, pos - 2));
    pos++; // skip '|'

    switch (line[0]) {
    case 'M': {
        if (line[pos++] != 'Q') return "Expecting Q";
        if (line[pos++] != '=') return "Expecting =";
        size_t pos2 = line.find('|', pos);
        if (pos2 == std::string::npos) return "Missing | after Q";
        long seqNum = std::stol(line.substr(pos, pos2 - pos));
        pos = pos2 + 1;

        if (line[pos++] != 'S') return "Expecting S";
        if (line[pos++] != '=') return "Expecting =";
        pos2 = line.find('|', pos);
        if (pos2 == std::string::npos) return "Missing | after S";
        int status = std::stoi(line.substr(pos, pos2 - pos));
        pos = pos2 + 1;

        if (line[pos++] != 'M') return "Expecting M";
        if (line[pos++] != '=') return "Expecting =";
        if (line[pos++] != '"') return "Expecting \"";

        std::string msg;
        if (!readUntilSkipEscaped(line, pos, '"', msg)) return "Malformed string";
        if (pos != line.size()) return "Trailing text after message";

        fireMessageReceived(ts, seqNum, status, msg);
        break;
    }
    case 'E': {
        std::map<std::string, std::string> params;
        parseIncomingParams(line, pos, params);

        std::string requestId = params["I"]; params.erase("I");
        std::string userName = params["U"]; params.erase("U");
        std::string cmd = params["C"]; params.erase("C");
        std::string type = params["T"]; params.erase("T");
        std::string objectId = params["O"]; params.erase("O");

        fireCommand(requestId, cmd, userName, type, objectId, params);
        break;
    }
    default:
        return "Unknown message type";
    }

    return std::string(); // null equivalent
}

void RawAmiClient::parseIncomingParams(const std::string& str, size_t pos, std::map<std::string, std::string>& out) {
    while (pos < str.size()) {
        if (str[pos] == '|') pos++;
        size_t eq = str.find('=', pos);
        if (eq == std::string::npos) break;
        std::string key = str.substr(pos, eq - pos);
        pos = eq + 1;

        std::string val;
        if (str[pos] == '"') {
            ++pos;
            if (!readUntilSkipEscaped(str, pos, '"', val)) break;
            if (pos < str.size() && (str[pos] == 'J' || str[pos] == 'U'))
                ++pos;
        }
        else {
            size_t end = str.find('|', pos);
            if (end == std::string::npos) end = str.size();
            val = str.substr(pos, end - pos);
            if (!val.empty() && (val.back() == 'L' || val.back() == 'D')) val.pop_back();
            pos = end;
        }

        out[key] = val;
    }
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

void RawAmiClient::fireCommand(const std::string& requestId,
    const std::string& cmd,
    const std::string& userName,
    const std::string& objectType,
    const std::string& objectId,
    const std::map<std::string, std::string>& params) {
    std::lock_guard<std::mutex> lock(listenersMutex_);
    for (auto& l : listeners_)
        l->onCommand(this, requestId, cmd, userName, objectType, objectId, params);
}
