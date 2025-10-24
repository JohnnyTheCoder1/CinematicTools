#include "Main.h"
#include "Util/Util.h"
#include "AlienIsolation.h"

#include <algorithm>
#include <boost/filesystem.hpp>
#include <boost/chrono.hpp>
#include <fstream>
#include <string>
#include <Psapi.h>
#pragma comment(lib, "Psapi.lib")

static const char* g_gameName = "Alien: Isolation";
static const char* g_moduleName = "AI.exe";
static const char* g_className = "Alien: Isolation";
static const char* g_configFile = "./Cinematic Tools/config.ini";

Main* g_mainHandle = nullptr;
HINSTANCE g_dllHandle = NULL;

HINSTANCE g_gameHandle = NULL;
HWND g_gameHwnd = NULL;

WNDPROC g_origWndProc = 0;
bool g_shutdown = false;
bool g_hasFocus = true;

ID3D11DeviceContext* g_d3d11Context = nullptr;
ID3D11Device* g_d3d11Device = nullptr;
IDXGISwapChain* g_dxgiSwapChain = nullptr;

Main::Main() :
  m_Initialized(false),
  m_ConfigChanged(false),
  m_dtConfigCheck(0)
{

}

Main::~Main()
{
  util::log::Write("~Main()");

  // Save config and disable hooks before exit
  if (m_ConfigChanged)
    SaveConfig();

  util::hooks::SetHookState(false);
  SetWindowLongPtr(g_gameHwnd, -4, (LONG_PTR)g_origWndProc);
}

