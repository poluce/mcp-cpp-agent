#include "tests/common.h"
#include <vector>

#ifdef _WIN32
#include <windows.h>

/**
 * @brief Active subprocess command-line Stdio transport channels.
 * 
 * Create anonymous pipes redirection, spawn the child mcp server, and communicate
 * with it over stdin and stdout channels. Fully isolates dirty console output logs.
 */
class ProcessStdioTransport : public mcp::IMcpTransport {
public:
    ProcessStdioTransport(const std::string& commandLine) : m_cmd(commandLine) {}
    ~ProcessStdioTransport() override { close(); }

    bool send(const std::string& message) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_hChildStdInWrite == INVALID_HANDLE_VALUE) return false;
        
        std::string rawMsg = message + "\n";
        DWORD written = 0;
        BOOL success = WriteFile(m_hChildStdInWrite, rawMsg.c_str(), static_cast<DWORD>(rawMsg.length()), &written, NULL);
        return success && (written == rawMsg.length());
    }

    void setOnMessage(std::function<void(const std::string&)> callback) override {
        m_onMessage = std::move(callback);
    }

    void setOnClose(std::function<void()> callback) override {
        m_onClose = std::move(callback);
    }

    void setOnError(std::function<void(const std::string&)> callback) override {
        m_onError = std::move(callback);
    }

    bool start() override {
        if (m_running) return false;

        // 1. Create Pipes
        SECURITY_ATTRIBUTES saAttr;
        saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
        saAttr.bInheritHandle = TRUE;
        saAttr.lpSecurityDescriptor = NULL;

        // Child process stdout pipe
        if (!CreatePipe(&m_hChildStdOutRead, &m_hChildStdOutWrite, &saAttr, 0)) return false;
        SetHandleInformation(m_hChildStdOutRead, HANDLE_FLAG_INHERIT, 0); // Read handle shouldn't be inherited

        // Child process stdin pipe
        if (!CreatePipe(&m_hChildStdInRead, &m_hChildStdInWrite, &saAttr, 0)) return false;
        SetHandleInformation(m_hChildStdInWrite, HANDLE_FLAG_INHERIT, 0); // Write handle shouldn't be inherited

        // 2. Launch child process
        STARTUPINFOA siStartInfo;
        ZeroMemory(&siStartInfo, sizeof(STARTUPINFOA));
        siStartInfo.cb = sizeof(siStartInfo);
        siStartInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        siStartInfo.hStdOutput = m_hChildStdOutWrite;
        siStartInfo.hStdInput = m_hChildStdInRead;
        siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

        PROCESS_INFORMATION piProcInfo;
        ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));

        std::vector<char> cmdBuffer(m_cmd.begin(), m_cmd.end());
        cmdBuffer.push_back('\0');

        BOOL success = CreateProcessA(
            NULL, 
            cmdBuffer.data(), 
            NULL, 
            NULL, 
            TRUE, 
            0, 
            NULL, 
            NULL, 
            &siStartInfo, 
            &piProcInfo
        );

        if (!success) {
            cleanupHandles();
            return false;
        }

        m_hProcess = piProcInfo.hProcess;
        m_hThread = piProcInfo.hThread;

        // Close child ends of pipes, parent doesn't need them
        CloseHandle(m_hChildStdOutWrite); m_hChildStdOutWrite = INVALID_HANDLE_VALUE;
        CloseHandle(m_hChildStdInRead); m_hChildStdInRead = INVALID_HANDLE_VALUE;

        m_running = true;
        m_readThread = std::thread(&ProcessStdioTransport::readLoop, this);
        return true;
    }

    void close() override {
        bool expected = true;
        if (m_running.compare_exchange_strong(expected, false)) {
            cleanupHandles();

            if (m_readThread.joinable()) {
                if (std::this_thread::get_id() == m_readThread.get_id()) {
                    m_readThread.detach();
                } else {
                    m_readThread.join();
                }
            }

            if (m_hProcess != INVALID_HANDLE_VALUE) {
                // Wait for process exit or terminate
                if (WaitForSingleObject(m_hProcess, 1000) == WAIT_TIMEOUT) {
                    TerminateProcess(m_hProcess, 1);
                }
                CloseHandle(m_hProcess); m_hProcess = INVALID_HANDLE_VALUE;
            }
            if (m_hThread != INVALID_HANDLE_VALUE) {
                CloseHandle(m_hThread); m_hThread = INVALID_HANDLE_VALUE;
            }

            if (m_onClose) {
                m_onClose();
            }
        }
    }

