// RawAmiClient.hpp
#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <atomic>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <map>
#include <variant>
#include "AmiTypes.hpp"
#include <any>            
#include <unordered_map>
 

namespace ami {
    extern std::mutex g_logMutex;
    extern std::mutex coutMutex;
    class RawAmiClientListener;

    class SocketBase
    {
    public:
        virtual ~SocketBase() = default;

        explicit SocketBase(boost::asio::io_context& io_context)
            : io_context_(io_context)
        {
        }

        virtual bool connect(const std::string& host, const std::string& port) = 0;
        virtual void disconnect() = 0;
        virtual void send_message(const std::string& message) = 0;
        virtual std::size_t read_until(boost::asio::streambuf& buf, char delim) = 0;
        virtual char read_char(boost::system::error_code& ec) = 0;

    protected:
        boost::asio::io_context& io_context_;
    };

    class TcpSocket final : public SocketBase
    {
        using super = SocketBase;

    public:
        explicit TcpSocket(boost::asio::io_context& io_context)
            : super(io_context)
            , socket_(io_context)
        {
        }

        bool connect(const std::string& host, const std::string& port) override;
        void disconnect() override;
        void send_message(const std::string& message) override;
        std::size_t read_until(boost::asio::streambuf& buf, char delim) override;
        char read_char(boost::system::error_code& ec) override;

    private:
        boost::asio::ip::tcp::socket socket_;
    };

    class SslSocket final : public SocketBase
    {
        using super = SocketBase;

    public:
        using ssl_socket = boost::asio::ssl::stream<boost::asio::ip::tcp::socket>;

        ~SslSocket() override;

        SslSocket(
            boost::asio::io_context& io_context,
            std::string server_certificate_public_key_file,
            std::string client_certificate_public_key_file,
            std::string client_certificate_private_key_file
        );

    SslSocket(
            boost::asio::io_context & io_context,
            std::string p12_keystore_file,
            std::string p12_keystore_pass);

    bool connect(const std::string & host, const std::string & port) override
    {
        return connect_using_p12(host, port);
    }
    void disconnect() override;
    void send_message(const std::string& message) override;
    std::size_t read_until(boost::asio::streambuf & buf, char delim) override;
    char read_char(boost::system::error_code & ec) override;

private:
    bool verify_certificate(bool preverified, boost::asio::ssl::verify_context& ctx);

    bool connect_using_p12(const std::string & host, const std::string & port);
    bool connect_using_pem_files(const std::string & host, const std::string & port);

private:
    boost::asio::ssl::context ssl_context_;
    std::unique_ptr<ssl_socket> ssl_socket_;
    std::string host_; // store for SSL verification
    STACK_OF(X509) * ca_ = sk_X509_new_null(); // store for SSL verification
    std::string server_certificate_public_key_file_;
    std::string client_certificate_public_key_file_;
    std::string client_certificate_private_key_file_;

    std::string p12_keystore_file_;
    std::string p12_keystore_pass_;
};

class RawAmiClient {
public:
    static const std::string DEFAULT_HOST;
    static const int DEFAULT_PORT;

    RawAmiClient() = default;

    ~RawAmiClient();

    bool connect(
            const std::string& host = DEFAULT_HOST,
            int port = DEFAULT_PORT,
            bool logErrorOnRetries = true,
            bool autoFlush = false,
            std::string p12_keystore_file = {},
            std::string p12_keystore_pass = {});
    void disconnect();



        bool sendMessage(const std::string& msg, bool flush = false);

        void addListener(std::shared_ptr<RawAmiClientListener> listener);
        bool removeListener(std::shared_ptr<RawAmiClientListener> listener);

        bool isConnected() const;
        long getNow() const;


