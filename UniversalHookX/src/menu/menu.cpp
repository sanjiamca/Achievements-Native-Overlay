#define _CRT_SECURE_NO_WARNINGS
#include <wincodec.h>
#include <windows.h>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "d3d11.lib")

#include "../dependencies/imgui/imgui.h"
#include "../dependencies/imgui/imgui_impl_win32.h"
#include "../hooks/hooks.hpp"
#include "../utils/utils.hpp"
#include "menu.hpp"

#include <d3d11.h>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

// --- KÜTÜPHANE TRİPLERİNİ (Hataları) ATLAMAK İÇİN KERNEL'E DİREKT BAĞLANDIK ---
#define TH32CS_SNAPPROCESS 0x00000002
struct PROCESSENTRY32W_CUSTOM {
    DWORD dwSize;
    DWORD cntUsage;
    DWORD th32ProcessID;
    ULONG_PTR th32DefaultHeapID;
    DWORD th32ModuleID;
    DWORD cntThreads;
    DWORD th32ParentProcessID;
    LONG pcPriClassBase;
    DWORD dwFlags;
    WCHAR szExeFile[MAX_PATH];
};
extern "C" __declspec(dllimport) HANDLE WINAPI CreateToolhelp32Snapshot(DWORD dwFlags, DWORD th32ProcessID);
extern "C" __declspec(dllimport) BOOL WINAPI Process32FirstW(HANDLE hSnapshot, PROCESSENTRY32W_CUSTOM* lppe);
extern "C" __declspec(dllimport) BOOL WINAPI Process32NextW(HANDLE hSnapshot, PROCESSENTRY32W_CUSTOM* lppe);
// ------------------------------------------------------------------------------

using json = nlohmann::json;
namespace fs = std::filesystem;
namespace ig = ImGui;

extern ID3D11Device* g_pd3dDevice;

namespace Menu {
    int hotkeyMain = VK_HOME;
    bool hotkeyShift = false;
    bool hotkeyCtrl = false;
    bool hotkeyAlt = false;
    bool bWaitingForKey = false;

    // --- GÖREV YÖNETİCİSİ RADARI ---
    bool IsProcessRunning(const wchar_t* processName) {
        bool exists = false;
        PROCESSENTRY32W_CUSTOM entry;
        entry.dwSize = sizeof(PROCESSENTRY32W_CUSTOM);
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            return false;

        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (lstrcmpiW(entry.szExeFile, processName) == 0) {
                    exists = true;
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
        return exists;
    }

    std::string GetKeyName(int vk) {
        if (vk == 0)
            return "None";
        char name[128];
        UINT scanCode = MapVirtualKey(vk, MAPVK_VK_TO_VSC);
        switch (vk) {
            case VK_LEFT:
            case VK_UP:
            case VK_RIGHT:
            case VK_DOWN:
            case VK_PRIOR:
            case VK_NEXT:
            case VK_END:
            case VK_HOME:
            case VK_INSERT:
            case VK_DELETE:
            case VK_DIVIDE:
            case VK_NUMLOCK:
                scanCode |= 0x100;
                break;
        }
        if (GetKeyNameTextA(scanCode << 16, name, 128) == 0)
            return "Key " + std::to_string(vk);
        return std::string(name);
    }

    struct AchievementInfo {
        std::string internalName;
        std::string name;
        std::string description;
        bool earned = false;
        float rarity = -1.0f;
        ID3D11ShaderResourceView* texture = nullptr;
    };

    struct GameStats {
        std::string configName = "";
        std::string configPath = "";
        long long totalMs = 0;
        bool found = false;
        std::vector<AchievementInfo> achievements;
    };

    struct Notification {
        std::string name;
        std::string desc;
        ID3D11ShaderResourceView* icon;
        float timestamp;
    };
    std::vector<Notification> activeNotifications;

    std::wstring StringToWString(const std::string& str) {
        if (str.empty( ))
            return std::wstring( );
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size( ), NULL, 0);
        std::wstring wstrTo(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size( ), &wstrTo[0], size_needed);
        return wstrTo;
    }

