# Extend the standard TF-A package for this module without changing Buildroot.
# The switch is opt-in in TF-A; this external tree targets STM32 firmware 0.9+.
ifeq ($(ARM_TRUSTED_FIRMWARE_PLATFORM),mcom03)
define NMS_TFA_APPLY_BC_POWEROFF_PATCH
	$(APPLY_PATCHES) $(@D) \
		$(BR2_EXTERNAL_INMYS_NMS_SM_K1892_PATH)/patches/arm-trusted-firmware '*.patch'
endef
ARM_TRUSTED_FIRMWARE_POST_PATCH_HOOKS += NMS_TFA_APPLY_BC_POWEROFF_PATCH
ARM_TRUSTED_FIRMWARE_MAKE_OPTS += NMS_BC_PCAL_POWEROFF=1
endif
