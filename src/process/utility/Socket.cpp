#include "Socket.h"
#include <iostream>
#include <algorithm>

asio::io_context Socket::io_context;
const uint32_t Socket::MTU = 1024;

Socket::Socket(const std::string& ip, uint16_t port)
    : endpoint(asio::ip::address::from_string(ip), port), socket(io_context) {
    try {
        socket.connect(endpoint);
    } catch (const std::exception& e) {
        std::cerr << "Error connecting to endpoint: " << e.what() << std::endl;
        throw;
    }
}

Socket::~Socket()
{
}

void Socket::sendData(const std::string& data) {
    asio::error_code err;
    bool hadError = false;

    for (std::size_t i = 0; i < (data.size() + MTU - 1) / MTU; ++i) {
        const std::size_t offset = i * MTU;
        const std::size_t len = std::min<std::size_t>(MTU, data.size() - offset);
        asio::write(socket, asio::buffer(data.data() + offset, len), err);

        if (err) {
            std::cerr << "Error sending data: " << err.message() << std::endl;
            hadError = true;
            break;
        }
    }

    if (!hadError) {
        asio::write(socket, asio::buffer("\n", 1), err);
        if (err) {
            std::cerr << "Error sending frame delimiter: " << err.message() << std::endl;
        }
    }
}
