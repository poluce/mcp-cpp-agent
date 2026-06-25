#pragma once
#include <string>
#include <functional>
#include <memory>
#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>
#include "mcp_core/IMcpTransport.h"
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

// Declarations of modular tests
void test_initialize();
void test_json_rpc();
void test_capabilities();
void test_error_response();

void test_stdio_transport();
void test_http_transport();
void test_process_lifecycle();

void test_tools();
void test_resources();
void test_prompts();
void test_notifications();

void test_with_filesystem_server();
void test_with_anysearch_mcp();
void test_with_inspector_cases();
