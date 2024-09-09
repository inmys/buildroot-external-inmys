# buildroot-external-inmys
Buildroot customizations for Inmys devices

Kernel: 4.19


DTS changelog:
* Added camera hog
* ```mmc0 = &sdhci; mmc1 = &sdmmc0;```
* Removed adt7473-1 support
