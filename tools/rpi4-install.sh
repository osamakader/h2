#!/bin/sh
# rpi4-install.sh BOOT: put h2 on a Raspberry Pi 4 SD card made for o2
# (o2's tools/rpi4-card.sh), whose first (FAT) partition is mounted at
# BOOT. Copies build/arm64-rpi4/h2.bin there as h2.img and points
# config.txt at it, keeping the original as config.txt.o2. o2's own
# kernel8.img stays: h2 carries its own copy of the guest.
# rpi4-install.sh --direct BOOT: back to booting o2 directly.
# Build first: make ARCH=arm64 PLATFORM=rpi4 GUEST=../o2/build/arm64-rpi4/kernel.elf
set -e

usage() {
	echo "usage: $0 [--direct] BOOT (the card's mounted boot partition)" >&2
	exit 1
}

direct=0
if [ "$1" = "--direct" ]; then
	direct=1
	shift
fi
[ $# -eq 1 ] || usage
boot=$1
if [ ! -f "$boot/config.txt" ] || [ ! -f "$boot/start4.elf" ]; then
	echo "$boot doesn't look like a Pi 4 boot partition (no config.txt, start4.elf)" >&2
	exit 1
fi

if [ $direct = 1 ]; then
	if [ ! -f "$boot/config.txt.o2" ]; then
		echo "no $boot/config.txt.o2 to go back to" >&2
		exit 1
	fi
	cp "$boot/config.txt.o2" "$boot/config.txt"
	rm -f "$boot/h2.img"
	sync
	echo "$boot: boots o2 directly again"
	exit 0
fi

h2=$(dirname "$0")/../build/arm64-rpi4
if [ ! -f "$h2/h2.bin" ]; then
	echo "no $h2/h2.bin: make ARCH=arm64 PLATFORM=rpi4 GUEST=..." >&2
	exit 1
fi
if [ ! -s "$h2/guest" ] || [ "$(cat "$h2/guest")" = "" ]; then
	echo "$h2/h2.bin has only the built-in guest: build it with GUEST=" >&2
	exit 1
fi

# where h2 wants to be: its Image header's text_offset (RAM starts at 0)
addr=$(od -An -t x8 -j 8 -N 8 "$h2/h2.bin" | tr -d ' ')
addr=0x$(echo "$addr" | sed 's/^0*//')

[ -f "$boot/config.txt.o2" ] || cp "$boot/config.txt" "$boot/config.txt.o2"
cp "$h2/h2.bin" "$boot/h2.img"
sed -e '/^kernel=/d' -e '/^kernel_address=/d' "$boot/config.txt.o2" > "$boot/config.txt"
printf '# h2, with %s in it (tools/rpi4-install.sh)\nkernel=h2.img\nkernel_address=%s\n' \
	"$(basename "$(cat "$h2/guest")")" "$addr" >> "$boot/config.txt"
sync
echo "$boot: boots h2 ($(cat "$h2/guest")) at $addr"
