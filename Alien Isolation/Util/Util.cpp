#include "Util.h"
#include "../Main.h"

#include <codecvt>
#include <locale>
#include <Psapi.h>
#pragma comment(lib, "Psapi.lib")

namespace {
  // Helper to check if a memory page has readable protections
  static bool IsReadableProtection(DWORD prot)
  {
    return (prot & PAGE_READONLY) || (prot & PAGE_READWRITE) || (prot & PAGE_WRITECOPY) ||
           (prot & PAGE_EXECUTE_READ) || (prot & PAGE_EXECUTE_READWRITE) || (prot & PAGE_EXECUTE_WRITECOPY);
  }
}

// Loads resource data from the .dll file based on resource IDs in resource.h
bool util::GetResource(int ID, void* &pData, DWORD& size)
{
  HRSRC rc = FindResource(g_dllHandle, MAKEINTRESOURCE(ID), RT_RCDATA);
  if (!rc)
  {
    util::log::Error("FindResource failed, GetLastError 0x%X resource ID %d", GetLastError(), ID);
    return false;
  }

  HGLOBAL hglobal = LoadResource(g_dllHandle, rc);
  if (!hglobal)
  {
    util::log::Error("LoadResource failed, GetLastError 0x%X resource ID %d", GetLastError(), ID);
    return false;
  }

  pData = LockResource(hglobal);
  size = SizeofResource(g_dllHandle, rc);

  if (pData == nullptr || size == 0)
  {
    util::log::Error("GetResource returned empty data, GetLastError 0x%X resource ID %d", GetLastError(), ID);
    return false;
  }

  return true;
}

float util::math::CatmullRomInterpolate(float y0, float y1, float y2, float y3, float mu)
{
  float mu2 = mu * mu;
  float a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
  float a1 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
  float a2 = -0.5f * y0 + 0.5f * y2;
  float a3 = y1;

  return a0 * mu * mu2 + a1 * mu2 + a2 * mu + a3;
}

XMVECTOR util::math::ExtractYaw(XMVECTOR quat)
{
  // We only need to extract yaw
  XMVECTOR left = XMVector3Rotate(XMVectorSet(1, 0, 0, 0), quat);
  left.m128_f32[1] = 0;

  left = XMVector3Normalize(left);
  XMVECTOR up = XMVectorSet(0, 1, 0, 0);

  XMMATRIX rotationMatrix;
  rotationMatrix.r[0] = left;
  rotationMatrix.r[1] = up;
  rotationMatrix.r[2] = XMVector3Normalize(XMVector3Cross(left, up));
  rotationMatrix.r[3] = XMVectorSet(0, 0, 0, 1);

  quat = XMQuaternionRotationMatrix(rotationMatrix);
  quat = XMQuaternionNormalize(quat);

  return quat;
}

std::string util::VkToString(DWORD vk)
{
  unsigned int scanCode = MapVirtualKey(vk, MAPVK_VK_TO_VSC);

  switch (vk)
  {
    case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN: // arrow keys
    case VK_PRIOR: case VK_NEXT: // page up and page down
    case VK_END: case VK_HOME:
    case VK_INSERT: case VK_DELETE:
    case VK_DIVIDE: // numpad slash
    case VK_NUMLOCK:
    {
      scanCode |= 0x100; // set extended bit
      break;
    }
  }

  wchar_t wKeyName[50];
  memset(wKeyName, 0, 100);

  int length = GetKeyNameTextW(scanCode << 16, (LPWSTR)&wKeyName, 50);
  wKeyName[length] = L'\0';

  std::wstring_convert<std::codecvt_utf8<wchar_t>> utf8_conv;
  return utf8_conv.to_bytes(wKeyName);
}

std::string util::KeyLparamToString(LPARAM lparam)
{
  unsigned int scanCode = (lparam >> 16) & 0xFF;
  if ((lparam >> 24) & 1)
    scanCode |= 0x100;

  wchar_t wKeyName[50];
  memset(wKeyName, 0, 100);

  int length = GetKeyNameTextW(scanCode << 16, (LPWSTR)&wKeyName, 50);
  wKeyName[length] = L'\0';

  std::wstring_convert<std::codecvt_utf8<wchar_t>> utf8_conv;
  return utf8_conv.to_bytes(wKeyName);
}

BYTE util::CharToByte(char c)
{
  BYTE b;
  sscanf_s(&c, "%hhx", &b);
  return b;
}

BOOL util::WriteMemory(DWORD_PTR dwAddress, const void* cpvPatch, DWORD dwSize)
{
  DWORD dwProtect;
  if (VirtualProtect((void*)dwAddress, dwSize, PAGE_READWRITE, &dwProtect)) //Unprotect the memory
    memcpy((void*)dwAddress, cpvPatch, dwSize); //Write our patch
  else
    return false; //Failed to unprotect, so return false..

  return VirtualProtect((void*)dwAddress, dwSize, dwProtect, new DWORD); //Reprotect the memory
}

bool util::IsPtrReadable(const void* ptr, size_t bytes)
{
  if (!ptr) return false;
  MEMORY_BASIC_INFORMATION mbi{ 0 };
  if (!VirtualQuery(ptr, &mbi, sizeof(mbi)))
    return false;
  if (mbi.State != MEM_COMMIT)
    return false;
  if (!IsReadableProtection(mbi.Protect))
    return false;
  // Ensure the requested size fits in this region
  const uintptr_t begin = reinterpret_cast<uintptr_t>(ptr);
  const uintptr_t end = begin + bytes;
  const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
  return end <= regionEnd;
}

bool util::IsAddressInModule(HMODULE hModule, const void* addr, size_t size)
{
  if (!hModule || !addr) return false;
  MODULEINFO info{ 0 };
  if (!GetModuleInformation(GetCurrentProcess(), hModule, &info, sizeof(info)))
    return false;

  const uintptr_t base = reinterpret_cast<uintptr_t>(info.lpBaseOfDll);
  const uintptr_t end = base + info.SizeOfImage;
  const uintptr_t a = reinterpret_cast<uintptr_t>(addr);
  const uintptr_t b = a + size;
  return (a >= base) && (b <= end);
}

bool util::IsSteamBuild()
{
  // simplest heuristic: Steam client dll present OR steam_api loaded by the game
  return GetModuleHandleA("steam_api.dll") != nullptr
      || GetModuleHandleA("steamclient.dll") != nullptr;
}

