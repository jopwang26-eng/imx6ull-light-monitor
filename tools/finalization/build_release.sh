#!/bin/sh
# Ubuntu：从实际工程重建；不部署、不改已有运行文件或启动环境。
set -eu
project=${LM_PROJECT_DIR:-/home/jopwang/linux/IMX6ULL/Drivers/Linux_Drivers/light_monitor}
kernel=${LM_KERNEL_DIR:-/home/jopwang/linux/IMX6ULL/linux}
cross=${LM_CROSS_COMPILE:-/usr/local/arm/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-}
output=$(mktemp -d /tmp/light-monitor-release.XXXXXX)
dtb=imx6ull-alientek-emmc-lightbeep-test
echo "RELEASE_DIR=$output"
mkdir -p "$output/bin" "$output/source/app/tests" "$output/source/tools" "$output/source/device-tree" "$output/validation"
# 随包保留板端证据与限制；重建时从工程validation目录原样携带。
cp "$project/validation/"*.md "$output/validation/"
for evidence in "$project/validation/"*.txt; do
    [ ! -f "$evidence" ] || cp "$evidence" "$output/validation/"
done
# 额外携带已经板测的独立NACK工具，普通安装器不会把测试模块当正式驱动安装。
mkdir -p "$output/source/tools/i2c-nack-test" "$output/test-only/i2c-nack-test"
cp "$project/tools/i2c-nack-test/"*.c "$project/tools/i2c-nack-test/"*.sh "$project/tools/i2c-nack-test/Makefile" "$project/tools/i2c-nack-test/README.md" "$output/source/tools/i2c-nack-test/"
cp "$project/tools/i2c-nack-test/lightap_nack_test.ko" "$project/tools/i2c-nack-test/check_nack" "$project/tools/i2c-nack-test/run_board.sh" "$output/test-only/i2c-nack-test/"
make -C "$kernel" M="$project" ARCH=arm CROSS_COMPILE="$cross" modules
make -B -C "$project/app" CROSS_COMPILE="$cross" LDFLAGS=-static
make -C "$kernel" ARCH=arm CROSS_COMPILE="$cross" "$dtb.dtb"
for tool in beep_test beep_api_test lightap_test; do
    "${cross}gcc" -std=gnu99 -Wall -Wextra -Werror -O2 -static "$project/$tool.c" -o "$output/bin/$tool"
done
"${cross}gcc" -std=gnu99 -Wall -Wextra -Werror -O2 -static "$project/tools/nfs_observer.c" -o "$output/bin/nfs_observer"
for name in lightkey lightap3216c lightbeep; do
    cp "$project/$name.ko" "$output/bin/"
    cp "$project/$name.c" "$output/source/"
done
cp "$project/Makefile" "$output/source/"
cp "$project/app/light_monitor" "$output/bin/"
cp "$project/"load_light*.sh "$project/restore_ap3216c.sh" "$output/bin/"
cp "$project/app/start_light_monitor.sh" "$output/bin/"
cp "$project/tools/"*.sh "$output/source/tools/"
cp "$project/tools/nfs_observer.c" "$output/source/tools/"
cp "$project/tools/"*.py "$output/source/tools/"
cp "$project/tools/"collect_board_env.sh "$project/tools/"soak_board.sh "$project/tools/"test_buzzer_unavailable.sh "$output/bin/"
cp "$project/tools/prepare_nfs_board.sh" "$output/bin/"
cp "$project/tools/install_release.sh" "$output/"
cp "$project/tools/RELEASE_README.md" "$output/README.md"
cp "$project/app/"*.c "$project/app/"*.h "$project/app/Makefile" "$project/app/README.md" "$project/app/"*.sh "$output/source/app/"
cp "$project/app/tests/"*.py "$project/app/tests/"*.c "$output/source/app/tests/"
for name in beep_test beep_api_test lightap_test; do cp "$project/$name.c" "$output/source/"; done
for name in imx6ull-alientek-emmc imx6ull-alientek-emmc-lightkey-test imx6ull-alientek-emmc-lightbeep-test; do
    cp "$kernel/arch/arm/boot/dts/$name.dts" "$output/source/device-tree/"
done
cp "$kernel/arch/arm/boot/dts/$dtb.dtb" "$output/bin/"
if [ "${1:-}" = --test ]; then
    make -C "$project/app" native
    gcc -Wall -Wextra -Werror -shared -fPIC "$project/app/tests/fsync_gate.c" -ldl -o "$project/app/fsync_gate.so"
    gcc -Wall -Wextra -Werror -shared -fPIC "$project/app/tests/control_faults.c" -ldl -o "$project/app/control_faults.so"
    (
        cd "$project/app"
        python3 tests/test_process.py ./light_monitor_native
        python3 tests/test_logging.py ./light_monitor_native
        python3 tests/test_control.py ./light_monitor_native
        gcc -std=gnu99 -Wall -Wextra -Werror -O2 "$project/tools/nfs_observer.c" -o "$output/nfs_observer_native"
        python3 "$project/tools/test_observer.py" "$output/nfs_observer_native" ./light_monitor_native
    ) > "$output/native-tests.txt" 2>&1
    cat "$output/native-tests.txt"
fi
{
    uname -a
    "${cross}gcc" --version | head -n 1
    cat "$kernel/include/config/kernel.release"
    for name in lightkey lightap3216c lightbeep; do /sbin/modinfo "$project/$name.ko"; done
    sha256sum "$kernel/.config" "$kernel/Module.symvers" "$kernel/arch/arm/boot/dts/imx6ull.dtsi"
} > "$output/build-environment.txt"
(
    cd "$output"
    find bin source validation test-only -type f -print | LC_ALL=C sort | xargs sha256sum > MANIFEST.sha256
    sha256sum README.md install_release.sh build-environment.txt >> MANIFEST.sha256
    if [ -f native-tests.txt ]; then sha256sum native-tests.txt nfs_observer_native >> MANIFEST.sha256; fi
    sha256sum -c MANIFEST.sha256 > manifest-check.txt
)
tar -czf "$output.tar.gz" -C "$output" .
sha256sum "$output.tar.gz"
echo "RELEASE_DIR=$output"
echo "RELEASE_ARCHIVE=$output.tar.gz"
