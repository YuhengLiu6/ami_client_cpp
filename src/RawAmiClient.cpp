// RawAmiClient.cpp
#include <AmiClientCpp/RawAmiClient.hpp>
#include <AmiClientCpp/RawAmiClientListener.hpp>
#include <AmiClientCpp/AmiTypes.hpp>

#include <boost/asio.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/asio/ssl/verify_mode.hpp>
#include <iostream>
#include <chrono>
#include <algorithm>
#include <openssl/bio.h>
#include <sstream>
#include <cctype>
#include <map>
#include <variant>
#include <string>
#include <optional>
#include <vector>
#include <stdexcept>
#include <memory>
#include <iomanip>  
#include <bitset>
#include <nlohmann/json.hpp> 
#include <boost/beast/core/detail/base64.hpp>
#include <boost/bind/bind.hpp>
#include <cppcodec/base64_rfc4648.hpp>
#include <openssl/pkcs12.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <stdio.h>

namespace ami {

bool TcpSocket::connect(const std::string & host, const std::string & port)
{
    boost::asio::ip::tcp::resolver resolver(io_context_);
    auto endpoints = resolver.resolve(host, port);
    boost::asio::connect(socket_, endpoints);
    return true;
}

void TcpSocket::disconnect()
{
    if (socket_.is_open()) {
        boost::system::error_code ec;
        socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        socket_.close(ec);
    }
}

void TcpSocket::send_message(const std::string & message)
{
    if (socket_.is_open()) {
        boost::asio::write(socket_, boost::asio::buffer(message));
    }
}

std::size_t TcpSocket::read_until(boost::asio::streambuf & buf, const char delim)
{
    return boost::asio::read_until(socket_, buf, delim);
}

char TcpSocket::read_char(boost::system::error_code & ec)
{
    char c = 0;
    boost::asio::read(socket_, boost::asio::buffer(&c, 1), ec);
    return c;
}

SslSocket::~SslSocket()
{
    sk_X509_free(ca_);
}

SslSocket::SslSocket(
        boost::asio::io_context & io_context,
        std::string server_certificate_public_key_file,
        std::string client_certificate_public_key_file,
        std::string client_certificate_private_key_file)
    : super(io_context)
    , ssl_context_(boost::asio::ssl::context::tlsv12_client)
    , server_certificate_public_key_file_(std::move(server_certificate_public_key_file))
    , client_certificate_public_key_file_(std::move(client_certificate_public_key_file))
    , client_certificate_private_key_file_(std::move(client_certificate_private_key_file))
{
  if (!server_certificate_public_key_file.empty()) {
    boost::system::error_code ec;
    const auto r =
        ssl_context_.load_verify_file(server_certificate_public_key_file_, ec);
    if (r.failed()) {
      std::string error = "failed to load and verify kestore file: ";
      error.append(server_certificate_public_key_file_)
          .append(": ")
          .append(r.message());
      throw std::runtime_error(error);
    }
  }
}

SslSocket::SslSocket(
        boost::asio::io_context & io_context,
        std::string p12_keystore_file,
        std::string p12_keystore_pass)
    : super(io_context)
    , ssl_context_(boost::asio::ssl::context::tlsv12_client)
    , p12_keystore_file_(std::move(p12_keystore_file))
    , p12_keystore_pass_(std::move(p12_keystore_pass))
{
}

bool SslSocket::connect_using_pem_files(const std::string & host, const std::string & port)
{
    bool result = true;
    host_ = host; // keep for SSL verification

    if (!client_certificate_private_key_file_.empty()) {
        ssl_context_.use_certificate_file(client_certificate_public_key_file_, boost::asio::ssl::context::pem);
    }
    if (!client_certificate_private_key_file_.empty()) {
        ssl_context_.use_private_key_file(client_certificate_private_key_file_, boost::asio::ssl::context::pem);
    }
    ssl_context_.set_verify_mode(boost::asio::ssl::verify_peer);
    ssl_context_.set_verify_callback(
            boost::bind(&SslSocket::verify_certificate, this,
            boost::placeholders::_1, boost::placeholders::_2));

    ssl_socket_ = std::make_unique<ssl_socket>(io_context_, ssl_context_);

    boost::asio::ip::tcp::resolver resolver(io_context_);
    auto endpoints = resolver.resolve(host, port);
    boost::asio::connect(ssl_socket_->lowest_layer(), endpoints);
    try {
        ssl_socket_->handshake(boost::asio::ssl::stream_base::client);
        std::cout << "ssl: handshake performed successfully" << std::endl;
    } catch (boost::system::system_error & e) {
        std::cerr << "error in ssl handshake: " << e.what() << std::endl;
        result = false;
    }
    return result;
}

namespace {

struct BIOHelper
{
    ~BIOHelper()
    {
        BIO_free(bio_);
    }

    BIOHelper()
    {
        bio_ = BIO_new(BIO_s_mem());
        if (!bio_) {
            throw std::runtime_error("failed to create BIO");
        }
    }

    boost::asio::const_buffer write_to_asio_buffer(X509 * cert)
    {
        if (!cert) {
            return {};
        }
        write(cert);
        return write_bio();
    }

    boost::asio::const_buffer write_to_asio_buffer(EVP_PKEY * pkey)
    {
        if (!pkey) {
            return {};
        }
        write(pkey);
        return write_bio();
    }

