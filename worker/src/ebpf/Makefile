# eBPF compilation Makefile
# Requires: clang >= 12, libbpf-dev, bpftool
#
# Step 1: make            → compile .bpf.o files
# Step 2: make skeleton   → generate .skel.h headers (needed by CMake build)
# Step 3: CMake with -DENABLE_EBPF=ON will pick up .skel.h automatically
#
# Output:
#   process_trace.bpf.o   → kernel-side BPF bytecode
#   tcp_flow.bpf.o        → kernel-side BPF bytecode
#   process_trace.skel.h  → libbpf skeleton header (user-space loader)
#   tcp_flow.skel.h       → libbpf skeleton header (user-space loader)

CLANG   ?= clang
CFLAGS  := -g -O2 -target bpf -D__TARGET_ARCH_x86
INCLUDES := -I/usr/include/x86_64-linux-gnu

BPF_OBJS := process_trace.bpf.o tcp_flow.bpf.o

all: $(BPF_OBJS)

%.bpf.o: %.bpf.c observe_events.h vmlinux.h
	@echo "  CLANG  $<"
	$(CLANG) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Generate skeleton headers (requires bpftool)
skeleton: $(BPF_OBJS)
	@echo "  SKEL   process_trace.skel.h"
	bpftool gen skeleton process_trace.bpf.o > process_trace.skel.h
	@echo "  SKEL   tcp_flow.skel.h"
	bpftool gen skeleton tcp_flow.bpf.o > tcp_flow.skel.h

check-deps:
	@command -v $(CLANG)  >/dev/null 2>&1 || { echo "ERROR: clang not found"; exit 1; }
	@command -v bpftool    >/dev/null 2>&1 || { echo "ERROR: bpftool not found"; exit 1; }
	@test -f vmlinux.h     || { echo "ERROR: vmlinux.h missing (run: bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h)"; exit 1; }
	@echo "All dependencies satisfied."

clean:
	rm -f $(BPF_OBJS) *.skel.h

.PHONY: all skeleton check-deps clean
