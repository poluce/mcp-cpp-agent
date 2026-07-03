#include <QCommandLineParser>
#include <QApplication>
#include <QTimer>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonDocument>
#include <QNetworkProxyFactory>
#include <iostream>

#include "examples/multi_server_agent/AgentSession.h"
#include "examples/multi_server_agent/DiagnosticReporter.h"
#include "LlmBackends.h"
#include "AgentMainWindow.h"
#include "mcp_qt_client/McpServerManager.h"

#include <mutex>

// 声明定义在 AgentMainWindow.cpp 中的全局日志路径变量
extern QString g_logFilePath;

// 🌟 全局日志长连接文件指针与其互斥锁，杜绝高频并发 open/close 引发的文件锁自我死锁
QFile* g_logFile = nullptr;
std::mutex g_logMutex;

void updateGlobalLogFile(const QString& path) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logFile) {
        g_logFile->close();
        delete g_logFile;
        g_logFile = nullptr;
    }
    if (!path.isEmpty()) {
        g_logFile = new QFile(path);
        // 以追加、无缓冲模式长连接打开，close 由程序退出或路径更改触发
        if (!g_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text | QIODevice::Unbuffered)) {
            delete g_logFile;
            g_logFile = nullptr;
        }
    }
}

// 自定义 Qt 消息拦截处理器，支持控制台彩色输出与日志文件存盘
void myMessageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
    // 🌟 防重入互斥锁，避免在写文件报错时递归触发本处理器导致死循环崩溃
    static bool inHandler = false;
    if (inHandler) {
        std::cerr << msg.toStdString() << "\n";
        return;
    }
    inHandler = true;

    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    QString typeStr;
    switch (type) {
    case QtDebugMsg:    typeStr = "Debug"; break;
    case QtInfoMsg:     typeStr = "Info "; break;
    case QtWarningMsg:  typeStr = "Warn "; break;
    case QtCriticalMsg: typeStr = "Error"; break;
    case QtFatalMsg:    typeStr = "Fatal"; break;
    }

    QString logLine = QString("[%1] [%2] %3\n").arg(timestamp, typeStr, msg);

    // 1. 同步打印到控制台终端，供实时阅读
    std::cerr << logLine.toStdString() << std::flush;

    // 2. 如果全局日志文件已打开，直接高效写入（免去任何文件重开，防范文件锁死锁）
    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        if (g_logFile && g_logFile->isOpen()) {
            QTextStream stream(g_logFile);
            stream.setEncoding(QStringConverter::Utf8);
            stream << logLine;
        }
    }

    inHandler = false;
}

