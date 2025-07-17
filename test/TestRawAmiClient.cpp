// TestRawAmiClient.cpp
#include "../src/RawAmiClient.hpp"
#include "../src/RawAmiClientListener.hpp"

#include <iostream>
#include <memory>
#include <thread>
#include <chrono>

class MyListener : public RawAmiClientListener {
public:
    //void onConnect(RawAmiClient* client) override {
    //    std::cout << "[Listener] Connected to server." << std::endl;
    //    // 如果协议需要登录，可以在此处发送登录消息
    //     //client->sendMessage("L|I=\"demo\"", true);
    //    //const std::string msg = R"(O|I="test1"|T="RawClient"|name="michael"|number=1)";

    //    //client->sendMessage(msg, true);

    //    const std::string loginId = "demo";  // 建议用 UUID 或类似唯一标识
    //    std::string loginMsg = "L|I=\"" + loginId + "\"";
    //    client->sendMessage(loginMsg, true);
    //}

    void onConnect(RawAmiClient* client) override {
        std::cout << "[Listener] Connected to server." << std::endl;

        std::thread([client]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10)); // 稍微让出线程
            client->sendMessage("L|I=\"demo\"", true);
            const std::string msg = R"(O|I="test1"|T="rawclient"|name="michael"|number=1)";

            client->sendMessage(msg, true);
            }).detach();
    }

    void onDisconnect(RawAmiClient* client) override {
        std::cout << "[Listener] Disconnected from server." << std::endl;
    }

    void onMessageReceived(RawAmiClient* client,
        long ts,
        long seqNum,
        int status,
        const std::string& message) override
    {
        std::cout << "[Listener] MessageReceived: ts=" << ts
            << " seq=" << seqNum
            << " status=" << status
            << " msg=\"" << message << "\"" << std::endl;
    }

    void onMessageSent(RawAmiClient* client,
        const std::string& message) override
    {
        std::cout << "[Listener] MessageSent: \"" << message << "\"" << std::endl;
    }

    void onCommand(RawAmiClient* client,
        const std::string& requestId,
        const std::string& cmd,
        const std::string& userName,
        const std::string& objectType,
        const std::string& objectId,
        const std::map<std::string, std::string>& params) override
    {
        std::cout << "[Listener] Command: id=" << requestId
            << " cmd=" << cmd
            << " user=" << userName
            << " type=" << objectType
            << " obj=" << objectId
            << " params={";
        for (auto& kv : params) {
            std::cout << kv.first << ":" << kv.second << ",";
        }
        std::cout << "}" << std::endl;
    }

    void onLoggedIn(RawAmiClient* client) override {
        std::cout << "[Listener] LoggedIn." << std::endl;
    }
};

int main(int argc, char* argv[]) {
    std::string host = RawAmiClient::DEFAULT_HOST;
    int port = RawAmiClient::DEFAULT_PORT;
    if (argc > 1) host = argv[1];
    if (argc > 2) port = std::stoi(argv[2]);

    auto client = std::make_shared<RawAmiClient>();
    auto listener = std::make_shared<MyListener>();
    client->addListener(listener);

    std::cout << "Connecting to " << host << ":" << port << " ..." << std::endl;
    if (!client->connect(host, port, true /*logError*/, false /*autoFlush*/)) {
        std::cerr << "Failed to connect." << std::endl;
        return 1;
    }

    // 维持主线程存活，等待异步回调
    // 你也可以在这里 sendMessage() 或 pumpIncomingEvent() 来驱动
    while (client->isConnected()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "Exiting test." << std::endl;
    return 0;
}
