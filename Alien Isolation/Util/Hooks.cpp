#include "Util.h"
#include "../Main.h"

#include "../AlienIsolation.h"
#include <MinHook.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <DirectXMath.h>
#include <unordered_map>
#include <cstring>
#include <Windows.h>

#pragma comment(lib, "d3d11.lib")

// Function definitions
typedef HRESULT(__stdcall* tIDXGISwapChain_Present)(IDXGISwapChain*, UINT, UINT);
typedef HRESULT(__stdcall* tIDXGISwapChain1_Present1)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
typedef BOOL(WINAPI* tSetCursorPos)(int, int);

typedef int(__thiscall* tCameraUpdate)(CATHODE::AICameraManager*);
typedef int(__thiscall* tInputUpdate)(void*);
typedef int(__thiscall* tGamepadUpdate)(void*);

typedef int(__thiscall* tPostProcessUpdate)(int);
typedef char(__stdcall* tTonemapUpdate)(CATHODE::DayToneMapSettings*, int);
typedef bool(__thiscall* tCombatManagerUpdate)(void*, CATHODE::Character*);

//////////////////////////
////   RENDER HOOKS   ////
//////////////////////////

// Most games today run on DirectX 11. This hook is called just before
// the image is actually drawn on screen so we can draw more stuff
// on top, like the tools UI.

tIDXGISwapChain_Present oIDXGISwapChain_Present = nullptr;
tIDXGISwapChain1_Present1 oIDXGISwapChain1_Present1 = nullptr;

namespace
{
  void HandlePresent(IDXGISwapChain* pSwapchain, UINT SyncInterval, UINT Flags)
  {
    static bool loggedDeviceFailure = false;
    static bool loggedFirstPresent = false;
    static bool loggedSwapChainCapture = false;

    if (!loggedFirstPresent)
    {
      util::log::Write(">>> Present hook CALLED! pSwapchain=0x%p SyncInterval=%u Flags=%u", pSwapchain, SyncInterval, Flags);
      loggedFirstPresent = true;
    }

    if (!g_dxgiSwapChain)
      g_dxgiSwapChain = pSwapchain;

    if (g_dxgiSwapChain == pSwapchain && !loggedSwapChainCapture)
    {
      util::log::Ok("Captured IDXGISwapChain from Present hook (0x%p)", g_dxgiSwapChain);
      loggedSwapChainCapture = true;
    }

    if (!g_d3d11Device)
    {
      ID3D11Device* pDevice = nullptr;
      HRESULT hr = pSwapchain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&pDevice));
      if (FAILED(hr))
      {
        if (!loggedDeviceFailure)
        {
          util::log::Warning("SwapChain::GetDevice failed while capturing interfaces, HRESULT 0x%X", hr);
          loggedDeviceFailure = true;
        }
      }
      else if (!pDevice)
      {
        if (!loggedDeviceFailure)
        {
          util::log::Warning("SwapChain::GetDevice succeeded but returned null device pointer");
          loggedDeviceFailure = true;
        }
      }
      else
      {
        g_d3d11Device = pDevice;
        util::log::Ok("Captured ID3D11Device from Present hook (0x%p)", g_d3d11Device);
      }
    }

    if (g_d3d11Device && !g_d3d11Context)
    {
      g_d3d11Device->GetImmediateContext(&g_d3d11Context);
      if (g_d3d11Context)
        util::log::Ok("Captured ID3D11DeviceContext from Present hook (0x%p)", g_d3d11Context);
    }

    if (!g_shutdown && g_mainHandle)
    {
      CTRenderer* pRenderer = g_mainHandle->GetRenderer();
      UI* pUI = g_mainHandle->GetUI();
      CameraManager* pCameraManager = g_mainHandle->GetCameraManager();

      if (pRenderer && pRenderer->IsReady() && pUI && pUI->IsReady() && pCameraManager)
      {
        pUI->BindRenderTarget();
        pRenderer->UpdateMatrices();
        //g_mainHandle->GetCameraManager()->DrawTrack();
        pUI->Draw();
      }
    }
  }
}

HRESULT __stdcall hIDXGISwapChain_Present(IDXGISwapChain* pSwapchain, UINT SyncInterval, UINT Flags)
{
  HandlePresent(pSwapchain, SyncInterval, Flags);
  return oIDXGISwapChain_Present ? oIDXGISwapChain_Present(pSwapchain, SyncInterval, Flags) : S_OK;
}

