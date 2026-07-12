#include "mcp_qt_transport/QtProcessStdioTransport.h"

#include <QCoreApplication>
#include <QDebug>
#include <QNetworkProxyFactory>
#include <QNetworkProxyQuery>
#include <QThread>
#include <QUrl>

#include <string_view>

namespace mcp_qt {

namespace {

// systemProxyForQuery 在部分 Windows/PAC 环境下可能阻塞很久；进程级缓存一次即可。
QString cachedSystemProxyUrl()
{
    static const QString kProxy = []() -> QString {
        const QList<QNetworkProxy> proxies = QNetworkProxyFactory::systemProxyForQuery(
            QNetworkProxyQuery(QUrl(QStringLiteral("https://example.com"))));
        if (proxies.isEmpty()) {
            return {};
        }

        const QNetworkProxy &proxy = proxies.first();
        if (proxy.type() != QNetworkProxy::HttpProxy && proxy.type() != QNetworkProxy::Socks5Proxy) {
            return {};
        }
        if (proxy.hostName().isEmpty() || proxy.port() <= 0) {
            return {};
        }

        const QString scheme = (proxy.type() == QNetworkProxy::HttpProxy)
            ? QStringLiteral("http")
            : QStringLiteral("socks5");
        return QStringLiteral("%1://%2:%3").arg(scheme, proxy.hostName()).arg(proxy.port());
    }();
    return kProxy;
}

bool onAppMainThread()
{
    const QCoreApplication *app = QCoreApplication::instance();
    return app && QThread::currentThread() == app->thread();
}

bool envHasProxy(const std::unordered_map<std::string, std::string> &env)
{
    return env.count("HTTP_PROXY") || env.count("http_proxy")
        || env.count("HTTPS_PROXY") || env.count("https_proxy");
}

void insertProxyEnv(QProcessEnvironment &env, const QString &proxyUrl)
{
    env.insert(QStringLiteral("HTTP_PROXY"), proxyUrl);
    env.insert(QStringLiteral("HTTPS_PROXY"), proxyUrl);
    env.insert(QStringLiteral("http_proxy"), proxyUrl);
    env.insert(QStringLiteral("https_proxy"), proxyUrl);
}

} // namespace

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
    // 代理探测可能很慢：不要持有 m_mutex，避免与 readyRead/close 死锁。
    bool userHasProxy = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        userHasProxy = envHasProxy(m_env);
    }

    QString proxyUrl;
    if (!userHasProxy) {
        proxyUrl = cachedSystemProxyUrl();
        if (!proxyUrl.isEmpty()) {
            qDebug() << "[QtProcessStdioTransport] Auto-detected system proxy:" << proxyUrl;
        }
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_started) {
        return true;
    }

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

    // 先注入系统代理，再叠加用户自定义环境变量（后者覆盖同名键）
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (!proxyUrl.isEmpty()) {
        insertProxyEnv(env, proxyUrl);
    }
    for (const auto &kv : m_env) {
        env.insert(QString::fromStdString(kv.first), QString::fromStdString(kv.second));
    }
    m_process->setProcessEnvironment(env);

    QStringList qargs;
    for (const auto &a : m_args) {
        qargs.push_back(QString::fromStdString(a));
    }

    m_process->start(QString::fromStdString(m_command), qargs);
    m_started = true;
    return true;
}

void QtProcessStdioTransport::close() {
    std::function<void()> closeCb;
    QProcess *p = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
#ifdef _WIN32
        if (m_jobObject) {
            CloseHandle(m_jobObject);
            m_jobObject = nullptr;
        }
#endif
        if (!m_started) {
            return;
        }
        m_started = false;

        p = m_process;
        closeCb = m_onClose;
    }

    if (p && p->state() != QProcess::NotRunning) {
        // 主线程禁止 waitForFinished：子进程（npx 等）卡住时会直接冻死 UI。
        if (onAppMainThread()) {
            p->kill();
        } else {
            p->terminate();
            if (!p->waitForFinished(500)) {
                p->kill();
                p->waitForFinished(500);
            }
        }
    }

    if (closeCb) {
        closeCb();
    }
}

bool QtProcessStdioTransport::send(const std::string& message) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_started || (m_process->state() != QProcess::Running && m_process->state() != QProcess::Starting)) {
        return false;
    }

    m_process->write(message.c_str(), message.size());
    m_process->write("\n", 1);
    // 主线程不要同步等写完成；后台线程保留短超时。
    if (!onAppMainThread()) {
        m_process->waitForBytesWritten(100);
    }
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
