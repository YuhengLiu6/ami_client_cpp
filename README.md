# C++ AMI Client Connection Guide

##  Connection Setup

1. **Specify different login ID for each client**  
2. **Declare client instance and listener instance** to handle callbacks  
3. **Set options:**
 `ENABLE_AUTO_PROCESS_INCOMING` | By default, this client will automatically read inbound messages and process them in a separate thread. If disabled, you must manually call `pumpIncomingEvent()`. 
 `ENABLE_QUIET`                 | Same as setting `O="QUIET"`, which tells the AMI relay not to send message acks back to this client. 
 `DISABLE_AUTO_RECONNECT`      | By default, this client will keep trying to reconnect to the AMI server. See `setAutoReconnectFrequencyMs(long)` for details. 
 `ENABLE_SEND_TIMESTAMPS`      | Should this client send timestamps. Useful for enabling delayed message detection from the AMI client. 
 `ENABLE_SEND_SEQNUM`          | Should this client send sequence numbers. Useful for linking a particular client message to the message in AMI server. 
 `LOG_CONNECTION_RETRY_ERRORS` | Should this client log errors each time a connection retry fails. If not set, only logs on the first connection failure. 
 `LOG_MESSAGES`                | If set, all messages will be logged using the standard `java.util.logging.Logger` framework. 
 `ENABLE_AUTO_FLUSH_OUTGOING`  | If set, a separate thread is started which will automatically flush messages as they are written. 
  ```cpp
  int opts = AmiClient::ENABLE_AUTO_PROCESS_INCOMING
  | AmiClient::ENABLE_AUTO_FLUSH_OUTGOING
  | AmiClient::ENABLE_SEND_SEQNUM
  | AmiClient::ENABLE_SEND_TIMESTAMPS;

  if (!client->start(host, port, loginId, opts)) {
      std::cerr << "[Main] Failed to start AmiClient." << std::endl;
      return 1;
  }
```

4. **Keep main thread alive**

##  Listener Callbacks

1. `onConnect`  
2. `onLoggedIn`  
3. `onMessageSent`  
4. `onMessageReceived`  
5. `onDisconnect`  
6. `onCommand`

##  AutoFlush Modes

### 1. Buffer Size Mode
```cpp
size_t threshold = 100;
client->setAutoFlushBufferSizeThreshold(threshold);
```

### 2. Time Interval Mode
```cpp
client->setAutoFlushBufferSizeThreshold(0);
long intervalMs = 5000;
client->setAutoFlushBufferMillis(intervalMs);
```

##  Core Functions

### `startReader()`
Used to manually pump incoming events asynchronously. Each time, it reads data from the socket until a newline (`\n`) is found. Then, it calls `processIncoming()` to handle the message.  
**Used within** `RawAmiClient::connect()` method.

### `pumpIncomingEvent()`
Synchronously and manually pumps one incoming event.  
It reads a single AMI protocol line from the socket, parses it, and dispatches it using `processIncoming()`.  
**Used within** `AmiClient::runnerLoop()` when `ENABLE_AUTO_PROCESS_INCOMING` is not set.

### `processIncoming()`
Detects login success messages and triggers `onLoggedIn()` if applicable.  
Parses the structure of each received message based on its type and dispatches to the appropriate listener methods.

### `readUntilSkipEscaped()`
Parses a quoted string from input, starting at `pos` and ending at `endChar`, handling escape sequences such as `\\`, `\"`, `\n`, `\uXXXX`.

##  Message Sending and Flushing

### `sendMessage()`
Appends the current message to the batch buffer.  
If autoflush is on:
- In **buffer size mode**, it flushes immediately if the buffer exceeds the threshold.
- In **time interval mode**, it notifies the flush thread in `autoFlushLoop()` to continue processing.

### `sendMessage(msg, flush)`
Fast-send API.  
- If `flush` is `true`: send the message immediately.  
- Otherwise: buffer it and let `autoFlushLoop()` process it if autoflush is enabled.

### `flush(clearAfterSend)`
- **If `clearAfterSend == true`**: immediately send and clear the buffer, ignoring autoflush settings.
- **If `clearAfterSend == false`**:
  - If not in auto flush mode: flush immediately.
  - If in auto flush mode: notify autoflush thread.

### `sendMessageAndFlush()`
Appends the message to the batch buffer, immediately writes to the socket, and clears the batch buffer.  
Temporary output buffer is cleared in `startMessage()`.

### `autoFlushLoop()`
If autoflush is enabled, this loop automatically flushes outgoing messages based on either:
- time intervals, or
- buffer size thresholds.

##  Background Logic

### `runnerLoop()`
The main background thread loop responsible for maintaining the connection.

