#include "utils.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

static constexpr size_t kChunkSize = 1024 * 1024; // 1MB
static constexpr int kMaxConnectAttempts = 3;

bool send_all(int sock, const void *data, size_t len) {
    return tfu::write_exact(sock, data, len) == static_cast<ssize_t>(len);
}

bool recv_all(int sock, void *buf, size_t len) {
    return tfu::read_exact(sock, buf, len) == static_cast<ssize_t>(len);
}

int connect_with_retry(const std::string &host, int port) {
    for (int attempt = 1; attempt <= kMaxConnectAttempts; ++attempt) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            std::perror("socket");
            return -1;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(port));
        if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
            std::cerr << "invalid host: " << host << "\n";
            close(sock);
            return -1;
        }

        if (connect(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0) {
            return sock;
        }

        std::cerr << "connect attempt " << attempt << " failed: " << std::strerror(errno) << "\n";
        close(sock);
        sleep(1);
    }
    return -1;
}

int main(int argc, char *argv[]) {
    if (argc < 4 || argc > 5) {
        std::cerr << "Usage: " << argv[0] << " <host> <port> <file> [remote_filename]\n";
        return 1;
    }

    std::string host = argv[1];
    int port = std::stoi(argv[2]);
    std::string path = argv[3];
    std::string remote_name = (argc == 5) ? argv[4] : "";

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "failed to open file: " << path << "\n";
        return 1;
    }

    in.seekg(0, std::ios::end);
    uint64_t file_size = static_cast<uint64_t>(in.tellg());
    in.seekg(0, std::ios::beg);

    if (file_size == 0) {
        std::cerr << "file is empty\n";
        return 1;
    }

    std::string filename;
    if (!remote_name.empty()) {
        filename = remote_name;
    } else {
        auto pos = path.find_last_of("/\\");
        filename = (pos == std::string::npos) ? path : path.substr(pos + 1);
    }

    std::string checksum;
    try {
        checksum = tfu::sha256_file(path);
    } catch (const std::exception &ex) {
        std::cerr << "checksum error: " << ex.what() << "\n";
        return 1;
    }

    int sock = connect_with_retry(host, port);
    if (sock < 0) {
        std::cerr << "unable to connect to " << host << ":" << port << "\n";
        return 1;
    }

    // send metadata
    uint32_t name_len = static_cast<uint32_t>(filename.size());
    uint32_t name_len_net = htonl(name_len);
    if (!send_all(sock, &name_len_net, sizeof(name_len_net))) {
        std::cerr << "failed to send filename length\n";
        close(sock);
        return 1;
    }
    if (!send_all(sock, filename.data(), name_len)) {
        std::cerr << "failed to send filename\n";
        close(sock);
        return 1;
    }

    uint64_t file_size_net = tfu::htonll(file_size);
    if (!send_all(sock, &file_size_net, sizeof(file_size_net))) {
        std::cerr << "failed to send file size\n";
        close(sock);
        return 1;
    }

    uint32_t checksum_len = static_cast<uint32_t>(checksum.size());
    uint32_t checksum_len_net = htonl(checksum_len);
    if (!send_all(sock, &checksum_len_net, sizeof(checksum_len_net))) {
        std::cerr << "failed to send checksum length\n";
        close(sock);
        return 1;
    }
    if (!send_all(sock, checksum.data(), checksum_len)) {
        std::cerr << "failed to send checksum\n";
        close(sock);
        return 1;
    }

    std::vector<char> buffer(kChunkSize);
    uint64_t remaining = file_size;
    while (remaining > 0) {
        size_t to_read = static_cast<size_t>(std::min<uint64_t>(remaining, kChunkSize));
        in.read(buffer.data(), to_read);
        std::streamsize n = in.gcount();
        if (n <= 0) {
            std::cerr << "failed to read from file\n";
            close(sock);
            return 1;
        }
        if (!send_all(sock, buffer.data(), static_cast<size_t>(n))) {
            std::cerr << "failed to send data" << std::endl;
            close(sock);
            return 1;
        }
        remaining -= static_cast<uint64_t>(n);
    }

    // wait for response
    uint8_t status;
    if (!recv_all(sock, &status, 1)) {
        std::cerr << "no response from server\n";
        close(sock);
        return 1;
    }

    uint32_t resp_len_net;
    if (!recv_all(sock, &resp_len_net, sizeof(resp_len_net))) {
        std::cerr << "failed to read response length\n";
        close(sock);
        return 1;
    }
    uint32_t resp_len = ntohl(resp_len_net);
    std::string resp;
    if (resp_len > 0) {
        resp.resize(resp_len);
        if (!recv_all(sock, resp.data(), resp_len)) {
            std::cerr << "failed to read response message\n";
            close(sock);
            return 1;
        }
    }

    if (status == 1) {
        std::cout << "SUCCESS: " << resp << "\n";
        close(sock);
        return 0;
    }

    std::cerr << "ERROR: " << resp << "\n";
    close(sock);
    return 1;
}
