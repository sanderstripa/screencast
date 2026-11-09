#include "../src/recording_path.h"
#include <fstream>
#include <iostream>
int main(){
    winrt::init_apartment();
    auto folder=std::filesystem::temp_directory_path()/(L"Screencast-path-test-"+std::to_wstring(GetCurrentProcessId()));
    try{
        std::filesystem::create_directory(folder);
        SYSTEMTIME now{};now.wYear=2026;now.wMonth=11;now.wDay=10;now.wHour=0;now.wMinute=15;now.wSecond=30;
        auto first=recordingPath(folder.wstring(),&now);
        if(std::filesystem::path(first).filename()!=L"Screencast_2026-11-10_00-15-30.mp4")throw std::runtime_error("bad timestamp");
        std::ofstream(std::filesystem::path(first))<<"preserve existing recording";
        auto second=recordingPath(folder.wstring(),&now);
        if(first==second||std::filesystem::path(second).filename()!=L"Screencast_2026-11-10_00-15-30_1.mp4")throw std::runtime_error("collision");
        if(std::filesystem::file_size(first)!=27)throw std::runtime_error("overwrote existing recording");
        for(auto const& item:std::filesystem::directory_iterator(folder))if(item.path()!=first)throw std::runtime_error("probe left behind");
        bool rejected=false;try{recordingPath((folder/L"missing").wstring());}catch(winrt::hresult_error const&){rejected=true;}
        if(!rejected)throw std::runtime_error("missing folder accepted");
        rejected=false;try{recordingPath(first);}catch(winrt::hresult_error const&){rejected=true;}
        if(!rejected)throw std::runtime_error("file accepted as folder");
        std::filesystem::remove_all(folder);std::cout<<"PASS: timestamp, collision suffix, preservation, write probe cleanup, invalid folder\n";
    }catch(std::exception const& e){std::cerr<<e.what();std::filesystem::remove_all(folder);return 1;}
    catch(...){std::filesystem::remove_all(folder);return 1;}
}
