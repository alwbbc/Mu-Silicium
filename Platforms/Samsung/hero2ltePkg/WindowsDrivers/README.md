# hero2lte Windows drivers

## hero2touch
KMDF 1.15 lower filter under `mshidkmdf` for the S6SA552 touchscreen. It drives the hardware directly:
- opens the clock gates for HSI2C7 and HSI2C15 and sets their pinmux
- turns on the S2MPS16 LDO32/LDO33 regulators
- polls the touch IRQ line (gpa1-0) and reports the first finger as an absolute HID mouse

The device binds to `ACPI\HERO0552`, which is declared in `AcpiTables/DSDT.asl`.

### Build (Linux, no Windows tools needed)
`build.sh` uses clang and lld-link with the WDK and SDK NuGet headers/libs
(`Microsoft.Windows.WDK.ARM64`, `Microsoft.Windows.SDK.CPP` 10.0.26100). It test-signs the driver with osslsigncode.

### WinPE injection (offline, no DISM)
1. Extract `\Windows\System32\config\SYSTEM` from boot.wim image 2 (wimlib).
2. Generate the registry edits with `Resources/Scripts/hero2lte_winpe_regs.py` and apply them with `hivexsh -w`.
   This adds a DriverDatabase package, the DeviceIds entry and the service keys.
3. With `wimlib-imagex update`, add the hive, `drivers\hero2touch.sys`, the `FileRepository` package folder and `INF\hero2touch.inf`.
4. In BCD, set `16000049` (testsigning) and `16000048` (nointegritychecks) on the Setup loader object.
5. Run `hero2lte_winpe.py win.iso winpe.img --wim boot.new.wim --bcd BCD`.
