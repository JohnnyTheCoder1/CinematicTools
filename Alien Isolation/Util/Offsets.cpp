#include "Util.h"
#include "../Main.h"

#include <fstream>
#include <unordered_map>
#include <vector>
#include <memory>
#include <stdexcept>
#include <Psapi.h>

namespace util
{
  namespace offsets
  {
    struct CompiledSig {
      std::vector<BYTE> bytes;   // compact: only actual bytes (no spaces)
      std::string       mask;    // same length as bytes, 'x' or '?'
      int refStart = -1;         // index in bytes where [ ... ] begins (first '?')
      int refSize  = 0;          // number of bytes inside brackets
    };
  }
}

namespace
{
  using util::offsets::CompiledSig;

  bool m_UseScannedResults = false;
  std::unordered_map<std::string, util::offsets::Signature> m_Signatures;

  // Fill with hardcoded offsets if you don't want to use scanning
  // These should be relative to the module base.
  std::unordered_map<std::string, int> m_HardcodedOffsets = {
    {"OFFSET_D3D", 0x17DF5CC},
    {"OFFSET_MAIN", 0x12F0C88},

    // Function starts (AI.exe x86)
    {"OFFSET_CAMERAUPDATE", 0x00421C0},
    {"OFFSET_GETCAMERAMATRIX", 0x5B0B40},
    {"OFFSET_POSTPROCESSUPDATE", 0x608C50},

    {"OFFSET_TONEMAPUPDATE", 0x00218400},

  {"OFFSET_INPUTUPDATE", 0x0058D640},
  {"OFFSET_GAMEPADUPDATE", 0x60EE30},
  {"OFFSET_COMBATMANAGERUPDATE", 0x0038A740},

    {"OFFSET_SHOWMOUSE", 0x1359B44},
    {"OFFSET_DRAWUI", 0x1240F27},
    {"OFFSET_FREEZETIME", 0x12F194C},
    {"OFFSET_SCALEFORM", 0x134A78C},
    {"OFFSET_TIMESCALE", 0x0DC6EA0},
    {"OFFSET_POSTPROCESS", 0x15D0970}
  };

  static CompiledSig Compile(std::string const& sig) {
    CompiledSig out;
    bool inRef = false;
    for (size_t i = 0; i < sig.size();) {
      char c = sig[i];
      if (c == ' ') { ++i; continue; }
      if (c == '[') {
        if (!inRef && out.refStart < 0)
          inRef = true;
        ++i;
        continue;
      }
      if (c == ']') {
        if (inRef)
          inRef = false;
        ++i;
        continue;
      }

      if (c == '?') {
        // accept "?" or "??"
        if (i + 1 < sig.size() && sig[i+1] == '?') ++i;
        out.bytes.push_back(0x00);
        out.mask.push_back('?');
        if (inRef) {
          if (out.refStart < 0) out.refStart = (int)out.bytes.size() - 1;
          out.refSize++;
        }
        ++i;
      } else {
        // read two hex chars -> one byte
        auto hex = [](char h)->int {
          if (h >= '0' && h <= '9') return h - '0';
          if (h >= 'A' && h <= 'F') return 10 + (h - 'A');
          if (h >= 'a' && h <= 'f') return 10 + (h - 'a');
          return -1;
        };
        if (i + 1 >= sig.size()) throw std::runtime_error("Odd hex length");
        int hi = hex(sig[i]), lo = hex(sig[i+1]);
        if (hi < 0 || lo < 0)   throw std::runtime_error("Bad hex in signature");
        out.bytes.push_back((BYTE)((hi << 4) | lo));
        out.mask.push_back('x');
        i += 2;
      }
    }
    return out;
  }

  static BYTE* FindPattern(BYTE* base, size_t size, CompiledSig const& s) {
    size_t n = s.bytes.size();
    if (n == 0 || n > size) return nullptr;
    BYTE* end = base + (size - n);
    for (BYTE* p = base; p <= end; ++p) {
      size_t j = 0;
      for (; j < n; ++j) {
        if (s.mask[j] == 'x' && p[j] != s.bytes[j]) break;
      }
      if (j == n) return p;
    }
    return nullptr;
  }
}

