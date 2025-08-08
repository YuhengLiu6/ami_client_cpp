// AmiClient.cpp
#include <AmiClientCpp/AmiClient.hpp>
#include <algorithm>

namespace ami {
    AmiClient::AmiClient()
        : autoFlush_(false) {
    }

    AmiClient::~AmiClient() { close(); }

    bool AmiClient::start(
        const std::string& host,
        int port,
        const std::string& loginId,
        int options,
        std::string p12_keystore_file,
        std::string p12_keystore_pass)
    {
        host_ = host;
        port_ = port;
        loginId_ = loginId;
        setOptions(options);

        // Synchronous initial connect & login
        bool autoFlush = (options & ENABLE_AUTO_FLUSH_OUTGOING) != 0;
        if (!rawClient_.connect(
            host_,
            port_,
            logConnectionRetryErrors_,
            autoFlush,
            p12_keystore_file,
            p12_keystore_pass)) {
            return false;
        }
        sendLogin_();

        // Start background runner for reconnect or auto-process
        running_.store(true);
        runnerThread_ = std::thread(&AmiClient::runnerLoop_, this);
        return true;
    }



    bool AmiClient::pumpIncomingEvent() {
        // Manual pump
        return rawClient_.pumpIncomingEvent();
    }

    void AmiClient::close() {
        running_.store(false);
        runnerCv_.notify_all();
        if (runnerThread_.joinable()) runnerThread_.join();
    }

    bool AmiClient::isConnected() const {
        return rawClient_.isConnected();
    }

    long AmiClient::getAutoReconnectFrequencyMs() const {
        return autoReconnectFrequencyMs_;
    }

    void AmiClient::setAutoReconnectFrequencyMs(long ms) {
        autoReconnectFrequencyMs_ = ms;
    }

    size_t AmiClient::getAutoFlushBufferSizeThreshold() {
        return rawClient_.getAutoFlushBufferSizeThreshold();
    }
    void AmiClient::setAutoFlushBufferSizeThreshold(size_t threshold) {
        rawClient_.setAutoFlushBufferSizeThreshold(threshold);
    }

    long AmiClient::getAutoFlushBufferMillis() const {
        return rawClient_.getAutoFlushBufferMillis();
    }
    void AmiClient::setAutoFlushBufferMillis(long millis) {
        rawClient_.setAutoFlushBufferMillis(millis);
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
        autoflush_ = (options & ENABLE_AUTO_FLUSH_OUTGOING) != 0;

        rawClient_.setDebug(logMessages_);
    }

    int AmiClient::getOptions() const {
        return options_;
    }


    void AmiClient::runnerLoop_() {
        while (running_.load()) {
            // 1) Ensure connected
            if (!rawClient_.isConnected()) {
                if (autoReconnect_) {
                    // try reconnect loop
                    while (running_.load() && !rawClient_.connect(host_, port_, logConnectionRetryErrors_, autoflush_, "", "")) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(autoReconnectFrequencyMs_));
                    }
                    if (running_.load()) {
                        sendLogin_();
                    }
                }
                else {

                    std::unique_lock<std::mutex> lk(runnerMutex_);
                    runnerCv_.wait(lk, [&]() { return !running_.load() || rawClient_.isConnected(); });
                }
            }
            // 2) Process incoming if enabled
            if (autoProcessIncoming_ && rawClient_.isConnected()) {

                while (running_.load() && rawClient_.pumpIncomingEvent()) {
                }
                rawClient_.disconnect();
            }
            // 3) Sleep briefly to avoid busy spin when neither mode
            if (!autoReconnect_ && !autoProcessIncoming_) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }

        if (rawClient_.isConnected()) rawClient_.disconnect();
    }



    AmiClient& AmiClient::startStatusMessage() {
        rawClient_.startMessage('S', includeSeqNum_, includeNow_);
        return *this;
    }
    AmiClient& AmiClient::startObjectMessage(const std::string& type,
        const std::string& id,
        long expiresOn) {
        rawClient_.startMessage('O', includeSeqNum_, includeNow_)
            .addMessageParamString("T", type);
        if (!id.empty()) rawClient_.addMessageParamString("I", id);
        if (expiresOn)  rawClient_.addMessageParamLong("E", expiresOn);
        return *this;
    }
    AmiClient& AmiClient::startResponseMessage(const std::string& requestId,
        int status,
        const std::string& msg) {
        rawClient_.startMessage('R', includeSeqNum_, includeNow_)
            .addMessageParamString("I", requestId)
            .addMessageParamInt("S", status);
        if (!msg.empty()) rawClient_.addMessageParamString("M", msg);
        return *this;
    }
    AmiClient& AmiClient::startCommandDefinition(const std::string& id) {
        rawClient_.startMessage('C', includeSeqNum_, includeNow_)
            .addMessageParamString("I", id);
        return *this;
    }
    AmiClient& AmiClient::startDeleteMessage(const std::string& type,
        const std::string& id) {
        rawClient_.startMessage('D', includeSeqNum_, includeNow_)
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


}
