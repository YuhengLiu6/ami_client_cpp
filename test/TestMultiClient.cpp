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
#include <vector>

namespace ami {
    extern std::mutex coutMutex;

    class MultiClientListener : public AmiClientListener {
    public:
        MultiClientListener(int id) : clientId(id) {}
        std::atomic<bool> ready{ false };
        void onConnect(AmiClient* client) override {
            std::lock_guard<std::mutex> lk(coutMutex);
            std::cout << "[Client " << clientId << "] Connected" << std::endl;
        }

        void onLoggedIn(AmiClient* client) override {
            ready = true;

            std::thread([client, this]() {
                for (int i = 1; i <= 10; ++i) {

                    if (!ready.load()) break;
                    client->startObjectMessage("MultiTest", "msg" + std::to_string(i))
                        .addMessageParamString("payload", "Hello")
                        .sendMessageAndFlush();
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                }).detach();
        }

        void onMessageSent(AmiClient* client, const std::string& message) override {
            std::lock_guard<std::mutex> lk(coutMutex);
            std::cout << "[Client " << clientId << "] Sent: " << message;
        }

        void onMessageReceived(AmiClient* client,
            long  timestamp,
            long  seqNum,
            int status,
            const std::string& message) override {
            std::lock_guard<std::mutex> lk(coutMutex);
            std::cout << "[Client " << clientId << "] Received: seq="
                << seqNum << " msg=\"" << message << "\"" << std::endl;
        }

        void onDisconnect(AmiClient* client) override {
            std::lock_guard<std::mutex> lk(coutMutex);
            std::cout << "[Client " << clientId << "] Disconnected" << std::endl;
        }

        void onCommand(AmiClient* source,
            const std::string& requestId,
            const std::string& cmd,
            const std::string& userName,
            const std::string& objectType,
            const std::string& objectId,
            const std::map<std::string, AmiValue>& params) override {

        }

    private:
        int clientId;
    };
}


int main(int argc, char* argv[]) {
    using namespace ami;

    std::string host = AmiClient::DEFAULT_HOST;
    int port = AmiClient::DEFAULT_PORT;
    std::string loginId = "demo";

    if (argc > 1) host = argv[1];
    if (argc > 2) port = std::stoi(argv[2]);
    if (argc > 3) loginId = argv[3];

    const int opts = AmiClient::ENABLE_AUTO_PROCESS_INCOMING |
        AmiClient::ENABLE_AUTO_FLUSH_OUTGOING |
        AmiClient::ENABLE_SEND_SEQNUM |
        AmiClient::ENABLE_SEND_TIMESTAMPS;

    std::vector<std::thread> threads;

    for (int id = 1; id <= 3; ++id) {
        threads.emplace_back([host, port, loginId, id, opts]() {
            auto client = AmiClient::create();
            auto listener = std::make_shared<MultiClientListener>(id);
            client->addListener(listener);

            {
                std::lock_guard<std::mutex> lk(coutMutex);
                std::cout << "[Main] Starting client " << id
                    << " (loginId='" << loginId + std::to_string(id) << "')..." << std::endl;
            }

            if (!client->start(host, port, loginId + std::to_string(id), opts)) {
                std::lock_guard<std::mutex> lk(coutMutex);
                std::cerr << "[Main] Client " << id << " failed to start." << std::endl;
                return;
            }

            while (client->isConnected()) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            });
    }

    for (auto& t : threads) {
        t.join();
    }

    std::cout << "[Main] All clients have disconnected. Exiting." << std::endl;
    return 0;
}