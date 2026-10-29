//
// Galaxy S7 Edge (hero2lte) / Exynos 8890 DSDT: Cortex-A53 cluster only.
//
DefinitionBlock ("DSDT.aml", "DSDT", 2, "SAMSUN", "S5E8890 ", 8)
{
    Scope (\_SB)
    {
        Device (CPU0)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 0)

            Method (_STA, 0, NotSerialized) { Return (0x0F) }
        }

        //
        // UFS host (UFSHCI + Exynos vendor regs, UniPro, PMA, UFSP).
        // Link is brought up by UEFI; hero2ufs.sys only issues UTP commands.
        //
        Device (UFS0)
        {
            Name (_HID, "HERO0155")
            Name (_UID, 0)
            Name (_CCA, 1)   // UEFI enables UFS IO coherency (vendor DT: dma-coherent)

            Method (_STA, 0, NotSerialized) { Return (0x0F) }

            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x155A0000, 0x00003000)
                // GIC SPI 209 (DT: interrupts = <0 0xd1 0>); kept masked, the miniport polls
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 241 }
            })
        }

        //
        // S6SA552 touchscreen on HSI2C7 (0x48), powered from S2MPS16 LDO32/33
        // on HSI2C15. hero2touch.sys drives all of it directly.
        //
        Device (TSC0)
        {
            Name (_HID, "HERO0552")
            Name (_UID, 0)

            Method (_STA, 0, NotSerialized) { Return (0x0F) }
        }
    }
}
