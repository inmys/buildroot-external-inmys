################################################################################
#
# Add csitpg driver (can be build into kernel)
#
################################################################################

LINUX_EXTENSIONS += csitpg

define CSITPG_PREPARE_KERNEL
	dest=drivers/media/i2c ; \
	mkdir -p $(LINUX_DIR)/$${dest}/csitpg; \
	cp -dpfr $(CSITPG_DIR)/* $(LINUX_DIR)/$${dest}/csitpg/ ; \
	echo "source \"$${dest}/csitpg/Kconfig\"" \
		>> $(LINUX_DIR)/$${dest}/Kconfig ; \
	echo 'obj-y += csitpg/' >> $(LINUX_DIR)/$${dest}/Makefile
endef
