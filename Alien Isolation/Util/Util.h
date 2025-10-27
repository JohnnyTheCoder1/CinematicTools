#pragma once

#include <DirectXMath.h>
#include <string>
#include <vector>
#include <Windows.h>

namespace util
{
  namespace hooks
  {
    enum HookType
    {
      MinHook,
      VTable
    };

    struct Hook
    {
      int Address;
      unsigned int Index;
      LPVOID* Original;
      HookType Type;
      bool Enabled;
    };

    bool Init();
    void InstallGameHooks();
  bool IsPresentHookInstalled();

    // if name is empty, then perform on all hooks
    void SetHookState(bool enabled, std::string const& name = "");
  };

  namespace log
  {
    void Init();

    void Write(const char* format, ...);
    void Warning(const char* format, ...);
    void Error(const char* format, ...);
    void Ok(const char* format, ...);
  };

  namespace offsets
  {
    struct Signature
    {
      BYTE* Pattern{ nullptr }; // The pattern to search
      std::string Mask;  // Which bytes should be evaluated (x = evaluate, ? = skip)

      bool HasReference{ false }; // Interpret the offset from the assembly reference
      int ReferenceOffset{ 0 }; // How far in the signature is the assembly reference
      int ReferenceSize{ 0 }; // How many bytes is the assembly reference (usually 4, obsolete?)
      int AddOffset{ 0 }; // How much bytes should be added to the final result

      int Result;

      Signature(std::string const& sig, int offset = 0);
    };

    void Scan();
    int GetOffset(std::string const& name);
    int GetRelOffset(std::string const& name);
  }

  bool GetResource(int, void*&, DWORD&);
  std::string VkToString(DWORD vk);
  std::string KeyLparamToString(LPARAM lparam);
  BYTE CharToByte(char c);

  BOOL WriteMemory(DWORD_PTR, const void*, DWORD);

  // Memory safety helpers
  // Returns true if the pointer appears to be readable for at least `bytes` bytes.
  bool IsPtrReadable(const void* ptr, size_t bytes = 1);
  // Returns true if [addr, addr+size) lies within the specified module's image range.
  bool IsAddressInModule(HMODULE hModule, const void* addr, size_t size);
  // Returns true if the game appears to be a Steam build.
  bool IsSteamBuild();

  namespace math
  {
    float CatmullRomInterpolate(float y0, float y1, float y2, float y3, float mu);
    DirectX::XMVECTOR ExtractYaw(DirectX::XMVECTOR quat);
  }
}