#pragma once
#include <windows.h>
#include <string>
#include <stdexcept>
#include <utility>

// Held for the entire UI lifetime, including asynchronous recording shutdown.
class SingleInstance {
    HANDLE mutex{},ready{};bool owned{};
    std::wstring windowClass;
    void takeOwnership(){owned=true;ResetEvent(ready);}
    bool activateExisting() {
        auto window=FindWindowW(windowClass.c_str(),nullptr);
        if(!window)return false;
        DWORD process{};GetWindowThreadProcessId(window,&process);AllowSetForegroundWindow(process);
        DWORD_PTR response{};
        if(!SendMessageTimeoutW(window,activationMessage(),0,0,SMTO_ABORTIFHUNG,5000,&response))
            throw std::runtime_error("Screencast is already running but is not responding");
        if(response!=1){ShowWindowAsync(window,IsIconic(window)?SW_RESTORE:SW_SHOW);SetForegroundWindow(window);}
        return true;
    }
public:
    explicit SingleInstance(std::wstring name=L"SanderStripa.Screencast",std::wstring cls=L"ScreencastWindow")
        :windowClass(std::move(cls)) {
        mutex=CreateMutexW(nullptr,FALSE,(L"Local\\"+name+L".Instance").c_str());
        ready=CreateEventW(nullptr,TRUE,FALSE,(L"Local\\"+name+L".Ready").c_str());
        if(!mutex || !ready){if(mutex)CloseHandle(mutex);if(ready)CloseHandle(ready);throw std::runtime_error("Cannot initialize Screencast instance guard");}
    }
    ~SingleInstance(){if(owned)ReleaseMutex(mutex);CloseHandle(ready);CloseHandle(mutex);}
    SingleInstance(SingleInstance const&)=delete;
    SingleInstance& operator=(SingleInstance const&)=delete;
    static UINT activationMessage(){static UINT value=RegisterWindowMessageW(L"Screencast.ActivateExisting.v1");return value;}
    static void activate(HWND window) {
        if(IsIconic(window))ShowWindow(window,SW_RESTORE);
        else if(!IsWindowVisible(window))ShowWindow(window,SW_SHOW);
        BringWindowToTop(window);SetForegroundWindow(window);
    }
    bool acquire() {
        auto value=WaitForSingleObject(mutex,0);
        if(value==WAIT_OBJECT_0 || value==WAIT_ABANDONED) {
            takeOwnership();
            // Also reuse an older running version that predates this guard.
            return !activateExisting();
        }
        if(value!=WAIT_TIMEOUT)throw std::runtime_error("Cannot check Screencast instance");
        HANDLE handles[]{mutex,ready};value=WaitForMultipleObjects(2,handles,FALSE,15000);
        if(value==WAIT_OBJECT_0 || value==WAIT_ABANDONED_0){takeOwnership();return true;}
        if(value==WAIT_OBJECT_0+1 && activateExisting())return false;
        // The first window may have closed between the ready signal and lookup.
        value=WaitForSingleObject(mutex,1000);
        if(value==WAIT_OBJECT_0 || value==WAIT_ABANDONED){takeOwnership();return true;}
        throw std::runtime_error("Screencast is still starting; no second instance was opened");
    }
    void markReady(){SetEvent(ready);}
};
