# ========== 独立版Makefile，不需要依赖顶层scripts，适配CentOS 10 ==========
LIBBPF_DIR := ../libbpf/src

# 自己定义编译工具链，不用引入外部规则
CC := gcc
CLANG := clang
BPFTOOL := bpftool
ARCH := $(shell uname -m | sed 's/x86_64/x86/' | sed 's/aarch64/arm64/')

# 分类配置，和之前一致
CATEGORIES = cpu disk file memo network
ALL_BPF_SRCS := $(wildcard $(addsuffix /*.bpf.c, $(CATEGORIES)))
APPS := $(patsubst %.bpf.c, %, $(notdir $(ALL_BPF_SRCS)))

# 输出目录
OUTPUT ?= .output
BIN_DIR ?= bin/
$(shell mkdir -p $(OUTPUT) $(BIN_DIR))

# 编译参数
INCLUDES := -I$(LIBBPF_DIR) -I$(LIBBPF_DIR)/include/uapi -I$(LIBBPF_DIR)/include -I./common -I$(OUTPUT)
CFLAGS += -g -Wall $(INCLUDES)
LDFLAGS += -L$(LIBBPF_DIR) -lbpf -lelf -lz -pthread

vpath %.bpf.c $(CATEGORIES)

# Get Clang's default includes on this system. We'll explicitly add these dirs
# to the includes list when compiling with `-target bpf` because otherwise some
# architecture-specific dirs will be "missing" on some architectures/distros -
# headers such as asm/types.h, asm/byteorder.h, asm/socket.h, asm/sockios.h,
# sys/cdefs.h etc. might be missing.
#
# Use '-idirafter': Don't interfere with include mechanics except where the
# build would have failed anyways.
CLANG_BPF_SYS_INCLUDES ?= $(shell $(CLANG) -v -E - </dev/null 2>&1 \
	| sed -n '/<...> search starts here:/,/End of search list./{ s| \(/.*\)|-idirafter \1|p }')
DUPLICATE_NAMES := $(sort $(foreach app,$(APPS),$(if $(filter 2,$(words $(filter %/$(app).bpf.c,$(ALL_BPF_SRCS)))),$(app))))
ifneq ($(DUPLICATE_NAMES),)
$(error 工具重名: $(DUPLICATE_NAMES)，请修改不同分类下的工具名避免冲突)
endif

# 先编译libbpf，避免找不到libbpf.a
$(LIBBPF_DIR)/staticobjs/libbpf.a:
	make -C $(LIBBPF_DIR) BUILD_STATIC_ONLY=1

all: $(LIBBPF_DIR)/staticobjs/libbpf.a $(APPS)

$(CATEGORIES): % : $(patsubst %.bpf.c, %, $(notdir $(wildcard $*/*.bpf.c)))
	@echo "✅ 分类 $@ 所有工具编译完成"

# ------------------- 编译规则 -------------------
$(OUTPUT)/irq_stat.bpf.o: cpu/irq_stat.bpf.c common/vmlinux.h | $(OUTPUT)
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH) $(INCLUDES) $(CLANG_BPF_SYS_INCLUDES) -c $< -o $@

$(OUTPUT)/irq_stat.bpf.linked.o: $(OUTPUT)/irq_stat.bpf.o
	$(BPFTOOL) gen object $@ $<

$(OUTPUT)/irq_stat.skel.h: $(OUTPUT)/irq_stat.bpf.linked.o
	$(BPFTOOL) gen skeleton $< > $@
	sed -i 's|<bpf/libbpf.h>|<libbpf.h>|g' $@

$(OUTPUT)/irq_stat.o: cpu/irq_stat.c $(OUTPUT)/irq_stat.skel.h | $(OUTPUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(APPS): % : $(OUTPUT)/%.o $(LIBBPF_DIR)/libbpf.a
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o cpu/$@
	@echo "✅ 工具 $@ 编译完成"

clean:
	rm -rf $(OUTPUT) $(BIN_DIR)/*
	make -C $(LIBBPF_DIR) clean

.PHONY: all clean $(APPS) $(CATEGORIES)