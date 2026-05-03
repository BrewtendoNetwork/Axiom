#include <3ds.h>
#include <3ds/services/httpc.h>
#include "CTRPluginFramework.hpp"
#include "HTTPSessionManager.hpp"

namespace CTRPluginFramework
{
    HTTPSessionManager::HTTPSessionManager() : handle(), lastTxnTime(0), isActive(false)
    {
        memset(&handle, 0, sizeof(handle));
    }

    HTTPSessionManager::~HTTPSessionManager()
    {
        close();
    }

    bool HTTPSessionManager::init()
    {
        Lock lock(sessionMutex);
        
        if (isActive) return true;

        Result rc = httpcOpenContext(&handle, HTTPC_METHOD_GET, "http://dummy", 1);
        if (R_FAILED(rc)) {
            return false;
        }

        httpcSetSSLOpt(&handle, SSLCOPT_DisableVerify);
        httpcSetKeepAlive(&handle, HTTPC_KEEPALIVE_ENABLED);

        isActive = true;
        lastTxnTime = osGetTime();
        return true;
    }

    bool HTTPSessionManager::ensureValid()
    {
        Lock lock(sessionMutex);

        if (!isActive) {
            return init();
        }

        u64 currentTime = osGetTime();
        const u64 SESSION_TIMEOUT_MS = 30000; // 30 second timeout
        
        if ((currentTime - lastTxnTime) > SESSION_TIMEOUT_MS) {
            close();
            return init();
        }

        return true;
    }

    void HTTPSessionManager::updateActivity()
    {
        Lock lock(sessionMutex);
        lastTxnTime = osGetTime();
    }

    void HTTPSessionManager::close()
    {
        if (isActive) {
            httpcCloseContext(&handle);
            memset(&handle, 0, sizeof(handle));
            isActive = false;
        }
    }

    void HTTPSessionManager::reset()
    {
        Lock lock(sessionMutex);
        close();
        init();
    }

    httpcContext* HTTPSessionManager::getHandle()
    {
        if (ensureValid()) {
            return &handle;
        }
        return nullptr;
    }

    static HTTPSessionManager miiverseSession;

    HTTPSessionManager& getMiiverseSession()
    {
        return miiverseSession;
    }

    void initHTTPSessions()
    {
        getMiiverseSession().init();
    }

    void finiHTTPSessions()
    {
        getMiiverseSession().close();
    }

    void resetMiiverseSession()
    {
        getMiiverseSession().reset();
    }

    void updateMiiverseSessionActivity()
    {
        getMiiverseSession().updateActivity();
    }

    bool validateMiiverseSession()
    {
        return getMiiverseSession().ensureValid();
    }
}
