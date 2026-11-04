# Linaro 2018 ld aborts with a buffer overflow in the Russian locale.
export LC_ALL = C

include $(sort $(wildcard $(BR2_EXTERNAL_INMYS_PATH)/package/*/*.mk))
