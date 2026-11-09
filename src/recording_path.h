#pragma once
#include <windows.h>
#include <objbase.h>
#include <winrt/base.h>
#include <filesystem>
#include <string>

inline std::wstring recordingPath(std::wstring const& folder,SYSTEMTIME const* timestamp=nullptr){
    auto fail=[](DWORD error){throw winrt::hresult_error(HRESULT_FROM_WIN32(error));};
    if(folder.empty())fail(ERROR_PATH_NOT_FOUND);
    DWORD attributes=GetFileAttributesW(folder.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES)fail(GetLastError());
    if(!(attributes&FILE_ATTRIBUTE_DIRECTORY))fail(ERROR_DIRECTORY);
    GUID id{};winrt::check_hresult(CoCreateGuid(&id));wchar_t guid[40]{};StringFromGUID2(id,guid,40);
    auto probe=(std::filesystem::path(folder)/(std::wstring(L".Screencast-write-test-")+guid)).wstring();
    HANDLE file=CreateFileW(probe.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
    if(file==INVALID_HANDLE_VALUE)fail(GetLastError());
    DWORD written{};char marker='S';BOOL ok=WriteFile(file,&marker,1,&written,nullptr);DWORD error=GetLastError();CloseHandle(file);
    if(!ok||written!=1)fail(ok?ERROR_WRITE_FAULT:error);
    ULARGE_INTEGER available{};if(!GetDiskFreeSpaceExW(folder.c_str(),&available,nullptr,nullptr))fail(GetLastError());
    if(available.QuadPart<64ull*1024*1024)fail(ERROR_DISK_FULL);
    SYSTEMTIME now{};if(timestamp)now=*timestamp;else GetLocalTime(&now);
    wchar_t base[100]{};swprintf_s(base,L"Screencast_%04u-%02u-%02u_%02u-%02u-%02u",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond);
    for(unsigned suffix=0;suffix<100000;++suffix){
        auto name=std::wstring(base)+(suffix?L"_"+std::to_wstring(suffix):L"")+L".mp4";
        auto path=(std::filesystem::path(folder)/name).wstring();
        if(GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES){DWORD e=GetLastError();if(e==ERROR_FILE_NOT_FOUND)return path;fail(e);}
    }
    fail(ERROR_FILE_EXISTS);return {};
}