HRESULT __stdcall hIDXGISwapChain1_Present1(IDXGISwapChain1* pSwapchain, UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* pPresentParameters)
{
  HandlePresent(pSwapchain, SyncInterval, Flags);
  return oIDXGISwapChain1_Present1 ? oIDXGISwapChain1_Present1(pSwapchain, SyncInterval, Flags, pPresentParameters) : S_OK;
}

//////////////////////////
////   CAMERA HOOKS   ////
//////////////////////////

// This is an example of what a camera hook could look like.
// Different games and engines have different kinds of methods
// and it's up to you to figure out what to hook and how to
// override the game's camera. Some games might require multiple
// hooks.

tCameraUpdate oCameraUpdate = nullptr;

int __fastcall hCameraUpdate(CATHODE::AICameraManager* pCameraManager, void* /*edx*/)
{
  g_mainHandle->GetCameraManager()->OnCameraUpdateBegin();
  int result = oCameraUpdate ? oCameraUpdate(pCameraManager) : 0;
  g_mainHandle->GetCameraManager()->OnCameraUpdateEnd();
  return result;
}


//////////////////////////
////   INPUT HOOKS    ////
//////////////////////////

tInputUpdate oInputUpdate = nullptr;
tGamepadUpdate oGamepadUpdate = nullptr;
tSetCursorPos oSetCursorPos = nullptr;

int __fastcall hInputUpdate(void* _this, void* /*edx*/)
{
  CameraManager* pCameraManager = g_mainHandle->GetCameraManager();
  if (pCameraManager->IsCameraEnabled() && pCameraManager->IsKbmDisabled())
    return 0;

  return oInputUpdate ? oInputUpdate(_this) : 0;
}

int __fastcall hGamepadUpdate(void* _this, void* /*edx*/)
{
  CameraManager* pCameraManager = g_mainHandle->GetCameraManager();
  InputSystem* pInputSystem = g_mainHandle->GetInputSystem();

  if (pCameraManager->IsCameraEnabled()
    && pCameraManager->IsGamepadDisabled()
    && !pInputSystem->IsUsingSecondPad())
    return 0;
  
  return oGamepadUpdate ? oGamepadUpdate(_this) : 0;
}

BOOL WINAPI hSetCursorPos(int x, int y)
{
  UI* pUI = g_mainHandle ? g_mainHandle->GetUI() : nullptr;
  if (pUI && pUI->IsEnabled())
    return TRUE;

  return oSetCursorPos ? oSetCursorPos(x, y) : TRUE;
}


//////////////////////////
////   OTHER HOOKS    ////
//////////////////////////

// These could be hooks on AI to disable them, object iterators
// to find certain components, something to disable the usual
// outlines on friendly players/characters etc...
// Hooks on cursor functions are usually needed to show the mouse
// when tools UI is open.

tPostProcessUpdate oPostProcessUpdate = nullptr;
tCombatManagerUpdate oCombatManagerUpdate = nullptr;
tTonemapUpdate oTonemapUpdate = nullptr;

int __fastcall hPostProcessUpdate(int _this, void* /*edx*/)
{
  int result = oPostProcessUpdate ? oPostProcessUpdate(_this) : 0;
  __try {
    auto* pPostProcess = reinterpret_cast<CATHODE::PostProcess*>(_this + 0x1918);
    if (util::IsPtrReadable(pPostProcess, sizeof(void*)))
    {
      g_mainHandle->GetCameraManager()->OnPostProcessUpdate(pPostProcess);
      g_mainHandle->GetVisualsController()->OnPostProcessUpdate(pPostProcess);
    }
  } __except(EXCEPTION_EXECUTE_HANDLER) {
    // skip on bad offset
    util::log::Warning("Exception in hPostProcessUpdate, likely bad offset for PostProcess struct. Skipping update.");
  }
  return result;
}

bool __fastcall hCombatManagerUpdate(void* _this, void* _EDX, CATHODE::Character* pTargetChr)
{
  if (g_mainHandle->GetCharacterController()->IsPlayerInvisible())
  {
    CATHODE::Character* pPlayer = CATHODE::Main::Singleton()->m_CharacterManager->m_PlayerCharacters[0];
    if (pPlayer == pTargetChr)
      return false;
  }

  return oCombatManagerUpdate ? oCombatManagerUpdate(_this, pTargetChr) : false;
}

