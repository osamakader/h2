# h2
Minimal phase-1 hypervisor for `arm64` and `riscv64`.

## Build

```sh
make ARCH=arm64
make ARCH=riscv64
```

Override toolchains if needed:

```sh
make ARCH=arm64 CROSS_COMPILE=aarch64-none-elf-
make ARCH=riscv64 CROSS_COMPILE=riscv64-none-elf-
```

## Run on QEMU virt

```sh
make ARCH=arm64 run          # h2 and its built-in test guest
make ARCH=riscv64 run
```

Expected output (arm64; riscv64 says "VS via ECALL"):

```text
h2: hypervisor online
h2: device tree at 0x0000000040000000, h2 reserved 0x0000000040100000-...
guest: hello from EL1 via HVC
```

`CPUS` (default 3), `MEM` (128M) and `QEMU_ARGS` adjust the machine. On
arm64 qemu starts h2 in EL2 (`virtualization=on`); on riscv64 OpenSBI
starts it in HS-mode (`-cpu rv64,h=true`).

## Guests

`make GUEST=path/to/kernel.elf` embeds an ELF kernel in h2 instead of the
built-in guest. h2 loads its segments at their physical addresses and
enters it in EL1 / VS-mode with the MMU off, as firmware would: on arm64
with the DTB in x0, on riscv64 with the hartid in a0 and the DTB in a1.
h2 reserves its own memory in the DTB (`/memreserve/`) and keeps the
guest out of it with an identity stage 2 translation, forwards PSCI / SBI
calls to the firmware, and starts the CPUs the guest starts. Devices are
passed through.

h2 links at 0x40100000 on arm64 and 0x84000000 on riscv64; the guest must
fit around it.

[o2](https://github.com/osamakader/o2) boots this way. With o2 built
next to h2 (`make -C ../o2 ARCH=arm64`):

```sh
make ARCH=arm64 run-o2       # O2=path/to/o2 if it is elsewhere
```

and the same with `ARCH=riscv64`. On riscv64 the guest gets its timer,
IPIs and external interrupts from h2; its PLIC claims and completions trap
to h2.

## Raspberry Pi 4

`PLATFORM=rpi4` builds h2 for the Pi 4 (arm64) into `build/arm64-rpi4`:
`h2.bin` is a raw image with an arm64 Image header, linked at 0x1000000,
so the firmware loads it as it loads Linux and starts it in EL2 with the
DTB in x0. The console is the PL011 (uart0), as the firmware set it up.
The firmware keeps CPUs 1-3 in its spin table; h2 takes them at boot and
has them wait on the same spin table for the guest, which starts them as
its DTB says, through h2. The DTB is passed on as the firmware made it.

On qemu's raspi4b (qemu 9.0 or later):

```sh
make ARCH=arm64 PLATFORM=rpi4 run        # the built-in guest; fetches the DTB
make -C ../o2 ARCH=arm64 PLATFORM=rpi4   # o2 for the Pi: kernel.elf, sd.img
make ARCH=arm64 PLATFORM=rpi4 run-o2
```

On a real Pi 4, starting from o2's SD card (o2's README: `tools/rpi4-card.sh`):

1. `make ARCH=arm64 PLATFORM=rpi4 GUEST=../o2/build/arm64-rpi4/kernel.elf`
2. Mount the card's first (FAT) partition and run
   `tools/rpi4-install.sh /path/to/boot`: it copies `h2.bin` there as
   `h2.img` and sets `kernel=h2.img` and `kernel_address` (h2's load
   address, 0x1000000) in `config.txt`, keeping o2's as `config.txt.o2`.
   o2 is inside `h2.img`; h2 puts it at 0x200000, where o2's own
   `kernel8.img` would have gone. `tools/rpi4-install.sh --direct
   /path/to/boot` goes back to booting o2 directly.
3. The serial console is the same as o2's (GPIO 14/15, 115200 8N1). h2
   prints its lines first, then o2 its boot messages and the shell.

It boots o2 to its shell on a Raspberry Pi 4 Model B (and on qemu's
raspi4b, where usertests -q passes).

