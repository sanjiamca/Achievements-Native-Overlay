#include "hooks.hpp"
#include "../console/console.hpp"
#include "../dependencies/minhook/MinHook.h"
#include "../menu/menu.hpp"
#include "../utils/utils.hpp"
#include "backend/dx10/hook_directx10.hpp"
#include "backend/dx11/hook_directx11.hpp"
#include "backend/dx12/hook_directx12.hpp"
#include "backend/dx9/hook_directx9.hpp"
#include "backend/opengl/hook_opengl.hpp"
#include "backend/vulkan/hook_vulkan.hpp"
#include <cstdio>
#include <mutex>
#include <thread>

static HWND g_hWindow = NULL;
static std::mutex g_mReinitHooksGuard;

static DWORD WINAPI ReinitializeGraphicalHooks(LPVOID lpParam) {
    std::lock_guard<std::mutex> guard{g_mReinitHooksGuard};

    LOG("[!] Hooks will reinitialize!\n");

    HWND hNewWindow = U::GetProcessWindow( );
    while (hNewWindow == reinterpret_cast<HWND>(lpParam)) {
        hNewWindow = U::GetProcessWindow( );
    }

    H::bShuttingDown = true;

    H::Free( );
    H::Init( );

    H::bShuttingDown = false;
    Menu::bShowMenu = true;

    return 0;
}

static WNDPROC oWndProc;

// --- MENU.CPP'DEKI TUS AYARLARINA KOPRU ---
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace Menu {
    extern int hotkeyMain;
    extern bool hotkeyShift;
    extern bool hotkeyCtrl;
    extern bool hotkeyAlt;
    extern bool bWaitingForKey;
} // namespace Menu

static LRESULT WINAPI WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN) {

        // 1. DLL Yeniden Başlatma (INSERT)
        if (wParam == VK_INSERT) {
            HANDLE hHandle = CreateThread(NULL, 0, ReinitializeGraphicalHooks, NULL, 0, NULL);
            if (hHandle != NULL)
                CloseHandle(hHandle);
            return 0;
        }

        // 2. Güvenli Kapatma (END)
        if (wParam == VK_END) {
            H::bShuttingDown = true;
            U::UnloadDLL( );
            return 0;
        }

        // --- 3. BIZIM EFSANE TUŞ DİNLEYİCİ SİSTEMİMİZ ---

        // Eger menuden tus atama islemindeysek, oyun tuslari hic gormesin
        if (Menu::bWaitingForKey)
            return 0;

        // Atanan ana tusa (Orn: Tab, F3) basildi mi?
        if (wParam == Menu::hotkeyMain) {
            // Kombinasyonlar dogru sekilde basili tutuluyor mu kontrol et
            bool isShiftDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            bool isCtrlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            bool isAltDown = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

            if (Menu::hotkeyShift == isShiftDown && Menu::hotkeyCtrl == isCtrlDown && Menu::hotkeyAlt == isAltDown) {
                Menu::bShowMenu = !Menu::bShowMenu; // Menuyu ac/kapat
                return 0;                           // Bu kombinasyonu oyundan gizle
            }
        }

    } else if (uMsg == WM_DESTROY) {
        HANDLE hHandle = CreateThread(NULL, 0, ReinitializeGraphicalHooks, (LPVOID)hWnd, 0, NULL);
        if (hHandle != NULL)
            CloseHandle(hHandle);
    }

    // --- IMGUI (MENU) ETKİLEŞİMİ ---
    if (Menu::bShowMenu) {
        ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);

        // Menu acikken oyuna klavye ve fare gitmesin (Oyun arkada sacmalamasin)
        if (uMsg == WM_MOUSEMOVE || uMsg == WM_MOUSEWHEEL ||
            uMsg == WM_LBUTTONDOWN || uMsg == WM_LBUTTONUP ||
            uMsg == WM_RBUTTONDOWN || uMsg == WM_RBUTTONUP ||
            uMsg == WM_KEYDOWN || uMsg == WM_KEYUP) {
            return true;
        }
    }

    return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

namespace Hooks {
    void Init( ) {
        g_hWindow = U::GetProcessWindow( );
#ifdef DISABLE_LOGGING_CONSOLE
        bool bNoConsole = GetConsoleWindow( ) == NULL;
        if (bNoConsole) {
            AllocConsole( );
        }
#endif

        RenderingBackend_t eRenderingBackend = U::GetRenderingBackend( );
        switch (eRenderingBackend) {
            case DIRECTX9:
                DX9::Hook(g_hWindow);
                break;
            case DIRECTX10:
                DX10::Hook(g_hWindow);
                break;
            case DIRECTX11:
                DX11::Hook(g_hWindow);
                break;
            case DIRECTX12:
                DX12::Hook(g_hWindow);
                break;
            case OPENGL:
                GL::Hook(g_hWindow);
                break;
            case VULKAN:
                VK::Hook(g_hWindow);
                break;
        }
#ifdef DISABLE_LOGGING_CONSOLE
        if (bNoConsole) {
            FreeConsole( );
        }
#endif

        oWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtr(g_hWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProc)));
    }

    void Free( ) {
        if (oWndProc) {
            SetWindowLongPtr(g_hWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(oWndProc));
        }

        MH_DisableHook(MH_ALL_HOOKS);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        RenderingBackend_t eRenderingBackend = U::GetRenderingBackend( );
        switch (eRenderingBackend) {
            case DIRECTX9:
                DX9::Unhook( );
                break;
            case DIRECTX10:
                DX10::Unhook( );
                break;
            case DIRECTX11:
                DX11::Unhook( );
                break;
            case DIRECTX12:
                DX12::Unhook( );
                break;
            case OPENGL:
                GL::Unhook( );
                break;
            case VULKAN:
                VK::Unhook( );
                break;
        }
    }
} // namespace Hooks
