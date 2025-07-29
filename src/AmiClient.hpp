// AmiClient.hpp
#pragma once
#include <string>
#include <map>
#include <vector>
#include <memory>
#include <mutex>
#include "RawAmiClient.hpp"
#include "RawAmiClientListener.hpp"
#include "AmiClientListener.hpp"
#include "AmiClientCommandDef.hpp"
/**
 * High-level AMI client wrapping RawAmiClient for easy use.
 */
class AmiClient : public RawAmiClientListener, public std::enable_shared_from_this<AmiClient> {
public:
    // === options bits===
    static constexpr int ENABLE_AUTO_PROCESS_INCOMING = 1 << 1;   // 2
    static constexpr int ENABLE_QUIET = 1 << 2;   // 4
    static constexpr int DISABLE_AUTO_RECONNECT = 1 << 3;   // 8
    static constexpr int ENABLE_SEND_TIMESTAMPS = 1 << 5;   // 32
    static constexpr int ENABLE_SEND_SEQNUM = 1 << 6;   // 64
    static constexpr int LOG_CONNECTION_RETRY_ERRORS = 1 << 7;   // 128
    static constexpr int LOG_MESSAGES = 1 << 8;   // 256
    static constexpr int ENABLE_AUTO_FLUSH_OUTGOING = 1 << 9;   // 512


    static std::shared_ptr<AmiClient> create() {
        auto instance = std::shared_ptr<AmiClient>(new AmiClient());
        instance->initialize();
        return instance;
    }

    static constexpr const char* DEFAULT_HOST = "localhost";
    static const int DEFAULT_PORT = 3289;

    ~AmiClient() override;

    /**
     * Connects to AMI relay, logs in, and optionally enables auto-flush.
     */
    bool start(const std::string& host,
        int port,
        const std::string& loginId,
        int options);

    /**
     * Disconnects and stops client.
     */
    void close();

    // Fluent API for building and sending messages
    AmiClient& startStatusMessage();
    AmiClient& startObjectMessage(const std::string& type,
        const std::string& id = std::string(),
        long expiresOn = 0);
    AmiClient& startResponseMessage(const std::string& requestId,
        int status = 0,
        const std::string& msg = std::string());
    AmiClient& startCommandDefinition(const std::string& id);
    AmiClient& startDeleteMessage(const std::string& type,
        const std::string& id);

    AmiClient& addMessageParamNull(const std::string& key);
    AmiClient& addMessageParamString(const std::string& key,
        const std::string& value);
    AmiClient& addMessageParamInt(const std::string& key,
        int value);
    AmiClient& addMessageParamLong(const std::string& key,
        long value);
    AmiClient& addMessageParamDouble(const std::string& key,
        double value);
    AmiClient& addMessageParamBoolean(const std::string& key,
        bool value);
    AmiClient& sendMessage();
    AmiClient& sendMessageAndFlush();
    AmiClient& flush(bool clearAfterSend = true);
  
    AmiClient& sendCommandDefinition(const AmiClientCommandDef& def);

    // Listener management
    void addListener(std::shared_ptr<AmiClientListener> listener);
    bool removeListener(std::shared_ptr<AmiClientListener> listener);

    bool isConnected() const;

    // RawAmiClientListener overrides (forward events)
    void onConnect(RawAmiClient* source) override;
    void onDisconnect(RawAmiClient* source) override;
    void onLoggedIn(RawAmiClient* source) override;
    void onMessageReceived(RawAmiClient* source,
        long long timestamp,
        long seqNum,
        int status,
        const std::string& message) override;
    void onMessageSent(RawAmiClient* source,
        const std::string& message) override;
    void onCommand(RawAmiClient* source,
        const std::string& requestId,
        const std::string& cmd,
        const std::string& userName,
        const std::string& objectType,
        const std::string& objectId,
        const std::map<std::string, AmiValue>& params) override;

private:
    AmiClient(); // Declare the constructor only once

    void initialize() {
        rawClient_.addListener(std::static_pointer_cast<RawAmiClientListener>(shared_from_this()));
    }

    RawAmiClient rawClient_;                // underlying raw client
    std::vector<std::shared_ptr<AmiClientListener>> listeners_;
    std::mutex listenerMutex_;
    std::string loginId_;
    

    void sendLogin_();
    void setOptions(int options);

    int  options_;
    bool autoFlush_;
    bool autoProcessIncoming_;
    bool quietMode_;
    bool autoReconnect_;
    bool includeSeqNum_;
    bool includeNow_;
    bool logConnectionRetryErrors_;
    bool logMessages_;
    bool autoFlushOutgoing_;
};