    boost::asio::const_buffer write_to_asio_buffer(STACK_OF(X509) * ca)
    {
        if (!ca || sk_X509_num(ca) == 0) {
            return {};
        }
        for (int i = 0; i < sk_X509_num(ca); ++i) {
            X509 * cert = sk_X509_value(ca, i);
            write(cert);
        }
        return write_bio();
    }

private:
    void write(X509 * cert)
    {
        if (!PEM_write_bio_X509(bio_, cert)) {
            throw std::runtime_error("failed to write X509 certificate to BIO");
        }
    }

    void write(EVP_PKEY * pkey) {
        if (!PEM_write_bio_PrivateKey(bio_, pkey, nullptr, nullptr, 0, nullptr, nullptr)) {
            throw std::runtime_error("failed to write private key to BIO");
        }
    }

    boost::asio::const_buffer write_bio()
    {
        const auto sz = BIO_ctrl_pending(bio_);
        if (sz <= 0) {
            throw std::runtime_error("failed to get BIO size");
        }
        buf_ = std::move(std::vector<unsigned char>(sz));
        BIO_read(bio_, buf_.data(), sz);
        return {buf_.data(), buf_.size()};
    }

private:
    BIO * bio_ = nullptr;
    std::vector<unsigned char> buf_;
};

} // namespace

bool SslSocket::connect_using_p12(const std::string & host, const std::string & port)
{
    bool result = true;
    host_ = host; // kep for ssl verification

    EVP_PKEY * pkey = nullptr;
    X509 * cert = nullptr;
    PKCS12 * p12 = nullptr;

    try {
        if (p12_keystore_file_.empty()) {
            throw std::runtime_error("p12 keystore file name is empty");
        }

        FILE *fp = fopen(p12_keystore_file_.c_str(), "rb");
        if (!fp) {
            throw std::runtime_error("cannot open p12 keystore file");
        }

        p12 = d2i_PKCS12_fp(fp, nullptr);
        fclose(fp);
        if (!p12) {
            ERR_print_errors_fp(stderr);
            throw std::runtime_error("error loading PKCS#12 file");
        }

        // extract private key and certificates

        if (!PKCS12_parse(p12, p12_keystore_pass_.c_str(), &pkey, &cert, &ca_)) {
            ERR_print_errors_fp(stderr);
            throw std::runtime_error("failed to parse PKCS#12 file");
        }

        BIOHelper pkey_bio;
        auto pkey_asio_buffer = pkey_bio.write_to_asio_buffer(pkey);
        if (pkey_asio_buffer.size() > 0) {
            //std::cout << "******* Private Key:\n" << std::string_view((const char*)pkey_asio_buffer.data(), pkey_asio_buffer.size()) << std::endl;
            ssl_context_.use_private_key(pkey_asio_buffer, boost::asio::ssl::context::file_format::pem);
        }

        BIOHelper cert_bio;
        auto cert_asio_buffer = cert_bio.write_to_asio_buffer(cert);
        if (cert_asio_buffer.size() > 0) {
            //std::cout << "***** Certificate: \n" << std::string_view((const char *)cert_asio_buffer.data(), cert_asio_buffer.size()) << std::endl;
            ssl_context_.use_certificate(cert_asio_buffer, boost::asio::ssl::context::pem);
        }

        BIOHelper ca_bio;
        auto ca_asio_buffer = ca_bio.write_to_asio_buffer(ca_);
        if (ca_asio_buffer.size() > 0) {
            //std::cout << "********* certificate chain:\n" << std::string_view((const char *)ca_asio_buffer.data(), ca_asio_buffer.size()) << std::endl;
            ssl_context_.use_certificate_chain(ca_asio_buffer);
        }

        ssl_context_.set_verify_mode(boost::asio::ssl::verify_peer);
        ssl_context_.set_verify_callback(
        boost::bind(&SslSocket::verify_certificate, this,
                      boost::placeholders::_1, boost::placeholders::_2));

        ssl_socket_ = std::make_unique<ssl_socket>(io_context_, ssl_context_);

        boost::asio::ip::tcp::resolver resolver(io_context_);
        auto endpoints = resolver.resolve(host, port);
        boost::asio::connect(ssl_socket_->lowest_layer(), endpoints);
        ssl_socket_->handshake(boost::asio::ssl::stream_base::client);
    }
    catch (const std::exception &e) {
      std::cerr << "error setting up SSL context: " << e.what() << std::endl;
      result = false;
    }

    // clean up
    PKCS12_free(p12);
    X509_free(cert);
    EVP_PKEY_free(pkey);

    return result;
}

void SslSocket::disconnect()
{
    if (ssl_socket_ && ssl_socket_->lowest_layer().is_open()) {
        boost::system::error_code ec;
        ssl_socket_->lowest_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        ssl_socket_->lowest_layer().close(ec);
    }
}

void SslSocket::send_message(const std::string & message)
{
    if (ssl_socket_ && ssl_socket_->lowest_layer().is_open()) {
        boost::asio::write(*ssl_socket_, boost::asio::buffer(message));
    }
}

std::size_t SslSocket::read_until(boost::asio::streambuf & buf, const char delim)
{
    return boost::asio::read_until(*ssl_socket_, buf, delim);
}

char SslSocket::read_char(boost::system::error_code & ec)
{
    char c = 0;
    boost::asio::read(*ssl_socket_, boost::asio::buffer(&c, 1), ec);
    return c;
}

bool SslSocket::verify_certificate(
        const bool preverified,
        boost::asio::ssl::verify_context & ctx)
{
    if (preverified) {
        return true;
    }

    char subject_name[256];
    X509 * server_native_cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
    if (server_native_cert == nullptr) {
        std::cerr << "failed to get incoming server certificate while verifying certificate" << std::endl;
        return false;
    }
    X509_NAME_oneline(X509_get_subject_name(server_native_cert), subject_name, 256);
    std::cout << "Verifying certificate: " << subject_name << std::endl;

    const EVP_PKEY * server_pubkey = X509_get_pubkey(server_native_cert);
    if (!server_pubkey) {
        std::cerr << "failed to get server pubkey from server native certificate" << std::endl;
        return false;
    }

    SSL_CTX * native_ctx = ssl_context_.native_handle();
    if (native_ctx == nullptr) {
        std::cerr << "failed to get native ssl context from boost ssl context" << std::endl;
        return false;
    }

    // loop over all certificates chain

    for (int i = 0; i < sk_X509_num(ca_); ++i) {
        X509 * next_cert = sk_X509_value(ca_, i);
        if (next_cert) {
            EVP_PKEY * next_pubkey = X509_get_pubkey(next_cert);
            if (next_pubkey && EVP_PKEY_eq(server_pubkey, next_pubkey)) {
                std::cout << "found certificate successfully in verifying" << std::endl;
                return true;
            }
            std::cout << "pubkey " << i << " not matched" << std::endl;
        }
    }

    return false;
}

using namespace std::chrono;
std::mutex coutMutex;
std::mutex g_logMutex;
template<typename Method, typename... Args>
void RawAmiClient::notifyListeners(Method method, Args&&... args) {
    std::vector<std::shared_ptr<RawAmiClientListener>> tmp;
    {
        std::lock_guard<std::mutex> lock(listenersMutex_);
        tmp = listeners_;
    }
    for (auto& l : tmp) {
        (l.get()->*method)(std::forward<Args>(args)...);
    }
}


const std::string RawAmiClient::DEFAULT_HOST = "localhost";
const int RawAmiClient::DEFAULT_PORT = 3289;
using base64 = cppcodec::base64_rfc4648;
using json = nlohmann::json;

RawAmiClient::~RawAmiClient() {
    disconnect();
}

bool RawAmiClient::connect(
        const std::string& host,
        const int port,
        const bool /*logErrorOnRetries*/,
        const bool autoFlush,
        std::string p12_keystore_file,
        std::string p12_keystore_pass)
{
    if (connected_) throw std::runtime_error("Already connected");
    try {
        const bool isSsl = !p12_keystore_file.empty() || !p12_keystore_pass.empty();
        if(!isSsl) {
            socket_ = std::make_unique<TcpSocket>(ioCtx_);
        }
        else {
            socket_ = std::make_unique<SslSocket>(
                    ioCtx_,
                    p12_keystore_file,
                    p12_keystore_pass);
        }
        connected_ = socket_->connect(host, std::to_string(port));
        if (!connected_) {
            return false;
        }

        {
            std::lock_guard lk(coutMutex);
            std::cout << "[connect] Connected to " << host << ":" << port << std::endl;
        }

        autoFlush_ = autoFlush;
        fireConnect();

        if (autoFlush_) {
            stopAutoFlush_ = false;
            needsFlush_ = false;
            autoFlushThread_ = std::thread(&RawAmiClient::autoFlushLoop, this);
        }
        return true;
    }
    catch (const std::exception& e) {
        std::lock_guard lk(coutMutex);
        std::cerr << "[connect error] " << e.what() << std::endl;
        return false;
    }
}


void RawAmiClient::disconnect() {
    if (!connected_) return;

    {
        std::lock_guard lk(coutMutex);
        std::cout << "[disconnect] Shutting down..." << std::endl;
    }

    connected_ = false;
    stopAutoFlush_ = true;
    flushCv_.notify_all();

    socket_->disconnect();

    if (readerThread_.joinable())     readerThread_.join();
    if (autoFlushThread_.joinable())  autoFlushThread_.join();

    loggedIn_ = false;
    fireDisconnect();

    {
        std::lock_guard lk(coutMutex);
        std::cout << "[disconnect] Done." << std::endl;
    }
}


bool RawAmiClient::isConnected() const {
    return connected_;
}

long RawAmiClient::getNow() const {
    return duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
}


void RawAmiClient::startReader() {
    readerThread_ = std::thread([this]() {
        boost::asio::streambuf buf;
        while (connected_) {
            try {
                socket_->read_until(buf, '\n');
                std::istream is(&buf);
                std::string line;

                while (std::getline(is, line)) {
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();

                    size_t st = 0;
                    while (st < line.size() && std::isspace((unsigned char)line[st])) ++st;
                    while (!line.empty() && std::isspace((unsigned char)line.back())) line.pop_back();
                    if (st) line.erase(0, st);

                    if (line.empty()) continue;

                    auto err = processIncoming(line);

                    if (debug_) {
                        std::lock_guard lk(coutMutex);
                        if (err.empty())
                            std::cout << "[DEBUG-reader] OK: " << line << std::endl;
                        else
                            std::cerr << "[DEBUG-reader] WARN: " << err << "  line='" << line << "'" << std::endl;
                    }
                }
            }
            catch (const std::exception& e) {
                std::lock_guard lk(coutMutex);
                std::cerr << "[reader] Exception: " << e.what() << std::endl;
                break;
            }
        }
        });
}

void RawAmiClient::parseIncomingParams(
    const std::string& str, size_t pos,
    std::map<std::string, AmiValue>& out) {

    while (pos < str.size()) {
        if (str[pos] == '|') ++pos;

        size_t eq = str.find('=', pos);
        if (eq == std::string::npos) break;

        std::string key = str.substr(pos, eq - pos);
        pos = eq + 1;

        AmiValue val;


        if (str[pos] == '"') {
            ++pos;
            std::string quoted;
            if (!readUntilSkipEscaped(str, pos, '"', quoted)) break;

            if (pos < str.size()) {
                char suffix = str[pos];
                if (suffix == 'J') {
                    ++pos;
                    val = json::parse(quoted);
                }
                else if (suffix == 'U') {
                    ++pos;
                    std::string padded = quoted + std::string((4 - quoted.size() % 4) % 4, '=');
                    std::vector<uint8_t> decoded = base64::decode(padded);
                    val = decoded;
                }
                else {
                    val = quoted;
                }
            }
            else {
                val = quoted;
            }


        }
        else if (str[pos] == '\'') {
            ++pos;
            std::string quoted;
            if (!readUntilSkipEscaped(str, pos, '\'', quoted)) break;
            val = quoted;


        }
        else if (str.compare(pos, 4, "true") == 0) {
            val = true;
            pos += 4;
        }
        else if (str.compare(pos, 5, "false") == 0) {
            val = false;
            pos += 5;
        }
        else if (str.compare(pos, 4, "null") == 0) {
            val = nullptr;
            pos += 4;
        }
        else {
            size_t end = str.find('|', pos);
            if (end == std::string::npos) end = str.size();
            std::string raw = str.substr(pos, end - pos);
            pos = end;

            try {
                if (raw.find('.') != std::string::npos) {
                    if (!raw.empty() && raw.back() == 'D') raw.pop_back();
                    val = std::stod(raw);
                }
                else if (!raw.empty() && raw.back() == 'L') {
                    raw.pop_back();
                    val = std::stol(raw);
                }
                else {
                    val = std::stoi(raw);
                }
            }
            catch (...) {
                val = raw;
            }
        }

        out[key] = val;
    }
}




std::string RawAmiClient::processIncoming(const std::string& line) {
    if (line.find("Welcome to 3forge AMI") != std::string::npos ||
        line.find("logged in") != std::string::npos ||
        line.find("|Q=0|S=0|") != std::string::npos) {
        fireOnLogin();  
    }

    std::string s = line;
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();

    if (debug_) {
        std::cout << "[DEBUG-processIncoming] Incoming type: '" << s[0] << "' | Raw: " << s << std::endl;
    }
    if (s.size() < 3 || s[1] != '@') return "Invalid header";

    size_t pipe1 = s.find('|', 2);
    if (pipe1 == std::string::npos) return "Missing | after timestamp";

    long ts = 0;
    try {
        ts = std::stoll(s.substr(2, pipe1 - 2));
    }
    catch (...) {
        return "Invalid timestamp";
    }

    size_t pos = pipe1 + 1;

    try {
        switch (s[0]) {
        case 'M': {

            auto require = [&](char expected, const char* msg) {
                if (pos >= s.size() || s[pos] != expected)
                    throw std::runtime_error(msg);
                ++pos;
                };

            require('Q', "Expecting Q");
            require('=', "Expecting =");

            size_t pipe2 = s.find('|', pos);
            if (pipe2 == std::string::npos) return "Missing | after Q";
            long seqNum = std::stol(s.substr(pos, pipe2 - pos));
            pos = pipe2 + 1;

            require('S', "Expecting S");
            require('=', "Expecting =");
            size_t pipe3 = s.find('|', pos);
            if (pipe3 == std::string::npos) return "Missing | after S";
            int status = std::stoi(s.substr(pos, pipe3 - pos));
            pos = pipe3 + 1;

            require('M', "Expecting M");
            require('=', "Expecting =");
            require('"', "Expecting opening quote");

            std::string msg;
            if (!readUntilSkipEscaped(s, pos, '"', msg)) {
                if (debug_) {
                    std::cout << "[DEBUG] readUntilSkipEscaped Failed! " << msg << std::endl;
                }
                return "Malformed message string";
            }


            while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos])))
                ++pos;

