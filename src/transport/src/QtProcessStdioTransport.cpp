#include "mcp_qt_transport/QtProcessStdioTransport.h"
#include <QDebug>
#include <string_view>

namespace mcp_qt {

QtProcessStdioTransport::QtProcessStdioTransport(const std::string& command, const std::vector<std::string>& args, QObject* parent)
    : QObject(parent), m_command(command), m_args(args), m_process(new QProcess(this)) {
    connect(m_process, &QProcess::readyReadStandardOutput, this, &QtProcessStdioTransport::handleReadyReadStandardOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, &QtProcessStdioTransport::handleReadyReadStandardError);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &QtProcessStdioTransport::handleProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &QtProcessStdioTransport::handleProcessError);

#ifdef _WIN32
    connect(m_process, &QProcess::started, this, [this]() {
        if (m_jobObject) {
            qint64 pid = m_process->processId();
            if (pid > 0) {
                HANDLE hProcess = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
                if (hProcess) {
                    AssignProcessToJobObject(m_jobObject, hProcess);
                    CloseHandle(hProcess);
                }
            }
        }
    });
#endif
}

QtProcessStdioTransport::~QtProcessStdioTransport() {
    if (m_process) {
        m_process->disconnect();
    }
    close();
}

void QtProcessStdioTransport::setEnvironment(const std::unordered_map<std::string, std::string>& env) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_env = env;
}

bool QtProcessStdioTransport::start() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_started) return true;

#ifdef _WIN32
    if (!m_jobObject) {
        m_jobObject = CreateJobObjectW(nullptr, nullptr);
        if (m_jobObject) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION info;
            ZeroMemory(&info, sizeof(info));
            info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!SetInformationJobObject(m_jobObject, JobObjectExtendedLimitInformation, &info, sizeof(info))) {
                CloseHandle(m_jobObject);
                m_jobObject = nullptr;
            }
        }
    }
#endif
    
    if (!m_env.empty()) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        for (const auto& kv : m_env) {
            env.insert(QString::fromStdString(kv.first), QString::fromStdString(kv.second));
        }
        m_process->setProcessEnvironment(env);
    }
    
    QStringList qargs;
    for (const auto& a : m_args) qargs.push_back(QString::fromStdString(a));
    
    m_process->start(QString::fromStdString(m_command), qargs);
    m_started = true;
    return true;
}

void QtProcessStdioTransport::close() {
    std::function<void()> closeCb;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
#ifdef _WIN32
        if (m_jobObject) {
            CloseHandle(m_jobObject);
            m_jobObject = nullptr;
        }
#endif
        if (!m_started) return;
        m_started = false;
        
        if (m_process->state() != QProcess::NotRunning) {
            m_process->terminate();
            if (!m_process->waitForFinished(500)) {
                m_process->kill();
                m_process->waitForFinished(500);
            }
        }
        closeCb = m_onClose;
    }
    
    if (closeCb) {
        closeCb();
    }
}

bool QtProcessStdioTransport::send(const std::string& message) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_started || (m_process->state() != QProcess::Running && m_process->state() != QProcess::Starting)) return false;
    
    m_process->write(message.c_str(), message.size());
    m_process->write("\n", 1);
    m_process->waitForBytesWritten(100);
    return true;
}

void QtProcessStdioTransport::setOnMessage(std::function<void(const std::string&)> callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_onMessage = std::move(callback);
}

void QtProcessStdioTransport::setOnClose(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_onClose = std::move(callback);
}

void QtProcessStdioTransport::setOnError(std::function<void(const std::string&)> callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_onError = std::move(callback);
}

void QtProcessStdioTransport::setProtocolVersion(const std::string&) {
    // Stdio doesn't negotiate protocol version at the transport level
}

void QtProcessStdioTransport::handleReadyReadStandardOutput() {
    QByteArray data = m_process->readAllStandardOutput();
    
    std::vector<std::string> messages;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_buffer.append(data.constData(), data.size());
        
        size_t pos = 0;
        while ((pos = m_buffer.find('\n')) != std::string::npos) {
            std::string line = m_buffer.substr(0, pos);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            messages.push_back(line);
            m_buffer.erase(0, pos + 1);
        }
    }
    
    std::function<void(const std::string&)> msgCb;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        msgCb = m_onMessage;
    }
    
    if (msgCb) {
        for (const auto& msg : messages) {
            if (!msg.empty()) {
                auto start = msg.find_first_not_of(" \t\r\n");
                if (start != std::string::npos) {
                    auto end = msg.find_last_not_of(" \t\r\n");
                    std::string_view trimmed(msg.data() + start, end - start + 1);
                    
                    if (trimmed.front() == '{' && trimmed.back() == '}') {
                        msgCb(msg);
                    } else {
                        qWarning() << "[QtProcessStdioTransport] Filtered out dirty stdout message:"
                                   << QString::fromStdString(msg);
                    }
                }
            }
        }
    }
}

void QtProcessStdioTransport::handleReadyReadStandardError() {
    QByteArray data = m_process->readAllStandardError();
    std::string errStr(data.constData(), data.size());
    std::function<void(const std::string&)> errCb;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        errCb = m_onError;
    }
    if (errCb) {
        errCb(errStr);
    } else {
        qDebug() << "[MCP Server stderr]" << data;
    }
}

void QtProcessStdioTransport::handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    std::function<void()> closeCb;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_started) return;
        m_started = false;
        closeCb = m_onClose;
    }
    if (closeCb) {
        closeCb();
    }
}

void QtProcessStdioTransport::handleProcessError(QProcess::ProcessError error) {
    std::string errStr = "QProcess error occurred: " + std::to_string(error);
    std::function<void(const std::string&)> errCb;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        errCb = m_onError;
    }
    if (errCb) {
        errCb(errStr);
    }
    handleProcessFinished(-1, QProcess::CrashExit);
}

} // namespace mcp_qt
