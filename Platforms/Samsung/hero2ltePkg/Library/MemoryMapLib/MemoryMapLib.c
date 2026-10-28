#include <Library/MemoryMapLib.h>

//
// Galaxy S7 Edge (SM-G935F, hero2lte) - Exynos 8890, 4GB
// Source: TWRP dump (/proc/iomem, /proc/device-tree reserved-memory, cmdline)
//   DRAM:  0x80000000-0xFE3FFFFF, 0x880000000-0x8FFFFFFFF
//   Framebuffer: s3cfb.bootloaderfb=0xE2A00000, 1440x2560x32, stride 5760
//
STATIC
EFI_MEMORY_REGION_DESCRIPTOR
gMemoryDescriptor[] = {
  // Name, Address, Length, HobOption, ResourceType, ResourceAttribute, MemoryType, ArmAttribute

  // DDR Regions
  {"Exynos Snapshot",    0x91200000, 0x01000000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK},       // ess_setup
  {"SEC Debug Logs",     0x92E00000, 0x00300000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK},       // tima/avc/auto_summary logs
  {"Tima",               0xB1000000, 0x00200000, AddMem, SYS_MEM, UNCACHEABLE, Reserv, UNCACHED_UNBUFFERED},
  {"UEFI FD",            0xB2000000, 0x00200000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},
  {"UEFI Stack",         0xB2200000, 0x00040000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},
  {"DXE Heap",           0xB2240000, 0x0DDC0000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK},       // up to 0xC0000000
  {"ION / Camera",       0xC0000000, 0x1EC00000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK},       // crypto..gpu_buffer + camera
  {"Free Memory 1",      0xDEC00000, 0x03E00000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK},
  {"Display Reserved",   0xE2A00000, 0x01C20000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},    // 2x 1440*2560*4
  {"Free Memory 2",      0xE4620000, 0x0B9E0000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK},
  {"Modem",              0xF0000000, 0x08800000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK},
  {"Secure Camera",      0xFC000000, 0x01C00000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK},
  {"RAM Partition",      0x880000000,0x80000000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK},       // 2GB

  // Register Regions
  {"Chip Info",          0x10000000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"GICD",               0x11001000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"GICC",               0x11002000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"GICH",               0x11004000, 0x00002000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"GICV",               0x11006000, 0x00002000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"PMU",                0x105C0000, 0x00010000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"SYSREG FSYS0",       0x10E50000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"UFS",                0x155A0000, 0x00003000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},      // HCI, VS, UniPro, PMA, UFSP
  {"Decon",              0x13960000, 0x00010000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE}       // decon_f
};

VOID
GetMemoryMap (
  OUT EFI_MEMORY_REGION_DESCRIPTOR **MemoryDescriptor,
  OUT UINT8                         *MemoryDescriptorCount)
{
  // Pass Data
  *MemoryDescriptor      = gMemoryDescriptor;
  *MemoryDescriptorCount = ARRAY_SIZE (gMemoryDescriptor);
}
