#include "menu.h"

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <MinHook.h>

#include <atomic>
#include <cwchar>
#include <iterator>
#include <mutex>
#include <string>

namespace {

using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

PresentFn g_originalPresent = nullptr;
ResizeBuffersFn g_originalResizeBuffers = nullptr;

IDXGISwapChain* g_activeSwapChain = nullptr; // Weak identity only. Do not AddRef.
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
HWND g_gameWindow = nullptr;
WNDPROC g_originalWndProc = nullptr;

std::mutex g_renderMutex;
std::atomic<bool> g_menuInitialized{false};
std::atomic<bool> g_hooksInstalled{false};

void SafeReleaseRtv() {
    if (g_rtv) {
        g_rtv->Release();
        g_rtv = nullptr;
    }
}

bool IsBattlefrontProcess() {
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return false;

    const wchar_t* file = wcsrchr(path, L'\\');
    file = file ? file + 1 : path;
    return _wcsicmp(file, L"starwarsbattlefrontii.exe") == 0;
}

LRESULT CALLBACK HookWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

void RestoreWindowProc() {
    if (!g_gameWindow || !g_originalWndProc || !IsWindow(g_gameWindow)) {
        g_originalWndProc = nullptr;
        g_gameWindow = nullptr;
        return;
    }

    // Do not clobber a later subclass installed by the game or another overlay.
    const auto current = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrW(g_gameWindow, GWLP_WNDPROC)
    );
    if (current == HookWndProc) {
        SetWindowLongPtrW(
            g_gameWindow,
            GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(g_originalWndProc)
        );
    }

    g_originalWndProc = nullptr;
    g_gameWindow = nullptr;
}

void ReleaseRenderState() {
    if (g_menuInitialized.exchange(false)) {
        bf2dev::ShutdownMenu();
    }

    RestoreWindowProc();
    SafeReleaseRtv();

    if (g_context) {
        g_context->Release();
        g_context = nullptr;
    }

    if (g_device) {
        g_device->Release();
        g_device = nullptr;
    }

    g_activeSwapChain = nullptr;
}

bool CreateRenderTarget(
    IDXGISwapChain* swapChain,
    ID3D11Device* device,
    ID3D11RenderTargetView** outRtv)
{
    if (!swapChain || !device || !outRtv) return false;

    *outRtv = nullptr;
    ID3D11Texture2D* backBuffer = nullptr;

    const HRESULT getBufferHr = swapChain->GetBuffer(
        0,
        __uuidof(ID3D11Texture2D),
        reinterpret_cast<void**>(&backBuffer)
    );
    if (FAILED(getBufferHr) || !backBuffer) {
        return false;
    }

    const HRESULT createHr = device->CreateRenderTargetView(
        backBuffer,
        nullptr,
        outRtv
    );
    backBuffer->Release();

    return SUCCEEDED(createHr) && *outRtv != nullptr;
}

bool SwapChainStateChanged(IDXGISwapChain* swapChain) {
    if (!swapChain || swapChain != g_activeSwapChain) return true;
    if (!g_menuInitialized.load() || !g_device || !g_context || !g_gameWindow) return true;

    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(swapChain->GetDesc(&desc)) || !desc.OutputWindow) return true;
    if (desc.OutputWindow != g_gameWindow) return true;

    ID3D11Device* currentDevice = nullptr;
    const HRESULT hr = swapChain->GetDevice(
        __uuidof(ID3D11Device),
        reinterpret_cast<void**>(&currentDevice)
    );
    if (FAILED(hr) || !currentDevice) return true;

    const bool changed = currentDevice != g_device;
    currentDevice->Release();
    return changed;
}

LRESULT CALLBACK HookWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    bool handled = false;
    const LRESULT result = bf2dev::HandleWndProc(hwnd, msg, wParam, lParam, handled);
    if (handled) return result;

    WNDPROC original = g_originalWndProc;
    if (original) {
        return CallWindowProcW(original, hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool InitializeFromSwapChain(IDXGISwapChain* swapChain) {
    if (!swapChain) return false;

    // Level loads can replace the swap chain, D3D device, or output HWND.
    // Tear down every object derived from the old chain before adopting the new one.
    if (g_activeSwapChain || g_menuInitialized.load() || g_device || g_context || g_rtv) {
        ReleaseRenderState();
    }

    ID3D11Device* newDevice = nullptr;
    const HRESULT deviceHr = swapChain->GetDevice(
        __uuidof(ID3D11Device),
        reinterpret_cast<void**>(&newDevice)
    );
    if (FAILED(deviceHr) || !newDevice) {
        return false;
    }

    ID3D11DeviceContext* newContext = nullptr;
    newDevice->GetImmediateContext(&newContext);
    if (!newContext) {
        newDevice->Release();
        return false;
    }

    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(swapChain->GetDesc(&desc)) || !desc.OutputWindow || !IsWindow(desc.OutputWindow)) {
        newContext->Release();
        newDevice->Release();
        return false;
    }

    ID3D11RenderTargetView* newRtv = nullptr;
    if (!CreateRenderTarget(swapChain, newDevice, &newRtv)) {
        newContext->Release();
        newDevice->Release();
        return false;
    }

    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previousWndProc = SetWindowLongPtrW(
        desc.OutputWindow,
        GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(HookWndProc)
    );
    if (previousWndProc == 0 && GetLastError() != ERROR_SUCCESS) {
        newRtv->Release();
        newContext->Release();
        newDevice->Release();
        return false;
    }

    g_activeSwapChain = swapChain;
    g_device = newDevice;
    g_context = newContext;
    g_rtv = newRtv;
    g_gameWindow = desc.OutputWindow;
    g_originalWndProc = reinterpret_cast<WNDPROC>(previousWndProc);

    bf2dev::InitializeMenu(g_gameWindow, g_device, g_context);
    g_menuInitialized.store(true);
    return true;
}

HRESULT __stdcall HookPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags) {
    {
        std::lock_guard<std::mutex> lock(g_renderMutex);

        if (SwapChainStateChanged(swapChain)) {
            InitializeFromSwapChain(swapChain);
        }

        if (g_menuInitialized.load() &&
            swapChain == g_activeSwapChain &&
            g_rtv &&
            g_context)
        {
            ID3D11RenderTargetView* previousRtv = nullptr;
            ID3D11DepthStencilView* previousDsv = nullptr;
            g_context->OMGetRenderTargets(1, &previousRtv, &previousDsv);

            g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
            bf2dev::RenderMenu();

            g_context->OMSetRenderTargets(1, &previousRtv, previousDsv);
            if (previousRtv) previousRtv->Release();
            if (previousDsv) previousDsv->Release();
        }
    }

    return g_originalPresent(swapChain, syncInterval, flags);
}

