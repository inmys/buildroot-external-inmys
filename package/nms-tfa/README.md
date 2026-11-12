# NMS board controller poweroff in TF-A

This package fragment extends Buildroot's standard arm-trusted-firmware
package for MCom03. It applies the local TF-A patch and passes
`NMS_BC_PCAL_POWEROFF=1`. The upstream platform defaults to no board-specific
poweroff action when that option is absent.

Requirements: NMS-SM-K1892VM21YA wiring, STM32 firmware 0.9+ with the virtual
PCAL6416 register map, and I2C2 through PCA9540B 0x70 channel 1 to STM32 0x2a.
Deploy the updated BL31 in the actual SD/QSPI boot chain; updating Linux alone
does not activate this feature. Reboot still uses the existing PSCI watchdog.

The final SYSTEM_OFF handler takes ownership of I2C2 after Linux shutdown,
enables its clock using the 27 MHz XTI bypass, selects the mux branch and
asserts virtual GPIO14 through PCAL output/direction writes. All polling waits
are bounded. I2C failure retains the platform's previous watchdog-servicing
halt loop.

Validated on ELVEES-SMARC-CB, 2026-11-12: Linux poweroff produced OFF/LINUX_OFF
in STM32; a real short POWER press without removing power produced
RUNNING/BUTTON_ON and booted Linux. The STM32 application was not reflashed.
Clean extraction, patch application and TF-A build through Buildroot passed.
RTC/WOL wake policy and full DCDC removal remain separate work.
