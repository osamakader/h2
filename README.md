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

Arm boots this image directly at EL2 on `virt`:

```sh
qemu-system-aarch64 \
  -machine virt,virtualization=on -cpu cortex-a57 -nographic \
  -kernel build/arm64/h2.elf
```

Expected output:

```text
h2: hypervisor online
guest: hello from EL1 via HVC
```

RISC-V requires a hypervisor-capable CPU model. This skeleton expects to be started in supervisor/HS context by firmware or a test harness; `-bios none` platform boot policy varies by QEMU version and machine configuration.

```sh
qemu-system-riscv64 \
  -machine virt -cpu rv64,h=true -nographic \
  -kernel build/riscv64/h2.elf
```

Expected output, when the image is entered in HS-mode with the H extension available:

```text
h2: hypervisor online
guest: hello from VS via ECALL
```
