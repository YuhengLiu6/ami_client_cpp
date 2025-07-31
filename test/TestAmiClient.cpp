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

        std::cout << "[Listener] Logged in successfully." << std::endl;
        int ctr = 0;
        while (ctr < 20) {
            client->startObjectMessage("clienttest", "1")
                .addMessageParamString("I", "bst_"+ std::to_string(ctr))
                .addMessageParamString("name", "From_C++" + std::to_string(ctr))
                .addMessageParamInt("age", ctr)
                .sendMessageAndFlush();
            ctr++;

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }



        AmiClientCommandDef def("sample_cmd_def");
        def.setConditions({ AmiClientCommandDef::CONDITION_USER_CLICK })
            .setName("ClickCommand")
            .setHelp("Triggers on user click")
            .setPriority(5);

        client->sendCommandDefinition(def);
    }

    void onDisconnect(AmiClient* client) override {

        std::cout << "[Listener] Disconnected from server." << std::endl;
    }

    void onMessageReceived(AmiClient* client,
        long timestamp,
        long seqNum,
        int status,
        const std::string& message) override {

 
        std::cout << "[Listener] MessageReceived: seq=" << seqNum
            << " status=" << status
            << " msg=\"" << message << "\"" << std::endl;
    }

    void onMessageSent(AmiClient* client,
        const std::string& message) override {

        std::cout << "[Listener] MessageSent:" << message << std::endl;
    }

    void MyAmiListener::onCommand(AmiClient* source,
        const std::string& requestId,
        const std::string& cmd,
        const std::string& userName,
        const std::string& objectType,
        const std::string& objectId,
        const std::map<std::string, AmiValue>& params)
    {
        std::cout << "[Listener] Command received:" << std::endl;

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
    bool autoFlush = false;

    if (argc > 1) host = argv[1];
    if (argc > 2) port = std::stoi(argv[2]);
    if (argc > 3) loginId = argv[3];

    auto client = AmiClient::create(); 
    auto listener = std::make_shared<MyAmiListener>();
    client->addListener(listener);

    {
        std::lock_guard<std::mutex> lk(coutMutex);
        std::cout << "[Main] Starting AmiClient to " << host << ":" << port
            << " with loginId=\"" << loginId << "\"..." << std::endl;
    }

    int opts = AmiClient::ENABLE_AUTO_PROCESS_INCOMING ;
    //int opts = AmiClient::ENABLE_AUTO_PROCESS_INCOMING | AmiClient::LOG_MESSAGES;
    

    if (!client->start(host, port, loginId, opts)) {
        std::cerr << "[Main] Failed to start AmiClient." << std::endl;
        return 1;
    }

    

    while (client->isConnected()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "[Main] Exiting test." << std::endl;
    return 0;
}