// test/UnitTest.cpp

#include <gtest/gtest.h>
#include <AmiClientCpp/RawAmiClient.hpp>
#include <nlohmann/json.hpp>
#include <boost/asio.hpp>
#include <thread>
#include <chrono>

using json = nlohmann::json;

// Fake AMI server
static constexpr int TEST_PORT = 15000;
static constexpr char const* TEST_HOST = "127.0.0.1";

void runFakeAmiServerOnce() {
    using boost::asio::ip::tcp;
    boost::asio::io_context ioctx;
    tcp::acceptor acceptor(ioctx, tcp::endpoint(tcp::v4(), TEST_PORT));
    tcp::socket sock(ioctx);
    acceptor.accept(sock);


    long ts = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::string welcome =
        "M@" + std::to_string(ts) +
        "|Q=0|S=0|M=\"Welcome to Fake AMI\"\n";
    boost::asio::write(sock, boost::asio::buffer(welcome));


    boost::asio::streambuf buf;
    boost::asio::read_until(sock, buf, '\n');



    std::string ack =
        "M@" + std::to_string(ts + 1) +
        "|Q=1|S=0|M=\"OK Logged In\"\n";
    boost::asio::write(sock, boost::asio::buffer(ack));
    sock.close();
}

namespace ami {

    class TestableRawAmiClient : public RawAmiClient {
    public:
        using RawAmiClient::processIncoming;
        using RawAmiClient::parseIncomingParams;
        using RawAmiClient::readUntilSkipEscaped;
        using RawAmiClient::resetSeqNum;
        using RawAmiClient::setAutoFlushBufferSizeThreshold;
        using RawAmiClient::getAutoFlushBufferSizeThreshold;
        using RawAmiClient::setAutoFlushBufferMillis;
        using RawAmiClient::getAutoFlushBufferMillis;
        using RawAmiClient::getNow;
        using RawAmiClient::startMessage;
        using RawAmiClient::resetMessage;
        using RawAmiClient::getOutputBuffer;
        using RawAmiClient::addMessageParamString;
        using RawAmiClient::addMessageParamInt;
        using RawAmiClient::addMessageParamLong;
        using RawAmiClient::addMessageParamDouble;
        using RawAmiClient::addMessageParamBoolean;
        using RawAmiClient::addMessageParamEnum;
        using RawAmiClient::addMessageParamBinary;
    };
}

// ----------------- Non-connection Test -----------------
TEST(RawAmiClientProcessIncoming, EmptyString) {
    ami::TestableRawAmiClient c;
    EXPECT_EQ(c.processIncoming(""), "Invalid header");
}

TEST(RawAmiClientProcessIncoming, MissingPipeAfterTimestamp) {
    ami::TestableRawAmiClient c;
    EXPECT_EQ(c.processIncoming("M@123Q=1|S=0|M=\"msg\""),
        "Missing | after timestamp");
}

TEST(RawAmiClientProcessIncoming, InvalidTimestamp) {
    ami::TestableRawAmiClient c;
    EXPECT_EQ(c.processIncoming("M@abc|Q=1|S=0|M=\"msg\""),
        "Invalid timestamp");
}

TEST(RawAmiClientProcessIncoming, ValidMessage) {
    ami::TestableRawAmiClient c;
    EXPECT_EQ(c.processIncoming("M@123|Q=1|S=0|M=\"hello\""), "");
}

TEST(RawAmiClientProcessIncoming, ValidCommand) {
    ami::TestableRawAmiClient c;
    EXPECT_EQ(c.processIncoming(R"(E@456|I="req1"|U="usr"|C="cmd"|T="type"|O="obj")"),
        "");
}

TEST(RawAmiClientProcessIncoming, UnknownTypeHandledGracefully) {
    ami::TestableRawAmiClient c;
    std::string unknown = "Z@123|Q=1|S=0|M=\"unknown\"";
    EXPECT_EQ(c.processIncoming(unknown), "");
}

TEST(RawAmiClientParseParams, MixedTypes) {
    ami::TestableRawAmiClient c;

    std::string s = R"(A="foo"|B="{\"bar\":2}"J|C="AQID"U|D=123L|E=4.56D|F=true|G=false|H=null)";

    size_t pos = 0;
    std::map<std::string, ami::AmiValue> out;
    c.parseIncomingParams(s, pos, out);

    ASSERT_EQ(out.size(), 8u);
    EXPECT_EQ(std::get<std::string>(out["A"]), "foo");


    ASSERT_TRUE(std::holds_alternative<json>(out["B"]));
    EXPECT_EQ(std::get<json>(out["B"])["bar"], 2);

    // long/ double / bool / null
    EXPECT_EQ(std::get<long>(out["D"]), 123L);
    EXPECT_DOUBLE_EQ(std::get<double>(out["E"]), 4.56);
    EXPECT_TRUE(std::get<bool>(out["F"]));
    EXPECT_FALSE(std::get<bool>(out["G"]));
    EXPECT_TRUE(std::holds_alternative<std::nullptr_t>(out["H"]));
}

