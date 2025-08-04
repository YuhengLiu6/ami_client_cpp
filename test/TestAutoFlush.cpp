#include <AmiClientCpp/AmiClient.hpp>
#include <AmiClientCpp/AmiClientCommandDef.hpp>
#include <AmiClientCpp/AmiClientListener.hpp>
#include <AmiClientCpp/AmiTypes.hpp>

#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <mutex>
#include <iomanip>

namespace ami {

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

            // ------- 1) Buffer size threshold test -------
            size_t threshold = 100;
            client->setAutoFlushBufferSizeThreshold(threshold);
            {
                std::lock_guard<std::mutex> lk(coutMutex);
                std::cout << "[Test] Buffer-size threshold = " << threshold << " bytes" << std::endl;
            }

            for (int i = 1; i <= 30; ++i) {
                client->startObjectMessage("TestType", "bufMsg" + std::to_string(i))
                    .addMessageParamString("data", std::string(30, 'X'))
                    .sendMessage();  // buffered

                {
                    std::lock_guard<std::mutex> lk(coutMutex);
                    std::cout << "[Test] Buffered message " << i
                        << ", buffer size now ~(" << (i * 30 + 20) << ") bytes" << std::endl;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(200));

            // ------- 2) Time-based auto-flush test -------
            client->setAutoFlushBufferSizeThreshold(0);
            long intervalMs = 5000;
            client->setAutoFlushBufferMillis(intervalMs);

            {
                std::lock_guard<std::mutex> lk(coutMutex);
                std::cout << "[Test] Time-based auto-flush interval = " << intervalMs << " ms" << std::endl;
            }

            client->startObjectMessage("TestType", "timeMsg")
                .addMessageParamString("payload", "time-test")
                .sendMessage();

            {
                std::lock_guard<std::mutex> lk(coutMutex);
                std::cout << "[Test] Buffered one message, waiting for timed flush..." << std::endl;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs + 200));

            {
                std::lock_guard<std::mutex> lk(coutMutex);
                std::cout << "[Test] Time-based auto-flush test completed." << std::endl;
            }

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
            long timestamp,
            long seqNum,
            int status,
            const std::string& message) override {
            std::lock_guard<std::mutex> lk(coutMutex);
            std::cout << "[Listener] MessageReceived: seq=" << seqNum
                << " status=" << status
                << " msg=\"" << message << "\"" << std::endl;
        }

        void onCommand(AmiClient*,
            const std::string&,
            const std::string&,
            const std::string&,
            const std::string&,
            const std::string&,
            const std::map<std::string, AmiValue>&) override {
            // Not used in this test
        }
    };

}  // namespace ami

int main(int argc, char* argv[]) {
    using namespace ami;

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

    int opts = AmiClient::ENABLE_AUTO_PROCESS_INCOMING |
        AmiClient::ENABLE_AUTO_FLUSH_OUTGOING |
        AmiClient::ENABLE_SEND_TIMESTAMPS |
        AmiClient::ENABLE_SEND_SEQNUM;

    if (!client->start(host, port, loginId, opts)) {
        std::cerr << "[Main] Failed to start AmiClient." << std::endl;
        return 1;
    }

    while (client->isConnected()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "[Main] Test finished, exiting." << std::endl;
    return 0;
}