util::offsets::Signature::Signature(std::string const& sig, int offset /* = 0 */)
{
  AddOffset = offset;
  Compiled = std::make_unique<CompiledSig>(Compile(sig));
  HasReference = (Compiled->refStart >= 0);
  ReferenceSize = Compiled->refSize;
}

void util::offsets::Scan()
{
  m_Signatures.clear();

  // Gameplay/update hooks
  m_Signatures.emplace("OFFSET_CAMERAUPDATE", Signature(
    "55 8B EC 83 E4 F0 81 EC 74 01 00 00 53 56 8B F1 "
    "8B 86 D8 01 00 00 33 DB 57 85 C0 74 12 38 98 4D 01 00 00 74 0A "
    "38 98 4F 01 00 00 75 02 8B D8 "
    "80 BE 21 02 00 00 00 74 0C "
    "8B 86 F0 01 00 00 85 C0 74 02 8B D8 "
    "85 DB 0F 84 ?? ?? ?? ?? "
    "F3 0F 10 43 44 F3 0F 10 4B 40 F3 0F 10 53 3C F3 0F 10 5B 2C "
    "F3 0F 11 44 24 48 0F 57 C0 "
    "E8 ?? ?? ?? ?? "
    "F3 0F 7E 00 "
    "F3 0F 10 35 [ ?? ?? ?? ?? ]", 0));

  m_Signatures.emplace("OFFSET_GETCAMERAMATRIX", Signature(
    "8B 44 24 04 56 8B F1 "
    "8D 96 4B 03 00 00 2B D0 90 "
    "8A 08 88 0C 02 40 84 C9 75 F6 "
    "8B 44 24 0C 89 86 14 03 00 00 "
    "83 F8 01 75 4E "
    "A1 [ ?? ?? ?? ?? ] "
    "50 E8 ?? ?? ?? ?? 6A 01 50 "
    "89 86 70 03 00 00 "
    "E8 ?? ?? ?? ??", 0));

  m_Signatures.emplace("OFFSET_POSTPROCESSUPDATE", Signature(
    "83 EC 08 53 8B 5C 24 10 55 56 57 "
    "8B 7C 24 20 8B CF 2B CB "
    "B8 AB AA AA 2A F7 E9 D1 FA 8B C2 C1 E8 1F 03 C2 "
    "83 F8 20 0F 8E ?? ?? ?? ?? "
    "8B 74 24 24 85 F6 0F 8E ?? ?? ?? ?? "
    "57 8D 44 24 14 53 50 E8 ?? ?? ?? ?? "
    "8B 6C 24 20 8B C6 99 2B C2 D1 F8 "
    "8B F0 99 2B C2 D1 F8 03 F0 "
    "8B CF 2B CD B8 AB AA AA 2A F7 E9 "
    "8B 4C 24 1C D1 FA 8B C2 C1 E8 1F 03 C2 "
    "2B CB 89 44 24 2C", 0));

  m_Signatures.emplace("OFFSET_TONEMAPUPDATE", Signature(
    "83 EC 20 53 56 8B F1 "
    "E8 ?? ?? ?? ?? "
    "8B 98 74 03 00 00 85 DB 0F 84 ?? ?? ?? ?? "
    "57 8D 4C 24 0C E8 ?? ?? ?? ?? "
    "8B 7C 24 30 57 8B CE E8 ?? ?? ?? ?? D9 5C 24 10 "
    "57 8B CE E8 ?? ?? ?? ?? D9 5C 24 18 "
    "57 8B CE E8 ?? ?? ?? ?? D9 5C 24 1C "
    "F3 0F 10 44 24 10 0F 2E 43 10 9F F6 C4 44 7A ?? "
    "F3 0F 10 44 24 14 0F 2E 43 14 9F F6 C4 44 7A ?? "
    "F3 0F 10 44 24 18 0F 2E 43 18 9F F6 C4 44 7A ?? "
    "F3 0F 10 44 24 1C 0F 2E 43 1C 9F F6 C4 44", 0));

  m_Signatures.emplace("OFFSET_INPUTUPDATE", Signature(
    "0F 57 ED 56 8B B1 40 10 00 00 85 F6 74 32 "
    "80 7E 10 00 74 2C 80 7E 11 00 74 26 "
    "33 C0 39 81 58 10 00 00 76 1C "
    "8D 91 80 0A 00 00 F3 0F 11 2A 40 83 C2 44 "
    "3B 81 58 10 00 00 72 F0 "
    "F3 0F 10 35 [ ?? ?? ?? ?? ] "
    "B0 02 84 41 3C 74 08 F3 0F 11 B1 C4 0A 00 00", 0));

  m_Signatures.emplace("OFFSET_GAMEPADUPDATE", Signature(
    "8B 44 24 04 F6 44 08 14 80 74 15 "
    "F3 0F 10 05 [ ?? ?? ?? ?? ] F3 0F 11 44 24 04 D9 44 24 04 C2 04 00 "
    "0F 57 C0 F3 0F 11 44 24 04 D9 44 24 04 C2 04 00 "
    "80 7C 24 08 00 74 0A "
    "F3 0F 10 05 ?? ?? ?? ?? EB 08 "
    "F3 0F 10 05 ?? ?? ?? ?? "
    "8B 44 24 04 F6 44 08 14 80", 0));

  m_Signatures.emplace("OFFSET_COMBATMANAGERUPDATE", Signature(
    "55 8B EC 83 E4 F0 F3 0F 10 55 0C 83 EC 64 53 56 8B F1 "
    "F3 0F 10 86 A0 01 00 00 F3 0F 59 C2 F3 0F 58 86 74 01 00 00 "
    "0F 28 C8 "
    "F3 0F 59 0D [ ?? ?? ?? ?? ] "
    "0F 2F 0D [ ?? ?? ?? ?? ] "
    "57 76 15 "
    "F3 0F 58 0D [ ?? ?? ?? ?? ] "
    "F3 0F 2C C1 0F 57 C9 F3 0F 2A C8 "
    "F3 0F 59 0D [ ?? ?? ?? ?? ]", 0));

  // Globals / data pointers
  m_Signatures.emplace("OFFSET_POSTPROCESS", Signature(
    "A1 [ ?? ?? ?? ?? ] 85 C0 74 ?? 8B 48 ??", 0));

  m_Signatures.emplace("OFFSET_SCALEFORM", Signature(
    "8B 0D [ ?? ?? ?? ?? ] 85 C9 74 ?? 8B 01 FF 50 ??", 0));

  m_Signatures.emplace("OFFSET_FREEZETIME", Signature(
    // legacy pattern; keep but also provide a timer-subss AOB below
    "F6 05 [ ?? ?? ?? ?? ] 00 75 ??", 0));

  m_Signatures.emplace("OFFSET_TIMESCALE", Signature(
    "F3 0F 10 05 [ ?? ?? ?? ?? ] F3 0F 59 ?? ??", 0));

  // AOB: local mode timer subss (subss xmm0, [ecx+0x0C]) — useful for per-mode timer freeze
  m_Signatures.emplace("AOB_TIMER_SUBSS", Signature(
    "F3 0F 5C 41 0C", 0));

  // AOB: global dt candidate — two movss [imm32] occurrences in same function
  m_Signatures.emplace("AOB_GLOBAL_DT", Signature(
    "F3 0F 10 0D [ ?? ?? ?? ?? ] ?? ?? ?? ?? F3 0F 10 0D [ ?? ?? ?? ?? ]", 0));

  // AOB: player flag detection pattern used to gate player-specific logic
  m_Signatures.emplace("AOB_PLAYER_FLAG", Signature(
    "0F B7 86 3C 03 00 00 66 85 C0 75", 0));

  util::log::Write("Scanning for offsets...");

  MODULEINFO info;
  if (!GetModuleInformation(GetCurrentProcess(), g_gameHandle, &info, sizeof(MODULEINFO)))
  {
    util::log::Error("GetModuleInformation failed, GetLastError 0x%X", GetLastError());
    util::log::Error("Offset scanning unavailable");
    return;
  }

  bool allFound = true;
  bool foundAny = false;
  uintptr_t moduleBase = reinterpret_cast<uintptr_t>(info.lpBaseOfDll);

  for (auto& kv : m_Signatures)
  {
    auto& sig = kv.second;
    auto* cs = sig.Compiled.get();
    if (!cs)
    {
      util::log::Error("Signature %s has no compiled data", kv.first.c_str());
      allFound = false;
      sig.Result = 0;
      continue;
    }

    BYTE* p = FindPattern((BYTE*)info.lpBaseOfDll, info.SizeOfImage, *cs);
    if (!p) {
      util::log::Error("Could not find pattern for %s", kv.first.c_str());
      allFound = false;
      sig.Result = 0;
      continue;
    }

    if (sig.HasReference)
    {
      if (cs->refSize != 4)
      {
        util::log::Error("Signature %s expected 4-byte reference, got %d", kv.first.c_str(), cs->refSize);
        allFound = false;
        sig.Result = 0;
        continue;
      }

      BYTE* ref = p + cs->refStart;
      uint32_t addr = *reinterpret_cast<uint32_t*>(ref);
      sig.Result = static_cast<uintptr_t>(addr) + sig.AddOffset;
    }
    else
    {
      sig.Result = reinterpret_cast<uintptr_t>(p) + sig.AddOffset;
    }

    foundAny = true;
    uintptr_t rva = sig.Result >= moduleBase ? sig.Result - moduleBase : 0;
    util::log::Write("%s resolved at 0x%08X (RVA 0x%X)", kv.first.c_str(), static_cast<unsigned int>(sig.Result), static_cast<unsigned int>(rva));
  }

  if (allFound && foundAny)
    util::log::Ok("All offsets found");
  else if (foundAny)
    util::log::Warning("Some offsets were not found; gameplay hooks will be skipped for missing entries");
  else
    util::log::Warning("All offsets could not be found, this might result in a crash");

  m_UseScannedResults = foundAny;
}

