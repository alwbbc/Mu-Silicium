#include <Library/GpioBankLib.h>

//
// Exynos 8890 GPIO Banks
//
// Only the ALIVE controller (pinctrl@10580000, CtrlNum 0) is filled in for now:
// it holds every hero2lte key (gpa0-3/4/5, gpa2-4). Other controllers follow
// PcdGpioCtrlAddr order and must be added from the DT pinctrl nodes when needed.
// Offsets use the standard Exynos 0x20 bank stride (verify if a key misbehaves).
//
STATIC
EFI_GPIO_BANK
mGpioBanks[] = {
  //
  // ALIVE
  //
  { .CtrlNum = 0, .Id = BANK_ID_A, .Number = 0, .Offset = 0x00 },
  { .CtrlNum = 0, .Id = BANK_ID_A, .Number = 1, .Offset = 0x20 },
  { .CtrlNum = 0, .Id = BANK_ID_A, .Number = 2, .Offset = 0x40 },
  { .CtrlNum = 0, .Id = BANK_ID_A, .Number = 3, .Offset = 0x60 }
};

VOID
GetGpioBanks (
  OUT EFI_GPIO_BANK **Bank,
  OUT UINT8          *Count)
{
  // Pass Data
  *Bank  = mGpioBanks;
  *Count = ARRAY_SIZE (mGpioBanks);
}
