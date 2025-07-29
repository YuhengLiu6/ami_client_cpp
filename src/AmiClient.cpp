// AmiClient.cpp
#include "AmiClient.hpp"
#include <algorithm>


AmiClient::AmiClient()
    : autoFlush_(false) {
    // Constructor implementation
}

AmiClient::~AmiClient() {
    // Destructor implementation
}

bool AmiClient::start(const std::string& host,
    int port,
    const std::string& loginId,
    int options) {
    loginId_ = loginId;
    setOptions(options);

    // Pass logConnectionRetryErrors_ and autoflush flag to RawAmiClient
    bool autoFlush = (options & ENABLE_AUTO_FLUSH_OUTGOING) != 0;
    if (!rawClient_.connect(host,
        port,
        logConnectionRetryErrors_,
        autoFlush))
        return false;

    sendLogin_();

    // If auto-process is enabled, spin up the reader thread
    if (autoProcessIncoming_) {
        rawClient_.startReader();
    }
    return true;
}

void AmiClient::close() {
    rawClient_.disconnect();
}

bool AmiClient::isConnected() const {
    return rawClient_.isConnected();
}


void AmiClient::sendLogin_() {
    rawClient_.startMessage('L', includeSeqNum_, includeNow_);
    rawClient_.addMessageParamString("I", loginId_);
    if (quietMode_)
        rawClient_.addMessageParamString("O", "QUIET");
    rawClient_.sendMessageAndFlush();
    rawClient_.fireOnLogin();
}



void AmiClient::setOptions(int options) {
    options_ = options;
    autoProcessIncoming_ = (options & ENABLE_AUTO_PROCESS_INCOMING) != 0;
    quietMode_ = (options & ENABLE_QUIET) != 0;
    autoReconnect_ = (options & DISABLE_AUTO_RECONNECT) == 0;
    includeSeqNum_ = (options & ENABLE_SEND_SEQNUM) != 0;
    includeNow_ = (options & ENABLE_SEND_TIMESTAMPS) != 0;
    logConnectionRetryErrors_ = (options & LOG_CONNECTION_RETRY_ERRORS) != 0;
    logMessages_ = (options & LOG_MESSAGES) != 0;


}

int AmiClient::getOptions() const {
    return options_;
}



// Fluent message API
AmiClient& AmiClient::startStatusMessage() {
    rawClient_.startMessage('S', false, false);
    return *this;
}
AmiClient& AmiClient::startObjectMessage(const std::string& type,
    const std::string& id,
    long expiresOn) {
    rawClient_.startMessage('O', false, false)
        .addMessageParamString("T", type);
    if (!id.empty()) rawClient_.addMessageParamString("I", id);
    if (expiresOn)  rawClient_.addMessageParamLong("E", expiresOn);
    return *this;
}
AmiClient& AmiClient::startResponseMessage(const std::string& requestId,
    int status,
    const std::string& msg) {
    rawClient_.startMessage('R', false, false)
        .addMessageParamString("I", requestId)
        .addMessageParamInt("S", status);
    if (!msg.empty()) rawClient_.addMessageParamString("M", msg);
    return *this;
}
AmiClient& AmiClient::startCommandDefinition(const std::string& id) {
    rawClient_.startMessage('C', false, false)
        .addMessageParamString("I", id);
    return *this;
}
AmiClient& AmiClient::startDeleteMessage(const std::string& type,
    const std::string& id) {
    rawClient_.startMessage('D', false, false)
        .addMessageParamString("T", type)
        .addMessageParamString("I", id);
    return *this;
}

AmiClient& AmiClient::addMessageParamNull(const std::string& key) {
    rawClient_.addMessageParamNull(key);
    return *this;
}
AmiClient& AmiClient::addMessageParamString(const std::string& key,
    const std::string& value) {
    rawClient_.addMessageParamString(key, value);
    return *this;
}
AmiClient& AmiClient::addMessageParamInt(const std::string& key,
    int value) {
    rawClient_.addMessageParamInt(key, value);
    return *this;
}
AmiClient& AmiClient::addMessageParamLong(const std::string& key,
    long value) {
    rawClient_.addMessageParamLong(key, value);
    return *this;
}
AmiClient& AmiClient::addMessageParamDouble(const std::string& key,
    double value) {
    rawClient_.addMessageParamDouble(key, value);
    return *this;
}
AmiClient& AmiClient::addMessageParamBoolean(const std::string& key,
    bool value) {
    rawClient_.addMessageParamBoolean(key, value);
    return *this;
}

