#pragma once

#include <3ds.h>
#include <3ds/services/httpc.h>
#include <CTRPluginFramework.hpp>

namespace CTRPluginFramework
{
    class HTTPSessionManager
    {
    public:
        HTTPSessionManager();
        ~HTTPSessionManager();

        bool ensureValid();
        void updateActivity();
        void reset();
        httpcContext* getHandle();

        bool init();
        void close();

    private:
        httpcContext handle;
        u64 lastTxnTime;
        bool isActive;
        Mutex sessionMutex;
    };

    HTTPSessionManager& getMiiverseSession();
    void initHTTPSessions();
    void finiHTTPSessions();
    void resetMiiverseSession();
    void updateMiiverseSessionActivity();
    bool validateMiiverseSession();
}
