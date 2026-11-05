# SciFair-OS build file.
#
#   make            build build/scifair-os.elf
#   make run        run it in QEMU (serial output appears in this terminal)
#   make run-kvm    same, but using your real CPU directly (much faster, needs KVM)
#   make log        run it and save serial output (experiment data) to results.txt
#   make iso        build a bootable CD/USB image (needs grub-mkrescue + xorriso)
#   make test       check the pi algorithm against Python on this computer
#   make clean      delete build files

CC      := gcc
LD      := ld
QEMU    := qemu-system-i386

CFLAGS  := -m32 -march=i686 -std=gnu11 -O2 -g -Wall -Wextra \
           -ffreestanding -fno-builtin -fno-pie -fno-pic -fno-stack-protector \
           -fno-asynchronous-unwind-tables -fno-tree-loop-distribute-patterns \
           -mno-mmx -mno-sse -mno-sse2 -mno-80387 -nostdlib
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib -z noexecstack -z max-page-size=0x1000

C_SRC   := $(wildcard src/*.c)
S_SRC   := $(wildcard src/*.s)
OBJ     := $(patsubst src/%.s,build/%.o,$(S_SRC)) $(patsubst src/%.c,build/%.o,$(C_SRC))
KERNEL  := build/scifair-os.elf
ISO     := build/scifair-os.iso

QEMU_FLAGS := -kernel $(KERNEL) -m 64 -vga std

.PHONY: all run run-kvm log iso run-iso test clean

all: $(KERNEL)

build:
	mkdir -p build

build/%.o: src/%.c src/kernel.h src/pi_core.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: src/%.s | build
	$(CC) -m32 -c $< -o $@

$(KERNEL): $(OBJ) linker.ld
	$(LD) $(LDFLAGS) $(OBJ) -o $@
	@echo "Built $@"

run: $(KERNEL)
	$(QEMU) $(QEMU_FLAGS) -serial stdio

run-kvm: $(KERNEL)
	$(QEMU) $(QEMU_FLAGS) -accel kvm -cpu host -serial stdio

log: $(KERNEL)
	$(QEMU) $(QEMU_FLAGS) -serial file:results.txt
	@echo "Experiment data saved to results.txt"

iso: $(KERNEL)
	mkdir -p build/iso/boot/grub
	cp $(KERNEL) build/iso/boot/scifair-os.elf
	printf 'set timeout=0\nset default=0\nmenuentry "SciFair-OS" {\n  multiboot /boot/scifair-os.elf\n  boot\n}\n' > build/iso/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) build/iso
	@echo "Built $(ISO)"

run-iso: iso
	$(QEMU) -cdrom $(ISO) -m 64 -serial stdio

build/test_pi: tests/test_pi.c src/pi_core.c src/pi_core.h | build
	cc -O2 -Wall -Wextra tests/test_pi.c src/pi_core.c -o $@

test: build/test_pi
	@for n in 1 10 100 762 768 1000 5000 10000; do \
	  if [ "$$(./build/test_pi $$n)" = "$$(python3 tests/ref_pi.py $$n)" ]; then \
	    echo "  $$n digits: correct"; \
	  else echo "  $$n digits: WRONG"; exit 1; fi; \
	done

clean:
	rm -rf build
