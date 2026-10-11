#include "../src/single_instance.h"
#include <shellapi.h>
#include <shobjidl.h>
#include <filesystem>
#include <fstream>
#include <wrl.h>

namespace {
int activations{};
LRESULT CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l) {
    if(m==SingleInstance::activationMessage()){++activations;SingleInstance::activate(h);return 1;}
    return DefWindowProcW(h,m,w,l);
}
void require(bool value){if(!value)throw std::runtime_error("Single-instance test failed");}
void wait(HANDLE process) {
    auto deadline=GetTickCount64()+10000;
    while(WaitForSingleObject(process,0)==WAIT_TIMEOUT && GetTickCount64()<deadline) {
        MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
        MsgWaitForMultipleObjects(1,&process,FALSE,50,QS_ALLINPUT);
    }
    DWORD code{};require(GetExitCodeProcess(process,&code) && code==0);CloseHandle(process);
}
HANDLE launch(std::wstring const& exe,std::wstring const& scope) {
    auto command=L"\""+exe+L"\" --secondary \""+scope+L"\"";
    STARTUPINFOW startup{sizeof(startup)};startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;PROCESS_INFORMATION child{};
    require(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child)!=FALSE);
    CloseHandle(child.hThread);return child.hProcess;
}
void link(std::filesystem::path const& path,std::wstring const& exe,std::wstring const& scope) {
    Microsoft::WRL::ComPtr<IShellLinkW> shortcut;require(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&shortcut))));
    require(SUCCEEDED(shortcut->SetPath(exe.c_str())));require(SUCCEEDED(shortcut->SetArguments((L"--secondary \""+scope+L"\"").c_str())));
    Microsoft::WRL::ComPtr<IPersistFile> file;require(SUCCEEDED(shortcut.As(&file)));require(SUCCEEDED(file->Save(path.c_str(),TRUE)));
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    try {
        int count{};auto args=CommandLineToArgvW(GetCommandLineW(),&count);std::wstring scope=count>=3?args[2]:L"Screencast.InstanceTests."+std::to_wstring(GetCurrentProcessId());
        bool secondary=count>=2 && std::wstring(args[1])==L"--secondary";LocalFree(args);
        auto clsName=L"Screencast.Test."+scope;SingleInstance guard(scope,clsName);
        if(secondary)return guard.acquire()?3:0;
        require(guard.acquire());
        wchar_t filename[32768]{};GetModuleFileNameW(nullptr,filename,32768);std::wstring exe=filename;
        auto starting=launch(exe,scope);Sleep(150);require(WaitForSingleObject(starting,0)==WAIT_TIMEOUT);
        WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=procedure;cls.lpszClassName=clsName.c_str();require(RegisterClassW(&cls)!=0);
        auto window=CreateWindowW(clsName.c_str(),L"Instance test",WS_OVERLAPPEDWINDOW,0,0,320,180,nullptr,nullptr,instance,nullptr);require(window!=nullptr);guard.markReady();
        wait(starting);require(activations==1);
        require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)));
        auto root=std::filesystem::current_path()/L"instance-fixtures";std::filesystem::create_directories(root/L"Desktop");std::filesystem::create_directories(root/L"StartMenu");
        for(auto folder:{L"Desktop",L"StartMenu"}) {
            auto path=root/folder/L"Screencast.lnk";link(path,exe,scope);ShowWindow(window,SW_MINIMIZE);
            SHELLEXECUTEINFOW start{sizeof(start)};start.fMask=SEE_MASK_NOCLOSEPROCESS;start.lpFile=path.c_str();start.nShow=SW_HIDE;
            require(ShellExecuteExW(&start)!=FALSE && start.hProcess);wait(start.hProcess);require(!IsIconic(window));
        }
        require(activations==3);
        HANDLE children[5]{};for(auto& child:children)child=launch(exe,scope);for(auto child:children)wait(child);
        require(activations==8);DestroyWindow(window);CoUninitialize();return 0;
    }catch(std::exception const& error){std::ofstream file("instance-test-error.txt");file<<error.what();return 1;}
}