            fireMessageReceived(ts, seqNum, status, msg);



            break;
        }

        case 'E': {

            std::map<std::string, AmiValue> params;
            parseIncomingParams(s, pos, params);

            auto getStr = [&](const std::string& key) -> std::string {
                auto it = params.find(key);
                if (it != params.end() && std::holds_alternative<std::string>(it->second))
                    return std::get<std::string>(it->second);
                return {};
                };

            std::string requestId = getStr("I"); params.erase("I");
            std::string userName = getStr("U"); params.erase("U");
            std::string cmd = getStr("C"); params.erase("C");
            std::string type = getStr("T"); params.erase("T");
            std::string objectId = getStr("O"); params.erase("O");


            if (debug_) {
                std::cout << "[Processing E Command]: try to fire ecommand" << std::endl;
            }
            fireCommand(requestId, cmd, userName, type, objectId, params);
            break;
        }

        default:
            if (debug_) {
                std::cout << "[DEBUG] Unknown message type '" << s[0] << "', forwarding raw message.\n";
            }
            fireMessageReceived(ts, 0, 0, s);
            break;
        }
    }
    catch (const std::exception& ex) {
        std::string msg = ex.what();
        if (msg.find("Expecting Q") != std::string::npos) {
            return "Missing | after timestamp";  
        }
        return std::string("Exception during parse: ") + msg;
    }

    return {}; 
}







