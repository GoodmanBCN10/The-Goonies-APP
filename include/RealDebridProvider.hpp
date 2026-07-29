#pragma once
#include <string>
#include "nx_thread.hpp"
#include <atomic>
#include <mutex>
#include <functional>

class RealDebridProvider {
public:
    RealDebridProvider();
    virtual ~RealDebridProvider();

    bool Initialize();
    void Shutdown();
    
    int StartDownload(const std::string& torrentPathOrUrl, const std::string& outputFolder);
    void StopDownload(int downloadId);
    
    std::string GetUnlockedLinkFromMagnet(const std::string& magnetLink, std::function<void(const std::string&, float)> progressCallback, const std::atomic<bool>& cancelFlag);
    
    
    // Auth specific methods
    bool IsAuthenticated();
    void Logout();
    bool StartOAuthDeviceFlow();
    std::string GetUserCode() const;
    std::string GetVerificationUrl() const;
    bool IsAuthPending() const;
    bool DidAuthFail() const;
    
private:
    std::string m_accessToken;
    std::string m_refreshToken;
    
    // OAuth state
    std::string m_deviceCode;
    std::string m_userCode;
    std::string m_verificationUrl;
    std::atomic<bool> m_authPending{false};
    std::atomic<bool> m_authFailed{false};
    std::atomic<bool> m_authSuccess{false};
    std::atomic<bool> m_stopAuthThread{false};
    nx::thread m_authThread;
    
    void AuthPollingThread();
    void LoadTokens();
    void SaveTokens();
    
    // HTTP helper
    std::string HttpGet(const std::string& url, const std::string& authHeader = "");
    std::string HttpPost(const std::string& url, const std::string& postData, const std::string& authHeader = "");
};
