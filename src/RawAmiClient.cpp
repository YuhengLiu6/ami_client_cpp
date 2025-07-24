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


#include <iomanip>  // for std::setprecision
#include <bitset>



#include <nlohmann/json.hpp> 
#include <boost/beast/core/detail/base64.hpp>
#include <cppcodec/base64_rfc4648.hpp>
#include "AmiTypes.hpp"

using namespace std::chrono;
std::mutex coutMutex;

template<typename Method, typename... Args>
void RawAmiClient::notifyListeners(Method method, Args&&... args) {
    std::vector<std::shared_ptr<RawAmiClientListener>> tmp;
    {
        std::lock_guard<std::mutex> lock(listenersMutex_);
        tmp = listeners_;
    }
    for (auto& l : tmp) {
        (l.get()->*method)(std::forward<Args>(args)...);
    }
}


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

        {
            std::lock_guard lk(coutMutex);
            std::cout << "[connect] Connected to " << host << ":" << port << std::endl;
        }

        connected_ = true;
        autoFlush_ = autoFlush;
        startReader();
        fireConnect();

        if (autoFlush_) {
            stopAutoFlush_ = false;
            needsFlush_ = false;
            autoFlushThread_ = std::thread(&RawAmiClient::autoFlushLoop, this);
        }
        return true;
    }
    catch (const std::exception& e) {
        std::lock_guard lk(coutMutex);
        std::cerr << "[connect error] " << e.what() << std::endl;
        return false;
    }
}


void RawAmiClient::disconnect() {
    if (!connected_) return;

    {
        std::lock_guard lk(coutMutex);
        std::cout << "[disconnect] Shutting down..." << std::endl;
    }

    // 通知线程退出
    connected_ = false;
    stopAutoFlush_ = true;
    flushCv_.notify_all();

    // 关闭 socket 以唤醒 read_until
    if (socket_) {
        boost::system::error_code ec;
        socket_->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        socket_->close(ec);
    }

    // join 两条后台线程
    if (readerThread_.joinable())     readerThread_.join();
    if (autoFlushThread_.joinable())  autoFlushThread_.join();

    loggedIn_ = false;
    fireDisconnect();

    {
        std::lock_guard lk(coutMutex);
        std::cout << "[disconnect] Done." << std::endl;
    }
}


bool RawAmiClient::isConnected() const {
    return connected_;
}

long RawAmiClient::getNow() const {
    return duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
}


void RawAmiClient::startReader() {
    readerThread_ = std::thread([this]() {
        boost::asio::streambuf buf;
        while (connected_) {
            try {
                auto n = boost::asio::read_until(*socket_, buf, '\n');
                std::istream is(&buf);
                std::string line;
                //std::getline(is, line);
                //buf.consume(n);

                //// trim
                //while (!line.empty() && std::isspace((unsigned char)line.back()))
                //    line.pop_back();
                //size_t st = 0;
                //while (st < line.size() && std::isspace((unsigned char)line[st]))
                //    ++st;
                //if (st) line.erase(0, st);

                //if (line.empty())
                //    continue;

                //auto err = processIncoming(line);
                //{
                //    std::lock_guard lk(coutMutex);
                //    if (err.empty())
                //        std::cout << "[reader] OK: " << line << std::endl;
                //    else
                //        std::cerr << "[reader] ERR: " << err << std::endl;
                //}
                //if (!err.empty()) {
                //    std::lock_guard lk(coutMutex);
                //    std::cerr << "[reader] WARN: " << err << "  line='" << line << "'" << std::endl;
                //    continue;    // 忽略这一行，继续下一次 read
                //}

                while (std::getline(is, line)) {
                    // 去掉换行符残留
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();

                    // 去除首尾空格
                    size_t st = 0;
                    while (st < line.size() && std::isspace((unsigned char)line[st])) ++st;
                    while (!line.empty() && std::isspace((unsigned char)line.back())) line.pop_back();
                    if (st) line.erase(0, st);

                    if (line.empty()) continue;

                    auto err = processIncoming(line);
                    {
                        std::lock_guard lk(coutMutex);
                        /*if (err.empty())
                            std::cout << "[reader] OK: " << line << std::endl;
                        else
                            std::cerr << "[reader] WARN: " << err << "  line='" << line << "'" << std::endl;*/
                    }
                }
            }
            catch (const std::exception& e) {
                std::lock_guard lk(coutMutex);
                std::cerr << "[reader] Exception: " << e.what() << std::endl;
                break;
            }
        }
        });
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
    
    //std::cout << "[DEBUG] Incoming type: '" << s[0] << "' | Raw: " << s << std::endl;

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

    //long long ts = 0;
    //size_t pos = 0;

    //if (s.size() > 1 && s[1] == '@') {
    //    auto pipe1 = s.find('|', 2);
    //    if (pipe1 == std::string::npos) return "Missing | after timestamp";
    //    try {
    //        ts = std::stoll(s.substr(2, pipe1 - 2));
    //    }
    //    catch (...) {
    //        return "Invalid timestamp";
    //    }
    //    pos = pipe1 + 1;
    //}
    //else {
    //    // 没有 timestamp，就直接把 pos 设到第一个 '|'
    //    auto pipe0 = s.find('|', 1);
    //    if (pipe0 == std::string::npos) return "Missing | after header";
    //    pos = pipe0 + 1;
    //}

    //std::cout << "[processIncoming]: Get inside" << std::endl;

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
       

            while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos])))
                ++pos;

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

   /*         std::cout << "[Processing E Command] "
                << "RequestId: " << requestId
                << ", User: " << userName
                << ", Cmd: " << cmd
                << ", Type: " << type
                << ", ObjectId: " << objectId << std::endl;*/
            std::cout << "[Processing E Command]: try to fire ecommand" << std::endl;
            fireCommand(requestId, cmd, userName, type, objectId, params);
            break;
        }

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