char __stdcall hTonemapSettings(CATHODE::DayToneMapSettings* pTonemapSettings, int a2)
{
  char result = oTonemapUpdate ? oTonemapUpdate(pTonemapSettings, a2) : 0;
  g_mainHandle->GetVisualsController()->OnTonemapUpdate();

  return result;
}


/*----------------------------------------------------------------*/

namespace
{
  std::unordered_map<std::string, util::hooks::Hook> m_CreatedHooks;
  bool g_minHookInitialized = false;
  bool g_presentHookCreated = false;
  bool g_gameHooksInstalled = false;

  bool EnsureMinHookInitialized()
  {
    if (g_minHookInitialized)
      return true;

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK)
    {
      util::log::Error("Failed to initialize MinHook, MH_STATUS 0x%X", status);
      return false;
    }

    g_minHookInitialized = true;
    return true;
  }

  HWND CreateDummyWindow()
  {
    const char* className = "CTDummyWindowClass";

    WNDCLASSEXA wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = className;

    if (!RegisterClassExA(&wc))
    {
      if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return nullptr;
    }

    return CreateWindowExA(0, className, "CTDummyWindow", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);
  }

  void DestroyDummyWindow(HWND hwnd)
  {
    if (!hwnd)
      return;

    const char* className = "CTDummyWindowClass";
    DestroyWindow(hwnd);
    UnregisterClassA(className, GetModuleHandleA(nullptr));
  }
}

// Creates a normal function hook with MinHook, 
// which places a jmp instruction at the start of the function.
template <typename T>
static bool CreateHook(std::string const& name, int target, PVOID hook, T original)
{
  if (!EnsureMinHookInitialized())
    return false;

  LPVOID* pOriginal = reinterpret_cast<LPVOID*>(original);
  MH_STATUS result = MH_CreateHook((LPVOID)target, hook, pOriginal);
  if (result != MH_OK)
  {
    util::log::Error("Could not create %s hook. MH_STATUS 0x%X error code 0x%X", name.c_str(), result, GetLastError());
    return false;
  }

  result = MH_EnableHook((LPVOID)target);
  if (result != MH_OK)
  {
    util::log::Error("Could not enable %s hook. MH_STATUS 0x%X error code 0x%X", name.c_str(), result, GetLastError());
    return false;
  }

  util::hooks::Hook hookInfo{ 0 };
  hookInfo.Address = target;
  hookInfo.Type = util::hooks::HookType::MinHook;
  hookInfo.Enabled = true;

  m_CreatedHooks.emplace(name, hookInfo);
  return true;
}

