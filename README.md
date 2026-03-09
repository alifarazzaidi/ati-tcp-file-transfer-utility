# tcp-file-transfer-utility

A small C++ TCP file transfer utility that supports large file transfers (up to 16GB+), chunked streaming, and integrity verification via SHA-256.

## ✅ Overview
This repository contains:

- **`server`**: Accepts incoming TCP connections, receives a file in chunks, and verifies SHA-256 checksum.
- **`client`**: Connects to a server, streams a local file in chunks, and sends metadata (filename, size, checksum).
- **Chunked transfer**: Files are sent/received in 1MB chunks to avoid memory spikes.
- **Integrity check**: Both sides compute SHA-256 and validate the transmission.
- **Tests**: A minimal end-to-end test that transfers a file and checks checksum.

## 🛠️ Build

```bash
make
```

This produces:

- `./bin/server`
- `./bin/client`
- `./bin/test_transfer`

## ▶️ Run

### Start server

```bash
./bin/server <port> [output_dir]
```

Example:

```bash
./bin/server 8080 received_files
```

### Send a file (client)

```bash
./bin/client <server_ip> <port> <file_path> [remote_filename]
```

Example:

```bash
./bin/client 127.0.0.1 8080 ./data/large.bin
```

## 🧪 Test

Run the built-in integration test:

```bash
make test
```

This will start a local server, transfer a test file, and verify that the checksum matches.

## 📄 Project Structure

```
tcp-file-transfer-utility
│
├── src
│   ├── server.cpp
│   ├── client.cpp
│   └── utils.cpp
│
├── include
│   └── utils.h
│
├── tests
│   └── test_transfer.cpp
│
├── docs
│   └── architecture.md
│
├── README.md
├── Makefile
```

## 🔧 Design decisions

See `docs/architecture.md` for details on:

- Protocol design (metadata + chunked transfer)
- Checksum strategy (SHA-256)
- Error handling and retries
- Future improvements and performance considerations

## 📌 Notes

- The project is written in standard C++17 and should compile on Linux/macOS using `g++`.
- The protocol is deliberately simple (no encryption) but is built so it can be extended (e.g., TLS / compression).
