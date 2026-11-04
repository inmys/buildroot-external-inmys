# SD ext4 rootfs; independent of Linux MMC enumeration and NAND contents.
if test -z "${dtbname}"; then
    setenv dtbname am335x-var-custom.dtb
fi
setenv bootargs console=ttyS0,115200n8 root=PARTUUID=33580001-02 rootfstype=ext4 rootwait rw panic=3
if load mmc ${mmcdev}:${mmcpart} ${kernel_addr_r} zImage; then
    if load mmc ${mmcdev}:${mmcpart} ${fdt_addr_r} ${dtbname}; then
        bootz ${kernel_addr_r} - ${fdt_addr_r}
    fi
fi
# Let bootcmd try NAND when loading or booting from SD fails.
test 1 = 0