AmiClient& AmiClient::sendMessage() {
    rawClient_.sendMessage();
    return *this;
}
AmiClient& AmiClient::sendMessageAndFlush() {
    rawClient_.sendMessageAndFlush();
    return *this;
}
AmiClient& AmiClient::flush(bool clearAfterSend) {
    rawClient_.flush(clearAfterSend);
    return *this;
}

// Listener management
void AmiClient::addListener(std::shared_ptr<AmiClientListener> listener) {
    std::lock_guard<std::mutex> lk(listenerMutex_);
    listeners_.push_back(listener);
}
bool AmiClient::removeListener(std::shared_ptr<AmiClientListener> listener) {
    std::lock_guard<std::mutex> lk(listenerMutex_);
    auto it = std::find(listeners_.begin(), listeners_.end(), listener);
    if (it != listeners_.end()) {
        listeners_.erase(it);
        return true;
    }
    return false;
}

// RawAmiClientListener overrides: forward to high-level listeners
void AmiClient::onConnect(RawAmiClient* /*source*/) {
    std::vector<std::shared_ptr<AmiClientListener>> tmp;
    {
        std::lock_guard<std::mutex> lk(listenerMutex_);
        tmp = listeners_;
    }
    for (auto& l : tmp) l->onConnect(this);

}
void AmiClient::onDisconnect(RawAmiClient* /*source*/) {
    std::vector<std::shared_ptr<AmiClientListener>> tmp;
    {
        std::lock_guard<std::mutex> lk(listenerMutex_);
        tmp = listeners_;
    }
    for (auto& l : tmp) l->onDisconnect(this);
}
void AmiClient::onLoggedIn(RawAmiClient* /*source*/) {
    std::vector<std::shared_ptr<AmiClientListener>> tmp;
    {
        std::lock_guard<std::mutex> lk(listenerMutex_);
        tmp = listeners_;
    }
    for (auto& l : tmp) l->onLoggedIn(this);
}
void AmiClient::onMessageReceived(RawAmiClient* /*src*/,
    long long ts,
    long seq,
    int status,
    const std::string& msg) {
    std::vector<std::shared_ptr<AmiClientListener>> tmp;
    {
        std::lock_guard<std::mutex> lk(listenerMutex_);
        tmp = listeners_;
    }
    for (auto& l : tmp)
        l->onMessageReceived(this, ts, seq, status, msg);
}
void AmiClient::onMessageSent(RawAmiClient* /*src*/,
    const std::string& msg) {
    std::vector<std::shared_ptr<AmiClientListener>> tmp;
    {
        std::lock_guard<std::mutex> lk(listenerMutex_);
        tmp = listeners_;
    }
    for (auto& l : tmp)
        l->onMessageSent(this, msg);
}
void AmiClient::onCommand(RawAmiClient* /*src*/,
    const std::string& req,
    const std::string& cmd,
    const std::string& user,
    const std::string& type,
    const std::string& id,
    const std::map<std::string, AmiValue>& params) {
    std::vector<std::shared_ptr<AmiClientListener>> tmp;
    {
        std::lock_guard<std::mutex> lk(listenerMutex_);
        tmp = listeners_;
    }
    for (auto& l : tmp)
        l->onCommand(this, req, cmd, user, type, id, params);
}



AmiClient& AmiClient::sendCommandDefinition(const AmiClientCommandDef& def) {
    startCommandDefinition(def.getCommandId());
    if (auto v = def.getName())            addMessageParamString("N", *v);
    if (auto v = def.getArgumentsJson())   addMessageParamString("A", *v);
    if (auto v = def.getWhereClause())     addMessageParamString("W", *v);
    if (auto v = def.getHelp())            addMessageParamString("H", *v);
    if (auto v = def.getEnabledExpression()) addMessageParamString("E", *v);
    if (auto v = def.getFields())          addMessageParamString("F", *v);
    if (auto v = def.getFilterClause())    addMessageParamString("T", *v);
    if (auto v = def.getSelectMode())      addMessageParamString("M", *v);
    if (auto v = def.getStyle())           addMessageParamString("S", *v);
    if (auto v = def.getConditions())      addMessageParamString("C", *v);
    if (auto v = def.getLevel())           addMessageParamInt("L", *v);
    if (auto v = def.getPriority())        addMessageParamInt("P", *v);
    sendMessageAndFlush();
    return *this;
}