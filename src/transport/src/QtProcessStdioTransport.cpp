#include "mcp_qt_transport/QtProcessStdioTransport.h"
#include <QDebug>
#include <QNetworkProxyFactory>
#include <QNetworkProxyQuery>
#include <QUrl>
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
    
    // 构建进程环境变量：先注入系统代理，再叠加用户自定义配置
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    // 自动探测系统 HTTP/SOCKS5 代理并注入（仅当用户未手动指定任何代理变量时）
    {
        bool userHasProxy = m_env.count("HTTP_PROXY") || m_env.count("http_proxy")
                         || m_env.count("HTTPS_PROXY") || m_env.count("https_proxy");

        if (!userHasProxy) {
            QList<QNetworkProxy> proxies = QNetworkProxyFactory::systemProxyForQuery(
                QNetworkProxyQuery(QUrl(QStringLiteral("https://www.google.com"))));
            if (!proxies.isEmpty()) {
                const QNetworkProxy& proxy = proxies.first();
                if (proxy.type() == QNetworkProxy::HttpProxy || proxy.type() == QNetworkProxy::Socks5Proxy) {
                    QString proxyStr = QStringLiteral("%1://%2:%3")
                        .arg(proxy.type() == QNetworkProxy::HttpProxy ? QStringLiteral("http") : QStringLiteral("socks5"))
                        .arg(proxy.hostName())
                        .arg(proxy.port());
                    env.insert(QStringLiteral("HTTP_PROXY"), proxyStr);
                    env.insert(QStringLiteral("HTTPS_PROXY"), proxyStr);
                    env.insert(QStringLiteral("http_proxy"), proxyStr);
                    env.insert(QStringLiteral("https_proxy"), proxyStr);
                    qDebug() << "[QtProcessStdioTransport] Auto-detected system proxy:" << proxyStr;
                }
            }
        }
    }

    // 叠加用户自定义环境变量（会覆盖上面自动注入的同名变量）
    for (const auto& kv : m_env) {
        env.insert(QString::fromStdString(kv.first), QString::fromStdString(kv.second));
    }
    m_process->setProcessEnvironment(env);
    
    QStringList qargs;
    for (const auto& a : m_args) qargs.push_back(QString::fromStdString(a));
    
    m_process->start(QString::fromStdString(m_command), qargs);
    m_started = true;
    return true;
}

void QtProcessStdioTransport::close() {
    std::function<void()> closeCb;
    QProcess* p = nullptr;
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
        
        p = m_process;
        closeCb = m_onClose;
    }
    
    if (p && p->state() != QProcess::NotRunning) {
        p->terminate();
        if (!p->waitForFinished(500)) {
            p->kill();
            p->waitForFinished(500);
        }
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
    std::function<void(const std::string&)> msgCb;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_buffer.append(data.constData(), data.size());

        size_t pos = 0;
        while ((pos = m_buffer.find('\n')) != std::string::npos) {
            std::string line = m_buffer.substr(0, pos);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            messages.push_back(std::move(line));
            m_buffer.erase(0, pos + 1);
        }
        msgCb = m_onMessage;
    }

    if (!msgCb) return;

    for (const auto& msg : messages) {
        if (msg.empty()) continue;

        auto start = msg.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;

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

void QtProcessStdioTransport::handleReadyReadStandardError() {
    QByteArray data = m_process->readAllStandardError();
    // stderr 是服务端日志通道，不是传输层故障 —— 通过 serverLog 信号向上报告
    // 使用 fromLocal8Bit 兼容 Windows GBK 等本地编码（MCP 规范建议 UTF-8，但 Windows 子进程未必遵守）
    emit serverLog(QString::fromLocal8Bit(data));
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
    std::function<void()> closeCb;
    std::function<void(const std::string&)> errCb;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_started) return;
        m_started = false;
        errCb = m_onError;
        closeCb = m_onClose;
    }
    if (errCb) {
        errCb("QProcess error occurred: " + std::to_string(error));
    }
    if (closeCb) {
        closeCb();
    }
}

} // namespace mcp_qt
