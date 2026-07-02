#include <QCoreApplication>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>
#include <QTimer>
#include <memory>

#include <mcp_qt_client/McpQtClient.h>
#include <mcp_qt_client/McpServerManager.h>
#include <mcp_qt_client/McpToolRouter.h>

// ============================================================================
// 3. 动态刷新适配器：模拟主项目中的 ToolCoordinator
// ============================================================================
class ToolCoordinator : public QObject {
    Q_OBJECT
public:
    ToolCoordinator(mcp_qt::McpServerManager* manager, mcp_qt::McpToolRouter* router, QObject* parent = nullptr)
        : QObject(parent), m_manager(manager), m_router(router) {
        
        // 监听来自管理器的客户端连接和工具变更信号
        connect(m_manager, &mcp_qt::McpServerManager::clientConnected, this, &ToolCoordinator::onClientConnected);
        connect(m_manager, &mcp_qt::McpServerManager::clientToolsChanged, this, &ToolCoordinator::onServerToolsChanged);
        connect(m_manager, &mcp_qt::McpServerManager::clientErrorOccurred, this, &ToolCoordinator::onClientError);
    }

signals:
    // 触发系统提示词（System Prompt）重绘，通知 LLM 刷新工具集的信号
    void toolsRefreshed(const QJsonArray& updatedLlmTools);

private slots:
    void onClientConnected(const QString& serverName) {
        qDebug() << "[ToolCoordinator] 客户端已成功连接到服务器:" << serverName;
        // 连接成功后，可能需要第一次刷新 LLM 工具
        refreshLlmTools();
    }

    void onServerToolsChanged(const QString& serverName, const std::vector<mcp_qt::McpQtTool>& newTools) {
        qDebug() << "[ToolCoordinator] 服务器" << serverName << "发生了工具热更新！当前可用工具数:" << newTools.size();
        // 服务器动态刷新了工具，触发重绘
        refreshLlmTools();
    }

    void onClientError(const QString& serverName, const QString& error) {
        qWarning() << "[ToolCoordinator] 客户端" << serverName << "发生错误:" << error;
    }

private:
    void refreshLlmTools() {
        // 利用 McpToolRouter 将当前所有已连接服务器的缓存工具进行 namespace 前缀合并
        QJsonArray updatedTools = m_router->exportAllToolsToLlmFormat(mcp_qt::McpQtClient::LlmFormat::OpenAI);
        
        qDebug() << "\n[ToolCoordinator] === 重新生成带命名空间前缀的 LLM 工具集 ===";
        qDebug() << QJsonDocument(updatedTools).toJson(QJsonDocument::Indented);
        qDebug() << "=========================================================\n";

        emit toolsRefreshed(updatedTools);
    }

    mcp_qt::McpServerManager* m_manager;
    mcp_qt::McpToolRouter* m_router;
};

// 主程序演示
int main(int argc, char *argv[]) {
    QCoreApplication a(argc, argv);

    qDebug() << "=== MCP 多服务器丝滑接入粘合组件演示程序 ===";

    // 1. 初始化多服务器生命周期管理器
    mcp_qt::McpServerManager serverManager;

    // 2. 初始化工具命名空间与路由层
    mcp_qt::McpToolRouter toolRouter(&serverManager);

    // 3. 初始化工具协调器进行桥接
    ToolCoordinator coordinator(&serverManager, &toolRouter);

    // 连接信号：监听协调器的提示词重绘通知
    QObject::connect(&coordinator, &ToolCoordinator::toolsRefreshed, [](const QJsonArray& updatedTools) {
        qDebug() << "[LLM Integration] 已将最新合并工具集更新至系统提示词，通知模型当前可用工具数:" << updatedTools.size();
    });

    // 模拟配置文件：假设我们有两个服务器 config
    // - "github": Stdio 模式启动 Python / Node.js 脚本 (这里只作声明，不真正拉起，或者拉起失败)
    // - "mock-memory": 为了本地调试，手动注册一个模拟客户端，方便验证路由调用与刷新
    QJsonObject configJson{
        {"mcpServers", QJsonObject{
            {"github-mcp", QJsonObject{
                {"command", "node"},
                {"args", QJsonArray{"github_mcp_server.js"}},
                {"env", QJsonObject{
                    {"GITHUB_PERSONAL_ACCESS_TOKEN", "mock_token_123"}
                }}
            }},
            {"figma-mcp", QJsonObject{
                {"url", "http://localhost:3000/sse"}
            }}
        }}
    };

    qDebug() << "\n[Config] 正在加载配置文件...";
    serverManager.loadConfig(configJson);

    // 为了在没有物理运行 MCP 服务器的本地环境中演示核心路由功能：
    // 我们手动创建并注册一个 Mock 客户端，包含两个自定义工具
    qDebug() << "\n[Demo] 注册本地模拟客户端 'mock-memory' 以演示路由与调用...";
    auto mockClient = mcp_qt::McpQtClient::createForTest(&serverManager);
    
    // 注入模拟工具到 Mock 客户端的内部缓存
    // 这些工具将在 toolsChanged 信号触发或导出时被 router 读取
    mcp_qt::McpQtTool tool1{QStringLiteral("search_notes"), QStringLiteral("搜索记忆库中的笔记"), QJsonObject{}};
    mcp_qt::McpQtTool tool2{QStringLiteral("save_note"), QStringLiteral("保存新笔记至本地记忆库"), QJsonObject{}};
    
    serverManager.registerClient(QStringLiteral("mock-memory"), mockClient);

    // 模拟触发 toolsChanged 信号，这将会触发 ToolCoordinator 热刷新
    qDebug() << "[Demo] 模拟 mock-memory 服务端返回工具列表，触发 toolsChanged 信号...";
    emit mockClient->toolsChanged({tool1, tool2});

    // 4. 演示路由层 McpToolRouter 的解析与分发
    QString testLlmCallName = QStringLiteral("mock-memory_search_notes");
    QJsonObject testLlmArgs{{"query", "C++ Qt MCP integration"}};

    qDebug() << "\n[LLM Call] 模型决定调用带命名空间的工具:" << testLlmCallName;
    auto parsed = toolRouter.parseToolName(testLlmCallName);
    qDebug() << "[Router] 解析结果 -> 服务器:" << parsed.first << ", 原始工具名:" << parsed.second;

    // 因为是 createForTest，调用其 callTool 会返回空，但这证明了路由调用已经成功投递到对应的 Mock 客户端
    qDebug() << "[Router] 正在转发调用到该客户端...";
    toolRouter.callToolAsync(testLlmCallName, testLlmArgs, [](mcp_qt::McpResult res) {
        qDebug() << "[LLM Return] 得到工具返回结果 (isError:" << res.isError << ", errorString:" << res.errorString << ")";
    });

    // 延时退出程序
    QTimer::singleShot(2000, &a, &QCoreApplication::quit);

    return a.exec();
}

#include "main.moc"