//bool RawAmiClient::pumpIncomingEvent() {
//    // 留空或返回 connected_
//    return connected_;
//}

bool RawAmiClient::pumpIncomingEvent() {
    // 1) 防止并发调用
    bool expected = false;
    if (!isInReceive_.compare_exchange_strong(expected, true)) {
        throw std::runtime_error("Already in pump for receive");
    }

    // 2) 确保退出前重置标志
    struct ResetFlag { std::atomic<bool>& f; ~ResetFlag() { f.store(false); } };
    ResetFlag _reset{ isInReceive_ };

    // 3) 清空缓冲区，开始读字节
    inBuffer_.clear();
    try {
        while (connected_) {
            char c;
            boost::system::error_code ec;

            // 阻塞读一个字节
            boost::asio::read(*socket_,
                boost::asio::buffer(&c, 1),
                ec);

            if (ec) {
                // EOF 或 其它错误
                if (ec == boost::asio::error::eof) {
                    if (!inBuffer_.empty()) {
                        std::lock_guard<std::mutex> lk(coutMutex);
                        std::cerr << "[warning] Trailing text: " << inBuffer_ << "\n";
                    }
                    return false;
                }
                // 其它 I/O 错误
                std::lock_guard<std::mutex> lk(coutMutex);
                std::cerr << "[warning] Read error: " << ec.message() << "\n";
                disconnect();
                return false;
            }

            switch (c) {
            case '\n': {
                // 一行结束，交给 processIncoming 处理
                std::string err = processIncoming(inBuffer_);
                if (!err.empty()) {
                    std::lock_guard<std::mutex> lk(coutMutex);
                    std::cerr << "[warning] General error: "
                        << err
                        << " for string '" << inBuffer_ << "'\n";
                }
                return true;
            }
            case '\r':
                // 忽略回车
                continue;
            default:
                // 累积到缓冲区
                inBuffer_.push_back(c);
            }
        }

        // 如果连接关闭但缓冲区还有残余
        if (!inBuffer_.empty()) {
            std::lock_guard<std::mutex> lk(coutMutex);
            std::cerr << "[warning] Trailing text: " << inBuffer_ << "\n";
        }
        return false;
    }
    catch (const std::exception& ex) {
        // 捕获意外异常
        std::lock_guard<std::mutex> lk(coutMutex);
        std::cerr << "[warning] Exception in pumpIncomingEvent: "
            << ex.what() << "\n";
        disconnect();
        return false;
    }
}




bool RawAmiClient::sendMessage(const std::string& msg, bool flush) {
    assertConnected();

    std::lock_guard writeLock(writeMutex_);
    // buffered or immediate?
    if (!flush) {
        outBuffer_ += msg;
        if (outBuffer_.empty() || outBuffer_.back() != '\n')
            outBuffer_ += '\n';
        std::lock_guard lk(coutMutex);
        std::cout << "[sendMessage] buffered: " << msg << std::endl;
        return true;
    }

    // immediate write
    std::string toWrite = msg;
    if (toWrite.empty() || toWrite.back() != '\n')
        toWrite += '\n';
    boost::asio::write(*socket_, boost::asio::buffer(toWrite));
    fireMessageSent(toWrite);
    {
        std::lock_guard lk(coutMutex);
        std::cout << "[sendMessage] flushed: " << msg << std::endl;
    }
    return true;
}



