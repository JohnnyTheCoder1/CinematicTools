#include "Util.h"
#include "../Main.h"

#include <boost/assign.hpp>
#include <fstream>
#include <unordered_map>
#include <Psapi.h>

using namespace boost::assign;

namespace
{
  bool m_UseScannedResults = false;
  std::unordered_map<std::string, util::offsets::Signature> m_Signatures;

  // Fill with hardcoded offsets if you don't want to use scanning
  // These should be relative to the module base.
  std::unordered_map<std::string, int> m_HardcodedOffsets = map_list_of
  ("OFFSET_D3D", 0x17DF5CC)
  ("OFFSET_MAIN", 0x12F0C88)

  ("OFFSET_CAMERAUPDATE", 0x32300)
  ("OFFSET_GETCAMERAMATRIX", 0x5B0B40)
  ("OFFSET_POSTPROCESSUPDATE", 0x608C50)
  ("OFFSET_TONEMAPUPDATE", 0x208490)

  ("OFFSET_INPUTUPDATE", 0x57D6C0)
  ("OFFSET_GAMEPADUPDATE", 0x60EE30)
  ("OFFSET_COMBATMANAGERUPDATE", 0x37A800)

  ("OFFSET_SHOWMOUSE", 0x1359B44)
  ("OFFSET_DRAWUI", 0x1240F27)
  ("OFFSET_FREEZETIME", 0x12F194C)
  ("OFFSET_SCALEFORM", 0x134A78C)
  ("OFFSET_TIMESCALE", 0xDC6EA0)
  std::unordered_map<std::string, int> m_HardcodedOffsets = map_list_of
  ("OFFSET_D3D", 0x17DF5CC)
  ("OFFSET_MAIN", 0x12F0C88)

  ("OFFSET_CAMERAUPDATE", 0x32300)
  ("OFFSET_GETCAMERAMATRIX", 0x5B0B40)
  ("OFFSET_POSTPROCESSUPDATE", 0x608C50)
  ("OFFSET_TONEMAPUPDATE", 0x208490)

  ("OFFSET_INPUTUPDATE", 0x57D6C0)
  ("OFFSET_GAMEPADUPDATE", 0x60EE30)
  ("OFFSET_COMBATMANAGERUPDATE", 0x37A800)

  ("OFFSET_SHOWMOUSE", 0x1359B44)
  ("OFFSET_DRAWUI", 0x1240F27)
  ("OFFSET_FREEZETIME", 0x12F194C)
  ("OFFSET_SCALEFORM", 0x134A78C)
  ("OFFSET_TIMESCALE", 0xDC6EA0)
  ("OFFSET_POSTPROCESS", 0x15D0970);

  struct CompiledSig {
    std::vector<BYTE> bytes;   // compact: only actual bytes (no spaces)
    std::string       mask;    // same length as bytes, 'x' or '?'
    int refStart = -1;         // index in bytes where [ ... ] begins (first '?')
    int refSize  = 0;          // number of bytes inside brackets
    int add      = 0;          // AddOffset
  };

  static CompiledSig Compile(std::string const& sig, int addOffset) {
    CompiledSig out; out.add = addOffset;
    bool inRef = false;
    for (size_t i = 0; i < sig.size();) {
      char c = sig[i];
      if (c == ' ') { ++i; continue; }
      if (c == '[') { inRef = true; ++i; continue; }
      if (c == ']') { inRef = false; ++i; continue; }

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
  // store compiled form in Pattern/Mask using an internal encoding
  // simplest: stash pointer to CompiledSig via Pattern
  // but easier: repurpose fields
  AddOffset = offset;
  // abuse Pattern to store pointer to CompiledSig we allocate
  auto* cs = new CompiledSig(Compile(sig, offset));
  Pattern = reinterpret_cast<BYTE*>(cs);
  Mask.clear(); // unused
  HasReference = (cs->refStart >= 0);
  ReferenceOffset = cs->refStart;
  ReferenceSize   = cs->refSize;
}

void util::offsets::Scan()
{
  // Signature example
  // Scan memory and find this pattern. Question marks are wildcard bytes.
  // Brackets mean that the offset is extracted from the assembly reference
  // For example:
  // mov rax,[123456]
  // We want 123456, its place should be marked with wildcard bytes surrounded
  // by brackets in the signature.
  //
  // The last argument is the offset to be added to the result, useful when
  // you need a code offset for byte patches.

  m_Signatures.emplace("OFFSET_EXAMPLE", Signature("12 34 56 78 [ ?? ?? ?? ?? ] AA BB ?? DF", 0x20));

  util::log::Write("Scanning for offsets...");

  MODULEINFO info;
  if (!GetModuleInformation(GetCurrentProcess(), g_gameHandle, &info, sizeof(MODULEINFO)))
  {
    util::log::Error("GetModuleInformation failed, GetLastError 0x%X", GetLastError());
    util::log::Error("Offset scanning unavailable");
    return;
  }

  bool allFound = true;

  for (auto& kv : m_Signatures)
  {
    auto& sig = kv.second;
    auto* cs = reinterpret_cast<CompiledSig*>(sig.Pattern);
    BYTE* p = FindPattern((BYTE*)info.lpBaseOfDll, info.SizeOfImage, *cs);
    if (!p) {
      util::log::Error("Could not find pattern for %s", kv.first.c_str());
      allFound = false;
      continue;
    }
    if (sig.HasReference && cs->refSize == 4) {
      // 32-bit RIP-relative style: ref is a 32-bit displacement from end of ref
      BYTE* ref = p + cs->refStart;
      int disp = *reinterpret_cast<int*>(ref);
      BYTE* abs = (ref + 4) + disp;
      sig.Result = reinterpret_cast<int>(abs) + cs->add;
    } else {
      sig.Result = reinterpret_cast<int>(p) + cs->add;
    }
  }

  if (allFound)
    util::log::Ok("All offsets found");
  else
    util::log::Warning("All offsets could not be found, this might result in a crash");

  m_UseScannedResults = true;
}

int util::offsets::GetOffset(std::string const& name)
{
  // If a scan was done, prefer those results.
  // If something couldn't be found or there was no scan,
  // use the hardcoded offsets.

  if (m_UseScannedResults)
  {
    auto result = m_Signatures.find(name);
    if (result != m_Signatures.end())
    {
      if (result->second.Result)
        return result->second.Result;
    }
  }

  // If the offsets were scanned, their absolute position is known.
  // With hardcoded offsets, use relative offset because it's not
  // 100% guaranteed the game module will load at the same address space.

  auto hardcodedResult = m_HardcodedOffsets.find(name);
  if (hardcodedResult != m_HardcodedOffsets.end())
    return hardcodedResult->second + (int)g_gameHandle;

  util::log::Error("Offset %s does not exist", name.c_str());
  return 0;
}

int util::offsets::GetRelOffset(std::string const& name)
{
  if (m_UseScannedResults)
  {
    auto result = m_Signatures.find(name);
    if (result != m_Signatures.end())
    {
      if (result->second.Result)
        return result->second.Result - (int)g_gameHandle;
    }
  }

  auto hardcodedResult = m_HardcodedOffsets.find(name);
  if (hardcodedResult != m_HardcodedOffsets.end())
    return hardcodedResult->second;

  util::log::Error("Relative offset %s does not exist", name.c_str());
  return 0;
}
