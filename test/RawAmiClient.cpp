#include <boost/asio.hpp>
#include <iostream>

int main(int argc, char* argv[]) {
    // 从命令行读取主机和端口，默认为 localhost:3289
    const std::string host = (argc > 1 ? argv[1] : "localhost");
    const int port = (argc > 2 ? std::stoi(argv[2]) : 3289);

    boost::asio::io_context ioCtx;
    boost::asio::ip::tcp::socket socket(ioCtx);

    try {
        boost::asio::ip::tcp::resolver resolver(ioCtx);
        auto endpoints = resolver.resolve(host, std::to_string(port));
        boost::asio::connect(socket, endpoints);

        std::cout << "Connected to " << host << ':' << port << std::endl;
        // 目前仅验证连通，随后立即断开
        socket.close();
        std::cout << "Disconnected" << std::endl;
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Connection failed: " << e.what() << std::endl;
        return 1;
    }
}