static bool CreateDXGIPresentHook()
{
  if (g_presentHookCreated)
    return true;

  if (!EnsureMinHookInitialized())
    return false;

  HWND hwnd = CreateDummyWindow();
  if (!hwnd)
  {
    util::log::Error("Failed to create dummy window for Present hook, GetLastError 0x%X", GetLastError());
    return false;
  }

  DXGI_SWAP_CHAIN_DESC desc = { 0 };
  desc.BufferCount = 1;
  desc.BufferDesc.Width = 100;
  desc.BufferDesc.Height = 100;
  desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.BufferDesc.RefreshRate.Numerator = 60;
  desc.BufferDesc.RefreshRate.Denominator = 1;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.OutputWindow = hwnd;
  desc.SampleDesc.Count = 1;
  desc.Windowed = TRUE;
  desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  D3D_FEATURE_LEVEL featureLevels[] =
  {
    D3D_FEATURE_LEVEL_11_0,
    D3D_FEATURE_LEVEL_10_1,
    D3D_FEATURE_LEVEL_10_0,
    D3D_FEATURE_LEVEL_9_3
  };

  D3D_FEATURE_LEVEL obtainedLevel = D3D_FEATURE_LEVEL_11_0;
  IDXGISwapChain* pSwapChain = nullptr;
  ID3D11Device* pDevice = nullptr;
  ID3D11DeviceContext* pContext = nullptr;
  UINT createFlags = 0;

  HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createFlags,
    featureLevels, _countof(featureLevels), D3D11_SDK_VERSION, &desc, &pSwapChain, &pDevice, &obtainedLevel, &pContext);

  if (FAILED(hr))
  {
    hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createFlags,
      featureLevels, _countof(featureLevels), D3D11_SDK_VERSION, &desc, &pSwapChain, &pDevice, &obtainedLevel, &pContext);
  }

  if (FAILED(hr))
  {
    hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_REFERENCE, nullptr, createFlags,
      featureLevels, _countof(featureLevels), D3D11_SDK_VERSION, &desc, &pSwapChain, &pDevice, &obtainedLevel, &pContext);
  }

  if (FAILED(hr))
  {
    util::log::Error("Failed to create dummy D3D11 device for Present hook, HRESULT 0x%X", hr);
    DestroyDummyWindow(hwnd);
    return false;
  }

  void** vtbl = *reinterpret_cast<void***>(pSwapChain);
  util::log::Write("SwapChain vtable at 0x%p, Present at slot 8 is 0x%p", vtbl, vtbl[8]);
  
  bool hookCreated = CreateHook("SwapChainPresent", (int)vtbl[8], hIDXGISwapChain_Present, &oIDXGISwapChain_Present);

  if (hookCreated)
  {
    IDXGISwapChain1* pSwapChain1 = nullptr;
    HRESULT qiHr = pSwapChain->QueryInterface(__uuidof(IDXGISwapChain1), reinterpret_cast<void**>(&pSwapChain1));
    if (SUCCEEDED(qiHr) && pSwapChain1)
    {
      void** vtbl1 = *reinterpret_cast<void***>(pSwapChain1);
      util::log::Write("SwapChain1 vtable at 0x%p, Present1 at slot 22 is 0x%p", vtbl1, vtbl1[22]);
      if (CreateHook("SwapChainPresent1", (int)vtbl1[22], hIDXGISwapChain1_Present1, &oIDXGISwapChain1_Present1))
        util::log::Ok("Installed DXGI Present1 hook via dummy device");
      pSwapChain1->Release();
    }
    else
    {
      util::log::Warning("IDXGISwapChain1 interface unavailable for Present1 hook, HRESULT 0x%X", qiHr);
    }
  }

  pSwapChain->Release();
  pDevice->Release();
  pContext->Release();
  DestroyDummyWindow(hwnd);

  if (!hookCreated)
    return false;

  g_presentHookCreated = true;
  util::log::Ok("Installed DXGI Present hook via dummy device");
  return true;
}

// Write to VTable
static PBYTE WINAPI WriteToVTable(PDWORD* ppVTable, PVOID hook, SIZE_T iIndex)
{
  DWORD dwOld = 0;
  VirtualProtect((void*)((*ppVTable) + iIndex), sizeof(PDWORD), PAGE_EXECUTE_READWRITE, &dwOld);

  PBYTE pOrig = ((PBYTE)(*ppVTable)[iIndex]);
  (*ppVTable)[iIndex] = (DWORD)hook;

  VirtualProtect((void*)((*ppVTable) + iIndex), sizeof(PDWORD), dwOld, &dwOld);
  return pOrig;
}

// Hooks a function by changing the address at given index
// in the virtual function table.
template <typename T>
static void CreateVTableHook(std::string const& name, PDWORD* ppVTable, PVOID hook, SIZE_T iIndex, T original)
{
  LPVOID* pOriginal = reinterpret_cast<LPVOID*>(original);
  *pOriginal = reinterpret_cast<LPVOID>(WriteToVTable(ppVTable, hook, iIndex));

  util::hooks::Hook hookInfo{ 0 };
  hookInfo.Address = (int)ppVTable;
  hookInfo.Index = iIndex;
  hookInfo.Type = util::hooks::HookType::VTable;
  hookInfo.Original = pOriginal;
  hookInfo.Enabled = true;

  m_CreatedHooks.emplace(name, hookInfo);
}

bool util::hooks::Init()
{
  if (!EnsureMinHookInitialized())
    return false;

  if (!CreateDXGIPresentHook())
    return false;

  return true;
}

bool util::hooks::IsPresentHookInstalled()
{
  return g_presentHookCreated;
}

