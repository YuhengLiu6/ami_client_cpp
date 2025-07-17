// RawAmiClient.cpp
#include "RawAmiClient.hpp"
#include "RawAmiClientListener.hpp"
#include <boost/asio.hpp>
#include <iostream>
#include <chrono>
#include <algorithm>

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

std::string RawAmiClient::processIncoming(const std::string& line) {
    // TODO: 按 AMI 协议解析
    fireMessageReceived(getNow(), seqnum_++, 0, line);
    return std::string();
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
