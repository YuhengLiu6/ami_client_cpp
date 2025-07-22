// TestRawAmiClient.cpp
#include "../src/RawAmiClient.hpp"
#include "../src/RawAmiClientListener.hpp"
#include "../src/AmiTypes.hpp"
#include <variant>
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <iomanip>    // for std::setw
#include <exception>

class MyListener : public RawAmiClientListener {
public:

    void onLoggedIn(RawAmiClient* client) override {
        try {
            std::cout << "[Listener] LoggedIn." << std::endl;

            std::thread([client]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                //const std::string cmd1 =
                //    R"(C|I="bst"|N="2nd Bust Every Order"|H="busts all orders"|L=2)";
                //client->sendMessage(cmd1, /*autoFlush=*/true);

          
                //const std::string cmd2 =
                //    R"(O|I="test3"|T="rawclient"|name="jack"|number=3)";
                //client->sendMessage(cmd2, /*autoFlush=*/true);

                //  const std::string cmd3 =
                //    R"(D|I="test3"|T="rawclient")";
                //client->sendMessage(cmd3, /*autoFlush=*/true);



                client->startMessage('O', false, false)
                    .addMessageParamString("I", "test_chain")
                    .addMessageParamString("T", "rawclient")
                    .addMessageParamString("name", "superman")
                    .addMessageParamInt("number", 1)
                    //.sendMessageAndFlush();
                    .sendMessage();
                }).detach();  
        }
        catch (const std::exception& ex) {
            std::cerr << "[Exception@onLoggedIn] " << ex.what() << std::endl;
        }
    }

    void onConnect(RawAmiClient* client) override {
        try {
            std::cout << "[Listener] Connected to server." << std::endl;
            std::thread([client]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                client->sendMessage(R"(L|I="demo")", true);
            
                //client->sendMessage(R"(O|I="test1"|T="rawclient"|name="michael"|number=1)", true);

                }).detach();
        }
        catch (const std::exception& ex) {
            std::cerr << "[Exception@onConnect] " << ex.what() << std::endl;
        }
    }

    void onDisconnect(RawAmiClient* client) override {
        try {
            std::cout << "[Listener] Disconnected from server." << std::endl;
        }
        catch (const std::exception& ex) {
            std::cerr << "[Exception@onDisconnect] " << ex.what() << std::endl;
        }
    }

    /*void onMessageReceived(RawAmiClient* client, long ts, long seqNum, int status, const std::string& message) override {
        try {
            std::cout << "[Listener] MessageReceived: ts=" << ts
                << " seq=" << seqNum
                << " status=" << status
                << " msg=\"" << message << "\"" << std::endl;
        }
        catch (const std::exception& ex) {
            std::cerr << "[Exception@onMessageReceived] " << ex.what() << std::endl;
        }
    }*/

    void onMessageReceived(RawAmiClient* client,
        long long ts,
        long seqNum,
        int status,
        const std::string& message) override {
        try {
            // 1. 毫秒时间戳 → 本地可读时间
            auto tp = std::chrono::system_clock::time_point{ std::chrono::milliseconds(ts) };
            std::time_t tt = std::chrono::system_clock::to_time_t(tp);
            std::tm local_tm;
            #ifdef _WIN32
            localtime_s(&local_tm, &tt);
            #else
            localtime_r(&tt, &local_tm);
            #endif

            std::ostringstream timeBuf;
            timeBuf << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S");

            // 2. 基本信息
            std::cout << "[Listener] MessageReceived\n"
                << "  Timestamp: " << timeBuf.str()
                << "  (ms since epoch: " << ts << ")\n"
                << "  SeqNum:    " << seqNum << "\n"
                << "  Status:    " << status << "\n"
                << "  Length:    " << message.size() << " bytes\n";

            // 3. 原始内容
            std::cout << "  Raw Msg:   \"" << message << "\"\n";

            // 4. 可视化转义
            std::cout << "  Escaped:   \"";
            for (char c : message) {
                switch (c) {
                case '\n': std::cout << "\\n"; break;
                case '\r': std::cout << "\\r"; break;
                case '\t': std::cout << "\\t"; break;
                default:   std::cout << c;     break;
                }
            }
            std::cout << "\"\n";

            // 5. 如果像 JSON，就尝试解析并 pretty-print
            if (!message.empty() && message.front() == '{' && message.back() == '}') {
                try {
                    auto j = nlohmann::json::parse(message);
                    std::cout << "  Parsed JSON:\n"
                        << std::setw(4) << j << "\n";
                }
                catch (const std::exception& je) {
                    std::cout << "  [JSON parse error] " << je.what() << "\n";
                }
            }

            std::cout << std::endl;
        }
        catch (const std::exception& ex) {
            std::cerr << "[Exception@onMessageReceived] " << ex.what() << std::endl;
        }
    }


    void onMessageSent(RawAmiClient* client, const std::string& message) override {
        try {
            std::cout << "[Listener] MessageSent: \"" << message << "\"" << std::endl;
        }
        catch (const std::exception& ex) {
            std::cerr << "[Exception@onMessageSent] " << ex.what() << std::endl;
        }
    }

    void onCommand(RawAmiClient* client,
        const std::string& requestId,
        const std::string& cmd,
        const std::string& userName,
        const std::string& objectType,
        const std::string& objectId,
        const std::map<std::string, AmiValue>& params) override {
        try {
            std::cout << "[Listener] Command:\n"
                << "  id=" << requestId << "\n"
                << "  cmd=" << cmd << "\n"
                << "  user=" << userName << "\n"
                << "  type=" << objectType << "\n"
                << "  obj=" << objectId << "\n"
                << "  params={\n";
            for (const auto& kv : params) {
                std::cout << "    " << std::setw(12) << kv.first << ": ";
                try {
                    std::visit([](auto&& val) {
                        using T = std::decay_t<decltype(val)>;
                        if constexpr (std::is_same_v<T, std::nullptr_t>) {
                            std::cout << "null";
                        }
                        else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
                            std::cout << "[binary " << val.size() << " bytes]";
                        }
                        else if constexpr (std::is_same_v<T, nlohmann::json>) {
                            std::cout << val.dump();
                        }
                        else {
                            std::cout << val;
                        }
                        }, kv.second);
                }
                catch (const std::exception& ex2) {
                    std::cout << "[Error during visit: " << ex2.what() << "]";
                }
                std::cout << "\n";
            }
            std::cout << "  }" << std::endl;
        }
        catch (const std::exception& ex) {
            std::cerr << "[Exception@onCommand] " << ex.what() << std::endl;
        }
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

    std::cout << "[Main] Connecting to " << host << ":" << port << " ..." << std::endl;

    try {
        if (!client->connect(host, port, true, false)) {
            std::cerr << "[Main] Failed to connect." << std::endl;
            return 1;
        }
    
        //std::cout << "This is cuurent connected_:  " << client->isConnected() << " before while loop" << std::endl;
        while (client->isConnected()) {
            //std::cout << "This is cuurent connected_:  " << client->isConnected() << " inside loop" << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

    }
    catch (const std::exception& ex) {
        std::cerr << "[Main Exception] " << ex.what() << std::endl;
        return 2;
    }

    std::cout << "[Main] Exiting test." << std::endl;
    return 0;
}