int main(int argc, char* argv[]) {
    // 安装拦截器，捕获所有 Qt 的调试与输出日志
    qInstallMessageHandler(myMessageOutput);

    // 默认走系统代理，解决海外节点网络联通问题
    QNetworkProxyFactory::setUseSystemConfiguration(true);

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("multi_server_agent"));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringList{QStringLiteral("config")}, QStringLiteral("Path to MCP server config"), QStringLiteral("config")});
    parser.addOption({QStringList{QStringLiteral("task")}, QStringLiteral("Task text for the ReAct agent"), QStringLiteral("task")});
    parser.addOption({QStringList{QStringLiteral("server")}, QStringLiteral("Restrict execution to one server"), QStringLiteral("server")});
    parser.addOption({QStringList{QStringLiteral("timeout-ms")}, QStringLiteral("Connection timeout in milliseconds"), QStringLiteral("timeout"), QStringLiteral("30000")});
    parser.addOption({QStringList{QStringLiteral("log-file")}, QStringLiteral("Path to save execution log file"), QStringLiteral("log-file")});
    
    // LLM 相关参数
    parser.addOption({QStringList{QStringLiteral("use-real-llm")}, QStringLiteral("Use real OpenAI/DeepSeek API instead of mock engine")});
    parser.addOption({QStringList{QStringLiteral("api-url")}, QStringLiteral("LLM chat completions API URL"), QStringLiteral("api-url")});
    parser.addOption({QStringList{QStringLiteral("api-key")}, QStringLiteral("LLM API Key (defaults to OPENAI_API_KEY env var)"), QStringLiteral("api-key")});
    parser.addOption({QStringList{QStringLiteral("model")}, QStringLiteral("LLM Model name"), QStringLiteral("model")});
    
    parser.process(app);

    // 优先采用命令行显式指定的 --log-file
    if (parser.isSet(QStringLiteral("log-file"))) {
        g_logFilePath = parser.value(QStringLiteral("log-file"));
    } else if (parser.isSet(QStringLiteral("config"))) {
        // 如果命令行未指定 --log-file，但指定了 --config，自动读取配置文件中配置的 logFile 并应用
        QString configPath = parser.value(QStringLiteral("config"));
        QFile file(configPath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            if (doc.isObject()) {
                g_logFilePath = doc.object()["logFile"].toString();
            }
            file.close();
        }
    }

    // 🌟 初始化开启全局长连接日志文件写盘句柄
    updateGlobalLogFile(g_logFilePath);

    // 🌟 如果没有传入 --task 参数，自动切换到精美的 GUI 可视化运行模式！
    if (!parser.isSet(QStringLiteral("task"))) {
        mcp_agent::AgentMainWindow w;
        w.show();
        return app.exec();
    }

    // ==========================================
    // 命令行模式 (有 --task 传入时)
    // ==========================================
    if (!parser.isSet(QStringLiteral("config"))) {
        qCritical() << "Error: Config file must be specified in command-line mode via --config <file>";
        return 2;
    }

    mcp_qt::McpServerManager manager;
    DiagnosticReporter reporter;
    
    std::shared_ptr<mcp_agent::ILlmBackend> backend;
    if (parser.isSet(QStringLiteral("use-real-llm"))) {
        QString apiUrl = parser.value(QStringLiteral("api-url"));
        QString apiKey = parser.value(QStringLiteral("api-key"));
        QString model = parser.value(QStringLiteral("model"));
        
        if (apiUrl.isEmpty()) {
            apiUrl = QStringLiteral("https://api.openai.com/v1/chat/completions");
        }
        if (apiKey.isEmpty()) {
            apiKey = qEnvironmentVariable("DEEPSEEK_API_KEY");
            if (apiKey.isEmpty()) {
                apiKey = qEnvironmentVariable("OPENAI_API_KEY");
                if (apiKey.isEmpty()) {
                    std::cerr << "Error: API Key is required when using real LLM. Provide via --api-key or environment variables.\n";
                    return 1;
                }
            }
        }
        if (model.isEmpty()) {
            model = QStringLiteral("deepseek-v4-flash");
        }
        backend = std::make_shared<mcp_agent::OpenAiLlmBackend>(apiUrl, apiKey, model, &app);
    } else {
        backend = std::make_shared<mcp_agent::MockLlmBackend>();
    }

    // 🌟 在命令行模式下每次运行的第一时间，清空日志文件
    if (!g_logFilePath.isEmpty()) {
        QFile file(g_logFilePath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.close();
        }
    }

    AgentSession session(&manager, backend, &reporter);

    QObject::connect(&session, &AgentSession::finished, &app, [&](int exitCode) {
        // 利用 qInfo() 代替 stdout/stderr 直写，自动写入指定的日志文件中
        qInfo().noquote() << "[main] finished signal received, exitCode=" << exitCode;
        qInfo().noquote() << "Agent Execution Log\n===================\n" << reporter.renderExecutionLog();
        qInfo().noquote() << "SDK Diagnostic Report\n=====================\n" << reporter.renderText();
        qInfo().noquote() << "[main] exiting via std::exit";
        std::exit(exitCode);
    });

    AgentRunOptions options;
    options.configPath = parser.value(QStringLiteral("config"));
    options.task = parser.value(QStringLiteral("task"));
    options.serverFilter = parser.value(QStringLiteral("server"));
    options.timeoutMs = parser.value(QStringLiteral("timeout-ms")).toInt();
    
    options.useRealLlm = parser.isSet(QStringLiteral("use-real-llm"));
    options.apiUrl = parser.value(QStringLiteral("api-url"));
    options.apiKey = parser.value(QStringLiteral("api-key"));
    options.modelName = parser.value(QStringLiteral("model"));

    QTimer::singleShot(0, &session, [options, &session]() {
        session.start(options);
    });

    return app.exec();
}
