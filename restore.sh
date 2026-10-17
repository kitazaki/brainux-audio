#!/bin/sh
# Restore boot files and loading configuration. Never removes kernel modules.
set -eu
test "$(id -u)" = 0
backup=${1:?backup directory required}
case "$backup" in /home/user/backup-brain-audio-common-*) ;; *) exit 1 ;; esac
test -f "$backup/boot/zImage"
(cd "$backup"; sha256sum -c SHA256SUMS)
restore_ro=no
if findmnt -n -o OPTIONS /boot | grep -Eq '(^|,)ro(,|$)'; then
    mount -o remount,rw /boot
    restore_ro=yes
fi
trap 'sync; if [ "$restore_ro" = yes ]; then mount -o remount,ro /boot; fi' EXIT
for source_file in "$backup"/boot/*; do
    name=$(basename "$source_file")
    cp "$source_file" "/boot/$name.restore-new"
    cmp "$source_file" "/boot/$name.restore-new"
done
for source_file in "$backup"/boot/*; do
    name=$(basename "$source_file")
    mv "/boot/$name.restore-new" "/boot/$name"
done
systemctl disable brain-audio.service || true
if [ -f "$backup/previously-absent" ]; then
    while IFS= read -r path; do
        case "$path" in
            /etc/systemd/system/brain-audio.service|/usr/local/sbin/brain-audio-load) rm -f "$path" ;;
            *) echo 'Unexpected restoration path.' >&2; exit 1 ;;
        esac
    done < "$backup/previously-absent"
fi
cp -a "$backup/config/." /
systemctl daemon-reload
if grep -qx enabled "$backup/previous-service-state"; then
    systemctl enable brain-audio.service
fi
sync
echo 'Original boot files and loading configuration restored. Reboot manually.'
