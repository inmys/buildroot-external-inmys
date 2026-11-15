ELVEES_YOLOV8N_VERSION = 20260128
ELVEES_YOLOV8N_SOURCE = download?files=elcorenn-examples-data-$(ELVEES_YOLOV8N_VERSION).tar
ELVEES_YOLOV8N_SITE = https://box.elvees.com/index.php/s/elcorenn_examples_data
ELVEES_YOLOV8N_LICENSE = AGPL-3.0 (Ultralytics YOLOv8 model), vendor example data
# Only the requested model and smoke-test image are extracted (not all 311 MiB).
define ELVEES_YOLOV8N_EXTRACT_CMDS
	tar -xf '$(ELVEES_YOLOV8N_DL_DIR)/$(ELVEES_YOLOV8N_SOURCE)' \
		--strip-components=1 -C $(@D) \
		elcorenn-examples-data/models/yolov8n elcorenn-examples-data/media/bicycle.jpg
endef

define ELVEES_YOLOV8N_INSTALL_TARGET_CMDS
	mkdir -p $(TARGET_DIR)/usr/share/inmys/yolov8n
	$(INSTALL) -m 0644 $(@D)/models/yolov8n/model.json $(@D)/models/yolov8n/model.bin \
		$(@D)/media/bicycle.jpg $(TARGET_DIR)/usr/share/inmys/yolov8n/
endef

$(eval $(generic-package))