        RawAmiClient& startMessage(char type, bool includeSeqNum, bool includeNow);
        RawAmiClient& sendMessage();
        RawAmiClient& sendMessageAndFlush();
        RawAmiClient& addMessageParamNull(const std::string& key);
        RawAmiClient& addMessageParamString(const std::string& key, char value);
        RawAmiClient& addMessageParamString(const std::string& key, const std::string& value);
        RawAmiClient& addMessageParamString(const std::string& key, const std::string& value, size_t start, size_t end);
        RawAmiClient& addMessageParamEnum(const std::string& key, const std::string& value);
        RawAmiClient& addMessageParamEnum(const std::string& key, const std::string& value, size_t start, size_t end);
        RawAmiClient& addMessageParamEnum(const std::string& key, const std::vector<char>& value);
        RawAmiClient& addMessageParamJson(const std::string& key, const std::string& jsonStr);
        RawAmiClient& addMessageParamBinary(const std::string& key, const std::vector<uint8_t>& value);
        RawAmiClient& addMessageParamBinary(const std::string& key, const std::vector<uint8_t>& value, size_t start, size_t end);
        RawAmiClient& addMessageParamLong(const std::string& key, long value);
        RawAmiClient& addMessageParamInt(const std::string& key, int value);
        RawAmiClient& addMessageParamDouble(const std::string& key, double value);
        RawAmiClient& addMessageParamFloat(const std::string& key, float value);
        RawAmiClient& addMessageParamDoubleEncoded(const std::string& key, double value);
        RawAmiClient& addMessageParamFloatEncoded(const std::string& key, float value);
        RawAmiClient& addMessageParamBoolean(const std::string& key, bool value);
        void addMessageParams(const std::unordered_map<std::string, std::any>& params);
        void addMessageParamObject(const std::string& key, const std::any& value);

        RawAmiClient& flush(bool clearAfterSend);

        const std::string& getOutputBuffer() const;


        long getAutoFlushBufferMillis() const;
        void setAutoFlushBufferMillis(long millis);

        size_t getAutoFlushBufferSizeThreshold() const;
        void setAutoFlushBufferSizeThreshold(size_t threshold);

        bool pumpIncomingEvent();
        void setDebug(bool enable);

    protected:
        std::string processIncoming(const std::string& line);
        long resetSeqNum(long seqnum);
        void parseIncomingParams(const std::string& str, size_t pos, std::map<std::string, AmiValue>& out);
        bool readUntilSkipEscaped(const std::string& input, size_t& pos, char endChar, std::string& out);
        void resetMessage();
        std::atomic<bool> connected_{ false };
    private:
        void fireConnect();
        void fireDisconnect();
        void fireMessageReceived(long ts, long seq, int status, const std::string& msg);
        void fireMessageSent(const std::string& msg);
        void fireOnLogin();
        void fireCommand(const std::string& requestId,
            const std::string& cmd,
            const std::string& userName,
            const std::string& objectType,
            const std::string& objectId,
            const std::map<std::string, AmiValue>& params);

        //void parseIncomingParams(const std::string& str, size_t pos, std::map<std::string, AmiValue>& out);
        //bool readUntilSkipEscaped(const std::string& input, size_t& pos, char endChar, std::string& out);

        //std::string processIncoming(const std::string& line);
        //long resetSeqNum(long seqnum);

        //void resetMessage();
        void startReader();

        template<typename F, typename... Args>
        void notifyListeners(F fn, Args&&... args);

        void assertConnected() const;
        void assertInMessage() const;
        

        std::string inBuffer_;
        std::atomic<bool> isInReceive_{ false };
        std::string outBuffer_;
        std::atomic<bool> isInSend_;


        std::mutex seqnumMutex_;
        boost::asio::io_context ioCtx_;
        std::thread readerThread_;
        /*std::atomic<bool> connected_{ false };*/
        std::atomic<bool> receiving_{ false };
        std::atomic<bool> sending_{ false };
        std::atomic<bool> loggedIn_{ false };
        std::atomic<bool> needsFlush_{ false };

        std::thread              autoFlushThread_;
        std::atomic<bool>        stopAutoFlush_{ false };
        long                     autoFlushIntervalMs_{ 2 };

        std::mutex               flushMutex_;
        std::condition_variable  flushCv_;

        void autoFlushLoop();


        std::mutex listenersMutex_;
        std::vector<std::shared_ptr<RawAmiClientListener>> listeners_;

        long seqnum_{ 0 };
        bool autoFlush_{ false };

        std::mutex writeMutex_;
        friend class AmiClient;

        bool debug_{ false };

        size_t autoFlushBufferSizeThreshold_{ 0 };


        std::string batchBuffer_;

        std::unique_ptr<SocketBase> socket_;
    };

} // RAW_AMI_CLIENT_HPP
