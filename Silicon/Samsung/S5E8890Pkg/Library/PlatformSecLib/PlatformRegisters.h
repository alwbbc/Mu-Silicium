#ifndef _PLATFORM_REGISTERS_H_
#define _PLATFORM_REGISTERS_H_

//
// Decon Registers (Exynos 8890, from the kernel's decon_8890/regs-decon.h)
//
#define SHADOW_REG_UPDATE_REQ                   0x60
#define SHADOW_REG_UPDATE_REQ_GLOBAL            (1U << 31)

#define HW_SW_TRIG_CONTROL                      0x70
#define HW_SW_TRIG_CONTROL_TRIG_AUTO_MASK_TRIG  (1U << 12)
#define HW_SW_TRIG_CONTROL_SW_TRIG              (1U << 8)
#define HW_SW_TRIG_CONTROL_HW_TRIG_MASK         (1U << 5)
#define HW_SW_TRIG_CONTROL_HW_TRIG_EN           (1U << 4)

#endif /* _PLATFORM_REGISTERS_H_ */
