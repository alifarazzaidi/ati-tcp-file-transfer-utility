# Makefile for tcp-file-transfer-utility

CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
LDFLAGS :=

SRC_DIR := src
INCLUDE_DIR := include
BUILD_DIR := build
BIN_DIR := bin

SERVER := $(BIN_DIR)/server
CLIENT := $(BIN_DIR)/client
TEST := $(BIN_DIR)/test_transfer

SERVER_SRC := $(SRC_DIR)/server.cpp $(SRC_DIR)/utils.cpp
CLIENT_SRC := $(SRC_DIR)/client.cpp $(SRC_DIR)/utils.cpp
TEST_SRC := $(SRC_DIR)/utils.cpp tests/test_transfer.cpp

INCLUDES := -I$(INCLUDE_DIR)

.PHONY: all clean test

all: $(SERVER) $(CLIENT)

$(SERVER): $(SERVER_SRC)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)

$(CLIENT): $(CLIENT_SRC)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)

test: $(TEST)
	@echo "Running tests..."
	@$(TEST)

$(TEST): $(TEST_SRC)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)

clean:
	rm -rf $(BIN_DIR) build

.PHONY: help
help:
	@echo "Usage: make [all|test|clean]"