bool RawAmiClient::readUntilSkipEscaped(const std::string& input, size_t& pos, char endChar, std::string& out) {
    while (pos < input.size()) {
        char c = input[pos++];
        if (c == endChar) return true;
        if (c == '\\') {
            if (pos >= input.size()) return false;
            char next = input[pos++];
            switch (next) {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case '\\': out += '\\'; break;
            case '\'': out += '\''; break;
            case '\"': out += '"'; break;
            case 'u': {
                if (pos + 4 > input.size()) return false;
                int code = std::stoi(input.substr(pos, 4), nullptr, 16);
                out += static_cast<char>(code);
                pos += 4;
                break;
            }
            default:
                out += next;
            }
        }
        else {
            out += c;
        }
    }
    return false;
}






bool RawAmiClient::pumpIncomingEvent() {
    if (debug_) {
        std::lock_guard<std::mutex> lk(coutMutex);
        std::cout << "[DEBUG-pumpEvent] start\n";
    }

    bool expected = false;
    if (!isInReceive_.compare_exchange_strong(expected, true)) {
        if (debug_) {
            std::lock_guard<std::mutex> lk(coutMutex);
            std::cout << "[DEBUG-pumpEvent] Already in pump for receive\n";
        }
        throw std::runtime_error("Already in pump for receive");
    }

    struct ResetFlag {
        std::atomic<bool>& f;
        ~ResetFlag() {
            f.store(false);
        }
    } _reset{ isInReceive_ };

    inBuffer_.clear();
    try {
        while (connected_) {
            boost::system::error_code ec;
            char c = socket_->read_char(ec);

            if (ec) {
                if (ec == boost::asio::error::eof) {
                    if (!inBuffer_.empty()) {
                        std::lock_guard<std::mutex> lk(coutMutex);
                        std::cerr << "[warning] Trailing text: " << inBuffer_ << "\n";
                    }
                    if (debug_) {
                        std::lock_guard<std::mutex> lk(coutMutex);
                        std::cout << "[DEBUG-pumpEvent] EOF reached\n";
                    }
                    return false;
                }

                std::lock_guard<std::mutex> lk(coutMutex);
                std::cerr << "[warning] Read error: " << ec.message() << "\n";

                if (debug_) {
                    std::cerr << "[DEBUG-pumpEvent] Disconnecting due to read error\n";
                }

                disconnect();
                return false;
            }

            switch (c) {
            case '\n': {
                if (debug_) {
                    std::lock_guard<std::mutex> lk(coutMutex);
                    std::cout << "[DEBUG-pumpEvent] Received newline, processing: " << inBuffer_ << "\n";
                }

                std::string err = processIncoming(inBuffer_);
                if (!err.empty()) {
                    std::lock_guard<std::mutex> lk(coutMutex);
                    std::cerr << "[warning] General error: "
                        << err << " for string '" << inBuffer_ << "'\n";
                }

                return true;
            }
            case '\r':
                if (debug_) {
                    std::lock_guard<std::mutex> lk(coutMutex);
                    std::cout << "[DEBUG-pumpEvent] Ignoring carriage return\n";
                }
                continue;
            default:
                inBuffer_.push_back(c);
            }
        }

        if (!inBuffer_.empty()) {
            std::lock_guard<std::mutex> lk(coutMutex);
            std::cerr << "[warning] Trailing text: " << inBuffer_ << "\n";
        }

        if (debug_) {
            std::lock_guard<std::mutex> lk(coutMutex);
            std::cout << "[DEBUG-pumpEvent] Connection closed, exiting pump loop\n";
        }

        return false;
    }
    catch (const std::exception& ex) {
        std::lock_guard<std::mutex> lk(coutMutex);
        std::cerr << "[warning] Exception in pumpIncomingEvent: "
            << ex.what() << "\n";

        if (debug_) {
            std::cerr << "[DEBUG-pumpEvent] Disconnecting due to exception\n";
        }

        disconnect();
        return false;
    }
}



