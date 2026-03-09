#include "utils.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

static constexpr size_t kChunkSize = 1024 * 1024; // 1MB

std::string basename_safe(const std::string &path) {
    auto pos = path.find_last_of("/\\");
    if (pos == std::string::npos) return path;
    return path.substr(pos + 1);
}

bool send_response(int sock, bool ok, const std::string &message) {
    uint8_t status = ok ? 1 : 0;
    if (tfu::write_exact(sock, &status, 1) != 1) return false;
    uint32_t msg_len = htonl(static_cast<uint32_t>(message.size()));
    if (tfu::write_exact(sock, &msg_len, sizeof(msg_len)) != sizeof(msg_len)) return false;
    if (!message.empty()) {
        if (tfu::write_exact(sock, message.data(), message.size()) != static_cast<ssize_t>(message.size())) return false;
    }
    return true;
}

int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: " << argv[0] << " <port> [output_dir]\n";
        return 1;
    }

    int port = std::stoi(argv[1]);
    std::string output_dir = (argc == 3) ? argv[2] : ".";

    signal(SIGPIPE, SIG_IGN);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(server_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        std::perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 5) < 0) {
        std::perror("listen");
        close(server_fd);
        return 1;
    }

    std::cout << "Server listening on port " << port << "\n";

    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, reinterpret_cast<sockaddr *>(&client_addr), &client_len);
        if (client_fd < 0) {
            std::perror("accept");
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        std::cout << "Client connected: " << client_ip << ":" << ntohs(client_addr.sin_port) << "\n";

        // Read filename length
        uint32_t name_len_net;
        if (tfu::read_exact(client_fd, &name_len_net, sizeof(name_len_net)) != sizeof(name_len_net)) {
            send_response(client_fd, false, "failed to read filename length");
            close(client_fd);
            continue;
        }

        uint32_t name_len = ntohl(name_len_net);
        if (name_len == 0 || name_len > 4096) {
            send_response(client_fd, false, "invalid filename length");
            close(client_fd);
            continue;
        }

        std::string filename(name_len, '\0');
        if (tfu::read_exact(client_fd, filename.data(), name_len) != static_cast<ssize_t>(name_len)) {
            send_response(client_fd, false, "failed to read filename");
            close(client_fd);
            continue;
        }

        // Read filesize
        uint64_t file_size_net;
        if (tfu::read_exact(client_fd, &file_size_net, sizeof(file_size_net)) != sizeof(file_size_net)) {
            send_response(client_fd, false, "failed to read file size");
            close(client_fd);
            continue;
        }

        uint64_t file_size = tfu::ntohll(file_size_net);
        if (file_size == 0) {
            send_response(client_fd, false, "file size must be > 0");
            close(client_fd);
            continue;
        }

        // Read checksum
        uint32_t checksum_len_net;
        if (tfu::read_exact(client_fd, &checksum_len_net, sizeof(checksum_len_net)) != sizeof(checksum_len_net)) {
            send_response(client_fd, false, "failed to read checksum length");
            close(client_fd);
            continue;
        }
        uint32_t checksum_len = ntohl(checksum_len_net);
        if (checksum_len != 64) {
            send_response(client_fd, false, "expected 64-byte hex checksum");
            close(client_fd);
            continue;
        }

        std::string expected_checksum(checksum_len, '\0');
        if (tfu::read_exact(client_fd, expected_checksum.data(), checksum_len) != static_cast<ssize_t>(checksum_len)) {
            send_response(client_fd, false, "failed to read checksum");
            close(client_fd);
            continue;
        }

        std::string out_path = output_dir + "/" + basename_safe(filename);
        std::ofstream out(out_path, std::ios::binary);
        if (!out) {
            send_response(client_fd, false, "failed to open output file");
            close(client_fd);
            continue;
        }

        uint64_t remaining = file_size;
        std::vector<char> buffer(kChunkSize);
        while (remaining > 0) {
            size_t to_read = static_cast<size_t>(std::min<uint64_t>(remaining, kChunkSize));
            ssize_t n = tfu::read_exact(client_fd, buffer.data(), to_read);
            if (n <= 0) {
                send_response(client_fd, false, "connection closed unexpectedly");
                break;
            }
            out.write(buffer.data(), n);
            if (!out) {
                send_response(client_fd, false, "failed to write output file");
                break;
            }
            remaining -= static_cast<uint64_t>(n);
        }
        out.close();

        if (remaining != 0) {
            close(client_fd);
            continue;
        }

        std::string actual_checksum;
        try {
            actual_checksum = tfu::sha256_file(out_path);
        } catch (const std::exception &ex) {
            send_response(client_fd, false, std::string("checksum failed: ") + ex.what());
            close(client_fd);
            continue;
        }

        if (actual_checksum != expected_checksum) {
            send_response(client_fd, false, "checksum mismatch");
            close(client_fd);
            continue;
        }

        send_response(client_fd, true, "file received and verified");
        std::cout << "Received " << filename << " (" << file_size << " bytes) -> " << out_path << "\n";
        close(client_fd);
    }

    close(server_fd);
    return 0;
}
