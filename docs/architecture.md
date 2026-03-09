# Architecture & Design

This document describes the architecture of the **TCP File Transfer Utility** and the design decisions made to meet the requirements.

## ✅ Goals

- **Transfer large files (up to 16GB)** without high memory usage.
- **Integrity validation** using SHA-256.
- **Reliable transfer** with chunked streaming.
- **Simple protocol** that is easy to understand and extend.

## 📦 System Overview

The project contains two programs:

1. **Server** (`server`) - listens on a TCP port, accepts a single connection, and receives a file.
2. **Client** (`client`) - connects to the server, sends file metadata and the file data in chunks.

The server and client communicate using a simple, deterministic protocol (metadata followed by raw file bytes).

## 🧩 Protocol (Wire Format)

1. **Filename length** (uint32_t, network byte order)
2. **Filename** (UTF-8 bytes)
3. **File size** (uint64_t, network byte order)
4. **Checksum length** (uint32_t, network byte order) — currently always 64
5. **Checksum** (hex SHA-256 string)
6. **File bytes** (raw binary, in chunks)

After the client finishes sending the file, the server validates the checksum and responds:

1. **Status** (1 byte: `1` = success, `0` = failure)
2. **Message length** (uint32_t, network byte order)
3. **Message** (string)

## 🧱 Chunked Transfer (Memory Safety)

To support very large files (16GB+), the client reads and sends the file in **1MB chunks**.
This prevents the program from allocating huge buffers and keeps peak memory usage low.

## 🔒 Integrity Verification

Both the client and server compute SHA-256 checksums:

- Client computes checksum of the source file before transfer.
- Server computes checksum of the received file after transfer.
- The server compares its checksum with the client-provided checksum and returns an error if they do not match.

## ⚠️ Error Handling

The implementation includes basic error handling:

- Validates input parameters (port, file existence, etc.)
- Validates that metadata fields are within reasonable bounds (filename length, checksum length)
- Detects incomplete transfers and missing data
- Reports errors back to the client in a structured response message

## 🚀 Performance Considerations

- **Buffer size**: 1MB chunks are a good tradeoff between system call overhead and memory usage but can be tuned.
- **Network utilization**: The protocol is minimal and avoids extra framing overhead.
- **Extensions**:
  - Add compression (e.g., zstd) to reduce bandwidth usage.
  - Add parallel connections or windowing for better throughput on high-latency networks.
  - Add TLS for encryption (e.g., OpenSSL/mbedTLS).

## 🧩 Future Improvements

- Add **resume support** by sending file offsets and allowing partial transfers.
- Add **multiplexed transfers** (multiple files or multiple streams in one connection).
- Add **authentication** and **authorization**.
- Add a **cross-platform build** (Windows `Winsock` support).

## 🗺️ C4 / Diagram

This system is a simple point-to-point connection, so a full C4 diagram is overkill. The key components are:

- Client application
- Server application
- TCP socket pipe

```text
Client --> TCP --> Server
```