bool RawAmiClient::sendMessage(const std::string& msg, bool flush) {
    assertConnected();

    std::lock_guard writeLock(writeMutex_);
    if (!flush) {
        // Append message to output buffer (no immediate write)
        outBuffer_ += msg;
        if (outBuffer_.empty() || outBuffer_.back() != '\n')
            outBuffer_ += '\n';
        if (debug_) {
            std::lock_guard lk(coutMutex);
            std::cout << "[sendMessage] buffered: " << msg << std::endl;
        }


        // Auto-flush based on buffer size threshold
        if (autoFlush_
            && autoFlushBufferSizeThreshold_ > 0
            && outBuffer_.size() >= autoFlushBufferSizeThreshold_)
        {
            // Buffer has reached flush threshold — write to socket
            socket_->send_message(outBuffer_);
            fireMessageSent(outBuffer_);
            outBuffer_.clear();
            needsFlush_ = false;
            isInSend_ = false;
        }


        return true;
    }

    // If flush=true, write the message immediately
    std::string toWrite = msg;
    if (toWrite.empty() || toWrite.back() != '\n')
        toWrite += '\n';
    socket_->send_message(toWrite);
    fireMessageSent(toWrite);

    if (debug_) {
        std::lock_guard lk(coutMutex);
        std::cout << "[sendMessage] flushed: " << msg << std::endl;
    }
    return true;
}