RawAmiClient& RawAmiClient::flush(bool clearAfterSend) {
    assertConnected();
    if (clearAfterSend) {
        // 不论 autoFlush_，都立即写
        std::string buf;
        {
            std::lock_guard lk(writeMutex_);
            buf.swap(outBuffer_);
        }
        if (buf.empty() || buf.back() != '\n') buf += '\n';
        boost::asio::write(*socket_, boost::asio::buffer(buf));
        fireMessageSent(buf);
        needsFlush_ = false;
        isInSend_ = false;
        return *this;
    }
    else if (!autoFlush_) {
        // swap buffer
        std::string buf;
        {
            std::lock_guard lk(writeMutex_);
            buf.swap(outBuffer_);
        }
        if (buf.empty() || buf.back() != '\n')
            buf += '\n';

        boost::asio::write(*socket_, boost::asio::buffer(buf));
        fireMessageSent(buf);
        {
            std::lock_guard lk(coutMutex);
            std::cout << "[flush] wrote: " << buf << std::endl;
        }
        needsFlush_ = false;
    }
    else {
        // auto-flush 模式不变
        std::unique_lock lk(flushMutex_);
        needsFlush_ = true;
        flushCv_.notify_one();
    }

    if (clearAfterSend) {
        std::lock_guard lk(writeMutex_);
        outBuffer_.clear();
    }
    return *this;
}


RawAmiClient& RawAmiClient::sendMessage() {
    assertConnected();
    bool expected = true;
    if (!isInSend_.compare_exchange_strong(expected, false))
        throw std::runtime_error("Not in object send");

    needsFlush_ = true;
    return flush(false); // 不清空 outBuffer_
}


RawAmiClient& RawAmiClient::sendMessageAndFlush() {
    assertConnected();
    bool expected = true;
    if (!isInSend_.compare_exchange_strong(expected, false))
        throw std::runtime_error("Not in object send");
    needsFlush_ = true;
    return flush(true); // 发送后清空缓冲
}

void RawAmiClient::autoFlushLoop() {
    std::unique_lock lk(flushMutex_);
    while (!stopAutoFlush_) {
        flushCv_.wait(lk, [&]() {
            return needsFlush_.load() || stopAutoFlush_.load();
            });
        if (stopAutoFlush_) break;

        // swap + 写
        std::string buf;
        {
            std::lock_guard wl(writeMutex_);
            buf.swap(outBuffer_);
            needsFlush_ = false;
        }
        if (buf.empty() || buf.back() != '\n')
            buf += '\n';

        try {
            boost::asio::write(*socket_, boost::asio::buffer(buf));
            fireMessageSent(buf);
        }
        catch (...) {
            disconnect();
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(autoFlushIntervalMs_));
    }
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
    notifyListeners(&RawAmiClientListener::onConnect, this);
}

void RawAmiClient::fireDisconnect() {
    notifyListeners(&RawAmiClientListener::onDisconnect, this);
}

void RawAmiClient::fireOnLogin() {
    if (loggedIn_.exchange(true)) return;  // 保证只触发一次
    notifyListeners(&RawAmiClientListener::onLoggedIn, this);
}

void RawAmiClient::fireMessageReceived(long long ts, long seq, int status, const std::string& msg) {
    notifyListeners(&RawAmiClientListener::onMessageReceived,
        this, ts, seq, status, msg);
}

void RawAmiClient::fireMessageSent(const std::string& msg) {
    notifyListeners(&RawAmiClientListener::onMessageSent, this, msg);
}

void RawAmiClient::fireCommand(const std::string& requestId,
    const std::string& cmd,
    const std::string& userName,
    const std::string& objectType,
    const std::string& objectId,
    const std::map<std::string, AmiValue>& params) {
    notifyListeners(&RawAmiClientListener::onCommand,
        this, requestId, cmd, userName, objectType, objectId, params);
}



long RawAmiClient::resetSeqNum(long seqnum) {
    std::lock_guard<std::mutex> lock(seqnumMutex_);  // optional thread safety
    long old = seqnum_;
    seqnum_ = seqnum;
    return old;
}



//construct msg
void RawAmiClient::resetMessage() {
    assertConnected();
    isInSend_ = false;
    outBuffer_.clear();
}

void RawAmiClient::assertConnected() const {
    if (!connected_) throw std::runtime_error("not connected");
}

void RawAmiClient::assertInMessage() const {
    if (!isInSend_) throw std::runtime_error("not in object send, call startMessage(...) first");
}

