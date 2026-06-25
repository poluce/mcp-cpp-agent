#include <iostream>
#include <memory>
#include <chrono>
#include <thread>
#include <condition_variable>
#include <cassert>
#include "mcp_core/ConsoleStdioTransport.h"
#include "mcp_core/McpClientSession.h"

// Memory transport mock class to dynamically simulate server behaviors locally
class MockTransport : public mcp::IMcpTransport {
public:
    std::string lastSentMessage;
    std::function<void(const std::string&)> onSendCallback;
    std::function<void(const std::string&)> m_onMessage;
    std::function<void()> m_onClose;

    bool send(const std::string& message) override {
        lastSentMessage = message;
        if (onSendCallback) {
            onSendCallback(message);
        }
        return true;
    }
    void setOnMessage(std::function<void(const std::string&)> callback) override {
        m_onMessage = std::move(callback);
    }
    void setOnClose(std::function<void()> callback) override {
        m_onClose = std::move(callback);
    }
    void setOnError(std::function<void(const std::string&)>) override {}
    bool start() override { return true; }
    void close() override { if (m_onClose) m_onClose(); }

    void pushServerMessage(const std::string& msg) {
        if (m_onMessage) {
            m_onMessage(msg);
        }
    }
};

// Automate lifecycle scenario assertions in a closed-loop local test suite
void runLocalLifecycleTests() {
    std::cout << "========================================\n";
    std::cout << "  C++ MCP SDK Lifecycle Local Test Suite\n";
    std::cout << "========================================\n\n";

    // ----------------------------------------------------
    // Scenario 1: tools/list before initialize (Should be intercepted)
    // ----------------------------------------------------
    {
        auto transport = std::make_shared<MockTransport>();
        auto session = std::make_shared<mcp::McpClientSession>(transport);
        session->init();
        session->start();

        bool interceptCorrect = false;
        session->listTools([&](const std::vector<mcp::McpTool>&, const mcp::json& error) {
            if (!error.empty() && error.contains("code") && error["code"] == -32002) {
                interceptCorrect = true;
            }
        });

        assert(interceptCorrect && "Scenario 1 Failed: Sending tools/list before initialization should be intercepted locally.");
        assert(transport->lastSentMessage.empty() && "Scenario 1 Failed: Stdio packet should not be sent out on interception.");
        std::cout << "[✓] Scenario 1: Intercept business request before initialization\n";
    }

    // ----------------------------------------------------
    // Scenario 2: Normal initialize
    // ----------------------------------------------------
    {
        auto transport = std::make_shared<MockTransport>();
        auto session = std::make_shared<mcp::McpClientSession>(transport);
        session->init();
        session->start();

        bool initializedNotificationSent = false;
        transport->onSendCallback = [&](const std::string& msg) {
            mcp::json j = mcp::json::parse(msg);
            if (j.contains("method") && j["method"] == "notifications/initialized") {
                initializedNotificationSent = true;
            }
        };

        bool initSuccess = false;
        session->initialize("test-client", "1.0.0", [&](bool success, const mcp::json&) {
            initSuccess = success;
        });

        mcp::json mockResponse = {
            {"jsonrpc", "2.0"},
            {"id", 1},
            {"result", {
                {"protocolVersion", mcp::McpClientSession::MCP_PROTOCOL_VERSION},
                {"capabilities", mcp::json::object()},
                {"serverInfo", {{"name", "mock-server"}, {"version", "1.0.0"}}}
            }}
        };
        transport->pushServerMessage(mockResponse.dump());

        assert(initSuccess && "Scenario 2 Failed: Normal initialize handshake failed.");
        assert(initializedNotificationSent && "Scenario 2 Failed: initialized notification was not sent.");
        assert(session->state() == mcp::SessionState::Initialized && "Scenario 2 Failed: state was not updated to Initialized.");
        std::cout << "[✓] Scenario 2: Normal initialize handshake and notification\n";
    }

    // ----------------------------------------------------
    // Scenario 3: Duplicate initialize (Should be blocked)
    // ----------------------------------------------------
    {
        auto transport = std::make_shared<MockTransport>();
        auto session = std::make_shared<mcp::McpClientSession>(transport);
        session->init();
        session->start();

        session->initialize("test-client", "1.0.0", [](bool, const mcp::json&){});

        bool repeatIntercept = false;
        session->initialize("test-client", "1.0.0", [&](bool success, const mcp::json& error) {
            if (!success && error.contains("code") && error["code"] == -32600) {
                repeatIntercept = true;
            }
        });

        assert(repeatIntercept && "Scenario 3 Failed: Duplicate initialization calls should be intercepted.");
        std::cout << "[✓] Scenario 3: Prevent duplicate initialize calls\n";
    }

    // ----------------------------------------------------
    // Scenario 4: Server returns unsupported protocolVersion / Mismatch
    // ----------------------------------------------------
    {
        auto transport = std::make_shared<MockTransport>();
        auto session = std::make_shared<mcp::McpClientSession>(transport);
        session->init();
        session->start();

        bool initializedNotificationSent = false;
        transport->onSendCallback = [&](const std::string& msg) {
            mcp::json j = mcp::json::parse(msg);
            if (j.contains("method") && j["method"] == "notifications/initialized") {
                initializedNotificationSent = true;
            }
        };

        bool initSuccess = true; 
        session->initialize("test-client", "1.0.0", [&](bool success, const mcp::json&) {
            initSuccess = success;
        });

        // Simulate server returning mismatched version "2024-11-05" instead of "2025-11-25"
        mcp::json mockResponse = {
            {"jsonrpc", "2.0"},
            {"id", 1},
            {"result", {
                {"protocolVersion", "2024-11-05"},
                {"capabilities", mcp::json::object()},
                {"serverInfo", {{"name", "old-mock-server"}, {"version", "1.0.0"}}}
            }}
        };
        transport->pushServerMessage(mockResponse.dump());

        assert(!initSuccess && "Scenario 4 Failed: Handshake should fail on version mismatch.");
        assert(!initializedNotificationSent && "Scenario 4 Failed: initialized notification must not be sent on mismatch.");
        assert(session->state() == mcp::SessionState::Uninitialized && "Scenario 4 Failed: state should rollback to Uninitialized.");
        std::cout << "[✓] Scenario 4: Unmatched protocolVersion negotiation and rollback\n";
    }

    // ----------------------------------------------------
    // Scenario 5: Normal tools/list after initialized
    // ----------------------------------------------------
    {
        auto transport = std::make_shared<MockTransport>();
        auto session = std::make_shared<mcp::McpClientSession>(transport);
        session->init();
        session->start();

        session->initialize("test-client", "1.0.0", [](bool, const mcp::json&){});
        mcp::json mockResponse = {
            {"jsonrpc", "2.0"},
            {"id", 1},
            {"result", {
                {"protocolVersion", mcp::McpClientSession::MCP_PROTOCOL_VERSION},
                {"capabilities", mcp::json::object()},
                {"serverInfo", {{"name", "mock"}, {"version", "1"}}}
            }}
        };
        transport->pushServerMessage(mockResponse.dump());

        bool toolsGot = false;
        session->listTools([&](const std::vector<mcp::McpTool>& tools, const mcp::json&) {
            if (tools.size() == 1 && tools[0].name == "test_tool") {
                toolsGot = true;
            }
        });

        mcp::json toolsListResponse = {
            {"jsonrpc", "2.0"},
            {"id", 2},
            {"result", {
                {"tools", mcp::json::array({
                    {{"name", "test_tool"}, {"description", "testDesc"}, {"inputSchema", mcp::json::object()}}
                })}
            }}
        };
        transport->pushServerMessage(toolsListResponse.dump());

        assert(toolsGot && "Scenario 5 Failed: Cannot query tools/list after initialization.");
        std::cout << "[✓] Scenario 5: Business tools/list query after initialized\n";
    }

    std::cout << "\n========================================\n";
    std::cout << "  🎉 🎉 🎉 All Lifecycle self-tests PASSED!\n";
    std::cout << "========================================\n";
}

int main(int argc, char* argv[]) {
    bool isConformance = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--scenario") {
            isConformance = true;
            break;
        }
    }

    if (isConformance) {
        // Standard Stdio Conformance Flow (Scenario verification)
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

        session->initialize("mcp-conformance-client-cpp", "1.0.0", [&](bool success, const mcp::json& serverInfo) {
            if (!success) {
                std::lock_guard<std::mutex> lock(mtx);
                finished = true;
                cv.notify_one();
                return;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            
            std::lock_guard<std::mutex> lock(mtx);
            finished = true;
            cv.notify_one();
        });

        std::unique_lock<std::mutex> lock(mtx);
        cv.wait_for(lock, std::chrono::seconds(8), [&]{ return finished; });
        session->close();
    } else {
        // Local lifecycle scenario testing suite
        runLocalLifecycleTests();
    }
    return 0;
}
