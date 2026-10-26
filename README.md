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