bool Main::Initialize()
{
  boost::filesystem::path mainDir("./Cinematic Tools/");
  boost::filesystem::path profileDir("./Cinematic Tools/Profiles");

  if (!boost::filesystem::exists(mainDir))
    boost::filesystem::create_directory(mainDir);

  if (!boost::filesystem::exists(profileDir))
    boost::filesystem::create_directory(profileDir);

  util::log::Init();
  util::log::Write("Cinematic Tools for %s\n", g_gameName);

  // Needed for ImGui + other functionality
  // Window and module may not be ready immediately after injection; wait up to a few seconds
  {
    const int maxTries = 200; // ~10 seconds
    int tries = 0;
    while (tries++ < maxTries)
    {
      g_gameHwnd = FindWindowA(g_className, NULL);
      if (g_gameHwnd) break;
      Sleep(50);
    }
  }
  if (g_gameHwnd == NULL)
  {
    util::log::Error("Failed to retrieve window handle, GetLastError 0x%X", GetLastError());
    return false;
  }

  // Used for relative offsets
  {
    const int maxTries = 200; // ~10 seconds
    int tries = 0;
    while (tries++ < maxTries)
    {
      g_gameHandle = GetModuleHandleA(g_moduleName);
      if (g_gameHandle) break;
      Sleep(50);
    }
  }
  if (g_gameHandle == NULL)
  {
    util::log::Error("Failed to retrieve module handle, GetLastError 0x%X", GetLastError());
    return false;
  }

  // Safely resolve D3D device/swapchain from game's singleton
  {
    MODULEINFO modInfo{ 0 };
    if (!GetModuleInformation(GetCurrentProcess(), g_gameHandle, &modInfo, sizeof(modInfo)))
      util::log::Warning("GetModuleInformation failed, GetLastError 0x%X", GetLastError());

    int d3dSingletonAddr = util::offsets::GetOffset("OFFSET_D3D");
    if (!util::IsAddressInModule(g_gameHandle, (void*)d3dSingletonAddr, sizeof(void*)))
    {
      util::log::Error("OFFSET_D3D (0x%X) is outside module image. Likely version mismatch.", d3dSingletonAddr);
      return false;
    }

    CATHODE::D3D** ppD3D = reinterpret_cast<CATHODE::D3D**>(d3dSingletonAddr);
    CATHODE::D3D* pD3D = nullptr;

    const int maxPtrTries = 200; // ~10 seconds total wait
    int ptrTries = 0;
    while (ptrTries++ < maxPtrTries)
    {
      if (!util::IsPtrReadable(ppD3D, sizeof(*ppD3D)))
      {
        Sleep(50);
        continue;
      }

      pD3D = *ppD3D;
      if (pD3D && util::IsPtrReadable(pD3D, sizeof(void*)))
        break;

      Sleep(50);
    }

    if (!util::IsPtrReadable(ppD3D, sizeof(*ppD3D)))
    {
      util::log::Error("D3D singleton address not readable after waiting: 0x%X", d3dSingletonAddr);
      return false;
    }

    if (!pD3D)
    {
      util::log::Error("D3D singleton pointer stayed null (addr 0x%X) after waiting", d3dSingletonAddr);
      return false;
    }

    if (!util::IsPtrReadable(pD3D, sizeof(void*)))
    {
      util::log::Error("D3D singleton pointer is not readable: 0x%p", pD3D);
      return false;
    }

    // Wait until the device/swapchain are initialized by the game (up to ~10s)
    const int maxTries = 200;
    int tries = 0;
    while (tries++ < maxTries)
    {
      g_d3d11Device = pD3D->m_pDevice;
      g_dxgiSwapChain = pD3D->m_pSwapChain;
      if (g_d3d11Device && g_dxgiSwapChain)
        break;
      Sleep(50);
    }

    if (g_d3d11Device)
      g_d3d11Device->GetImmediateContext(&g_d3d11Context);

    if (!g_d3d11Context || !g_d3d11Device || !g_dxgiSwapChain)
    {
      util::log::Error("Failed to retrieve Dx11 interfaces");
      util::log::Error("Device 0x%X DeviceContext 0x%X SwapChain 0x%X", g_d3d11Device, g_d3d11Context, g_dxgiSwapChain);
      return false;
    }
  }

  // This disables the object glow thing
  // Apply small byte patch, but only if target lies within module image
  {
    BYTE GlowPatch[7] = { 0x80, 0xB9, 0x65, 0x70, 0x02, 0x00, 0x01 };
    void* patchAddr = (void*)((int)g_gameHandle + 0x3A3494);
    if (util::IsAddressInModule(g_gameHandle, patchAddr, sizeof(GlowPatch)))
    {
      if (!util::WriteMemory((DWORD_PTR)patchAddr, GlowPatch, (DWORD)sizeof(GlowPatch)))
        util::log::Warning("Glow patch VirtualProtect/WriteMemory failed at %p", patchAddr);
    }
    else
    {
      util::log::Warning("Skipping glow patch: address %p outside module image (possible version mismatch)", patchAddr);
    }
  }

  // Make timescale writable
  {
    int tsAddr = util::offsets::GetOffset("OFFSET_TIMESCALE");
    if (util::IsAddressInModule(g_gameHandle, (void*)tsAddr, sizeof(double)))
    {
      DWORD dwOld = 0;
      if (!VirtualProtect(reinterpret_cast<LPVOID>(tsAddr), sizeof(double), PAGE_READWRITE, &dwOld))
        util::log::Warning("Could not get write permissions to timescale (addr 0x%X)", tsAddr);
    }
    else
    {
      util::log::Warning("Skipping timescale protect: address 0x%X outside module image", tsAddr);
    }
  }

  // Retrieve game version and make a const variable for whatever version
  // the tools support. If versions mismatch, scan for offsets.
  // util::offsets::Scan();

  m_pRenderer = std::make_unique<CTRenderer>();
  if (!m_pRenderer->Initialize())
    return false;

  m_pCameraManager = std::make_unique<CameraManager>();
  m_pCharacterController = std::make_unique<CharacterController>();
  m_pInputSystem = std::make_unique<InputSystem>();
  m_pVisualsController = std::make_unique<VisualsController>();
  m_pUI = std::make_unique<UI>();

  m_pInputSystem->Initialize();
  if (!m_pUI->Initialize())
    return false;

  util::hooks::Init();

  // Subclass the window with a new WndProc to catch messages
  g_origWndProc = (WNDPROC)SetWindowLongPtr(g_gameHwnd, -4, (LONG_PTR)&WndProc);
  if (g_origWndProc == 0)
  {
    util::log::Error("Failed to set WndProc, GetLastError 0x%X", GetLastError());
    return false;
  }

  LoadConfig();
  m_Initialized = true;
  return true;
}