RawAmiClient& RawAmiClient::startMessage(char type, bool includeSeqNum, bool includeNow) {
    assertConnected();

    bool expected = false;
    if (!isInSend_.compare_exchange_strong(expected, true))
        throw std::runtime_error("Already in object send");

    outBuffer_.clear();
    outBuffer_ += type;
    if (includeSeqNum)
        outBuffer_ += "#" + std::to_string(seqnum_++);
    if (includeNow)
        outBuffer_ += "@" + std::to_string(getNow());

    return *this;
}


RawAmiClient& RawAmiClient::addMessageParamNull(const std::string& key) {
    assertInMessage();
    outBuffer_ += "|" + key + "=null";
    return *this;
}

RawAmiClient& RawAmiClient::addMessageParamString(const std::string& key, char value) {
    assertInMessage();
    outBuffer_ += "|" + key + "=\"";
    if (value == '"') outBuffer_ += "\\\"";
    else outBuffer_ += value;
    outBuffer_ += "\"";
    return *this;
}

RawAmiClient& RawAmiClient::addMessageParamString(const std::string& key, const std::string& value) {
    assertInMessage();
    outBuffer_ += "|" + key + "=\"";
    for (char c : value) {
        if (c == '\\' || c == '"') outBuffer_ += '\\';
        outBuffer_ += c;
    }
    outBuffer_ += "\"";
    return *this;
}

RawAmiClient& RawAmiClient::addMessageParamString(const std::string& key, const std::string& value, size_t start, size_t end) {
    return addMessageParamString(key, value.substr(start, end - start));
}

RawAmiClient& RawAmiClient::addMessageParamEnum(const std::string& key, const std::string& value) {
    assertInMessage();
    outBuffer_ += "|" + key + "='";
    for (char c : value) {
        if (c == '\\' || c == '\'') outBuffer_ += '\\';
        outBuffer_ += c;
    }
    outBuffer_ += "'";
    return *this;
}

RawAmiClient& RawAmiClient::addMessageParamEnum(const std::string& key, const std::string& value, size_t start, size_t end) {
    return addMessageParamEnum(key, value.substr(start, end - start));
}

RawAmiClient& RawAmiClient::addMessageParamEnum(const std::string& key, const std::vector<char>& value) {
    assertInMessage();
    outBuffer_ += "|" + key + "='";
    for (char c : value) {
        if (c == '\\' || c == '\'') outBuffer_ += '\\';
        outBuffer_ += c;
    }
    outBuffer_ += "'";
    return *this;
}

RawAmiClient& RawAmiClient::addMessageParamJson(const std::string& key, const std::string& jsonStr) {
    assertInMessage();
    outBuffer_ += "|" + key + "=\"";
    for (char c : jsonStr) {
        if (c == '\\' || c == '"') outBuffer_ += '\\';
        outBuffer_ += c;
    }
    outBuffer_ += "\"J";
    return *this;
}

RawAmiClient& RawAmiClient::addMessageParamBinary(const std::string& key, const std::vector<uint8_t>& value) {
    return addMessageParamBinary(key, value, 0, value.size());
}

RawAmiClient& RawAmiClient::addMessageParamBinary(const std::string& key, const std::vector<uint8_t>& value, size_t start, size_t end) {
    assertInMessage();
    outBuffer_ += "|" + key + "=\"";
    static const char* base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (size_t i = start; i < end; i += 3) {
        int val = (value[i] << 16) + ((i + 1 < end ? value[i + 1] : 0) << 8) + (i + 2 < end ? value[i + 2] : 0);
        outBuffer_ += base64_chars[(val >> 18) & 0x3F];
        outBuffer_ += base64_chars[(val >> 12) & 0x3F];
        outBuffer_ += (i + 1 < end) ? base64_chars[(val >> 6) & 0x3F] : '=';
        outBuffer_ += (i + 2 < end) ? base64_chars[val & 0x3F] : '=';
    }
    outBuffer_ += "\"U";
    return *this;
}

RawAmiClient& RawAmiClient::addMessageParamLong(const std::string& key, long value) {
    assertInMessage();
    outBuffer_ += "|" + key + "=" + std::to_string(value) + "L";
    return *this;
}

RawAmiClient& RawAmiClient::addMessageParamInt(const std::string& key, int value) {
    assertInMessage();
    outBuffer_ += "|" + key + "=" + std::to_string(value);
    return *this;
}

RawAmiClient& RawAmiClient::addMessageParamDouble(const std::string& key, double value) {
    assertInMessage();
    std::ostringstream oss;
    oss << std::setprecision(16) << value;
    outBuffer_ += "|" + key + "=" + oss.str() + "D";
    return *this;
}

