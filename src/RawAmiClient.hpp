// RawAmiClient.hpp
#ifndef RAW_AMI_CLIENT_HPP
#define RAW_AMI_CLIENT_HPP

#include <boost/asio.hpp>
#include <atomic>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <map>

class RawAmiClientListener;

class RawAmiClient {
public:
    static const std::string DEFAULT_HOST;
    static const int DEFAULT_PORT;

    RawAmiClient();
    ~RawAmiClient();

    bool connect(const std::string& host = DEFAULT_HOST,
        int port = DEFAULT_PORT,
        bool logErrorOnRetries = true,
        bool autoFlush = false);
    void disconnect();

    // 保留，内部线程不需要外部调用，可简化
    bool pumpIncomingEvent();

    bool sendMessage(const std::string& msg, bool flush = false);

    void addListener(std::shared_ptr<RawAmiClientListener> listener);
    bool removeListener(std::shared_ptr<RawAmiClientListener> listener);

    bool isConnected() const;
    long getNow() const;

private:
    void fireConnect();
    void fireDisconnect();
    void fireMessageReceived(long ts, long seq, int status, const std::string& msg);
    void fireMessageSent(const std::string& msg);
    void fireCommand(const std::string& requestId,
        const std::string& cmd,
        const std::string& userName,
        const std::string& objectType,
        const std::string& objectId,
        const std::map<std::string, std::string>& params);

    void parseIncomingParams(const std::string& str, size_t pos, std::map<std::string, std::string>& out);
    bool readUntilSkipEscaped(const std::string& input, size_t& pos, char endChar, std::string& out);

    std::string processIncoming(const std::string& line);

    // 新增：启动后台 reader thread
    void startReader();

    boost::asio::io_context ioCtx_;
    std::unique_ptr<boost::asio::ip::tcp::socket> socket_;
    std::thread readerThread_;
    std::atomic<bool> connected_;
    std::atomic<bool> receiving_;
    std::atomic<bool> sending_;

    std::mutex listenersMutex_;
    std::vector<std::shared_ptr<RawAmiClientListener>> listeners_;

    long seqnum_;
    bool autoFlush_;
    std::condition_variable flushCv_;
    std::mutex flushMutex_;
};

#endif // RAW_AMI_CLIENT_HPP