void Main::Run()
{
  // Main update loop
  boost::chrono::high_resolution_clock::time_point lastUpdate = boost::chrono::high_resolution_clock::now();
  while (!g_shutdown)
  {
    boost::chrono::duration<float> dt = boost::chrono::high_resolution_clock::now() - lastUpdate;
    lastUpdate = boost::chrono::high_resolution_clock::now();

    m_pInputSystem->Update();
    m_pCameraManager->Update(dt.count());
    m_pCharacterController->Update();
    m_pVisualsController->Update();
    m_pUI->Update(dt.count());

    // Check if config has been affected, if so, save it
    m_dtConfigCheck += dt.count();
    if (m_dtConfigCheck > 10.f)
    {
      m_dtConfigCheck = 0;
      if (m_ConfigChanged)
      {
        m_ConfigChanged = false;
        SaveConfig();
      }
    }

    Sleep(10);
  }
}

void Main::OnConfigChanged()
{
  m_ConfigChanged = true;
}

void Main::LoadConfig()
{
  // Read config.ini using inih by Ben Hoyt
  // https://github.com/benhoyt/inih

  m_pConfig = std::make_unique<INIReader>(g_configFile);
  int parseResult = m_pConfig->ParseError();

  // If there's problems reading the file, notify the user.
  // Code-wise it should be safe to just continue,
  // since you can still request variables from INIReader.
  // They'll just return the specified default value.
  if (parseResult != 0)
  {
    util::log::Warning("Config file could not be loaded, using default settings");
    m_ConfigChanged = true; // Mark config as dirty so defaults get saved in the file
  }

  m_pCameraManager->ReadConfig(m_pConfig.get());
  m_pInputSystem->ReadConfig(m_pConfig.get());
}

void Main::SaveConfig()
{
  util::log::Write("Saving current config...");

  std::fstream file;
  file.open(g_configFile, std::ios_base::out | std::ios_base::trunc);

  if (!file.is_open())
  {
    util::log::Error("Could not save config, failed to open file for writing. GetLastError 0x%X", GetLastError());
    return;
  }

  file << m_pCameraManager->GetConfig();
  file << m_pInputSystem->GetConfig();
  
  file.close();
}

void Main::OnMapChange()
{
  util::log::Write("Waiting for a map to load...");

  if (m_Initialized)
    util::hooks::SetHookState(false);

  bool loadingMap = true;
  while (loadingMap)
  {
    Sleep(100);
    loadingMap = false;
  }

  if (m_Initialized)
  {
    util::hooks::SetHookState(true);
    m_pCameraManager->OnMapChange();
  }
}

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK Main::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  if (ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam))
    return TRUE;

  switch (uMsg)
  {
  case WM_ACTIVATE:
    // Focus event
    g_hasFocus = (wParam != WA_INACTIVE);
    break;
  case WM_INPUT:
    if (!g_mainHandle->m_pUI->IsEnabled())
      g_mainHandle->m_pInputSystem->HandleRawInput(lParam);
    else if (g_mainHandle->GetUI()->HasKeyboardFocus())
      return TRUE;
    break;
  case WM_KEYDOWN:
    if (g_mainHandle->m_pInputSystem->HandleKeyMsg(wParam, lParam) ||
        g_mainHandle->GetUI()->HasKeyboardFocus() ||
        (g_mainHandle->GetCameraManager()->IsCameraEnabled() && g_mainHandle->GetCameraManager()->IsKbmDisabled()))
      return TRUE;
    break;
  case WM_MOUSEMOVE:
    g_mainHandle->m_pInputSystem->HandleMouseMsg(lParam);
    break;
  case WM_SIZE:
    // Resize event
    g_mainHandle->m_pUI->OnResize();
    break;
  case WM_DESTROY:
    g_shutdown = true;
    break;
  }

  return CallWindowProc(g_origWndProc, hwnd, uMsg, wParam, lParam);
}

