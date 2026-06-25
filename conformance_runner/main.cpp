#include <iostream>
#include <memory>
#include <chrono>
#include <thread>
#include <condition_variable>
#include "mcp_core/ConsoleStdioTransport.h"
#include "mcp_core/McpClientSession.h"

int main() {
    // Spawn standard console stdin/stdout streams as MCP Transport
    auto transport = std::make_shared<mcp::ConsoleStdioTransport>();
    auto session = std::make_shared<mcp::McpClientSession>(transport);

    std::mutex mtx;
    std::condition_variable cv;
    bool finished = false;

    session->init();
    if (!session->start()) {
        std::cerr << "Failed to start console stdio transport." << std::endl;
        return 1;
    }

    // Trigger standard MCP handshake immediately
    session->initialize("mcp-conformance-client-cpp", "1.0.0", [&](bool success, const mcp::json& serverInfo) {
        if (!success) {
            std::cerr << "Handshake error response: " << serverInfo.dump() << std::endl;
            std::lock_guard<std::mutex> lock(mtx);
            finished = true;
            cv.notify_one();
            return;
        }
        
        // Wait briefly for conformance tool to finish verification and disconnect
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        std::lock_guard<std::mutex> lock(mtx);
        finished = true;
        cv.notify_one();
    });

    std::unique_lock<std::mutex> lock(mtx);
    cv.wait_for(lock, std::chrono::seconds(8), [&]{ return finished; });

    session->close();
    return 0;
}
