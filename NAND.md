# Первоначальная запись NAND

Загрузить Linux с SD: вставить карту, зажать SW3, подать питание.
На модуле от root:

```sh
mkdir -p /mnt/sdboot
mount -t vfat /dev/mmcblk0p1 /mnt/sdboot
sh /mnt/sdboot/install-nand.sh --check /mnt/sdboot
sh /mnt/sdboot/install-nand.sh --install /mnt/sdboot
```

`--check` только проверяет. `--install` **стирает прежнюю систему и настройки
NAND**, устанавливает UBI rootfsA/rootfsB/configs, U-Boot и доступные копии SPL,
проверяет чтение и очищает старое окружение U-Boot. Плохие блоки учитываются.

После `DONE` выполнить `poweroff`, отключить питание, вынуть SD и включить
плату без SW3. Загрузится NAND, слот A. Этот цикл проверен на модуле.

## Установка по Ethernet или с USB-флешки

Если SD уже записана старым образом, повторять `dd` необязательно.
Передать `output/images/nand-install.tar` в `/tmp` модуля, затем:

```sh
mkdir -p /tmp/nand-install
tar -xf /tmp/nand-install.tar -C /tmp/nand-install
sh /tmp/nand-install/install-nand.sh --check /tmp/nand-install
sh /tmp/nand-install/install-nand.sh --install /tmp/nand-install
```

Тот же комплект можно распаковать на USB-флешку и передать установщику её
каталог. Скрипт также установлен в Linux как `/usr/sbin/nms-install-nand`.

Для дальнейших обновлений работающей NAND-системы использовать `update.swu`.
USB ROM/RNDIS-загрузка пустого модуля в текущем SDK не настроена.
