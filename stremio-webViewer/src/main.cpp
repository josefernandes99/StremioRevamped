#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#pragma comment(linker, "/ENTRY:mainCRTStartup")

#include <windows.h>
#include <shellscalingapi.h>
#include <gdiplus.h>
#include <iostream>
#include <sstream>
#include <VersionHelpers.h>
#include "discord_rpc.h"

#include "ui/mainwindow.h"
#include "core/globals.h"
#include "utils/crashlog.h"
#include "utils/config.h"
#include "tray/tray.h"
#include "mpv/player.h"
#include "node/server.h"
#include "ui/splash.h"
#include "webview/webview.h"
#include "updater/updater.h"
#include "utils/helpers.h"
#include "utils/config.h"
#include "utils/discord.h"
// This started as 1-week project so please don't take the code to seriously
int main(int argc, char* argv[])
{
    // Catch unhandled exceptions
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* info) -> LONG
    {
        std::wstringstream ws;
        ws << L"Unhandled exception! Code=0x" << std::hex << info->ExceptionRecord->ExceptionCode;
        AppendToCrashLog(ws.str());
        Cleanup();
        return EXCEPTION_EXECUTE_HANDLER;
    });
    atexit(Cleanup);

    // DPI
    if (IsWindowsVersionOrGreater(10, 0, 14393)){
        typedef BOOL(WINAPI *SetDpiCtxFn)(DPI_AWARENESS_CONTEXT);
        auto setDpiAwarenessContext = (SetDpiCtxFn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext");
        if(setDpiAwarenessContext){
            setDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    } else {
        // Fallback for Windows 8.1 and Windows 10 before 1607:
        SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
    }

    // parse cmd line
    for(int i=1; i<argc; i++){
        std::string arg(argv[i]);
        if(arg.rfind("--webui-url=", 0)==0){
            g_webuiUrls.insert(g_webuiUrls.begin(), Utf8ToWstring(arg.substr(12)));
        } else if(arg.rfind("--autoupdater-endpoint=",0)==0){
            g_updateUrl = arg.substr(23);
        } else if(arg=="--streaming-server-disabled"){
            g_streamingServer=false;
        } else if(arg=="--autoupdater-force-full"){
            g_autoupdaterForceFull=true;
        }
    }

    // single instance
    std::wstring launchProtocol;
    if(!CheckSingleInstance(argc, argv, launchProtocol)){
        return 0;
    }
    g_launchProtocol = launchProtocol;

    // check stremio-runtime duplicates
    std::vector<std::wstring> processesToCheck={L"stremio.exe", L"stremio-runtime.exe"};
    if(IsDuplicateProcessRunning(processesToCheck)){
        MessageBoxW(nullptr,
            L"An older version of Stremio or Stremio server may be running. There could be issues.",
            L"Stremio Already Running", MB_OK|MB_ICONWARNING);
    }

    // init GDI+
    Gdiplus::GdiplusStartupInput gpsi;
    if(Gdiplus::GdiplusStartup(&g_gdiplusToken, &gpsi, nullptr)!=Gdiplus::Ok){
        AppendToCrashLog(L"[BOOT]: GdiplusStartup failed.");
        return 1;
    }

    // Load config
    LoadSettings();

    // Initialize Discord RPC
    InitializeDiscord();

    // Updater
    g_updaterThread=std::thread(RunAutoUpdaterOnce);
    g_updaterThread.detach();

    g_hInst = GetModuleHandle(nullptr);
    g_darkBrush = CreateSolidBrush(RGB(0,0,0));

    // Register main window class
    WNDCLASSEX wcex={0};
    wcex.cbSize=sizeof(WNDCLASSEX);
    wcex.style=CS_HREDRAW|CS_VREDRAW;
    wcex.lpfnWndProc=WndProc;
    wcex.hInstance=g_hInst;
    wcex.hCursor=LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground=g_darkBrush;
    wcex.lpszClassName=szWindowClass;
    if(!RegisterClassEx(&wcex)){
        AppendToCrashLog(L"[BOOT]: RegisterClassEx failed!");
        return 1;
    }

    g_hWnd = CreateWindow(szWindowClass, szTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1200, 900,
        nullptr, nullptr,
        g_hInst, nullptr);
    if(!g_hWnd){
        AppendToCrashLog(L"[BOOT]: CreateWindow failed!");
        return 1;
    }

    //Add PlayPause Hotkey
    if (!RegisterHotKey(g_hWnd, 1, 0, VK_MEDIA_PLAY_PAUSE)) {
        AppendToCrashLog(L"[BOOT]: Failed to register hotkey!");
    }

    // Scale Values with DPI
    ScaleWithDPI();
    LoadCustomMenuFont();

    // Load Saved position
    WINDOWPLACEMENT wp;
    if (LoadWindowPlacement(wp)) {
        SetWindowPlacement(g_hWnd, &wp);
        ShowWindow(g_hWnd, wp.showCmd);
        UpdateWindow(g_hWnd);
    } else {
        ShowWindow(g_hWnd,SW_SHOW);
        UpdateWindow(g_hWnd);
    }

    // create splash
    CreateSplashScreen(g_hWnd);

    // init mpv
    if(!InitMPV(g_hWnd)){
        DestroyWindow(g_hWnd);
        return 1;
    }

    // node
    if(g_streamingServer){
        StartNodeServer();
    }

    // webview
    InitWebView2(g_hWnd);

    // message loop
    MSG msg;
    while(GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);

        // Run Discord RPC callbacks
        Discord_RunCallbacks();
    }

    if(g_darkBrush){
        DeleteObject(g_darkBrush);
        g_darkBrush=nullptr;
    }
    std::cout<<"Exiting...\n";
    return (int)msg.wParam;
}
