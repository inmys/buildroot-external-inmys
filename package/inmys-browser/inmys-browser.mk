INMYS_BROWSER_VERSION = 1
INMYS_BROWSER_SOURCE =
INMYS_BROWSER_LICENSE = MIT
INMYS_BROWSER_DEPENDENCIES = elvees-powervr-graphics elvees-vpu-modules \
	weston cog glib-networking shared-mime-info gst1-plugins-good gst1-plugins-bad

# Match the current image and keep /data/browser ownership stable on updates.
define INMYS_BROWSER_USERS
	browser 101 browser 105 * /var/lib/browser /bin/sh video,audio Browser
endef

define INMYS_BROWSER_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(INMYS_BROWSER_PKGDIR)/inmys-browser $(TARGET_DIR)/usr/sbin/inmys-browser
	$(INSTALL) -D -m 0755 $(INMYS_BROWSER_PKGDIR)/inmys-cog $(TARGET_DIR)/usr/libexec/inmys-cog
	$(INSTALL) -D -m 0644 $(INMYS_BROWSER_PKGDIR)/weston.ini $(TARGET_DIR)/etc/inmys-browser/weston.ini
	$(INSTALL) -D -m 0644 $(INMYS_BROWSER_PKGDIR)/rutube-low-effects.css $(TARGET_DIR)/etc/inmys-browser/rutube-low-effects.css
	$(INSTALL) -D -m 0644 $(INMYS_BROWSER_PKGDIR)/70-inmys-browser.rules $(TARGET_DIR)/etc/udev/rules.d/70-inmys-browser.rules
	$(INSTALL) -D -m 0644 $(INMYS_BROWSER_PKGDIR)/test.html $(TARGET_DIR)/usr/share/inmys/browser-test.html
endef

$(eval $(generic-package))
