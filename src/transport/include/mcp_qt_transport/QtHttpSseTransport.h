#pragma once

#include <functional>
#include <memory>
#include <string>
#include <QMap>
#include <QByteArray>
#include <QNetworkProxy>
#include <optional>

#include "mcp_core/IMcpTransport.h"

namespace mcp_qt {

struct QtHttpRequestConfig {
    QMap<QByteArray, QByteArray> defaultHeaders;
    std::optional<QNetworkProxy> proxy;
    bool allowAuthorizationOverride{true};
};

class QtHttpSseTransport final : public mcp::IMcpTransport {
public:
    using TokenProvider = std::function<std::string()>;
    using AuthRetryHandler = std::function<bool(const std::string&)>;

    explicit QtHttpSseTransport(const std::string& baseUrl);
    ~QtHttpSseTransport() override;

    bool send(const std::string& message) override;
    void setOnMessage(std::function<void(const std::string&)> callback) override;
    void setOnClose(std::function<void()> callback) override;
    void setOnError(std::function<void(const std::string&)> callback) override;
    bool start() override;
    void close() override;
    void setProtocolVersion(const std::string& version) override;

    void setTokenProvider(TokenProvider provider);
    void setAuthRetryHandler(AuthRetryHandler handler);

    void setRequestConfig(const QtHttpRequestConfig& config);
    QtHttpRequestConfig requestConfig() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mcp_qt
