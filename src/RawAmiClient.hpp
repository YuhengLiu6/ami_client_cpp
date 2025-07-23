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
#include <variant>
#include "AmiTypes.hpp"
#include <any>            // for std::any
#include <unordered_map>
namespace {
    std::mutex g_logMutex;
}
extern std::mutex coutMutex;
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

    RawAmiClient& RawAmiClient::flush(bool clearAfterSend);

    const std::string& getOutputBuffer() const;


    long getAutoFlushBufferMillis() const;
    void setAutoFlushBufferMillis(long millis);

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

    void parseIncomingParams(const std::string& str, size_t pos, std::map<std::string, AmiValue>& out);
    bool readUntilSkipEscaped(const std::string& input, size_t& pos, char endChar, std::string& out);

    std::string processIncoming(const std::string& line);

    long resetSeqNum(long seqnum);
    // 新增：启动后台 reader thread
    void startReader();


 
    void assertConnected() const;
    void assertInMessage() const;
	void resetMessage();

    std::string outBuffer_;
    std::atomic<bool> isInSend_;


    std::mutex seqnumMutex_;
    boost::asio::io_context ioCtx_;
    std::unique_ptr<boost::asio::ip::tcp::socket> socket_;
    std::thread readerThread_;
    std::atomic<bool> connected_;
    std::atomic<bool> receiving_;
    std::atomic<bool> sending_;
    std::atomic<bool> loggedIn_;
    std::atomic<bool> needsFlush_;
    // ==============================================
    std::thread              autoFlushThread_;
    std::atomic<bool>        stopAutoFlush_{ false };
    long                     autoFlushIntervalMs_{ 2 };    // 毫秒
    // 条件等待 & 锁，用于唤醒后台线程
    std::mutex               flushMutex_;
    std::condition_variable  flushCv_;

    void autoFlushLoop();


    std::mutex listenersMutex_;
    std::vector<std::shared_ptr<RawAmiClientListener>> listeners_;

    long seqnum_;
    bool autoFlush_;

    std::mutex writeMutex_;

};

#endif // RAW_AMI_CLIENT_HPP
