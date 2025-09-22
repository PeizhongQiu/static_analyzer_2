# 便携式测试项目 Makefile

# 编译器设置
CC = clang
CFLAGS = -Wall -Wno-unused-variable -O1 -g -I./include

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

# 生成所有bitcode文件
bitcode: $(BITCODE_FILES)
	@echo "✅ 所有bitcode文件生成完成"
	@echo "生成的文件:"
	@find . -name "*.bc" -exec ls -lh {} \;

# 生成单个bitcode文件
%.bc: %.c
	@echo "编译: $< -> $@"
	@$(CC) $(CFLAGS) -emit-llvm -c $< -o $@

# 生成compile_commands.json
compile_commands.json:
	@echo "生成compile_commands.json..."
	@echo '[' > compile_commands.json
	@for src in $(SOURCES); do \
		echo '  {' >> compile_commands.json; \
		echo '    "directory": "$(shell pwd)",' >> compile_commands.json; \
		echo '    "command": "$(CC) $(CFLAGS) -emit-llvm -c $src",' >> compile_commands.json; \
		echo '    "file": "'$src'"' >> compile_commands.json; \
		if [ "$src" != "$(lastword $(SOURCES))" ]; then \
			echo '  },' >> compile_commands.json; \
		else \
			echo '  }' >> compile_commands.json; \
		fi; \
	done
	@echo ']' >> compile_commands.json
	@echo "✅ compile_commands.json生成完成"

# 清理
clean:
	find . -name "*.bc" -delete
	rm -f compile_commands.json

# 验证
verify:
	@echo "=== 验证生成的文件 ==="
	@echo "Bitcode files:"
	@find . -name "*.bc" -exec ls -lh {} \; || echo "  没有找到.bc文件"
	@echo ""
	@echo "Source files:"
	@find . -name "*.c" -exec ls -lh {} \;
	@echo ""
	@echo "JSON files:"
	@ls -lh *.json 2>/dev/null || echo "  没有找到JSON文件"

# 测试单个文件编译
test:
	@echo "测试编译..."
	$(CC) $(CFLAGS) -emit-llvm -c src/globals.c -o src/globals.bc
	@echo "✅ 测试成功"
	@ls -lh src/globals.bc

.PHONY: all bitcode clean verify test
