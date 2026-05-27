# ── M1Switch E2E 测试 NRO 顶层构建 ──────────────────────────
# 遍历所有子目录，在每个子目录中构建 NRO。
# 最终产物输出到 tests/e2e/build/
#───────────────────────────────────────────────────────────────

SUBDIRS := $(filter-out common,$(shell ls -d */))

OUTDIR := $(CURDIR)/build

all: $(OUTDIR) $(SUBDIRS)

.PHONY: $(SUBDIRS) clean all

$(OUTDIR):
	@mkdir -p $(OUTDIR)

$(SUBDIRS):
	@$(MAKE) -C $@ all --no-print-directory
	@cp $@/build/$(@D).nro $(OUTDIR)/ 2>/dev/null || true
	@echo "  -> $(OUTDIR)/$(@D).nro"

clean:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean --no-print-directory; \
	done
	@rm -rf $(OUTDIR)
	@echo "  clean: all tests"

list:
	@echo "Available tests:"
	@for dir in $(SUBDIRS); do \
		echo "  $$dir"; \
	done

# 显示测试命名规范
help:
	@echo "M1Switch E2E Test Suite"
	@echo ""
	@echo "Naming:"
	@echo "  l0_<subsystem>    — 裸 syscall 测试，不依赖任何服务"
	@echo "  l1_<subsystem>    — 单一服务测试，只依赖一个 IPC 服务"
	@echo "  l2_<subsystem>    — 组合测试，多服务交互"
	@echo "  l3_<subsystem>    — 完整 switch-examples"
	@echo ""
	@echo "Usage:"
	@echo "  make          — 构建所有测试"
	@echo "  make l0_svc_exit — 构建单个测试"
	@echo "  make clean    — 清理所有"
