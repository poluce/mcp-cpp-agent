#include "tests/common.h"
#include "mcp_core/ConsoleStdioTransport.h"
#include <sstream>
#include <iostream>
#include <mutex>
#include <condition_variable>
#include <chrono>

void test_stdio_transport() {
    std::cout << "[Stdio Transport Test] Running ConsoleStdioTransport lifecycle tests...\n";
    
    // Scenario 1: Verify ConsoleStdioTransport send redirection to std::cout
    {
        std::stringstream mock_cout;
        auto old_cout_buf = std::cout.rdbuf(mock_cout.rdbuf());
        
        auto transport = std::make_shared<mcp::ConsoleStdioTransport>();
        bool send_ok = transport->send("{\"test\":\"data\"}");
        
        // 恢复 cout
        std::cout.rdbuf(old_cout_buf);
        
        TM_ASSERT_TRUE(send_ok, "send() should return true");
        TM_ASSERT_EQ(mock_cout.str(), "{\"test\":\"data\"}\n", "stdout output mismatch");
    }
    
    // Scenario 2: Verify ConsoleStdioTransport receive redirection from std::cin
    {
        std::stringstream mock_cin;
        mock_cin << "{\"method\":\"test_method\"}\n";
        auto old_cin_buf = std::cin.rdbuf(mock_cin.rdbuf());
        
        auto transport = std::make_shared<mcp::ConsoleStdioTransport>();
        
        std::string received_msg;
        std::mutex cv_mutex;
        std::condition_variable cv;
        bool got_msg = false;
        bool got_close = false;
        
        transport->setOnMessage([&](const std::string& msg) {
            std::lock_guard<std::mutex> lock(cv_mutex);
            received_msg = msg;
            got_msg = true;
            cv.notify_all();
        });
        
        transport->setOnClose([&]() {
            std::lock_guard<std::mutex> lock(cv_mutex);
            got_close = true;
            cv.notify_all();
        });
        
        bool start_ok = transport->start();
        TM_ASSERT_TRUE(start_ok, "start() should return true");
        
        // 等待读取线程处理完消息，并且触发 close 退出
        std::unique_lock<std::mutex> lock(cv_mutex);
        bool wait_ok = cv.wait_for(lock, std::chrono::milliseconds(1000), [&]() { return got_msg && got_close; });
        
        // 恢复 cin (必须在后台线程关闭后进行，避免 UAF)
        std::cin.rdbuf(old_cin_buf);
        
        TM_ASSERT_TRUE(wait_ok, "Should have received message and closed redirect std::cin within timeout");
        TM_ASSERT_EQ(received_msg, "{\"method\":\"test_method\"}", "Received message mismatch");
    }
    
    // Scenario 3: Verify start returns false if already running, and error callbacks
    {
        auto transport = std::make_shared<mcp::ConsoleStdioTransport>();
        transport->setOnError([](const std::string& err) {
            // Can be invoked if errors happen
        });
        
        std::stringstream mock_cin;
        auto old_cin_buf = std::cin.rdbuf(mock_cin.rdbuf());
        
        std::mutex close_mutex;
        std::condition_variable close_cv;
        bool is_closed = false;
        
        transport->setOnClose([&]() {
            std::lock_guard<std::mutex> lock(close_mutex);
            is_closed = true;
            close_cv.notify_one();
        });
        
        transport->start();
        bool start_again = transport->start();
        
        // 等待后台空 cin 触发的 EOF 线程关闭
        std::unique_lock<std::mutex> lock(close_mutex);
        close_cv.wait_for(lock, std::chrono::milliseconds(1000), [&]() { return is_closed; });
        
        std::cin.rdbuf(old_cin_buf);
        transport->close();
        
        TM_ASSERT_FALSE(start_again, "start() again should return false");
    }
}