RawAmiClient& RawAmiClient::addMessageParamFloat(const std::string& key, float value) {
    assertInMessage();
    std::ostringstream oss;
    oss << std::setprecision(8) << value;
    outBuffer_ += "|" + key + "=" + oss.str();
    return *this;
}

RawAmiClient& RawAmiClient::addMessageParamDoubleEncoded(const std::string& key, double value) {
    assertInMessage();
    union {
        double d;
        uint64_t bits;
    } u;
    u.d = value;
    outBuffer_ += "|" + key + "=D" + std::to_string(u.bits);  // 可以替换为 Base64 编码
    return *this;
}

RawAmiClient& RawAmiClient::addMessageParamFloatEncoded(const std::string& key, float value) {
    assertInMessage();
    union {
        float f;
        uint32_t bits;
    } u;
    u.f = value;
    outBuffer_ += "|" + key + "=F" + std::to_string(u.bits);  // 可以替换为 Base64 编码
    return *this;
}

RawAmiClient& RawAmiClient::addMessageParamBoolean(const std::string& key, bool value) {
    assertInMessage();
    outBuffer_ += "|" + key + "=" + std::string(value ? "true" : "false");
    return *this;
}


void RawAmiClient::addMessageParams(const std::unordered_map<std::string, std::any>& params) {
    for (const auto& [key, value] : params) {
        addMessageParamObject(key, value);
    }
}

void RawAmiClient::addMessageParamObject(const std::string& key, const std::any& value) {
    if (!value.has_value()) {
        addMessageParamNull(key);
        return;
    }

    try {
        // === Strings and Chars ===
        if (value.type() == typeid(std::string)) {
            addMessageParamString(key, std::any_cast<std::string>(value));
        }
        else if (value.type() == typeid(const char*)) {
            addMessageParamString(key, std::string(std::any_cast<const char*>(value)));
        }
        else if (value.type() == typeid(char)) {
            addMessageParamEnum(key, std::string(1, std::any_cast<char>(value)));
        }

        // === Integer family ===
        else if (value.type() == typeid(int)) {
            addMessageParamInt(key, std::any_cast<int>(value));
        }
        else if (value.type() == typeid(long)) {
            addMessageParamLong(key, std::any_cast<long>(value));
        }
        else if (value.type() == typeid(short)) {
            addMessageParamInt(key, std::any_cast<short>(value));
        }
        else if (value.type() == typeid(uint8_t)) {
            addMessageParamInt(key, std::any_cast<uint8_t>(value));
        }
        else if (value.type() == typeid(int8_t)) {
            addMessageParamInt(key, std::any_cast<int8_t>(value));
        }

        // === Floating point family ===
        else if (value.type() == typeid(float)) {
            addMessageParamFloat(key, std::any_cast<float>(value));
        }
        else if (value.type() == typeid(double)) {
            addMessageParamDouble(key, std::any_cast<double>(value));
        }

        // === Boolean ===
        else if (value.type() == typeid(bool)) {
            addMessageParamBoolean(key, std::any_cast<bool>(value));
        }

        // === Binary (byte[]) ===
        else if (value.type() == typeid(std::vector<uint8_t>)) {
            addMessageParamBinary(key, std::any_cast<std::vector<uint8_t>>(value));
        }
        else if (value.type() == typeid(std::vector<char>)) {
            const auto& vec = std::any_cast<std::vector<char>>(value);
            addMessageParamBinary(key, std::vector<uint8_t>(vec.begin(), vec.end()));
        }

        // === UUID, Complex, etc. as string ===
        else if (value.type() == typeid(std::shared_ptr<std::stringstream>)) {
            auto ss = std::any_cast<std::shared_ptr<std::stringstream>>(value);
            addMessageParamString(key, ss->str());
        }
        else if (value.type() == typeid(std::string_view)) {
            addMessageParamString(key, std::string(std::any_cast<std::string_view>(value)));
        }

        // === Fallback ===
        else {
            throw std::runtime_error("Unsupported type in addMessageParamObject for key: " + key);
        }
    }
    catch (const std::bad_any_cast& e) {
        throw std::runtime_error("Bad cast for key: " + key + " -> " + e.what());
    }
}



const std::string& RawAmiClient::getOutputBuffer() const {
    return outBuffer_;
}

long RawAmiClient::getAutoFlushBufferMillis() const {
    return autoFlushIntervalMs_;
}

void RawAmiClient::setAutoFlushBufferMillis(long millis) {
    autoFlushIntervalMs_ = millis;
}