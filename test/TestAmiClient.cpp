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
        std::lock_guard<std::mutex> lk(coutMutex);
        std::cout << "[Listener] Connected to server." << std::endl;
        // 登录已在 start() 内完成，可在此发送第一条业务消息
        std::thread([client]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            /*client->
                startObjectMessage("demoType", "demoId2")
                .addMessageParamString("name", "Bob")
                .addMessageParamInt("age", 29)
                .sendMessageAndFlush()
                .startCommandDefinition("demoCommand")
                .addMessageParamString("description", "This is a demo command")
                .sendMessageAndFlush();*/

            AmiClientCommandDef def("sample_cmd_def");
            def.setConditions({ AmiClientCommandDef::CONDITION_USER_CLICK })
                .setName("ClickCommand")
                .setHelp("Triggers on user click")
                .setPriority(5);

            client->sendCommandDefinition(def);
      
                

            }).detach();

        
    }

    void onLoggedIn(AmiClient* client) override {
        std::lock_guard<std::mutex> lk(coutMutex);
        std::cout << "[Listener] Logged in successfully." << std::endl;
        // 登录后发送一条对象创建示例
        /*std::thread([client]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            client->
                startObjectMessage("demoType", "demoId")
                .addMessageParamString("name", "Alice")
                .addMessageParamInt("age", 30)
                .sendMessageAndFlush();
            }).detach();*/
    }

    void onDisconnect(AmiClient* client) override {
        std::lock_guard<std::mutex> lk(coutMutex);
        std::cout << "[Listener] Disconnected from server." << std::endl;
    }

    void onMessageReceived(AmiClient* client,
        long long timestamp,
        long long seqNum,
        int status,
        const std::string& message) override {
        std::lock_guard<std::mutex> lk(coutMutex);
        // 打印简要信息
        std::cout << "[Listener] MessageReceived: seq=" << seqNum
            << " status=" << status
            << " msg=\"" << message << "\"" << std::endl;
    }

    void onMessageSent(AmiClient* client,
        const std::string& message) override {
        std::lock_guard<std::mutex> lk(coutMutex);
        std::cout << "[Listener] MessageSent: \"" << message << "\"" << std::endl;
    }

    void MyAmiListener::onCommand(AmiClient* source,
        const std::string& requestId,
        const std::string& cmd,
        const std::string& userName,
        const std::string& objectType,
        const std::string& objectId,
        const std::map<std::string, AmiValue>& params)
    {
        // 1. 简要日志
      /*  {
            std::lock_guard<std::mutex> lk(coutMutex);
            std::cout << "[Listener] Command received:"<< std::endl;
        }*/

        // 2. 打印所有参数
        /*for (const auto& [k, v] : params) {
            std::lock_guard<std::mutex> lk(coutMutex);
            printAmiValue(k, v);*/
        //}

        // 3. 业务处理完成后，立即发回 Response
        source
            ->startResponseMessage(requestId, /*status=*/0, /*message=*/"Processed")
            .addMessageParamLong("callback_code", 123)
            .sendMessageAndFlush();
    }

};

int main(int argc, char* argv[]) {
    std::string host = AmiClient::DEFAULT_HOST;
    int port = AmiClient::DEFAULT_PORT;
    std::string loginId = "demo";
    bool autoFlush = true;

    if (argc > 1) host = argv[1];
    if (argc > 2) port = std::stoi(argv[2]);
    if (argc > 3) loginId = argv[3];

    auto client = AmiClient::create(); // Use the factory method
    auto listener = std::make_shared<MyAmiListener>();
    client->addListener(listener);

    {
        std::lock_guard<std::mutex> lk(coutMutex);
        std::cout << "[Main] Starting AmiClient to " << host << ":" << port
            << " with loginId=\"" << loginId << "\"..." << std::endl;
    }

    if (!client->start(host, port, loginId, autoFlush)) {
        std::cerr << "[Main] Failed to start AmiClient." << std::endl;
        return 1;
    }

    while (client->isConnected()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "[Main] Exiting test." << std::endl;
    return 0;
}