RawAmiClient& RawAmiClient::sendMessage() {
    assertConnected();

    bool expected = true;
    if (!isInSend_.compare_exchange_strong(expected, false))
        throw std::runtime_error("Not in object send");

    // Append the current message to the batch buffer
    {
        std::lock_guard<std::mutex> lk(writeMutex_);
        batchBuffer_ += outBuffer_;
        if (batchBuffer_.empty() || batchBuffer_.back() != '\n')
            batchBuffer_ += '\n';
    }

    // Auto flush (threshold-based): if batch exceeds threshold, write immediately
    if (autoFlush_ && autoFlushBufferSizeThreshold_ > 0) {
        std::lock_guard<std::mutex> lk(writeMutex_);
        if (debug_) {
            std::lock_guard lk(coutMutex);
            std::cout << "[DEBUG-sendMessage] AutoFlush enabled - size threshold mode. "
                << "batchBuffer_.size() = " << batchBuffer_.size()
                << ", threshold = " << autoFlushBufferSizeThreshold_ << std::endl;
        }
        if (batchBuffer_.size() >= autoFlushBufferSizeThreshold_) {
            socket_->send_message(batchBuffer_);
            if (debug_) {
                std::lock_guard lk(coutMutex);
                std::cout << "[DEBUG-sendMessage] Threshold met, flushing immediately. Message:\n" << batchBuffer_ << std::endl;
            }
            fireMessageSent(batchBuffer_);
            batchBuffer_.clear();
        }
    }
    else if (autoFlush_) {
        // Auto flush (time-based): notify flush thread
        if (debug_) {
            std::lock_guard lk(coutMutex);
            std::cout << "[DEBUG-sendMessage] AutoFlush enabled - time-based mode. "
                << "Marking needsFlush_ = true and notifying autoFlushLoop." << std::endl;
        }
        needsFlush_ = true;
        flushCv_.notify_one();
    }

    return *this;
}


RawAmiClient& RawAmiClient::sendMessageAndFlush() {
    assertConnected();
    bool expected = true;
    if (!isInSend_.compare_exchange_strong(expected, false))
        throw std::runtime_error("Not in object send");

    // Append message to batch buffer
    {
        std::lock_guard<std::mutex> lk(writeMutex_);
        batchBuffer_ += outBuffer_;
        if (batchBuffer_.empty() || batchBuffer_.back() != '\n')
            batchBuffer_ += '\n';
    }

    // Immediately write to socket and clear buffer
    {
        std::lock_guard<std::mutex> lk(writeMutex_);
        socket_->send_message(batchBuffer_);
        fireMessageSent(batchBuffer_);
        batchBuffer_.clear();
    }

    return *this;
}

RawAmiClient& RawAmiClient::flush(bool clearAfterSend) {
    assertConnected();
    if (clearAfterSend) {
        // Always flush immediately, regardless of autoFlush_ setting
        std::string buf;
        {
            std::lock_guard lk(writeMutex_);
            buf.swap(outBuffer_);
        }
        // Ensure newline termination
        if (buf.empty() || buf.back() != '\n') buf += '\n';
        socket_->send_message(buf);

        fireMessageSent(buf);

        needsFlush_ = false;
        isInSend_ = false;
        return *this;
    }
    else if (!autoFlush_) {
        // Manual flush mode: flush immediately if autoFlush is disabled
        std::string buf;
        {
            std::lock_guard lk(writeMutex_);
            buf.swap(outBuffer_);
        }
        if (buf.empty() || buf.back() != '\n')
            buf += '\n';

        socket_->send_message(buf);
        fireMessageSent(buf);
        if (debug_) {
            std::lock_guard lk(coutMutex);
            std::cout << "[flush] wrote: " << buf << std::endl;
        }
        needsFlush_ = false;
    }
    else {
        // In auto-flush mode: just signal the flush thread to flush soon
        std::unique_lock lk(flushMutex_);
        needsFlush_ = true;
        flushCv_.notify_one();
    }

    //if (clearAfterSend) {
    //    std::lock_guard lk(writeMutex_);
    //    outBuffer_.clear();
    //}
    return *this;
}

void RawAmiClient::autoFlushLoop() {
    std::unique_lock<std::mutex> lk(flushMutex_);
    while (!stopAutoFlush_) {
        if (autoFlushBufferSizeThreshold_ > 0) {
            // Size-based mode: wait indefinitely until notified
            flushCv_.wait(lk);
        }
        else {
            // Time-based mode: wait for timeout or notify
            flushCv_.wait_for(lk, std::chrono::milliseconds(autoFlushIntervalMs_));
        }
        if (stopAutoFlush_) break;

        // In time-based mode, check if a flush is needed
        if (autoFlushBufferSizeThreshold_ == 0 && needsFlush_) {
            std::string buf;
            {
                std::lock_guard<std::mutex> wl(writeMutex_);
                buf.swap(batchBuffer_);
                needsFlush_ = false;
            }
            if (!buf.empty() && buf.back() != '\n') buf += '\n';
            try {
                socket_->send_message(buf);
                fireMessageSent(buf);
            }
            catch (...) {
                disconnect();
                return;
            }
        }
    }
}




void RawAmiClient::addListener(std::shared_ptr<RawAmiClientListener> listener) {
    std::lock_guard<std::mutex> lock(listenersMutex_);
    listeners_.push_back(listener);
}

bool RawAmiClient::removeListener(std::shared_ptr<RawAmiClientListener> listener) {
    std::lock_guard<std::mutex> lock(listenersMutex_);
    auto it = std::find(listeners_.begin(), listeners_.end(), listener);
    if (it != listeners_.end()) {
        listeners_.erase(it);
        return true;
    }
    return false;
}







void RawAmiClient::fireConnect() {
    notifyListeners(&RawAmiClientListener::onConnect, this);
}

void RawAmiClient::fireDisconnect() {
    notifyListeners(&RawAmiClientListener::onDisconnect, this);
}

void RawAmiClient::fireOnLogin() {
    if (loggedIn_.exchange(true)) return;  
    notifyListeners(&RawAmiClientListener::onLoggedIn, this);
}

void RawAmiClient::fireMessageReceived(long ts, long seq, int status, const std::string& msg) {
    notifyListeners(&RawAmiClientListener::onMessageReceived,
        this, ts, seq, status, msg);
}

