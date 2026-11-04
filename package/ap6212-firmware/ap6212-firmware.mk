AP6212_FIRMWARE_VERSION = 20250410
AP6212_FIRMWARE_BASE = https://gitlab.com/kernel-firmware/linux-firmware/-/raw/$(AP6212_FIRMWARE_VERSION)
AP6212_FIRMWARE_SITE = $(AP6212_FIRMWARE_BASE)/cypress
AP6212_FIRMWARE_SOURCE = cyfmac43430-sdio.bin
AP6212_FIRMWARE_EXTRA_DOWNLOADS = \
	$(AP6212_FIRMWARE_BASE)/cypress/cyfmac43430-sdio.clm_blob \
	$(AP6212_FIRMWARE_BASE)/brcm/brcmfmac43430-sdio.AP6212.txt \
	$(AP6212_FIRMWARE_BASE)/LICENCE.cypress \
	$(AP6212_FIRMWARE_BASE)/LICENCE.broadcom_bcm43xx
AP6212_FIRMWARE_LICENSE = PROPRIETARY
AP6212_FIRMWARE_LICENSE_FILES = LICENCE.cypress LICENCE.broadcom_bcm43xx

# Install only this module's files rather than the entire Broadcom firmware set.
define AP6212_FIRMWARE_EXTRACT_CMDS
	cp $(addprefix $(AP6212_FIRMWARE_DL_DIR)/,$(AP6212_FIRMWARE_SOURCE) $(notdir $(AP6212_FIRMWARE_EXTRA_DOWNLOADS))) $(@D)/
endef

define AP6212_FIRMWARE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/cyfmac43430-sdio.bin $(TARGET_DIR)/lib/firmware/brcm/brcmfmac43430-sdio.bin
	$(INSTALL) -D -m 0644 $(@D)/cyfmac43430-sdio.clm_blob $(TARGET_DIR)/lib/firmware/brcm/brcmfmac43430-sdio.clm_blob
	$(INSTALL) -D -m 0644 $(@D)/brcmfmac43430-sdio.AP6212.txt $(TARGET_DIR)/lib/firmware/brcm/brcmfmac43430-sdio.txt
endef

$(eval $(generic-package))
