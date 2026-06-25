#include <iostream>
#include <memory>
#include <chrono>
#include <thread>
#include <condition_variable>
#include <mutex>
#include "mcp_core/ConsoleStdioTransport.h"
#include "mcp_core/McpClientSession.h"
#include "tests/common.h"

// 声明外部的测试总入口
void runAllLocalTests();

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
        // 运行模块化本地测试集
        runAllLocalTests();
    }
    return 0;
}
