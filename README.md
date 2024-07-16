# buildroot-external-inmys
Конфигурационные файлы для сборки проектов на платформе rv1103 

## Описание файлов
* kernel
    * nms-sdb-mb_v2.dts - DTS для изделия NMS-SDB-MB
    * nms-sdb-mb_v2-fragment.config - фрагмент ядра для изделия NMS-SDB-MB
    * drivers - папка с дополнительными / измененными драйверами для ядра
* buildroot
    * nms-sdb-mb_v2_defconfig - конфигурационный файл для buildroot
* uboot
    * nms-sdb-mb_v2-ram.config - конфинг uboot для запуска из ram (первоначальная прошивка)
    * nms-sdb-mb_v2-emmc.config - конфинг uboot для запуска с emmc
* BoardConfig_IPC
    * BoardConfig-EMMC-Buildroot-RV1103_Luckfox_Pico_Plus-NMS_SDB.mk - основная конфигурация NMS-SDB-MB
    * BoardConfig-RAM-Buildroot-RV1103_Luckfox_Pico_Plus-NMS_SDB.mk - конфигурация для подготовки образа для ram.

