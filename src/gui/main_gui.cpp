// main_gui.cpp — entry point of the graphical client (Windows only).
// Creates a Win32 window with Direct3D 11, sets up Dear ImGui and runs the frame loop.
// The window code follows the official Dear ImGui "example_win32_directx11"; everything
// about mail is in GuiApp, which uses the same MailService as the command-line client.
#include <d3d11.h>
#include <windows.h>

#include <filesystem>
#include <string>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include "Config.h"
#include "GuiApp.h"
#include "Logger.h"
#include "MailService.h"
#include "TcpConnection.h"

// Defined in imgui_impl_win32.cpp: lets ImGui see mouse and keyboard messages.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
    LPARAM lParam);

namespace {

    ID3D11Device* g_device = nullptr;
    ID3D11DeviceContext* g_context = nullptr;
    IDXGISwapChain* g_swapChain = nullptr;
    ID3D11RenderTargetView* g_renderTarget = nullptr;
    bool g_occluded = false;
    UINT g_resizeWidth = 0, g_resizeHeight = 0;

    void createRenderTarget() {
        ID3D11Texture2D* backBuffer = nullptr;
        g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        g_device->CreateRenderTargetView(backBuffer, nullptr, &g_renderTarget);
        backBuffer->Release();
    }

    void cleanupRenderTarget() {
        if (g_renderTarget) { g_renderTarget->Release(); g_renderTarget = nullptr; }
    }

    bool createDevice(HWND hwnd) {
        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount = 2;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        const D3D_FEATURE_LEVEL levels[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
        D3D_FEATURE_LEVEL got;
        HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
            D3D11_SDK_VERSION, &sd, &g_swapChain, &g_device, &got,
            &g_context);
        if (res == DXGI_ERROR_UNSUPPORTED)  // no GPU support: use the software renderer instead
            res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2,
                D3D11_SDK_VERSION, &sd, &g_swapChain, &g_device, &got,
                &g_context);
        if (res != S_OK) return false;
        createRenderTarget();
        return true;
    }

    void cleanupDevice() {
        cleanupRenderTarget();
        if (g_swapChain) { g_swapChain->Release(); g_swapChain = nullptr; }
        if (g_context) { g_context->Release(); g_context = nullptr; }
        if (g_device) { g_device->Release(); g_device = nullptr; }
    }

    LRESULT WINAPI wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return true;
        switch (msg) {
        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED) return 0;
            g_resizeWidth = LOWORD(lParam);   // resize later, in the main loop
            g_resizeHeight = HIWORD(lParam);
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) return 0;  // disable the ALT menu
            break;
        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
        }
        return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    // A readable font: Segoe UI from Windows, plus Thai letters from Leelawadee UI if available.
    void loadFonts(ImGuiIO& io) {
        const char* segoe = "C:\\Windows\\Fonts\\segoeui.ttf";
        const char* thai = "C:\\Windows\\Fonts\\LeelawUI.ttf";
        if (!std::filesystem::exists(segoe)) return;  // keep ImGui's built-in font
        io.Fonts->AddFontFromFileTTF(segoe, 18.0f);
        if (std::filesystem::exists(thai)) {
            ImFontConfig merge;
            merge.MergeMode = true;
            io.Fonts->AddFontFromFileTTF(thai, 18.0f, &merge, io.Fonts->GetGlyphRangesThai());
        }
    }

    void applyStyle() {
        ImGui::StyleColorsLight();
        ImGuiStyle& s = ImGui::GetStyle();
        s.WindowRounding = 6.0f;
        s.ChildRounding = 6.0f;
        s.FrameRounding = 5.0f;
        s.PopupRounding = 6.0f;
        s.FramePadding = ImVec2(10, 6);
        s.ItemSpacing = ImVec2(10, 8);
        s.WindowPadding = ImVec2(14, 12);
        ImVec4* c = s.Colors;
        c[ImGuiCol_WindowBg] = ImVec4(0.96f, 0.97f, 0.98f, 1.0f);
        c[ImGuiCol_Button] = ImVec4(0.80f, 0.87f, 0.95f, 1.0f);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.68f, 0.79f, 0.93f, 1.0f);
        c[ImGuiCol_ButtonActive] = ImVec4(0.56f, 0.70f, 0.90f, 1.0f);
        c[ImGuiCol_Text] = ImVec4(0.09f, 0.13f, 0.18f, 1.0f);
        c[ImGuiCol_Header] = ImVec4(0.16f, 0.39f, 0.66f, 0.25f);
        c[ImGuiCol_HeaderHovered] = ImVec4(0.16f, 0.39f, 0.66f, 0.35f);
    }

}  // namespace

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // ---- the mail part: same start-up as the console client
    Config cfg;
    try {
        cfg = Config::load("config/client.conf");
    }
    catch (const ConfigError& e) {
        ::MessageBoxA(nullptr, e.what(), "Config error", MB_ICONERROR);
        return 1;
    }
    Logger::init(cfg.logFile);
    Logger::info("GUI client started");
    SocketSystem sockets;
    MailService service(cfg);
    GuiApp app(service);

    // ---- the window
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, wndProc, 0L, 0L, hInstance, nullptr, nullptr, nullptr,
                      nullptr, L"MailClientGui", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Lightweight Mail Client", WS_OVERLAPPEDWINDOW, 100, 100,
        1200, 780, nullptr, nullptr, wc.hInstance, nullptr);
    if (!createDevice(hwnd)) {
        cleanupDevice();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        ::MessageBoxA(nullptr, "Direct3D 11 could not be started.", "Error", MB_ICONERROR);
        return 1;
    }
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // do not write imgui.ini
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    applyStyle();
    loadFonts(io);
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    // ---- the frame loop: handle window messages, draw one frame, show it
    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        if (g_occluded && g_swapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            ::Sleep(10);  // window hidden or minimized: do not waste CPU
            continue;
        }
        g_occluded = false;

        if (g_resizeWidth != 0 && g_resizeHeight != 0) {
            cleanupRenderTarget();
            g_swapChain->ResizeBuffers(0, g_resizeWidth, g_resizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_resizeWidth = g_resizeHeight = 0;
            createRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        app.frame();
        ImGui::Render();

        const float clear[4] = { 0.96f, 0.97f, 0.98f, 1.0f };
        g_context->OMSetRenderTargets(1, &g_renderTarget, nullptr);
        g_context->ClearRenderTargetView(g_renderTarget, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        HRESULT hr = g_swapChain->Present(1, 0);  // 1 = wait for vsync (about 60 frames per second)
        g_occluded = (hr == DXGI_STATUS_OCCLUDED);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    cleanupDevice();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    Logger::info("GUI client exited");
    return 0;
}