HRESULT __stdcall HookResizeBuffers(
    IDXGISwapChain* swapChain,
    UINT bufferCount,
    UINT width,
    UINT height,
    DXGI_FORMAT newFormat,
    UINT swapChainFlags)
{
    bool rebuildActiveChain = false;

    {
        std::lock_guard<std::mutex> lock(g_renderMutex);
        rebuildActiveChain = (swapChain == g_activeSwapChain);
        if (rebuildActiveChain) {
            // DXGI requires every reference to the old back buffer to be released
            // before ResizeBuffers can succeed.
            SafeReleaseRtv();
        }
    }

    const HRESULT hr = g_originalResizeBuffers(
        swapChain,
        bufferCount,
        width,
        height,
        newFormat,
        swapChainFlags
    );

    if (SUCCEEDED(hr) && rebuildActiveChain) {
        std::lock_guard<std::mutex> lock(g_renderMutex);

        // The device/window may also change during a map transition. Let Present
        // perform a full rebind when that happens.
        if (SwapChainStateChanged(swapChain)) {
            InitializeFromSwapChain(swapChain);
        } else if (g_device) {
            CreateRenderTarget(swapChain, g_device, &g_rtv);
        }
    }

    return hr;
}

LRESULT CALLBACK DummyWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool GetSwapChainMethods(void** presentOut, void** resizeOut) {
    const wchar_t* className = L"BF2DevMenuDummyWindow";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DummyWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = className;

    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    HWND hwnd = CreateWindowExW(
        0, className, L"", WS_OVERLAPPEDWINDOW,
        0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr
    );
    if (!hwnd) return false;

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* swapChain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL featureLevel{};

    const D3D_FEATURE_LEVEL requested[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    const HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        requested,
        static_cast<UINT>(std::size(requested)),
        D3D11_SDK_VERSION,
        &sd,
        &swapChain,
        &device,
        &featureLevel,
        &context
    );

    if (FAILED(hr) || !swapChain) {
        if (context) context->Release();
        if (device) device->Release();
        DestroyWindow(hwnd);
        UnregisterClassW(className, wc.hInstance);
        return false;
    }

    void** vtable = *reinterpret_cast<void***>(swapChain);
    *presentOut = vtable[8];
    *resizeOut = vtable[13];

    context->Release();
    device->Release();
    swapChain->Release();
    DestroyWindow(hwnd);
    UnregisterClassW(className, wc.hInstance);
    return true;
}

DWORD WINAPI InitializeThread(void*) {
    if (!IsBattlefrontProcess()) return 0;
    if (!bf2dev::IsPrivateTestEnabled()) return 0;

    while (!GetModuleHandleW(L"d3d11.dll") || !GetModuleHandleW(L"dxgi.dll")) {
        Sleep(250);
    }

    void* present = nullptr;
    void* resize = nullptr;
    if (!GetSwapChainMethods(&present, &resize)) return 0;

    if (MH_Initialize() != MH_OK) return 0;

    if (MH_CreateHook(present, &HookPresent, reinterpret_cast<void**>(&g_originalPresent)) != MH_OK ||
        MH_CreateHook(resize, &HookResizeBuffers, reinterpret_cast<void**>(&g_originalResizeBuffers)) != MH_OK) {
        MH_Uninitialize();
        return 0;
    }

    if (MH_EnableHook(present) != MH_OK || MH_EnableHook(resize) != MH_OK) {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        return 0;
    }

    g_hooksInstalled.store(true);
    return 0;
}

void Cleanup() {
    if (g_hooksInstalled.exchange(false)) {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
    }

    std::lock_guard<std::mutex> lock(g_renderMutex);
    ReleaseRenderState();
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(nullptr, 0, InitializeThread, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    } else if (reason == DLL_PROCESS_DETACH) {
        Cleanup();
    }
    return TRUE;
}
