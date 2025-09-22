# 便携式测试项目 Makefile - 增强调试信息版本

# 编译器设置
CC = clang

# 增强的编译标志 - 保留更多类型和调试信息
CFLAGS = -Wall -Wno-unused-variable -O1 \
         -g3 -gdwarf-4 \
         -fno-eliminate-unused-debug-types \
         -fno-discard-value-names \
         -fstandalone-debug \
         -I./include -std=c99 -D__KERNEL__

# 源文件
SOURCES = src/kernel_impl.c \
          src/globals.c \
          src/buffer_manager.c \
          src/device_manager.c \
          src/interrupt_handlers.c \
          src/data_processor.c \
          drivers/test_irq_driver.c

# Bitcode文件
BITCODE_FILES = $(SOURCES:.c=.bc)

# 默认目标
all: bitcode

# 生成所有bitcode文件（保留更多调试信息）
bitcode: $(BITCODE_FILES)
	@echo "✅ 所有bitcode文件生成完成（增强调试信息）"
	@echo "生成的文件:"
	@find . -name "*.bc" -exec ls -lh {} \;

# 生成单个bitcode文件
%.bc: %.c
	@echo "编译: $< -> $@ (保留调试信息)"
	@$(CC) $(CFLAGS) -emit-llvm -c $< -o $@

# 生成compile_commands.json（增强调试信息版本）
compile_commands.json:
	@echo "生成compile_commands.json（增强调试信息版本）..."
	@echo '[' > compile_commands.json
	@i=0; for src in $(SOURCES); do \
		i=$$((i+1)); \
		echo '  {' >> compile_commands.json; \
		echo '    "directory": "$(shell pwd)",' >> compile_commands.json; \
		echo '    "command": "$(CC) $(CFLAGS) -emit-llvm -c $$src",' >> compile_commands.json; \
		echo '    "file": "$$src"' >> compile_commands.json; \
		if [ $$i -lt $(words $(SOURCES)) ]; then \
			echo '  },' >> compile_commands.json; \
		else \
			echo '  }' >> compile_commands.json; \
		fi; \
	done
	@echo ']' >> compile_commands.json
	@echo "✅ compile_commands.json生成完成（包含增强调试信息）"

# 清理
clean:
	find . -name "*.bc" -delete
	find . -name "*.ll" -delete
#	rm -f compile_commands.json

# 验证调试信息
verify-debug: bitcode
	@echo "🔍 验证调试信息质量..."
	@for bc in $(BITCODE_FILES); do \
		echo "检查 $$bc:"; \
		llvm-dis $$bc -o - | grep -c "!DI" || echo "  调试信息较少"; \
	done

# 生成人类可读的LLVM IR
ir: bitcode
	@echo "🔍 生成LLVM IR文件用于检查..."
	@for bc in $(BITCODE_FILES); do \
		ll_file=$${bc%.bc}.ll; \
		echo "$$bc -> $$ll_file"; \
		llvm-dis $$bc -o $$ll_file; \
	done
	@echo "✅ LLVM IR文件生成完成，可以检查结构体定义"

# 检查结构体定义
check-structs: ir
	@echo "🏗️ 检查结构体定义..."
	@echo "buffer_info 结构体:"
	@grep -A 10 -B 2 "buffer_info.*=" src/*.ll 2>/dev/null | head -15 || echo "  未找到buffer_info定义"
	@echo ""
	@echo "test_device 结构体:"
	@grep -A 15 -B 2 "test_device.*=" src/*.ll 2>/dev/null | head -20 || echo "  未找到test_device定义"

# 测试编译
test:
	@echo "测试编译（增强调试信息）..."
	$(CC) $(CFLAGS) -emit-llvm -c src/globals.c -o src/globals.bc
	@echo "✅ 测试成功"
	@ls -lh src/globals.bc
	@echo "检查调试信息:"
	@llvm-dis src/globals.bc -o - | grep -c "!DI" && echo "  包含调试信息" || echo "  调试信息不足"

.PHONY: all bitcode clean verify-debug ir check-structs test