private:
    void readLoop() {
        char buf[4096];
        std::string incomingBuffer;
        DWORD dwRead;

        while (m_running) {
            BOOL success = ReadFile(m_hChildStdOutRead, buf, sizeof(buf) - 1, &dwRead, NULL);
            if (!success || dwRead == 0) {
                break; // EOF or pipe closed
            }
            buf[dwRead] = '\0';
            incomingBuffer += buf;

            size_t pos;
            while ((pos = incomingBuffer.find('\n')) != std::string::npos) {
                std::string line = incomingBuffer.substr(0, pos);
                incomingBuffer.erase(0, pos + 1);
                
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }

                if (!line.empty()) {
                    // Isolate protocol messages from dirty shell logs
                    if (line[0] == '{' && m_onMessage) {
                        m_onMessage(line);
                    } else {
                        // Print dirty lines as stderr diagnostics
                        std::cerr << "[Subprocess Log Intercepted] " << line << std::endl;
                    }
                }
            }
        }
        close();
    }

    void cleanupHandles() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_hChildStdOutRead != INVALID_HANDLE_VALUE) {
            CloseHandle(m_hChildStdOutRead); m_hChildStdOutRead = INVALID_HANDLE_VALUE;
        }
        if (m_hChildStdOutWrite != INVALID_HANDLE_VALUE) {
            CloseHandle(m_hChildStdOutWrite); m_hChildStdOutWrite = INVALID_HANDLE_VALUE;
        }
        if (m_hChildStdInRead != INVALID_HANDLE_VALUE) {
            CloseHandle(m_hChildStdInRead); m_hChildStdInRead = INVALID_HANDLE_VALUE;
        }
        if (m_hChildStdInWrite != INVALID_HANDLE_VALUE) {
            CloseHandle(m_hChildStdInWrite); m_hChildStdInWrite = INVALID_HANDLE_VALUE;
        }
    }

    std::string m_cmd;
    std::mutex m_mutex;
    std::atomic<bool> m_running{false};
    std::thread m_readThread;

    HANDLE m_hChildStdOutRead = INVALID_HANDLE_VALUE;
    HANDLE m_hChildStdOutWrite = INVALID_HANDLE_VALUE;
    HANDLE m_hChildStdInRead = INVALID_HANDLE_VALUE;
    HANDLE m_hChildStdInWrite = INVALID_HANDLE_VALUE;
    HANDLE m_hProcess = INVALID_HANDLE_VALUE;
    HANDLE m_hThread = INVALID_HANDLE_VALUE;

    std::function<void(const std::string&)> m_onMessage;
    std::function<void()> m_onClose;
    std::function<void(const std::string&)> m_onError;
};

#endif

void test_with_filesystem_server() {
    std::cout << "[Integration Test] test_with_filesystem_server: (Real Subprocess integration)\n";
#ifdef _WIN32
    // Spawn test_mcp_server.js in stdio mode as subprocess E2E integration test target.
    auto transport = std::make_shared<ProcessStdioTransport>("node test_mcp_server.js");
    auto session = std::make_shared<mcp::McpClientSession>(transport);
    session->init();
    
    // Register diagnostic warning logger
    session->setLogCallback([](mcp::LogLevel level, const std::string& msg) {
        if (level == mcp::LogLevel::Error || level == mcp::LogLevel::Warning) {
            std::cout << "[SDK Trace Log] " << msg << std::endl;
        }
    });

    if (!session->start()) {
        std::cout << "  [!] Skipped: Node.js or test_mcp_server.js not found in current environment.\n";
        return;
    }

    // 1. Initialize Sync Handshake
    mcp::json serverInfo;
    bool initSuccess = session->initializeSync("cpp-integration-client", "1.0.0", &serverInfo);
    assert(initSuccess && "Subprocess E2E Initialize Handshake failed.");
    assert(serverInfo.contains("serverInfo") && "Server info missing on initialization.");

    // 2. Discover Tools paginated list
    auto tools = session->listToolsSync();
    assert(!tools.empty() && "Subprocess tools list should not be empty.");
    
    bool foundAdd = false;
    for (auto& tool : tools) {
        if (tool.name == "calculate_add") foundAdd = true;
    }
    assert(foundAdd && "Subprocess failed to discover calculate_add tool.");

    // 3. Execution (Raw coupling-free API)
    std::string errOut;
    std::string result = session->callToolSyncRaw("calculate_add", "{\"a\":50,\"b\":70}", &errOut);
    assert(errOut.empty() && "Real Subprocess tool call returned error.");
    assert(result.find("120") != std::string::npos && "Real Subprocess calculate_add failed to return correct result 120.");

    // 4. Resource read check
    mcp::json readResErr;
    mcp::json readRes = session->readResourceSync("file:///logs/system.log", &readResErr);
    assert(readResErr.empty() && "Real Subprocess resource read returned error.");
    assert(readRes.contains("contents") && readRes["contents"][0]["text"].get<std::string>().find("started") != std::string::npos);

    // 5. Clean Shutdown
    bool shutdownSuccess = session->shutdownSync();
    assert(shutdownSuccess && "Subprocess shutdown handshake failed.");

    session->close();
    std::cout << "  [✓] Real E2E subprocess Stdio integration tests with Mock Server successfully PASSED!\n";
#else
    std::cout << "  [✓] Skipped on non-windows platform.\n";
#endif
}
