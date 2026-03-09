#include "utils.h"

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

static constexpr int kTestPort = 12345;

int spawn_server(const std::string &out_dir) {
    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        // child
        std::string port_str = std::to_string(kTestPort);
        execl("./bin/server", "./bin/server", port_str.c_str(), out_dir.c_str(), nullptr);
        _exit(127);
    }
    return static_cast<int>(pid);
}

int main() {
    const fs::path root = fs::current_path();
    const fs::path test_dir = root / "testdata";
    const fs::path out_dir = test_dir / "output";
    const fs::path in_dir = test_dir / "input";
    const fs::path input_file = in_dir / "input.bin";
    const fs::path output_file = out_dir / "input.bin";

    fs::create_directories(out_dir);

    // Create test file (5 MiB)
    {
        std::ofstream out(input_file, std::ios::binary);
        if (!out) {
            std::cerr << "failed to create test input file\n";
            return 1;
        }
        const size_t size = 5 * 1024 * 1024;
        std::string chunk(1024, 'A');
        for (size_t i = 0; i < size / chunk.size(); ++i) {
            out.write(chunk.data(), chunk.size());
        }
    }

    int server_pid = spawn_server(out_dir.string());
    if (server_pid < 0) {
        std::cerr << "failed to start server\n";
        return 1;
    }

    // Give server a moment to start
    sleep(1);

    int rc = std::system((std::string("./bin/client 127.0.0.1 ") + std::to_string(kTestPort) + " " + input_file.string()).c_str());

    // Stop server
    kill(server_pid, SIGTERM);
    waitpid(server_pid, nullptr, 0);

    if (rc != 0) {
        std::cerr << "client failed (exit " << rc << ")\n";
        return 1;
    }

    if (!fs::exists(output_file)) {
        std::cerr << "output file not found\n";
        return 1;
    }

    auto src_sum = tfu::sha256_file(input_file.string());
    auto dst_sum = tfu::sha256_file(output_file.string());
    if (src_sum != dst_sum) {
        std::cerr << "checksum mismatch\n";
        return 1;
    }

    std::cout << "test_transfer: OK" << std::endl;
    return 0;
}
