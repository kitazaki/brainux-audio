#!/bin/sh
set -eu
test "$(id -u)" = 0
cd "$(dirname "$0")"
release=5.4.149-brain-audio-common-test3
test "$(cat KERNEL_RELEASE)" = "$release"
tr '\000' '\n' </proc/device-tree/compatible | grep -Eq '^sharp,pw-sh[3-7]$'
test -d /run/systemd/system
test -d /boot
sha256sum -c SHA256SUMS
# Enough room for all staged files while retaining the currently installed ones.
required_kb=$(du -sk boot | awk '{print $1 + 1024}')
available_kb=$(df -Pk /boot | awk 'END {print $4}')
test "$available_kb" -ge "$required_kb"
# Every trial uses a separate backup; old modules and backup directories remain.
backup=$(mktemp -d /home/user/backup-brain-audio-common-XXXXXXXX)
mkdir -p "$backup/boot" "$backup/config" "$backup/modules"
uname -r > "$backup/KERNEL_RELEASE"
cat /proc/device-tree/model > "$backup/device-model"
cp -p /boot/zImage "$backup/boot/"
for model in 1 2 3 4 5 6 7; do
    name=imx28-pwsh$model.dtb
    if [ -f "/boot/$name" ]; then cp -p "/boot/$name" "$backup/boot/"; fi
done
# Save complete configuration trees so the original setup can be recovered.
for path in /etc/modules /etc/modules-load.d /etc/systemd/system/brain-audio.service /usr/local/sbin/brain-audio-load; do
    if [ -e "$path" ]; then cp -a --parents "$path" "$backup/config/"; fi
done
for path in /etc/systemd/system/brain-audio.service /usr/local/sbin/brain-audio-load; do
    if [ ! -e "$path" ]; then printf '%s\n' "$path" >> "$backup/previously-absent"; fi
done
systemctl is-enabled brain-audio.service > "$backup/previous-service-state" 2>&1 || true
if [ -d "/lib/modules/$release" ]; then
    mkdir -p "$backup/modules"
    cp -a "/lib/modules/$release" "$backup/modules/"
fi
(cd "$backup"; find boot config modules -type f -exec sha256sum {} + > SHA256SUMS)
chmod -R a+rX "$backup"
printf 'Backup: %s\n' "$backup"
mkdir -p /lib/modules
if [ -d "modules/$release" ]; then
    mkdir -p "/lib/modules/$release"
    cp -a "modules/$release/." "/lib/modules/$release/"
else
    tar --no-same-owner -xzf "modules-$release.tar.gz" -C /lib/modules
fi
/sbin/depmod -a "$release"
for module in brain-bu26154; do
    /sbin/modinfo -k "$release" -F vermagic "$module" | grep -q "^$release "
done
# Replace legacy unconditional sound loading with the DT-based service.
for path in /etc/modules /etc/modules-load.d/*.conf; do
    test -f "$path" || continue
    sed -i --follow-symlinks '/^[[:space:]]*brain[-_]bu26154\([[:space:]#].*\)\{0,1\}$/d; /^[[:space:]]*snd[-_]soc[-_]ymu818b\([[:space:]#].*\)\{0,1\}$/d; /^[[:space:]]*snd[-_]soc[-_]mxs[-_]ymu818b\([[:space:]#].*\)\{0,1\}$/d' "$path"
done
install -D -m 755 brain-audio-load /usr/local/sbin/brain-audio-load
install -D -m 644 brain-audio.service /etc/systemd/system/brain-audio.service
systemctl daemon-reload
systemctl enable brain-audio.service
restore_ro=no
if findmnt -n -o OPTIONS /boot | grep -Eq '(^|,)ro(,|$)'; then
    mount -o remount,rw /boot
    restore_ro=yes
fi
trap 'sync; if [ "$restore_ro" = yes ]; then mount -o remount,ro /boot; fi' EXIT
# Stage and compare every boot file before replacing any of them.
for name in zImage imx28-pwsh1.dtb imx28-pwsh2.dtb imx28-pwsh3.dtb imx28-pwsh4.dtb imx28-pwsh5.dtb imx28-pwsh6.dtb imx28-pwsh7.dtb; do
    cp "boot/$name" "/boot/$name.common-new"
    cmp "boot/$name" "/boot/$name.common-new"
done
for name in zImage imx28-pwsh1.dtb imx28-pwsh2.dtb imx28-pwsh3.dtb imx28-pwsh4.dtb imx28-pwsh5.dtb imx28-pwsh6.dtb imx28-pwsh7.dtb; do
    mv "/boot/$name.common-new" "/boot/$name"
done
sync
printf 'Installed %s. Backup: %s\n' "$release" "$backup"
echo 'No reboot or playback was started. Reboot manually, then run sudo sh check-audio.sh.'
