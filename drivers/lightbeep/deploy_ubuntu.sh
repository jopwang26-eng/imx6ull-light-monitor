#!/bin/sh
# Ubuntu 执行：只部署本次产物；需要 sudo，口令仅在终端交互输入。
set -eu
src=/home/jopwang/linux/IMX6ULL/Drivers/Linux_Drivers/light_monitor
kernel=/home/jopwang/linux/IMX6ULL/linux
runtime=/home/jopwang/linux/nfs/rootfs/opt/light_monitor
dtb=imx6ull-alientek-emmc-lightbeep-test.dtb
backup="$runtime/backup_beep_$(date +%Y%m%d_%H%M%S)"
sudo mkdir -p "$backup"
for name in lightbeep.ko beep_test beep_api_test load_lightbeep.sh; do
    if [ -e "$runtime/$name" ]; then
        sudo cp -a "$runtime/$name" "$backup/"
    fi
done
if [ -e "/var/lib/tftpboot/$dtb" ]; then
    sudo cp -a "/var/lib/tftpboot/$dtb" "$backup/"
fi
sudo install -m 644 "$src/lightbeep.ko" "$runtime/"
sudo install -m 755 "$src/beep_test" "$src/beep_api_test" "$src/load_lightbeep.sh" "$runtime/"
sudo install -m 644 "$kernel/arch/arm/boot/dts/$dtb" /var/lib/tftpboot/
sha256sum "$src/lightbeep.ko" "$runtime/lightbeep.ko"
sha256sum "$kernel/arch/arm/boot/dts/$dtb" "/var/lib/tftpboot/$dtb"
echo "DEPLOYED: beep-v1; backup=$backup"
