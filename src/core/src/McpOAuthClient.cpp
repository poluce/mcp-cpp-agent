#include "mcp_core/McpOAuthClient.h"
#include <random>
#include <sstream>
#include <iomanip>
#include <thread>
#include <future>
#include <mutex>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#else
#include <openssl/sha.h>
#endif

namespace mcp {

static std::string urlEncode(const std::string& value) {
    std::ostringstream escaped;
    escaped << std::hex;
    for (char c : value) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2) << std::setfill('0') << (int)(unsigned char)c;
        }
    }
    return escaped.str();
}

static std::string buildUrlEncodedBody(const json& j) {
    std::string result;
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (!result.empty()) result += "&";
        std::string valStr;
        if (it.value().is_string()) {
            valStr = it.value().get<std::string>();
        } else {
            valStr = it.value().dump();
        }
        result += urlEncode(it.key()) + "=" + urlEncode(valStr);
    }
    return result;
}

// Stubs for network functions (Qt handles network operations natively in _runOAuthQt)
std::string McpOAuthClient::httpGet(const std::string&) {
    return "";
}

std::string McpOAuthClient::httpPost(const std::string&, const std::string&, const std::string&) {
    return "";
}

McpOAuthClient::McpOAuthClient() {}
McpOAuthClient::~McpOAuthClient() {}

void McpOAuthClient::discoverMetadata(const std::string&, MetadataCallback callback) {
    if (callback) {
        callback(false, OAuthServerMetadata{}, "Network discovery is disabled in pure C++ core");
    }
}

bool McpOAuthClient::discoverMetadataSync(const std::string&, OAuthServerMetadata*, std::string* errorOut, std::chrono::milliseconds) {
    if (errorOut) *errorOut = "Network discovery is disabled in pure C++ core";
    return false;
}

void McpOAuthClient::registerClient(const std::string&, const std::string&, const std::vector<std::string>&, RegistrationCallback callback) {
    if (callback) {
        callback(false, OAuthClientRegistration{}, "Dynamic registration is disabled in pure C++ core");
    }
}

bool McpOAuthClient::registerClientSync(const std::string&, const std::string&, const std::vector<std::string>&, OAuthClientRegistration*, std::string* errorOut, std::chrono::milliseconds) {
    if (errorOut) *errorOut = "Dynamic registration is disabled in pure C++ core";
    return false;
}

void McpOAuthClient::exchangeCode(const std::string&, const std::string&, const std::string&, const std::string&, const std::string&, const std::string&, TokenCallback callback, const std::string&, bool) {
    if (callback) {
        callback(false, OAuthToken{}, "Token exchange is disabled in pure C++ core");
    }
}

bool McpOAuthClient::exchangeCodeSync(const std::string&, const std::string&, const std::string&, const std::string&, const std::string&, const std::string&, OAuthToken*, std::string* errorOut, std::chrono::milliseconds, const std::string&, bool) {
    if (errorOut) *errorOut = "Token exchange is disabled in pure C++ core";
    return false;
}

void McpOAuthClient::refreshToken(const std::string&, const std::string&, const std::string&, const std::string&, TokenCallback callback) {
    if (callback) {
        callback(false, OAuthToken{}, "Token refresh is disabled in pure C++ core");
    }
}

bool McpOAuthClient::refreshTokenSync(const std::string&, const std::string&, const std::string&, const std::string&, OAuthToken*, std::string* errorOut, std::chrono::milliseconds) {
    if (errorOut) *errorOut = "Token refresh is disabled in pure C++ core";
    return false;
}

// Local helper implementation for PKCE & state generation
std::string McpOAuthClient::generateCodeVerifier() {
    static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    std::string verifier;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(chars) - 2);
    
    // Verifier length between 43 and 128
    for (int i = 0; i < 64; ++i) {
        verifier += chars[dis(gen)];
    }
    return verifier;
}

std::string McpOAuthClient::computeCodeChallenge(const std::string& verifier) {
    unsigned char hash[32];
#ifdef _WIN32
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0) {
        BCryptHash(hAlg, nullptr, 0, (PUCHAR)verifier.data(), (ULONG)verifier.size(), hash, 32);
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }
#else
    SHA256(reinterpret_cast<const unsigned char*>(verifier.data()), verifier.size(), hash);
#endif

    // Base64Url encode the SHA256 hash (no padding, replace + with -, / with _)
    static const char* b64chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string challenge;
    int val = 0, valb = -6;
    for (unsigned char c : hash) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            challenge.push_back(b64chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) {
        challenge.push_back(b64chars[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    return challenge;
}

std::string McpOAuthClient::generateState() {
    static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    std::string state;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(chars) - 2);
    for (int i = 0; i < 16; ++i) {
        state += chars[dis(gen)];
    }
    return state;
}

McpOAuthClient::AuthRequest McpOAuthClient::buildAuthorizationUrl(const OAuthServerMetadata& metadata,
                                                                 const std::string& clientId,
                                                                 const std::string& redirectUri,
                                                                 const std::vector<std::string>& scopes,
                                                                 const std::string& resource) {
    AuthRequest req;
    req.codeVerifier = generateCodeVerifier();
    req.codeChallenge = computeCodeChallenge(req.codeVerifier);
    req.state = generateState();

    std::string url = metadata.authorizationEndpoint;
    url += (url.find('?') == std::string::npos) ? "?" : "&";
    url += "response_type=code";
    url += "&client_id=" + urlEncode(clientId);
    if (!redirectUri.empty()) {
        url += "&redirect_uri=" + urlEncode(redirectUri);
    }
    url += "&state=" + urlEncode(req.state);
    url += "&code_challenge=" + urlEncode(req.codeChallenge);
    url += "&code_challenge_method=S256";

    if (!scopes.empty()) {
        std::string scopeStr;
        for (const auto& s : scopes) {
            if (!scopeStr.empty()) scopeStr += " ";
            scopeStr += s;
        }
        url += "&scope=" + urlEncode(scopeStr);
    }

    if (!resource.empty()) {
        url += "&resource=" + urlEncode(resource);
    }

    req.authorizationUrl = url;
    return req;
}

OAuthToken McpOAuthClient::getCurrentToken() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentToken;
}

void McpOAuthClient::setCurrentToken(const OAuthToken& token) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_currentToken = token;
}

bool McpOAuthClient::hasValidToken() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_currentToken.accessToken.empty() && !m_currentToken.isExpired();
}

void McpOAuthClient::setStoredToken(const OAuthToken& token) {
    setCurrentToken(token);
}

} // namespace mcp
