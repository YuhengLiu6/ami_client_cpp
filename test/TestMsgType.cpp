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

class MyAmiListener : public AmiClientListener {
public:
    void onConnect(AmiClient* client) override {
        std::cout << "[Listener] Connected to server." << std::endl;
    }

    void onLoggedIn(AmiClient* client) override {
        std::cout << "[Listener] Logged in. Sending test messages..." << std::endl;

        // 1) Send an object creation message
        
        client->startObjectMessage("cmdtest", "test1")
            .addMessageParamString("name", "jack")
            .addMessageParamInt("number", 3)
            .sendMessageAndFlush();

        client->startObjectMessage("cmdtest", "test2")
            .addMessageParamString("name", "mike")
            .addMessageParamInt("number", 4)
            .sendMessageAndFlush();

        // 2) Send a delete message
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        client->startDeleteMessage("cmdtest", "test1")
            .sendMessageAndFlush();

        // 3) Send a command definition
        client->startCommandDefinition("bst")
            .addMessageParamString("N", "2nd Bust Every Order")
            .addMessageParamString("H", "busts all orders")
            .addMessageParamInt("L", 2)
            .sendMessageAndFlush();
    }

    void onDisconnect(AmiClient* /*client*/) override {
        std::cout << "[Listener] Disconnected from server." << std::endl;
    }

    void onMessageReceived(AmiClient* /*client*/,
        long ts,
        long seq,
        int status,
        const std::string& message) override {
        std::cout << "[Listener] MessageReceived: seq=" << seq
            << " status=" << status
            << " msg=\"" << message << "\"" << std::endl;
    }

    void onMessageSent(AmiClient* /*client*/, const std::string& msg) override {
        std::cout << "[Listener] MessageSent: " << msg;
    }

    void onCommand(AmiClient* source,
        const std::string& requestId,
        const std::string& cmd,
        const std::string& userName,
        const std::string& objectType,
        const std::string& objectId,
        const std::map<std::string, AmiValue>& params) override {
  
        std::cout << "[Listener] Command received: id=" << requestId << " cmd=" << cmd << std::endl;

        source->startResponseMessage(requestId, 0, "Processed")
            .addMessageParamLong("callback_code", 123)
            .sendMessageAndFlush();
    }
};

int main(int argc, char* argv[]) {
    std::string host = AmiClient::DEFAULT_HOST;
    int port = AmiClient::DEFAULT_PORT;
    std::string loginId = "demo";

    if (argc > 1) host = argv[1];
    if (argc > 2) port = std::stoi(argv[2]);
    if (argc > 3) loginId = argv[3];

    std::string server_certificate_public_key_file = (argc > 4) ? argv[4] : "";
    std::string client_certificate_public_key_file = (argc > 5) ? argv[5] : "";
    std::string client_certificate_private_key_file = (argc > 6) ? argv[6] : "";


    auto client = AmiClient::create();
    auto listener = std::make_shared<MyAmiListener>();
    client->addListener(listener);

    {
        std::lock_guard<std::mutex> lk(coutMutex);
        std::cout << "[Main] Starting AmiClient to " << host << ":" << port
            << " with loginId=\"" << loginId << "\"..." << std::endl;
    }


    int opts = AmiClient::ENABLE_AUTO_PROCESS_INCOMING
        | AmiClient::ENABLE_AUTO_FLUSH_OUTGOING
        | AmiClient::ENABLE_SEND_SEQNUM
        | AmiClient::ENABLE_SEND_TIMESTAMPS;

    if (!client->start(
                host,
                port,
                loginId,
                opts,
                server_certificate_public_key_file,
                client_certificate_public_key_file,
                client_certificate_private_key_file)) {
        std::cerr << "[Main] Failed to start AmiClient." << std::endl;
        return 1;
    }


    while (client->isConnected()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "[Main] Exiting test." << std::endl;
    return 0;
}
