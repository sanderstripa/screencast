#pragma once
#include <WebView2.h>
#include <objidl.h>

// CONNECT uses Edge WebView2. Keep the actual HTML inputs/buttons and font rasterizer.
class InstallerWeb {
    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> view;
    HWND owner{};bool ready{};HRESULT failure=S_OK;
    std::function<void(std::wstring)> receive;
    static std::wstring quote(std::wstring const& value){
        std::wstring out=L"\"";for(auto c:value){if(c==L'\\'||c==L'\"')out+=L'\\';
            if(c<L' '){wchar_t escaped[7]{};swprintf_s(escaped,L"\\u%04x",unsigned(c));out+=escaped;}else out+=c;
        }return out+L"\"";
    }
    template<class Predicate> static void wait(Predicate done){
        auto until=GetTickCount64()+20000;
        while(!done()){
            if(GetTickCount64()>until)throw std::runtime_error("WebView2 installer operation timed out");
            MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
                if(message.message==WM_QUIT)throw std::runtime_error("Installer closed while initializing its interface");
                TranslateMessage(&message);DispatchMessageW(&message);
            }MsgWaitForMultipleObjects(0,nullptr,FALSE,10,QS_ALLINPUT);
        }
    }
public:
    bool initialized()const{return ready;}
    static std::wstring jsString(std::wstring const& value){return quote(value);}
    void resize(){if(controller){RECT r{};GetClientRect(owner,&r);check(controller->put_Bounds(r));}}
    void open(HWND parent,std::function<void(std::wstring)> callback){
        owner=parent;receive=std::move(callback);
        auto profile=std::filesystem::temp_directory_path()/L"Screencast-Installer-WebView2";
        check(CreateCoreWebView2EnvironmentWithOptions(nullptr,profile.c_str(),nullptr,
            Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([this](HRESULT result,ICoreWebView2Environment* env)->HRESULT{
                if(FAILED(result)){failure=result;return S_OK;}environment=env;
                auto hr=environment->CreateCoreWebView2Controller(owner,
                    Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>([this](HRESULT result,ICoreWebView2Controller* value)->HRESULT{
                        if(FAILED(result)){failure=result;return S_OK;}
                        controller=value;controller->get_CoreWebView2(&view);resize();controller->put_ZoomFactor(1.0);
                        ComPtr<ICoreWebView2Settings> settings;view->get_Settings(&settings);
                        settings->put_AreDefaultContextMenusEnabled(FALSE);settings->put_AreDevToolsEnabled(FALSE);
                        settings->put_AreHostObjectsAllowed(FALSE);settings->put_IsZoomControlEnabled(FALSE);
                        EventRegistrationToken token{};
                        view->add_NavigationStarting(Microsoft::WRL::Callback<ICoreWebView2NavigationStartingEventHandler>([](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* args)->HRESULT{
                            LPWSTR raw{};args->get_Uri(&raw);bool allowed=raw&&(std::wstring(raw)==L"https://screencast.setup/"||std::wstring(raw)==L"https://screencast.setup/reference/index.html");CoTaskMemFree(raw);if(!allowed)args->put_Cancel(TRUE);return S_OK;
                        }).Get(),&token);
                        view->AddWebResourceRequestedFilter(L"*",COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
                        view->add_WebResourceRequested(Microsoft::WRL::Callback<ICoreWebView2WebResourceRequestedEventHandler>([this](ICoreWebView2*,ICoreWebView2WebResourceRequestedEventArgs* args)->HRESULT{
                            ComPtr<ICoreWebView2WebResourceRequest> request;args->get_Request(&request);LPWSTR raw{};request->get_Uri(&raw);std::wstring uri=raw;CoTaskMemFree(raw);
                            int id{};wchar_t const* mime=L"text/html; charset=utf-8";
                            if(uri==L"https://screencast.setup/")id=203;
                            else if(uri==L"https://screencast.setup/style.css"){id=204;mime=L"text/css; charset=utf-8";}
                            else if(uri==L"https://screencast.setup/preview.js"){id=205;mime=L"text/javascript; charset=utf-8";}
                            else if(uri==L"https://screencast.setup/assets/ribbon.png"){id=206;mime=L"image/png";}
                            else if(uri==L"https://screencast.setup/assets/Montserrat.ttf"){id=202;mime=L"font/ttf";}
                            else if(uri==L"https://screencast.setup/reference/index.html")id=208;
                            else if(uri==L"https://screencast.setup/reference/style.css"){id=209;mime=L"text/css; charset=utf-8";}
                            else if(uri==L"https://screencast.setup/reference/setup.js"){id=210;mime=L"text/javascript; charset=utf-8";}
                            else if(uri==L"https://screencast.setup/reference/assets/Montserrat.ttf"){id=202;mime=L"font/ttf";}
                            else if(uri==L"https://screencast.setup/reference/assets/mobius.png"){id=206;mime=L"image/png";}
                            ComPtr<IStream> stream;
                            if(id){auto resource=FindResourceW(nullptr,MAKEINTRESOURCEW(id),RT_RCDATA);stream.Attach(SHCreateMemStream(static_cast<BYTE const*>(LockResource(LoadResource(nullptr,resource))),SizeofResource(nullptr,resource)));}
                            ComPtr<ICoreWebView2WebResourceResponse> response;
                            environment->CreateWebResourceResponse(stream.Get(),id?200:404,id?L"OK":L"Not Found",(std::wstring(L"Content-Type: ")+mime+L"\r\nCache-Control: no-store\r\n").c_str(),&response);
                            args->put_Response(response.Get());return S_OK;
                        }).Get(),&token);
                        view->add_WebMessageReceived(Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>([this](ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs* args)->HRESULT{
                            LPWSTR raw{};if(SUCCEEDED(args->TryGetWebMessageAsString(&raw))){std::wstring message=raw;CoTaskMemFree(raw);receive(std::move(message));}return S_OK;
                        }).Get(),&token);
                        view->add_NavigationCompleted(Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>([this](ICoreWebView2*,ICoreWebView2NavigationCompletedEventArgs* args)->HRESULT{
                            BOOL success{};args->get_IsSuccess(&success);if(success)ready=true;else failure=E_FAIL;return S_OK;
                        }).Get(),&token);
                        view->Navigate(L"https://screencast.setup/");return S_OK;
                    }).Get());if(FAILED(hr))failure=hr;return S_OK;
            }).Get()));
        wait([&]{return ready||FAILED(failure);});check(failure);
        wait([&]{return evaluate(L"document.fonts.status==='loaded'")==L"true";});
    }
    std::wstring evaluate(std::wstring const& script){
        bool finished{};HRESULT result{};std::wstring response;
        check(view->ExecuteScript(script.c_str(),Microsoft::WRL::Callback<ICoreWebView2ExecuteScriptCompletedHandler>([&](HRESULT hr,LPCWSTR value)->HRESULT{result=hr;if(value)response=value;finished=true;return S_OK;}).Get()));
        wait([&]{return finished;});check(result);return response;
    }
    void reference(bool enabled){
        ready=false;failure=S_OK;check(view->Navigate(enabled?L"https://screencast.setup/reference/index.html":L"https://screencast.setup/"));
        wait([&]{return ready||FAILED(failure);});check(failure);
        wait([&]{return evaluate(L"document.fonts.status==='loaded'")==L"true";});
    }
    void update(std::wstring const& state){if(ready)view->ExecuteScript((L"window.applyInstallerState("+state+L")").c_str(),nullptr);}
    void capture(std::filesystem::path const& path){
        evaluate(L"void(document.body.offsetHeight)");Sleep(100);
        ComPtr<IStream> stream;check(CreateStreamOnHGlobal(nullptr,TRUE,&stream));bool finished{};HRESULT result{};
        check(view->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG,stream.Get(),Microsoft::WRL::Callback<ICoreWebView2CapturePreviewCompletedHandler>([&](HRESULT hr)->HRESULT{result=hr;finished=true;return S_OK;}).Get()));
        wait([&]{return finished;});check(result);HGLOBAL memory{};check(GetHGlobalFromStream(stream.Get(),&memory));STATSTG stat{};check(stream->Stat(&stat,STATFLAG_NONAME));
        auto bytes=GlobalLock(memory);std::ofstream output(path,std::ios::binary);output.write(static_cast<char const*>(bytes),stat.cbSize.LowPart);GlobalUnlock(memory);if(!output)throw std::runtime_error("Could not write WebView2 screenshot");
    }
    ~InstallerWeb(){if(controller)controller->Close();}
};
