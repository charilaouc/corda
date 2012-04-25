/* Copyright (c) 2009, Avian Contributors

   Permission to use, copy, modify, and/or distribute this software
   for any purpose with or without fee is hereby granted, provided
   that the above copyright notice and this permission notice appear
   in all copies.

   There is NO WARRANTY for this software.  See license.txt for
   details. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tools.h"

namespace {

#define IMAGE_SIZEOF_SHORT_NAME 8

#define IMAGE_FILE_RELOCS_STRIPPED 1
#define IMAGE_FILE_LINE_NUMS_STRIPPED 4
#define IMAGE_FILE_MACHINE_AMD64 0x8664
#define IMAGE_FILE_MACHINE_I386 0x014c
#define IMAGE_FILE_32BIT_MACHINE 256

#define IMAGE_SCN_ALIGN_1BYTES 0x100000
#define IMAGE_SCN_ALIGN_2BYTES 0x200000
#define IMAGE_SCN_ALIGN_4BYTES 0x300000
#define IMAGE_SCN_ALIGN_8BYTES 0x400000
#define IMAGE_SCN_MEM_EXECUTE 0x20000000
#define IMAGE_SCN_MEM_READ 0x40000000
#define IMAGE_SCN_MEM_WRITE 0x80000000
#define IMAGE_SCN_CNT_CODE 32

struct IMAGE_FILE_HEADER {
  uint16_t Machine;
  uint16_t NumberOfSections;
  uint32_t TimeDateStamp;
  uint32_t PointerToSymbolTable;
  uint32_t NumberOfSymbols;
  uint16_t SizeOfOptionalHeader;
  uint16_t Characteristics;
} __attribute__((packed));

struct IMAGE_SECTION_HEADER {
  uint8_t Name[IMAGE_SIZEOF_SHORT_NAME];
  union {
    uint32_t PhysicalAddress;
    uint32_t VirtualSize;
  } Misc;
  uint32_t VirtualAddress;
  uint32_t SizeOfRawData;
  uint32_t PointerToRawData;
  uint32_t PointerToRelocations;
  uint32_t PointerToLinenumbers;
  uint16_t NumberOfRelocations;
  uint16_t NumberOfLinenumbers;
  uint32_t Characteristics;
} __attribute__((packed));

struct IMAGE_SYMBOL {
  union {
    struct {
      uint32_t Short;
      uint32_t Long;
    } Name;
  } N;
  uint32_t Value;
  int16_t SectionNumber;
  uint16_t Type;
  uint8_t StorageClass;
  uint8_t NumberOfAuxSymbols;
} __attribute__((packed));

inline unsigned
pad(unsigned n)
{
  return (n + (4 - 1)) & ~(4 - 1);
}

using namespace avian::tools;

void
writeObject(const uint8_t* data, unsigned size, OutputStream* out,
            const char* startName, const char* endName,
            const char* sectionName, int machine, int machineMask,
            int sectionMask)
{
  const unsigned sectionCount = 1;
  const unsigned symbolCount = 2;

  const unsigned sectionNumber = 1;

  const unsigned startNameLength = strlen(startName) + 1;
  const unsigned endNameLength = strlen(endName) + 1;

  const unsigned startNameOffset = 4;
  const unsigned endNameOffset = startNameOffset + startNameLength;

  IMAGE_FILE_HEADER fileHeader = {
    machine, // Machine
    sectionCount, // NumberOfSections
    0, // TimeDateStamp
    sizeof(IMAGE_FILE_HEADER)
    + sizeof(IMAGE_SECTION_HEADER)
    + pad(size), // PointerToSymbolTable
    symbolCount, // NumberOfSymbols
    0, // SizeOfOptionalHeader
    IMAGE_FILE_RELOCS_STRIPPED
    | IMAGE_FILE_LINE_NUMS_STRIPPED
    | machineMask // Characteristics
  };

  IMAGE_SECTION_HEADER sectionHeader = {
    "", // Name
    0, // PhysicalAddress
    0, // VirtualAddress
    pad(size), // SizeOfRawData
    sizeof(IMAGE_FILE_HEADER)
    + sizeof(IMAGE_SECTION_HEADER), // PointerToRawData
    0, // PointerToRelocations
    0, // PointerToLinenumbers
    0, // NumberOfRelocations
    0, // NumberOfLinenumbers
    sectionMask // Characteristics
  };

  strncpy(reinterpret_cast<char*>(sectionHeader.Name), sectionName,
          sizeof(sectionHeader.Name));

  IMAGE_SYMBOL startSymbol = {
    { 0 }, // Name
    0, // Value
    sectionNumber, // SectionNumber
    0, // Type
    2, // StorageClass
    0, // NumberOfAuxSymbols
  };
  startSymbol.N.Name.Long = startNameOffset;

  IMAGE_SYMBOL endSymbol = {
    { 0 }, // Name
    size, // Value
    sectionNumber, // SectionNumber
    0, // Type
    2, // StorageClass
    0, // NumberOfAuxSymbols
  };
  endSymbol.N.Name.Long = endNameOffset;

  out->writeChunk(&fileHeader, sizeof(fileHeader));
  out->writeChunk(&sectionHeader, sizeof(sectionHeader));

  out->writeChunk(data, size);
  out->writeRepeat(0, pad(size) - size);

  out->writeChunk(&startSymbol, sizeof(startSymbol));
  out->writeChunk(&endSymbol, sizeof(endSymbol));

  uint32_t symbolTableSize = endNameOffset + endNameLength;
  out->writeChunk(&symbolTableSize, 4);

  out->writeChunk(startName, startNameLength);
  out->writeChunk(endName, endNameLength);
}

template<unsigned BytesPerWord>
class WindowsPlatform : public Platform {
public:

  class PEObjectWriter : public ObjectWriter {
  public:

    virtual bool write(uint8_t* data, size_t size, OutputStream* out,
                       const char* startName, const char* endName,
                       unsigned alignment, unsigned accessFlags)
    {
      int machine;
      int machineMask;

      if (BytesPerWord == 8) {
        machine = IMAGE_FILE_MACHINE_AMD64;
        machineMask = 0;
      } else { // if (BytesPerWord == 8)
        machine = IMAGE_FILE_MACHINE_I386;
        machineMask = IMAGE_FILE_32BIT_MACHINE;
      }

      int sectionMask;
      switch (alignment) {
      case 0:
      case 1:
        sectionMask = IMAGE_SCN_ALIGN_1BYTES;
        break;
      case 2:
        sectionMask = IMAGE_SCN_ALIGN_2BYTES;
        break;
      case 4:
        sectionMask = IMAGE_SCN_ALIGN_4BYTES;
        break;
      case 8:
        sectionMask = IMAGE_SCN_ALIGN_8BYTES;
        break;
      default:
        fprintf(stderr, "unsupported alignment: %d\n", alignment);
        return false;
      }

      sectionMask |= IMAGE_SCN_MEM_READ;

      const char* sectionName;
      if (accessFlags & ObjectWriter::Writable) {
        if (accessFlags & ObjectWriter::Executable) {
          sectionName = ".rwx";
          sectionMask |= IMAGE_SCN_MEM_WRITE
            | IMAGE_SCN_MEM_EXECUTE
            | IMAGE_SCN_CNT_CODE;
        } else {
          sectionName = ".data";
          sectionMask |= IMAGE_SCN_MEM_WRITE;
        }
      } else {
        sectionName = ".text";
        sectionMask |= IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE;
      }

      writeObject(data, size, out, startName, endName, sectionName, machine,
                  machineMask, sectionMask);

      return true;
    }

    virtual void dispose() {
      delete this;
    }

  };

  virtual ObjectWriter* makeObjectWriter() {
    return new PEObjectWriter();
  }

  WindowsPlatform():
    Platform(PlatformInfo(PlatformInfo::Windows, BytesPerWord == 4 ? PlatformInfo::x86 : PlatformInfo::x86_64)) {}
};

WindowsPlatform<4> windows32Platform;
WindowsPlatform<8> windows64Platform;

}
