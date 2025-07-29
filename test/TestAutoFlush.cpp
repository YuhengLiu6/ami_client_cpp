#include "../src/AmiClient.hpp"
#include "../src/AmiClientListener.hpp"
#include "../src/AmiTypes.hpp"
#include "../src/AmiClientCommandDef.hpp"
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <mutex>
#include <iomanip>

extern std::mutex coutMutex;

class AutoFlushTestListener : public AmiClientListener {
public:
    void onConnect(AmiClient* client) override {
        std::lock_guard<std::mutex> lk(coutMutex);
        std::cout << "[Listener] Connected to server." << std::endl;
    }

    void onLoggedIn(AmiClient* client) override {
        {
            std::lock_guard<std::mutex> lk(coutMutex);
            std::cout << "[Listener] Logged in, starting auto-flush tests..." << std::endl;
        }

        size_t threshold = 100;
        client->setAutoFlushBufferSizeThreshold(threshold);
        {
            std::lock_guard<std::mutex> lk(coutMutex);
            std::cout << "[Test] Buffer-size threshold = " << threshold << " bytes" << std::endl;
        }

        for (int i = 1; i <= 5; ++i) {
            client->
                startObjectMessage("TestType", "bufMsg" + std::to_string(i))
                .addMessageParamString("data", std::string(30, 'X'))
                .sendMessage();  // buffered

            {
                std::lock_guard<std::mutex> lk(coutMutex);
                std::cout << "[Test] Buffered message " << i
                    << ", buffer size now ~(" << (i * 30 + 20) << ") bytes"
                    << std::endl;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // 给一点时间让最后一次 flush 完成
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // ------- 2) 时间间隔 自动刷新 测试 -------
        client->setAutoFlushBufferSizeThreshold(0);  // 禁用大小触发
        long intervalMs = 5000;
        client->setAutoFlushBufferMillis(intervalMs);
        std::cout << "[Test] Time-based auto-flush interval = " << intervalMs << " ms" << std::endl;

        // 发送一条 buffered 消息，观察定时 flush
        client->
            startObjectMessage("TestType", "timeMsg")
            .addMessageParamString("payload", "time-test")
            .sendMessage();  // buffered

        std::cout << "[Test] Buffered one message, waiting for timed flush..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs + 200));

        std::cout << "[Test] Time-based auto-flush test completed." << std::endl;

        // 关闭 client
        client->close();
    }

    void onDisconnect(AmiClient* client) override {
        std::lock_guard<std::mutex> lk(coutMutex);
        std::cout << "[Listener] Disconnected." << std::endl;
    }

    void onMessageSent(AmiClient* client, const std::string& message) override {
        std::lock_guard<std::mutex> lk(coutMutex);
        std::cout << "[Listener] MessageSent: " << message;
    }

    void onMessageReceived(AmiClient* client,
        long long timestamp,
        long long seqNum,
        int status,
        const std::string& message) override {
        std::lock_guard<std::mutex> lk(coutMutex);
        std::cout << "[Listener] MessageReceived: seq=" << seqNum
            << " status=" << status
            << " msg=\"" << message << "\"" << std::endl;
    }

    void onCommand(AmiClient* source,
        const std::string& requestId,
        const std::string& cmd,
        const std::string& userName,
        const std::string& objectType,
        const std::string& objectId,
        const std::map<std::string, AmiValue>& params) override {
        // 忽略
    }
};

int main(int argc, char* argv[]) {
    std::string host = AmiClient::DEFAULT_HOST;
    int port = AmiClient::DEFAULT_PORT;
    std::string loginId = "demo";

    if (argc > 1) host = argv[1];
    if (argc > 2) port = std::stoi(argv[2]);
    if (argc > 3) loginId = argv[3];

    auto client = AmiClient::create();
    auto listener = std::make_shared<AutoFlushTestListener>();
    client->addListener(listener);

    {
        std::lock_guard<std::mutex> lk(coutMutex);
        std::cout << "[Main] Starting AmiClient to " << host << ":" << port
            << " with loginId=\"" << loginId << "\"..." << std::endl;
    }

    int opts = AmiClient::ENABLE_AUTO_PROCESS_INCOMING | AmiClient::ENABLE_AUTO_FLUSH_OUTGOING;
    if (!client->start(host, port, loginId, opts)) {
        std::cerr << "[Main] Failed to start AmiClient." << std::endl;
        return 1;
    }

    // 等待测试完成
    while (client->isConnected()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "[Main] Test finished, exiting." << std::endl;
    return 0;
}
