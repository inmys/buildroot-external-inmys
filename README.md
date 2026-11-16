# INMYS NMS-SM-K1892VM21YA: Buildroot external tree

Конфигурация, DTS, пакеты и патчи для SMARC-модуля **NMS-SM-K1892VM21YA V1**
на ELVEES MCom-03R / К1892ВМ21Я. AArch64, glibc, Linux 6.18.

Из каталога этого репозитория; Buildroot будет склонирован в соседний `../buildroot/`:

```sh
git clone --branch 2026.02.3 --depth 1 https://gitlab.com/buildroot.org/buildroot.git ../buildroot
make -C ../buildroot O=../output BR2_EXTERNAL="$PWD" nms_sm_k1892vm21ya_defconfig
make -C ../output
```

Все четыре платы-носителя используют общие образы загрузчиков и rootfs.
Linux DTB выбирается переменной U-Boot `fdtfile`.

| Каталог | Содержимое |
| --- | --- |
| `configs/` | Buildroot defconfig |
| `board/nms-sm-k1892vm21ya/` | DTS, настройки загрузчиков, rootfs overlay и разметка образов |
| `package/` | Пакеты и их патчи |
| `patches/` | Патчи Linux и U-Boot |
| `support/` | Упаковка SD SBIMG и полного QSPI-образа |

В полном SDK подробная документация хранится рядом с репозиторием,
вне его Git-дерева: `sdk2/README.md` (сборка и обновления),
`sdk2/BUILDROOT_EXTERNAL.md` (устройство external tree),
`sdk2/HARDWARE.md` (аппаратные заметки).
