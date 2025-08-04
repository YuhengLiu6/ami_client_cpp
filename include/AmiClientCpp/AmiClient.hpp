// AmiClient.hpp
#pragma once
#include <string>
#include <map>
#include <vector>
#include <memory>
#include <mutex>

#include <AmiClientCpp/AmiClient.hpp>
#include <AmiClientCpp/RawAmiClient.hpp>
#include <AmiClientCpp/RawAmiClientListener.hpp>
#include <AmiClientCpp/AmiClientListener.hpp>
#include <AmiClientCpp/AmiTypes.hpp>
#include <AmiClientCpp/AmiClientCommandDef.hpp>
namespace ami {

    class AmiClient : public RawAmiClientListener, public std::enable_shared_from_this<AmiClient> {
    public:
        // Option flags
        static const int ENABLE_AUTO_PROCESS_INCOMING = 1 << 1;  // 2
        static const int ENABLE_QUIET = 1 << 2;  // 4
        static const int DISABLE_AUTO_RECONNECT = 1 << 3;  // 8
        static const int ENABLE_SEND_TIMESTAMPS = 1 << 5;  // 32
        static const int ENABLE_SEND_SEQNUM = 1 << 6;  // 64
        static const int LOG_CONNECTION_RETRY_ERRORS = 1 << 7;  // 128
        static const int LOG_MESSAGES = 1 << 8;  // 256
        static const int ENABLE_AUTO_FLUSH_OUTGOING = 1 << 9;  // 512


        long getAutoReconnectFrequencyMs() const;
        void setAutoReconnectFrequencyMs(long ms);


        void setOptions(int options);
        int getOptions() const;

        static std::shared_ptr<AmiClient> create() {
            auto instance = std::shared_ptr<AmiClient>(new AmiClient());
            instance->initialize();
            return instance;
        }

        static constexpr const char* DEFAULT_HOST = "localhost";
        static const int DEFAULT_PORT = 3289;

        ~AmiClient() override;


        bool start(
            const std::string& host,
            int port,
            const std::string& loginId,
            int options,
            std::string server_certificate_public_key_file = {},
            std::string client_certificate_public_key_file = {},
            std::string client_certificate_private_key_file = {});

        void close();


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


        bool pumpIncomingEvent();


        void addListener(std::shared_ptr<AmiClientListener> listener);
        bool removeListener(std::shared_ptr<AmiClientListener> listener);

        bool isConnected() const;


        void onConnect(RawAmiClient* source) override;
        void onDisconnect(RawAmiClient* source) override;
        void onLoggedIn(RawAmiClient* source) override;
        void onMessageReceived(RawAmiClient* source,
            long timestamp,
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

        size_t getAutoFlushBufferSizeThreshold();
        void   setAutoFlushBufferSizeThreshold(size_t threshold);

        long getAutoFlushBufferMillis() const;
        void setAutoFlushBufferMillis(long millis);

    private:
        AmiClient();

        void initialize() {
            rawClient_.addListener(std::static_pointer_cast<RawAmiClientListener>(shared_from_this()));
        }

        RawAmiClient rawClient_;
        std::vector<std::shared_ptr<AmiClientListener>> listeners_;
        std::mutex listenerMutex_;
        std::string loginId_;
        bool autoFlush_;

        void sendLogin_();
        void runnerLoop_();

        std::string host_;
        int port_;


        std::thread runnerThread_;
        std::atomic<bool> running_{ false };
        long autoReconnectFrequencyMs_{ 1000 };
        std::mutex runnerMutex_;
        std::condition_variable runnerCv_;


        int   options_{ 0 };
        bool  autoProcessIncoming_{ true };
        bool  quietMode_{ false };
        bool  autoReconnect_{ true };
        bool  includeSeqNum_{ false };
        bool  includeNow_{ false };
        bool  logConnectionRetryErrors_{ false };
        bool  logMessages_{ false };
        bool  autoflush_{ true };
    };

}