void RawAmiClient::fireMessageSent(const std::string& msg) {
    notifyListeners(&RawAmiClientListener::onMessageSent, this, msg);
}

void RawAmiClient::fireCommand(const std::string& requestId,
    const std::string& cmd,
    const std::string& userName,
    const std::string& objectType,
    const std::string& objectId,
    const std::map<std::string, AmiValue>& params) {
    notifyListeners(&RawAmiClientListener::onCommand,
        this, requestId, cmd, userName, objectType, objectId, params);
}



long RawAmiClient::resetSeqNum(long seqnum) {
    std::lock_guard<std::mutex> lock(seqnumMutex_);  
    long old = seqnum_;
    seqnum_ = seqnum;
    return old;
}

 void RawAmiClient::setDebug(bool enable) {
     debug_ = enable;
 }

//construct msg
void RawAmiClient::resetMessage() {
    assertConnected();
    isInSend_ = false;
    outBuffer_.clear();
}

void RawAmiClient::assertConnected() const {
    if (!connected_) throw std::runtime_error("not connected");
}

void RawAmiClient::assertInMessage() const {
    if (!isInSend_) throw std::runtime_error("not in object send, call startMessage(...) first");
}

RawAmiClient& RawAmiClient::startMessage(char type, bool includeSeqNum, bool includeNow) {
    assertConnected();

    bool expected = false;
    if (!isInSend_.compare_exchange_strong(expected, true))
        throw std::runtime_error("Already in object send");

    outBuffer_.clear();
    outBuffer_ += type;
    if (includeSeqNum)
        outBuffer_ += "#" + std::to_string(seqnum_++);
    if (includeNow)
        outBuffer_ += "@" + std::to_string(getNow());

    if (debug_) {
        std::cout << "[DEBUG-startMessage] type: " << type
            << ", includeSeqNum: " << includeSeqNum
            << ", includeNow: " << includeNow
            << ", seqnum_: " << seqnum_ 
            << ", timestamp: " << std::to_string(getNow()) << std::endl;
    }
    return *this;
}


RawAmiClient& RawAmiClient::addMessageParamNull(const std::string& key) {
    assertInMessage();
    outBuffer_ += "|" + key + "=null";
    return *this;
}

RawAmiClient& RawAmiClient::addMessageParamString(const std::string& key, char value) {
    assertInMessage();
    outBuffer_ += "|" + key + "=\"";
    if (value == '"') outBuffer_ += "\\\"";
    else outBuffer_ += value;
    outBuffer_ += "\"";
    return *this;
}

RawAmiClient& RawAmiClient::addMessageParamString(const std::string& key, const std::string& value) {
    assertInMessage();
    outBuffer_ += "|" + key + "=\"";
    for (char c : value) {
        if (c == '\\' || c == '"') outBuffer_ += '\\';
        outBuffer_ += c;
    }
    outBuffer_ += "\"";
    return *this;
}

RawAmiClient& RawAmiClient::addMessageParamString(const std::string& key, const std::string& value, size_t start, size_t end) {
    return addMessageParamString(key, value.substr(start, end - start));
}

RawAmiClient& RawAmiClient::addMessageParamEnum(const std::string& key, const std::string& value) {
    assertInMessage();
    outBuffer_ += "|" + key + "='";
    for (char c : value) {
        if (c == '\\' || c == '\'') outBuffer_ += '\\';
        outBuffer_ += c;
    }
    outBuffer_ += "'";
    return *this;
}

RawAmiClient& RawAmiClient::addMessageParamEnum(const std::string& key, const std::string& value, size_t start, size_t end) {
    return addMessageParamEnum(key, value.substr(start, end - start));
}

RawAmiClient& RawAmiClient::addMessageParamEnum(const std::string& key, const std::vector<char>& value) {
    assertInMessage();
    outBuffer_ += "|" + key + "='";
    for (char c : value) {
        if (c == '\\' || c == '\'') outBuffer_ += '\\';
        outBuffer_ += c;
    }
    outBuffer_ += "'";
    return *this;
}

RawAmiClient& RawAmiClient::addMessageParamJson(const std::string& key, const std::string& jsonStr) {
    assertInMessage();
    outBuffer_ += "|" + key + "=\"";
    for (char c : jsonStr) {
        if (c == '\\' || c == '"') outBuffer_ += '\\';
        outBuffer_ += c;
    }
    outBuffer_ += "\"J";
    return *this;
}

RawAmiClient& RawAmiClient::addMessageParamBinary(const std::string& key, const std::vector<uint8_t>& value) {
    return addMessageParamBinary(key, value, 0, value.size());
}

RawAmiClient& RawAmiClient::addMessageParamBinary(const std::string& key, const std::vector<uint8_t>& value, size_t start, size_t end) {
    assertInMessage();
    outBuffer_ += "|" + key + "=\"";
    static const char* base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (size_t i = start; i < end; i += 3) {
        int val = (value[i] << 16) + ((i + 1 < end ? value[i + 1] : 0) << 8) + (i + 2 < end ? value[i + 2] : 0);
        outBuffer_ += base64_chars[(val >> 18) & 0x3F];
        outBuffer_ += base64_chars[(val >> 12) & 0x3F];
        outBuffer_ += (i + 1 < end) ? base64_chars[(val >> 6) & 0x3F] : '=';
        outBuffer_ += (i + 2 < end) ? base64_chars[val & 0x3F] : '=';
    }
    outBuffer_ += "\"U";
    return *this;
}