void util::hooks::InstallGameHooks()
{
  if (g_gameHooksInstalled)
    return;

  if (!EnsureMinHookInitialized())
    return;

  const bool isSteamBuild = util::IsSteamBuild();

  auto safeCreate = [](const char* name, const char* key, auto hook, auto original)
  {
    int addr = util::offsets::GetOffset(key);
    if (!util::IsAddressInModule(g_gameHandle, (void*)addr, 16))
    {
      util::log::Warning("Skipping hook %s: address 0x%X outside module image", name, addr);
      return;
    }

    // Sanity check executable memory
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) && !(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)))
    {
        util::log::Warning("Skipping hook %s: target 0x%X not in executable memory", name, addr);
        return;
    }

    BYTE preview[6] = { 0 };
    if (!util::IsPtrReadable(reinterpret_cast<void*>(addr), sizeof(preview)))
    {
      util::log::Warning("Skipping hook %s: target 0x%X unreadable", name, addr);
      return;
    }

    memcpy(preview, reinterpret_cast<void*>(addr), sizeof(preview));
    util::log::Write("Hook %s targeting 0x%X bytes %02X %02X %02X %02X %02X %02X", name, addr,
      preview[0], preview[1], preview[2], preview[3], preview[4], preview[5]);

    CreateHook(name, addr, hook, original);
  };

  if (isSteamBuild)
  {
    util::log::Write("Steam build detected, installing all gameplay hooks...");
    safeCreate("CameraUpdate", "OFFSET_CAMERAUPDATE", hCameraUpdate, &oCameraUpdate);
    //safeCreate("InputUpdate", "OFFSET_INPUTUPDATE", hInputUpdate, &oInputUpdate);
    safeCreate("GamepadUpdate", "OFFSET_GAMEPADUPDATE", hGamepadUpdate, &oGamepadUpdate);
    safeCreate("PostProcessUpdate", "OFFSET_POSTPROCESSUPDATE", hPostProcessUpdate, &oPostProcessUpdate);
    safeCreate("TonemapUpdate", "OFFSET_TONEMAPUPDATE", hTonemapSettings, &oTonemapUpdate);
    //CreateHook("AICombatManagerUpdate", util::offsets::GetOffset("OFFSET_COMBATMANAGERUPDATE"), hCombatManagerUpdate, &oCombatManagerUpdate);
  }
  else
  {
    util::log::Warning("Non-Steam build detected - skipping cinematic/gameplay hooks to avoid crashes. Overlay/UI only.");
  }

  FARPROC setCursorProc = GetProcAddress(GetModuleHandleA("user32.dll"), "SetCursorPos");
  if (setCursorProc)
    CreateHook("SetCursorPos", (int)setCursorProc, hSetCursorPos, &oSetCursorPos);
  else
    util::log::Warning("Failed to locate SetCursorPos in user32.dll");

  g_gameHooksInstalled = true;
}

// In some cases it's useful or even required to disable all hooks or just certain ones
void util::hooks::SetHookState(bool enable, std::string const& name)
{
  if (name.empty())
  {
    MH_STATUS status = enable ? MH_EnableHook(MH_ALL_HOOKS) : MH_DisableHook(MH_ALL_HOOKS);
    if (status != MH_OK)
      util::log::Error("MinHook failed to %s all hooks, MH_STATUS 0x%X", (enable ? "enable" : "disable"), status);

    for (auto& entry : m_CreatedHooks)
    {
      Hook& hook = entry.second;
      if (hook.Type == HookType::MinHook) continue;
      if (hook.Enabled != enable)
      {
        *hook.Original = WriteToVTable((PDWORD*)hook.Address, *hook.Original, hook.Index);
        hook.Enabled = enable;
      }
      else
        util::log::Warning("VTable hook %s is already %s", name.c_str(), enable ? "enabled" : "disabled");
    }
  }
  else
  {
    auto result = m_CreatedHooks.find(name);
    if (result == m_CreatedHooks.end())
    {
      util::log::Error("Hook %s does not exit", name.c_str());
      return;
    }

    Hook& hook = result->second;
    if (hook.Type == HookType::MinHook)
    {
      MH_STATUS status = enable ? MH_EnableHook((LPVOID)hook.Address) : MH_DisableHook((LPVOID)hook.Address);
      if (status != MH_OK)
        util::log::Error("MinHook failed to %s hook %s, MH_STATUS 0x%X", (enable ? "enable" : "disable"), name.c_str(), status);
    }
    else
    {
      if (hook.Enabled != enable)
      {
        *hook.Original = WriteToVTable((PDWORD*)hook.Address, *hook.Original, hook.Index);
        hook.Enabled = enable;
      }
      else
        util::log::Warning("VTable hook %s is already %s", name.c_str(), enable ? "enabled" : "disabled");
    }
  }
}
