#include <iostream>
#include <string>
#include <memory>

// Core dependencies
#include <asio.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

// Optional dependencies
#include <leveldb/db.h>
#include <flatbuffers/flatbuffers.h>
#include <gtest/gtest.h>
#include <benchmark/benchmark.h>

int main(int argc, char* argv[]) {
    (void)argc;  // Suppress unused parameter warning
    (void)argv;  // Suppress unused parameter warning

    std::cout << "========================================" << std::endl;
    std::cout << "AstraDB - High-Performance Redis-Compatible Database" << std::endl;
    std::cout << "Version: 1.0.0" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    // Test spdlog
    std::cout << "[+] Testing spdlog..." << std::endl;
    try {
        auto console = spdlog::stdout_color_mt("console");
        console->info("Welcome to AstraDB!");
        console->info("spdlog is working correctly!");
        std::cout << "    [OK] spdlog initialized successfully" << std::endl;
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "    [ERROR] Log initialization failed: " << ex.what() << std::endl;
        return 1;
    }

    // Test Asio
    std::cout << "[+] Testing Asio..." << std::endl;
    try {
        asio::io_context io_ctx;
        std::cout << "    [OK] Asio io_context created successfully" << std::endl;
    } catch (const std::exception& ex) {
        std::cerr << "    [ERROR] Asio initialization failed: " << ex.what() << std::endl;
        return 1;
    }

    // Test LevelDB
    std::cout << "[+] Testing LevelDB..." << std::endl;
    try {
        leveldb::DB* db;
        leveldb::Options options;
        options.create_if_missing = true;
        leveldb::Status status = leveldb::DB::Open(options, "testdb", &db);
        if (status.ok()) {
            std::cout << "    [OK] LevelDB initialized successfully" << std::endl;
            delete db;
        } else {
            std::cerr << "    [ERROR] LevelDB initialization failed: " << status.ToString() << std::endl;
            return 1;
        }
    } catch (const std::exception& ex) {
        std::cerr << "    [ERROR] LevelDB initialization failed: " << ex.what() << std::endl;
        return 1;
    }

    // Test FlatBuffers
    std::cout << "[+] Testing FlatBuffers..." << std::endl;
    try {
        flatbuffers::FlatBufferBuilder builder;
        std::cout << "    [OK] FlatBuffers initialized successfully" << std::endl;
    } catch (const std::exception& ex) {
        std::cerr << "    [ERROR] FlatBuffers initialization failed: " << ex.what() << std::endl;
        return 1;
    }

    // Test Google Test (headers only)
    std::cout << "[+] Testing Google Test..." << std::endl;
    std::cout << "    [OK] Google Test headers included successfully" << std::endl;

    // Test Google Benchmark (headers only)
    std::cout << "[+] Testing Google Benchmark..." << std::endl;
    std::cout << "    [OK] Google Benchmark headers included successfully" << std::endl;

    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "All dependencies tested successfully!" << std::endl;
    std::cout << "AstraDB is ready to use!" << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}