TEST(RawAmiClientEscape, BasicEscapes) {
    ami::TestableRawAmiClient c;
    std::string input = "a\\)b)";
    size_t pos = 0;
    std::string out;
    bool ok = c.readUntilSkipEscaped(input, pos, ')', out);

    EXPECT_TRUE(ok);
    EXPECT_EQ(out, "a)b");
}

// --------------- ConnectionTest ---------------
class RawAmiClientConnectTest : public ::testing::Test {
protected:
    std::thread serverThread;
    void SetUp() override {
        serverThread = std::thread(runFakeAmiServerOnce);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    void TearDown() override {
        if (serverThread.joinable())
            serverThread.join();
    }
};

TEST_F(RawAmiClientConnectTest, SeqNumIncrement) {
    ami::TestableRawAmiClient c;
    ASSERT_TRUE(c.connect(TEST_HOST, TEST_PORT, false, false, {}, {}))
        << "connect() failed";
    ASSERT_TRUE(c.pumpIncomingEvent()) << "no welcome message";

    EXPECT_EQ(c.resetSeqNum(10), 0L);

    c.startMessage('X', true, false);
    EXPECT_TRUE(c.getOutputBuffer().rfind("X#10", 0) == 0);

    c.startMessage('Y', true, false);
    EXPECT_TRUE(c.getOutputBuffer().rfind("Y#11", 0) == 0);
}

TEST_F(RawAmiClientConnectTest, IncludeNow) {
    ami::TestableRawAmiClient c;
    ASSERT_TRUE(c.connect(TEST_HOST, TEST_PORT, false, false, {}, {}));
    ASSERT_TRUE(c.pumpIncomingEvent());

    c.resetSeqNum(0);
    c.startMessage('Z', false, true);
    std::string buf = c.getOutputBuffer();
    EXPECT_EQ(buf[0], 'Z');
    EXPECT_NE(buf.find('@'), std::string::npos);
}

TEST_F(RawAmiClientConnectTest, BuildMessageWithConnection) {
    ami::TestableRawAmiClient c;
    ASSERT_TRUE(c.connect(TEST_HOST, TEST_PORT, false, false, {}, {}));
    ASSERT_TRUE(c.pumpIncomingEvent());

    c.resetMessage();
    c.startMessage('O', false, false);
    c.addMessageParamString("S", "hello");
    c.addMessageParamInt("I", 42);
    c.addMessageParamLong("L", 1234567890123L);
    c.addMessageParamDouble("D", 3.1415);
    c.addMessageParamBoolean("B", true);
    c.addMessageParamEnum("E", "enumVal");
    std::vector<uint8_t> bin = { 0xDE,0xAD,0xBE,0xEF };
    c.addMessageParamBinary("X", bin);

    std::string out = c.getOutputBuffer();
    EXPECT_NE(out.find(R"(|S="hello")"), std::string::npos);
    EXPECT_NE(out.find("|I=42"), std::string::npos);
    EXPECT_NE(out.find("L=1234567890123L"), std::string::npos);
    EXPECT_NE(out.find("D=3.1415"), std::string::npos);
    EXPECT_NE(out.find("B=true"), std::string::npos);
    EXPECT_NE(out.find("E='enumVal'"), std::string::npos);
    EXPECT_NE(out.find(R"(|X="3q2+7w=="U)"), std::string::npos);
}

// --------------- AutoFlushSetting ---------------
TEST(RawAmiClientAccessors, AutoFlushSettings) {
    ami::TestableRawAmiClient c;
    c.setAutoFlushBufferSizeThreshold(999);
    EXPECT_EQ(c.getAutoFlushBufferSizeThreshold(), 999u);

    c.setAutoFlushBufferMillis(12345);
    EXPECT_EQ(c.getAutoFlushBufferMillis(), 12345);
}

TEST(RawAmiClientGetNow, ReturnsPositive) {
    ami::TestableRawAmiClient c;
    EXPECT_GT(c.getNow(), 0);
}
