// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Core/Boot/ElfReader.h"

#include <string>
#include <utility>

#include "Common/CommonTypes.h"
#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "Common/MsgHandler.h"
#include "Common/Swap.h"

#include "Core/HW/Memmap.h"
#include "Core/PowerPC/PPCSymbolDB.h"

static void bswap(u32& w)
{
  w = Common::swap32(w);
}
static void bswap(u16& w)
{
  w = Common::swap16(w);
}

static void byteswapHeader(Elf32_Ehdr& ELF_H)
{
  bswap(ELF_H.e_type);
  bswap(ELF_H.e_machine);
  bswap(ELF_H.e_ehsize);
  bswap(ELF_H.e_phentsize);
  bswap(ELF_H.e_phnum);
  bswap(ELF_H.e_shentsize);
  bswap(ELF_H.e_shnum);
  bswap(ELF_H.e_shstrndx);
  bswap(ELF_H.e_version);
  bswap(ELF_H.e_entry);
  bswap(ELF_H.e_phoff);
  bswap(ELF_H.e_shoff);
  bswap(ELF_H.e_flags);
}

static void byteswapSegment(Elf32_Phdr& sec)
{
  bswap(sec.p_align);
  bswap(sec.p_filesz);
  bswap(sec.p_flags);
  bswap(sec.p_memsz);
  bswap(sec.p_offset);
  bswap(sec.p_paddr);
  bswap(sec.p_vaddr);
  bswap(sec.p_type);
}

static void byteswapSection(Elf32_Shdr& sec)
{
  bswap(sec.sh_addr);
  bswap(sec.sh_addralign);
  bswap(sec.sh_entsize);
  bswap(sec.sh_flags);
  bswap(sec.sh_info);
  bswap(sec.sh_link);
  bswap(sec.sh_name);
  bswap(sec.sh_offset);
  bswap(sec.sh_size);
  bswap(sec.sh_type);
}

ElfReader::ElfReader(std::vector<u8> buffer) : BootExecutableReader(std::move(buffer))
{
  Initialize(m_bytes.data());
}

ElfReader::ElfReader(File::IOFile file) : BootExecutableReader(std::move(file))
{
  Initialize(m_bytes.data());
}

ElfReader::ElfReader(const std::string& filename) : BootExecutableReader(filename)
{
  Initialize(m_bytes.data());
}

ElfReader::~ElfReader() = default;

void ElfReader::Initialize(u8* ptr)
{
  base = (char*)ptr;
  base32 = (u32*)ptr;
  header = (Elf32_Ehdr*)ptr;
  byteswapHeader(*header);

  segments = (Elf32_Phdr*)(base + header->e_phoff);
  sections = (Elf32_Shdr*)(base + header->e_shoff);

  for (int i = 0; i < GetNumSegments(); i++)
  {
    byteswapSegment(segments[i]);
  }

  for (int i = 0; i < GetNumSections(); i++)
  {
    byteswapSection(sections[i]);
  }
  entryPoint = header->e_entry;

  bRelocate = (header->e_type != ET_EXEC);
}

const char* ElfReader::GetSectionName(int section) const
{
  if (sections[section].sh_type == SHT_NULL)
    return nullptr;

  int nameOffset = sections[section].sh_name;
  char* ptr = (char*)GetSectionDataPtr(header->e_shstrndx);

  if (ptr)
    return ptr + nameOffset;
  else
    return nullptr;
}

// This is just a simple elf loader, good enough to load elfs generated by devkitPPC
bool ElfReader::LoadIntoMemory(bool only_in_mem1) const
{
  INFO_LOG(MASTER_LOG, "String section: %i", header->e_shstrndx);

  if (bRelocate)
  {
    PanicAlert("Error: Dolphin doesn't know how to load a relocatable elf.");
    return false;
  }

  INFO_LOG(MASTER_LOG, "%i segments:", header->e_phnum);

  // Copy segments into ram.
  for (int i = 0; i < header->e_phnum; i++)
  {
    Elf32_Phdr* p = segments + i;

    INFO_LOG(MASTER_LOG, "Type: %i Vaddr: %08x Filesz: %i Memsz: %i ", p->p_type, p->p_vaddr,
             p->p_filesz, p->p_memsz);

    if (p->p_type == PT_LOAD)
    {
      u32 writeAddr = p->p_vaddr;
      const u8* src = GetSegmentPtr(i);
      u32 srcSize = p->p_filesz;
      u32 dstSize = p->p_memsz;

      if (only_in_mem1 && p->p_vaddr >= Memory::REALRAM_SIZE)
        continue;

      Memory::CopyToEmu(writeAddr, src, srcSize);
      if (srcSize < dstSize)
        Memory::Memset(writeAddr + srcSize, 0, dstSize - srcSize);  // zero out bss

      INFO_LOG(MASTER_LOG, "Loadable Segment Copied to %08x, size %08x", writeAddr, p->p_memsz);
    }
  }

  INFO_LOG(MASTER_LOG, "Done loading.");
  return true;
}

SectionID ElfReader::GetSectionByName(const char* name, int firstSection) const
{
  for (int i = firstSection; i < header->e_shnum; i++)
  {
    const char* secname = GetSectionName(i);

    if (secname != nullptr && strcmp(name, secname) == 0)
      return i;
  }
  return -1;
}

bool ElfReader::LoadSymbols() const
{
  bool hasSymbols = false;
  SectionID sec = GetSectionByName(".symtab");
  if (sec != -1)
  {
    int stringSection = sections[sec].sh_link;
    const char* stringBase = (const char*)GetSectionDataPtr(stringSection);

    // We have a symbol table!
    Elf32_Sym* symtab = (Elf32_Sym*)(GetSectionDataPtr(sec));
    int numSymbols = sections[sec].sh_size / sizeof(Elf32_Sym);
    for (int sym = 0; sym < numSymbols; sym++)
    {
      int size = Common::swap32(symtab[sym].st_size);
      if (size == 0)
        continue;

      // int bind = symtab[sym].st_info >> 4;
      int type = symtab[sym].st_info & 0xF;
      int sectionIndex = Common::swap16(symtab[sym].st_shndx);
      int value = Common::swap32(symtab[sym].st_value);
      const char* name = stringBase + Common::swap32(symtab[sym].st_name);
      if (bRelocate)
        value += sectionAddrs[sectionIndex];

      auto symtype = Symbol::Type::Data;
      switch (type)
      {
      case STT_OBJECT:
        symtype = Symbol::Type::Data;
        break;
      case STT_FUNC:
        symtype = Symbol::Type::Function;
        break;
      default:
        continue;
      }
      g_symbolDB.AddKnownSymbol(value, size, name, symtype);
      hasSymbols = true;
    }
  }
  g_symbolDB.Index();
  return hasSymbols;
}

bool ElfReader::IsWii() const
{
  // Use the same method as the DOL loader uses: search for mfspr from HID4,
  // which should only be used in Wii ELFs.
  //
  // Likely to have some false positives/negatives, patches implementing a
  // better heuristic are welcome.

  // Swap these once, instead of swapping every word in the file.
  u32 HID4_pattern = Common::swap32(0x7c13fba6);
  u32 HID4_mask = Common::swap32(0xfc1fffff);

  for (int i = 0; i < GetNumSegments(); ++i)
  {
    if (IsCodeSegment(i))
    {
      u32* code = (u32*)GetSegmentPtr(i);
      for (u32 j = 0; j < GetSegmentSize(i) / sizeof(u32); ++j)
      {
        if ((code[j] & HID4_mask) == HID4_pattern)
          return true;
      }
    }
  }

  return false;
}
