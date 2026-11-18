# INMYS NMS-SM-K1892VM21YA: Buildroot external tree

Конфигурация, DTS, пакеты и патчи для SMARC-модуля **NMS-SM-K1892VM21YA**
на ELVEES MCom-03R / К1892ВМ21Я. AArch64, glibc, Linux 6.18.

Выполните в каталоге, где будут созданы `buildroot/`, `buildroot-external-inmys/`
и `output/`:

```sh
git clone --branch 2026.02.3 --depth 1 https://gitlab.com/buildroot.org/buildroot.git buildroot
git clone https://github.com/inmys/buildroot-external-inmys.git -b nms-sm-k1892vm21ya
make -C buildroot O="$PWD/output" BR2_EXTERNAL="$PWD/buildroot-external-inmys" nms_sm_k1892vm21ya_defconfig
make -C output
```

Доступны два профиля:

| Defconfig | Состав | Размер rootfs |
| --- | --- | --- |
| `nms_sm_k1892vm21ya_defconfig` | `inmys-vision`, без Weston/Cog | 256 МиБ |
| `nms_sm_k1892vm21ya_browser_defconfig` | `inmys-vision` + Weston/Cog, WebRTC и CSI bridge | 512 МиБ |

Для браузерного профиля:

```sh
make -C buildroot O="$PWD/output-browser" BR2_EXTERNAL="$PWD/buildroot-external-inmys" nms_sm_k1892vm21ya_browser_defconfig
make -C output-browser
```

Используйте отдельный output для каждого профиля: Buildroot сохраняет файлы
отключённых пакетов при смене конфигурации в уже собранном дереве.
Разделы A/B в обоих вариантах по 512 МиБ; между профилями можно переходить
через `firmware.swu` без изменения разметки. CA Минцифры входят в оба профиля.
Базовый профиль не содержит Intel Wi-Fi и Realtek firmware; браузерный содержит.

Все четыре платы-носителя используют общие образы загрузчиков и rootfs.
Linux DTB выбирается переменной U-Boot `fdtfile`.

Результаты в `output/images/`:

* `sdcard.img` — образ загрузочной SD с rootfs.
* `qspi-full-nms-sm-k1892vm21ya.bin` — полный образ QSPI, 16 МиБ.
* `emmc.img` — образ разделов eMMC; загрузчик остаётся на SD или в QSPI.
* `firmware.swu` — A/B-обновление.

`firmware.swu` можно загрузить через веб-интерфейс обновления:
`http://<IP-платы>:8080/`.

Команды из каталога `output/`:

| Команда | Действие |
| --- | --- |
| `make` | Полная сборка образов |
| `make linux-rebuild && make` | Пересборка Linux и образов |
| `make uboot-rebuild && make` | Пересборка U-Boot и образов загрузчиков |
| `make` после правки Linux DTS/DTSI в external tree | Пересборка DTB и образов автоматически |

В браузерном профиле браузер запускается вручную: `inmys-browser https://example.org`
(без URL — локальная проверка WebGL). Команда останавливает `inmys-vision`,
запускает Weston + Cog; Ctrl+C или `inmys-browser stop` завершает сеанс
и возвращает ранее работавший `inmys-vision`. Автостарта браузера нет.

| Каталог | Содержимое |
| --- | --- |
| `configs/` | Buildroot defconfig |
| `board/nms-sm-k1892vm21ya/` | DTS, настройки загрузчиков, rootfs overlay и разметка образов |
| `package/` | Пакеты и их патчи |
| `patches/` | Патчи Linux, U-Boot и штатных пакетов |
| `support/` | Упаковка SD SBIMG и полного QSPI-образа |
