#!/bin/sh
# Read-only report: does not start playback or change the mixer.
set -u
echo '--- Kernel / device ---'
uname -r
tr '\000' '\n' < /proc/device-tree/model
echo
echo '--- Cards / PCM ---'
cat /proc/asound/cards
aplay -l
echo '--- Audio modules ---'
lsmod | grep -E 'ymu818|bu26154' || true
echo '--- Autoload service ---'
systemctl --no-pager --full status brain-audio.service || true
echo '--- Relevant kernel messages ---'
dmesg | grep -Ei 'ymu818|bu26154|saif|unknown symbol|invalid module|timed out' || true