- If the connection is lost and `autoReconnect_` is enabled: it will **automatically reconnect**.
- If `autoProcessIncoming_` is enabled: it will **continuously pump** incoming events using `pumpIncomingEvent()` until the client is disconnected or stopped.


# AMI Client Test 
#### All test files are in /src folder.

##  TestAmiClient

This file tests the end-to-end functionality of the `AmiClient` by:
- Connecting to a server  
- Sending object messages and command definitions  
- Verifying message send/receive  
- Handling listener callbacks correctly


```cpp
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
```

---

##  TestAutoFlush

This file tests the **auto-flush functionality** of `AmiClient`, including:
- **Buffer-size-based** auto-flushing
- **Time-interval-based** auto-flushing  
It does so by sending batched messages and verifying whether flushing occurs under the correct conditions.

```cpp
void onLoggedIn(AmiClient* client) override {
    {
        std::lock_guard<std::mutex> lk(coutMutex);
        std::cout << "[Listener] Logged in, starting auto-flush tests..." << std::endl;
    }
    // ------- 1) buffer size test -------
    size_t threshold = 100;
    client->setAutoFlushBufferSizeThreshold(threshold);
    {
        std::lock_guard<std::mutex> lk(coutMutex);
        std::cout << "[Test] Buffer-size threshold = " << threshold << " bytes" << std::endl;
    }

    for (int i = 1; i <= 30; ++i) {
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

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // ------- 2) time invertal test -------
    client->setAutoFlushBufferSizeThreshold(0);  
    long intervalMs = 5000;
    client->setAutoFlushBufferMillis(intervalMs);
    std::cout << "[Test] Time-based auto-flush interval = " << intervalMs << " ms" << std::endl;


    client->
        startObjectMessage("TestType", "timeMsg")
        .addMessageParamString("payload", "time-test")
        .sendMessage();  

    std::cout << "[Test] Buffered one message, waiting for timed flush..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs + 200));

    std::cout << "[Test] Time-based auto-flush test completed." << std::endl;


    client->close();
}
```

---

##  TestMsgType

This file tests the ability of `AmiClient` to:
- Send and receive various **message types**  
  - Object creation  
  - Object deletion  
  - Command definition  
- Handle **incoming commands** via listener callbacks

```cpp
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
```
---

##  TestMultiClient

This file tests **multiple concurrent `AmiClient` instances**, each using a unique login ID.  
It verifies their ability to:
- Connect independently  
- Log in  
- Send messages  
- Receive responses  
All within **parallel threads**, simulating real-world concurrent usage.

```cpp
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

int main(int argc, char* argv[]) {
    std::string host = AmiClient::DEFAULT_HOST;
    int port = AmiClient::DEFAULT_PORT;
    std::string loginBase = "demo";

    if (argc > 1) host = argv[1];
    if (argc > 2) port = std::stoi(argv[2]);
    if (argc > 3) loginBase = argv[3];

    const int opts = AmiClient::ENABLE_AUTO_PROCESS_INCOMING
        | AmiClient::ENABLE_AUTO_FLUSH_OUTGOING
        | AmiClient::ENABLE_SEND_SEQNUM
        | AmiClient::ENABLE_SEND_TIMESTAMPS;

    std::vector<std::thread> threads;
    for (int id = 1; id <= 3; ++id) {
        threads.emplace_back([=]() {
            auto client = AmiClient::create();
            auto listener = std::make_shared<MultiClientListener>(id);
            client->addListener(listener);

            {
                std::lock_guard<std::mutex> lk(coutMutex);
                std::cout << "[Main] Starting client " << id
                    << " (loginId='" << loginBase + std::to_string(id)
                    << "')..." << std::endl;
            }

            if (!client->start(host, port, loginBase + std::to_string(id), opts)) {
                std::lock_guard<std::mutex> lk(coutMutex);
                std::cerr << "[Main] Client " << id << " failed to start." << std::endl;
                return;
            }

 
            while (client->isConnected()) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            });
    }


    for (auto& t : threads) t.join();

    std::cout << "[Main] All clients have disconnected. Exiting." << std::endl;
    return 0;
}
```

<!-- TestAmiClient: This file tests the end-to-end functionality of the AmiClient by connecting to a server, sending object messages and command definitions, and verifying message send/receive and callback handling.
TestAutoFlush: This file tests the auto-flush functionality of AmiClient, including both buffer-size-based and time-interval-based auto-flushing modes by sending batched messages and verifying their flushing behavior.
TestMsgType: This file tests the ability of AmiClient to send and receive various message types—including object creation, deletion, and command definition—and handle incoming commands via listener callbacks.
TestMultiClient: This file tests multiple concurrent AmiClient instances, each with a unique login ID, by verifying their ability to independently connect, log in, send messages, and receive responses in parallel threads. -->