int util::offsets::GetOffset(std::string const& name)
{
  // If a scan located any offsets, prefer those results.
  if (m_UseScannedResults)
  {
    auto it = m_Signatures.find(name);
    if (it != m_Signatures.end() && it->second.Result)
      return static_cast<int>(it->second.Result);

    // Scanning is active but this key was not resolved; treat as missing.
    return 0;
  }

  // Fall back to hardcoded RVAs only when scanning yielded nothing.
  auto hardcodedResult = m_HardcodedOffsets.find(name);
  if (hardcodedResult != m_HardcodedOffsets.end())
  {
    uintptr_t base = reinterpret_cast<uintptr_t>(g_gameHandle);
    return static_cast<int>(base + hardcodedResult->second);
  }

  util::log::Error("Offset %s does not exist", name.c_str());
  return 0;
}

int util::offsets::GetRelOffset(std::string const& name)
{
  if (m_UseScannedResults)
  {
    auto it = m_Signatures.find(name);
    if (it != m_Signatures.end() && it->second.Result)
      return static_cast<int>(it->second.Result - reinterpret_cast<uintptr_t>(g_gameHandle));

    return 0;
  }

  auto hardcodedResult = m_HardcodedOffsets.find(name);
  if (hardcodedResult != m_HardcodedOffsets.end())
    return hardcodedResult->second;

  util::log::Error("Relative offset %s does not exist", name.c_str());
  return 0;
}