RawAmiClient& RawAmiClient::addMessageParamLong(const std::string& key, long value) {
    assertInMessage();
    outBuffer_ += "|" + key + "=" + std::to_string(value) + "L";
    return *this;
}

RawAmiClient& RawAmiClient::addMessageParamInt(const std::string& key, int value) {
    assertInMessage();
    outBuffer_ += "|" + key + "=" + std::to_string(value);
    return *this;
}

RawAmiClient& RawAmiClient::addMessageParamDouble(const std::string& key, double value) {
    assertInMessage();
    std::ostringstream oss;
    oss << std::setprecision(16) << value;
    outBuffer_ += "|" + key + "=" + oss.str() + "D";
    return *this;
}

RawAmiClient& RawAmiClient::addMessageParamFloat(const std::string& key, float value) {
    assertInMessage();
    std::ostringstream oss;
    oss << std::setprecision(8) << value;
    outBuffer_ += "|" + key + "=" + oss.str();
    return *this;
}

RawAmiClient& RawAmiClient::addMessageParamDoubleEncoded(const std::string& key, double value) {
    assertInMessage();
    union {
        double d;
        uint64_t bits;
    } u;
    u.d = value;
    outBuffer_ += "|" + key + "=D" + std::to_string(u.bits);  
    return *this;
}

RawAmiClient& RawAmiClient::addMessageParamFloatEncoded(const std::string& key, float value) {
    assertInMessage();
    union {
        float f;
        uint32_t bits;
    } u;
    u.f = value;
    outBuffer_ += "|" + key + "=F" + std::to_string(u.bits);  
    return *this;
}

RawAmiClient& RawAmiClient::addMessageParamBoolean(const std::string& key, bool value) {
    assertInMessage();
    outBuffer_ += "|" + key + "=" + std::string(value ? "true" : "false");
    return *this;
}


void RawAmiClient::addMessageParams(const std::unordered_map<std::string, std::any>& params) {
    for (const auto& [key, value] : params) {
        addMessageParamObject(key, value);
    }
}

void RawAmiClient::addMessageParamObject(const std::string& key, const std::any& value) {
    if (!value.has_value()) {
        addMessageParamNull(key);
        return;
    }

    try {
        // === Strings and Chars ===
        if (value.type() == typeid(std::string)) {
            addMessageParamString(key, std::any_cast<std::string>(value));
        }
        else if (value.type() == typeid(const char*)) {
            addMessageParamString(key, std::string(std::any_cast<const char*>(value)));
        }
        else if (value.type() == typeid(char)) {
            addMessageParamEnum(key, std::string(1, std::any_cast<char>(value)));
        }

        // === Integer family ===
        else if (value.type() == typeid(int)) {
            addMessageParamInt(key, std::any_cast<int>(value));
        }
        else if (value.type() == typeid(long)) {
            addMessageParamLong(key, std::any_cast<long>(value));
        }
        else if (value.type() == typeid(short)) {
            addMessageParamInt(key, std::any_cast<short>(value));
        }
        else if (value.type() == typeid(uint8_t)) {
            addMessageParamInt(key, std::any_cast<uint8_t>(value));
        }
        else if (value.type() == typeid(int8_t)) {
            addMessageParamInt(key, std::any_cast<int8_t>(value));
        }

        // === Floating point family ===
        else if (value.type() == typeid(float)) {
            addMessageParamFloat(key, std::any_cast<float>(value));
        }
        else if (value.type() == typeid(double)) {
            addMessageParamDouble(key, std::any_cast<double>(value));
        }

        // === Boolean ===
        else if (value.type() == typeid(bool)) {
            addMessageParamBoolean(key, std::any_cast<bool>(value));
        }

        // === Binary (byte[]) ===
        else if (value.type() == typeid(std::vector<uint8_t>)) {
            addMessageParamBinary(key, std::any_cast<std::vector<uint8_t>>(value));
        }
        else if (value.type() == typeid(std::vector<char>)) {
            const auto& vec = std::any_cast<std::vector<char>>(value);
            addMessageParamBinary(key, std::vector<uint8_t>(vec.begin(), vec.end()));
        }

        // === UUID, Complex, etc. as string ===
        else if (value.type() == typeid(std::shared_ptr<std::stringstream>)) {
            auto ss = std::any_cast<std::shared_ptr<std::stringstream>>(value);
            addMessageParamString(key, ss->str());
        }
        else if (value.type() == typeid(std::string_view)) {
            addMessageParamString(key, std::string(std::any_cast<std::string_view>(value)));
        }

        // === Fallback ===
        else {
            throw std::runtime_error("Unsupported type in addMessageParamObject for key: " + key);
        }
    }
    catch (const std::bad_any_cast& e) {
        throw std::runtime_error("Bad cast for key: " + key + " -> " + e.what());
    }
}



const std::string& RawAmiClient::getOutputBuffer() const {
    return outBuffer_;
}

long RawAmiClient::getAutoFlushBufferMillis() const {
    return autoFlushIntervalMs_;
}

void RawAmiClient::setAutoFlushBufferMillis(long millis) {
    {
        std::lock_guard<std::mutex> lk(flushMutex_);
        autoFlushIntervalMs_ = millis;
    }
    flushCv_.notify_one();
}



size_t RawAmiClient::getAutoFlushBufferSizeThreshold() const {
    return autoFlushBufferSizeThreshold_;
}

void RawAmiClient::setAutoFlushBufferSizeThreshold(size_t threshold) {
    {
        std::lock_guard<std::mutex> lk(flushMutex_);
        autoFlushBufferSizeThreshold_ = threshold;
    }
    flushCv_.notify_one();
}

}