    bool LoadTextureWIC(const std::string& path, ID3D11ShaderResourceView** out_srv) {
        if (!fs::exists(path) || !g_pd3dDevice)
            return false;
        HRESULT hrInit = CoInitialize(NULL);
        IWICImagingFactory* pFactory = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));
        if (FAILED(hr)) {
            if (SUCCEEDED(hrInit))
                CoUninitialize( );
            return false;
        }
        std::wstring wpath = StringToWString(path);
        IWICBitmapDecoder* pDecoder = nullptr;
        hr = pFactory->CreateDecoderFromFilename(wpath.c_str( ), NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pDecoder);
        bool success = false;
        if (SUCCEEDED(hr)) {
            IWICBitmapFrameDecode* pFrame = nullptr;
            hr = pDecoder->GetFrame(0, &pFrame);
            if (SUCCEEDED(hr)) {
                IWICFormatConverter* pConverter = nullptr;
                hr = pFactory->CreateFormatConverter(&pConverter);
                if (SUCCEEDED(hr)) {
                    hr = pConverter->Initialize(pFrame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom);
                    if (SUCCEEDED(hr)) {
                        UINT width, height;
                        pConverter->GetSize(&width, &height);
                        std::vector<BYTE> buffer(width * height * 4);
                        hr = pConverter->CopyPixels(NULL, width * 4, (UINT)buffer.size( ), buffer.data( ));
                        if (SUCCEEDED(hr)) {
                            D3D11_TEXTURE2D_DESC desc = { };
                            desc.Width = width;
                            desc.Height = height;
                            desc.MipLevels = 1;
                            desc.ArraySize = 1;
                            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                            desc.SampleDesc.Count = 1;
                            desc.Usage = D3D11_USAGE_DEFAULT;
                            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                            D3D11_SUBRESOURCE_DATA initData = { };
                            initData.pSysMem = buffer.data( );
                            initData.SysMemPitch = width * 4;
                            ID3D11Texture2D* pTexture = nullptr;
                            if (SUCCEEDED(g_pd3dDevice->CreateTexture2D(&desc, &initData, &pTexture))) {
                                if (SUCCEEDED(g_pd3dDevice->CreateShaderResourceView(pTexture, NULL, out_srv)))
                                    success = true;
                                pTexture->Release( );
                            }
                        }
                    }
                    pConverter->Release( );
                }
                pFrame->Release( );
            }
            pDecoder->Release( );
        }
        pFactory->Release( );
        if (SUCCEEDED(hrInit))
            CoUninitialize( );
        return success;
    }

    void FreeStats(GameStats& stats) {
        for (auto& ach : stats.achievements) {
            if (ach.texture) {
                ach.texture->Release( );
                ach.texture = nullptr;
            }
        }
        stats.achievements.clear( );
    }

    GameStats GetLiveStats( ) {
        GameStats stats;
        char buffer[MAX_PATH];
        GetModuleFileNameA(NULL, buffer, MAX_PATH);
        std::string exeName = fs::path(buffer).filename( ).string( );
        const char* ad = getenv("APPDATA");
        if (!ad)
            return stats;
        std::string appData = std::string(ad);

        try {
            std::string cfgDir = appData + "\\Achievements\\configs";
            if (fs::exists(cfgDir)) {
                for (auto& p : fs::directory_iterator(cfgDir)) {
                    if (p.path( ).extension( ) == ".json") {
                        std::ifstream f(p.path( ));
                        json j = json::parse(f);
                        if (j["process_name"] == exeName) {
                            stats.configName = j["name"];
                            stats.configPath = j.value("config_path", "");
                            stats.found = true;
                            break;
                        }
                    }
                }
            }

            if (stats.found) {
                std::ifstream pf(appData + "\\Achievements\\playtime-totals.json");
                if (pf.is_open( )) {
                    json pt = json::parse(pf);
                    stats.totalMs = pt[stats.configName].value("totalMs", 0LL);
                }

                std::string schema = stats.configPath + "\\achievements.json";
                std::string cache = appData + "\\Achievements\\cache\\" + stats.configName + "_achievements_cache.json";
                std::string percentsFile = stats.configPath + "\\achievementpercentages.json";

                if (fs::exists(schema)) {
                    std::ifstream sf(schema);
                    json sj = json::parse(sf);
                    std::ifstream cf(cache);
                    json cj = cf.is_open( ) ? json::parse(cf) : json::object( );

                    std::vector<float> rarityList;
                    if (fs::exists(percentsFile)) {
                        std::ifstream pff(percentsFile);
                        json pj = json::parse(pff);
                        if (pj.contains("achievements") && pj["achievements"].is_array( )) {
                            for (auto& item : pj["achievements"])
                                rarityList.push_back(item.value("percent", -1.0f));
                        }
                    }

                    int index = 0;
                    for (auto& a : sj) {
                        AchievementInfo info;
                        info.internalName = a["name"].get<std::string>( );
                        info.name = a["displayName"].value("english", "Achievement");
                        info.description = a["description"].value("english", "");
                        info.earned = cj.contains(info.internalName) ? cj[info.internalName].value("earned", false) : false;

                        if (index < rarityList.size( ))
                            info.rarity = rarityList[index];

                        std::string iconName = a.value("icon", "");
                        if (iconName.find(".") == std::string::npos)
                            iconName += ".jpg";
                        if (iconName.find("img/") == 0)
                            iconName = iconName.substr(4);
                        if (iconName.find("img\\") == 0)
                            iconName = iconName.substr(4);

                        std::string finalIconPath = stats.configPath + "\\img\\" + iconName;
                        LoadTextureWIC(finalIconPath, &info.texture);
                        stats.achievements.push_back(info);
                        index++;
                    }
                }
            }
        } catch (...) {
            stats.found = false;
        }
        return stats;
    }

    void BackgroundSync(GameStats& stats) {
        if (!stats.found)
            return;
        const char* ad = getenv("APPDATA");
        if (!ad)
            return;

        std::string playFile = std::string(ad) + "\\Achievements\\playtime-totals.json";
        if (fs::exists(playFile)) {
            std::ifstream pf(playFile);
            if (pf.is_open( )) {
                json pt = json::parse(pf);
                stats.totalMs = pt[stats.configName].value("totalMs", stats.totalMs);
            }
        }

        std::string cacheFile = std::string(ad) + "\\Achievements\\cache\\" + stats.configName + "_achievements_cache.json";
        if (fs::exists(cacheFile)) {
            std::ifstream cf(cacheFile);
            if (cf.is_open( )) {
                json cj = json::parse(cf);
                for (auto& ach : stats.achievements) {
                    if (!ach.earned) {
                        if (cj.contains(ach.internalName) && cj[ach.internalName].value("earned", false)) {
                            ach.earned = true;
                            Notification notif;
                            notif.name = ach.name;
                            notif.desc = ach.description;
                            notif.icon = ach.texture;
                            notif.timestamp = ig::GetTime( );
                            activeNotifications.push_back(notif);
                        }
                    }
                }
            }
        }
    }

    void RenderNotifications( ) {
        if (activeNotifications.empty( ))
            return;

        float currentTime = ig::GetTime( );
        ImVec2 screenRes = ig::GetIO( ).DisplaySize;

        for (int i = 0; i < activeNotifications.size( ); i++) {
            auto& notif = activeNotifications[i];
            float elapsed = currentTime - notif.timestamp;

            if (elapsed > 6.0f) {
                activeNotifications.erase(activeNotifications.begin( ) + i);
                i--;
                continue;
            }

            ImVec2 windowPos = ImVec2(screenRes.x - 320.0f, screenRes.y - 120.0f - (i * 90.0f));
            ig::SetNextWindowPos(windowPos, ImGuiCond_Always);
            ig::SetNextWindowSize(ImVec2(300, 85), ImGuiCond_Always);

            float alpha = 1.0f;
            if (elapsed > 5.0f)
                alpha = 6.0f - elapsed;
            ig::PushStyleVar(ImGuiStyleVar_Alpha, alpha);

            ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;

            ig::Begin(("Notif" + std::to_string(i)).c_str( ), NULL, flags);
            ig::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "🏆 ACHIEVEMENT UNLOCKED!");
            ig::Separator( );

            if (notif.icon) {
                ig::Image((ImTextureID)(intptr_t)notif.icon, ImVec2(40, 40));
                ig::SameLine( );
            }

            ig::BeginGroup( );
            ig::TextWrapped("%s", notif.name.c_str( ));
            ig::TextDisabled("%s", notif.desc.c_str( ));
            ig::EndGroup( );

            ig::End( );
            ig::PopStyleVar( );
        }
    }

    void InitializeContext(HWND hwnd) {
        if (ig::GetCurrentContext( ))
            return;
        ig::CreateContext( );
        ImGui_ImplWin32_Init(hwnd);
        ig::GetIO( ).IniFilename = ig::GetIO( ).LogFilename = nullptr;
    }

    void Render( ) {
        static GameStats liveData;
        static bool lastMenuState = false;
        static float lastSyncTime = ig::GetTime( );

        if (ig::GetTime( ) - lastSyncTime > 2.0f) {
            BackgroundSync(liveData);
            lastSyncTime = ig::GetTime( );
        }

        RenderNotifications( );

        if (bShowMenu && !lastMenuState) {
            FreeStats(liveData);
            liveData = GetLiveStats( );
        }
        lastMenuState = bShowMenu;

        if (!bShowMenu)
            return;

        ig::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
        ig::Begin("Project Zenith - Global Overlay", &bShowMenu);

        bool isAppRunning = IsProcessRunning(L"Achievements.exe");

        // --- İNGİLİZCE VE DUVARLI YENİ UYARI KUTUSU ---
        if (!isAppRunning) {
            ig::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.2f, 0.05f, 0.05f, 1.0f));
            ig::BeginChild("WarningBox", ImVec2(0, 50), true); // Kutuyu biraz daha yüksek yaptık (50)

            // Duvarı ekliyoruz (Kenardan 10 piksel boşluk kalacak şekilde aşağı kaydırır)
            ig::PushTextWrapPos(ig::GetWindowWidth( ) - 10.0f);
            ig::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "[!] WARNING: Achievements.exe is not running! ");
            ig::SameLine(0, 0); // Aradaki boşluğu sildik, metni direkt bağladık
            ig::TextDisabled("Please start the application to sync your progress.");
            ig::PopTextWrapPos( ); // Duvarı kapat

            ig::EndChild( );
            ig::PopStyleColor( );
            ig::Spacing( );
        }

        if (liveData.found) {
            ig::TextColored(ImVec4(0.7f, 0.5f, 1.0f, 1.0f), "Profile: %s", liveData.configName.c_str( ));
            int unlockedCount = 0;
            for (auto& ach : liveData.achievements) {
                if (ach.earned)
                    unlockedCount++;
            }
            ig::SameLine(ig::GetWindowWidth( ) - 250);
            ig::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Progress: %d/%d", unlockedCount, (int)liveData.achievements.size( ));
            long long s = liveData.totalMs / 1000;
            ig::SameLine(ig::GetWindowWidth( ) - 130);
            ig::Text("Playtime: %lldh %lldm", s / 3600, (s % 3600) / 60);

            ig::Separator( );
            ig::BeginChild("List", ImVec2(0, -40), true);
            for (auto& ach : liveData.achievements) {
                ig::BeginGroup( );
                if (ach.texture)
                    ig::Image((ImTextureID)(intptr_t)ach.texture, ImVec2(40, 40));
                else
                    ig::Button("X", ImVec2(40, 40));
                ig::SameLine( );

                ig::BeginGroup( );
                ig::Text("%s", ach.name.c_str( ));

                ig::PushTextWrapPos(ig::GetWindowWidth( ) - 140.0f);
                ig::TextDisabled("%s", ach.description.c_str( ));
                ig::PopTextWrapPos( );

                ig::EndGroup( );

                ig::SameLine(ig::GetWindowWidth( ) - 130);
                ig::BeginGroup( );
                ig::TextColored(ach.earned ? ImVec4(0, 1, 0, 1) : ImVec4(0.5f, 0.5f, 0.5f, 1), ach.earned ? "UNLOCKED" : "LOCKED");
                if (ach.rarity >= 0.0f) {
                    ig::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Rarity: %.1f%%", ach.rarity);
                }
                ig::EndGroup( );
                ig::EndGroup( );
                ig::Separator( );
            }
            ig::EndChild( );
        } else {
            // --- EN ALTTAKİ YAZILAR DA İNGİLİZCE OLDU ---
            if (!isAppRunning) {
                ig::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "Waiting for game data... (Please run the app)");
            } else {
                ig::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Game data not found!");
            }
        }

        if (ig::Button("Detach DLL")) {
            Hooks::bShuttingDown = true;
            Utils::UnloadDLL( );
        }
        ig::SameLine( );
        if (ig::Button("Sync Now")) {
            FreeStats(liveData);
            liveData = GetLiveStats( );
        }

        ig::SameLine(ig::GetWindowWidth( ) - 250);
        if (bWaitingForKey) {
            ig::Button("Press any key... (ESC to cancel)", ImVec2(230, 0));

            for (int i = 3; i < 256; i++) {
                if (i == VK_SHIFT || i == VK_CONTROL || i == VK_MENU || i == VK_LSHIFT || i == VK_RSHIFT || i == VK_LCONTROL || i == VK_RCONTROL || i == VK_LMENU || i == VK_RMENU)
                    continue;

                if (GetAsyncKeyState(i) & 0x8000) {
                    if (i == VK_ESCAPE) {
                        bWaitingForKey = false;
                    } else {
                        hotkeyMain = i;
                        hotkeyShift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                        hotkeyCtrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                        hotkeyAlt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
                        bWaitingForKey = false;
                    }
                    break;
                }
            }
        } else {
            std::string btnText = "Hotkey: ";
            if (hotkeyCtrl)
                btnText += "Ctrl + ";
            if (hotkeyShift)
                btnText += "Shift + ";
            if (hotkeyAlt)
                btnText += "Alt + ";
            btnText += GetKeyName(hotkeyMain);

            if (ig::Button(btnText.c_str( ), ImVec2(230, 0))) {
                bWaitingForKey = true;
            }
        }

        ig::End( );
    }
} // namespace Menu
