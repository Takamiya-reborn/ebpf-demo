LIBBPF_DIR := ../libbpf/src
CC := gcc
CLANG := clang
BPFTOOL := bpftool
ARCH := $(shell uname -m | sed 's/x86_64/x86/' | sed 's/aarch64/arm64/')

CATEGORIES = cpu disk file memo network
ALL_BPF_SRCS := $(wildcard $(addsuffix /*.bpf.c, $(CATEGORIES)))
APPS := $(patsubst %.bpf.c, %, $(notdir $(ALL_BPF_SRCS)))

$(foreach c,$(CATEGORIES),$(eval $c_APPS := $(patsubst %.bpf.c,%, $(notdir $(wildcard $c/*.bpf.c)))))

OUTPUT ?= .output
BIN_DIR ?= bin
# 使用简单赋值，避免重复执行 shell
_DUMMY := $(shell mkdir -p $(OUTPUT) $(BIN_DIR))

INCLUDES := -I$(LIBBPF_DIR) -I$(LIBBPF_DIR)/src -I$(LIBBPF_DIR)/include/uapi -I$(LIBBPF_DIR)/include -I./common -I$(OUTPUT)
CFLAGS += -g -O2 -Wall $(INCLUDES)
LDFLAGS += -lelf -lz -pthread

.DEFAULT_GOAL := all
# 建议保留 skel.h，方便开发时跳转查看定义
INTERMEDIATE_FILES = $(addprefix $(OUTPUT)/,$(APPS:=.bpf.o))
SKEL_FILES = $(addprefix $(OUTPUT)/,$(APPS:=.skel.h))
.SECONDARY: $(INTERMEDIATE_FILES) $(SKEL_FILES)

vpath %.bpf.c $(CATEGORIES)
vpath %.c $(CATEGORIES)

define category_rule
$1: $$($1_APPS)
endef
$(foreach c,$(CATEGORIES),$(eval $(call category_rule,$c)))

CLANG_BPF_SYS_INCLUDES ?= $(shell $(CLANG) -v -E - </dev/null 2>&1 \
	| sed -n '/<...> search starts here:/,/End of search list./{ s| \(/.*\)|-idirafter \1|p }')

DUPLICATE_NAMES := $(sort $(foreach app,$(APPS),$(if $(filter 2,$(words $(filter %/$(app).bpf.c,$(ALL_BPF_SRCS)))),$(app))))
ifneq ($(DUPLICATE_NAMES),)
$(error 工具重名: $(DUPLICATE_NAMES)，请修改)
endif

$(LIBBPF_DIR)/staticobjs/libbpf.a:
	$(MAKE) -C $(LIBBPF_DIR) BUILD_STATIC_ONLY=1

# 核心改进：利用依赖链实现并行化
all: $(LIBBPF_DIR)/staticobjs/libbpf.a $(APPS)

$(OUTPUT)/%.bpf.o: %.bpf.c common/vmlinux.h | $(OUTPUT)
	$(CLANG) -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH) $(INCLUDES) $(CLANG_BPF_SYS_INCLUDES) -c $< -o $@

$(OUTPUT)/%.bpf.linked.o: $(OUTPUT)/%.bpf.o
	$(BPFTOOL) gen object $@ $<

$(OUTPUT)/%.skel.h: $(OUTPUT)/%.bpf.linked.o
	$(BPFTOOL) gen skeleton $< > $@
	@# 如果你的 libbpf 安装在标准路径，下面这一行可能不需要，可以先注释掉观察
	sed -i 's|<bpf/libbpf.h>|<libbpf.h>|g' $@

$(OUTPUT)/%.o: %.c $(OUTPUT)/%.skel.h | $(OUTPUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(APPS): % : $(OUTPUT)/%.o $(LIBBPF_DIR)/staticobjs/libbpf.a
	$(CC) $(CFLAGS) $< $(LIBBPF_DIR)/staticobjs/libbpf.a $(LDFLAGS) -o $(BIN_DIR)/$@

clean:
	rm -rf $(OUTPUT) $(BIN_DIR)
	$(MAKE) -C $(LIBBPF_DIR) clean

.PHONY: all clean $(APPS) $(CATEGORIES)