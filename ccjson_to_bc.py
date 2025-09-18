#!/usr/bin/env python3
# ccjson_to_bc_clean.py - 直接编译生成干净的bitcode文件用于静态分析
import json, os, shlex, subprocess, sys
from pathlib import Path

CLANG = os.environ.get("CLANG", "clang")
TARGET = os.environ.get("TARGET", "")
OPTIMIZATION = os.environ.get("OPTIMIZATION", "-O0")  # 使用-O1优化等级

# 为静态分析过滤掉的标志
SANITIZER_FLAGS = {
    "-fsanitize=kernel-address", "-fsanitize-address-use-after-scope", 
    "-fasan-shadow-offset=0xdffffc0000000000", "-fsanitize=address",
    "-fsanitize-coverage=trace-pc", "-fsanitize-coverage=trace-cmp",
    "-fsanitize-coverage=trace-div", "-fsanitize-coverage=trace-gep",
    "-fsanitize-coverage=indirect-calls", "-fsanitize-coverage=trace-pc-guard",
    "-fsanitize=undefined", "-fsanitize=integer", "-fsanitize=nullability"
}

PROFILING_FLAGS = {
    "-fprofile-instr-generate", "-fprofile-instr-use",
    "-fprofile-generate", "-fprofile-use",
    "-fcoverage-mapping", "-fprofile-arcs", "-ftest-coverage"
}

SECURITY_FLAGS = {
    "-fstack-protector", "-fstack-protector-strong", "-fstack-protector-all",
    "-fstack-clash-protection", "-fcf-protection"
}

DEBUG_FLAGS = {
    "-gsplit-dwarf", "-gdwarf-5", "-gno-pubnames"
}

# options that take a separate next-arg and we want to KEEP both
KEEP_PAIR = {"-I", "-isystem", "-idirafter", "-iprefix", "-include", "-imacros"}
# options we ALWAYS drop, plus their next-arg if they take one
DROP_SINGLE = {"-c", "-E", "-pipe", "-MMD", "-MD", "-MP"}
DROP_PAIR = {"-o", "-Wp,", "-MF", "-MT", "-MQ"}

WRAPPER_PREFIXES = ("ccache", "sccache", "distcc", "icecc")


def norm_args(entry):
    """标准化编译参数"""
    if entry.get("arguments"):
        return list(entry["arguments"])
    return shlex.split(entry.get("command", ""), posix=True)


def is_c_compile(args):
    """检查是否是C编译命令"""
    return ("-c" in args) and any(a.endswith(".c") for a in args) and ("-E" not in args)


def pick_src(args):
    """选择源文件"""
    srcs = [a for a in args if a.endswith(".c")]
    return srcs[-1] if srcs else None


def is_flag_to_drop(flag):
    """检查是否是需要过滤的标志"""
    # 检查完整匹配
    if flag in SANITIZER_FLAGS or flag in PROFILING_FLAGS or flag in SECURITY_FLAGS:
        return True
    
    # 检查前缀匹配
    prefixes_to_drop = [
        "-fsanitize", "-fno-sanitize", "-fprofile", "-fcoverage",
        "-fstack-protector", "-fcf-protection", "-fstack-clash"
    ]
    
    return any(flag.startswith(prefix) for prefix in prefixes_to_drop)


def filter_flags_for_analysis(args):
    """为静态分析过滤编译标志"""
    out = []
    i = 1  # skip compiler name at args[0]
    
    while i < len(args):
        a = args[i]

        # drop wrappers 
        if i == 0 and (a in WRAPPER_PREFIXES or a.endswith(("gcc", "cc", "clang"))):
            i += 1
            continue

        # drop '-mllvm <param>'
        if a == "-mllvm":
            i += 2
            continue

        # 过滤所有插桩和性能分析标志
        if is_flag_to_drop(a):
            i += 1
            continue

        # drop singles
        if a in DROP_SINGLE or a.startswith("-Wp,"):
            i += 1
            continue

        # drop pairs we don't need
        if a in DROP_PAIR:
            i += 2
            continue

        # keep pair options (and their next argument)
        if a in KEEP_PAIR and i + 1 < len(args):
            out.extend([a, args[i + 1]])
            i += 2
            continue

        # 替换优化级别为分析友好的级别
        if a in ["-O0", "-O2", "-O3", "-Os", "-Oz"]:
            if OPTIMIZATION not in out:  # 避免重复
                out.append(OPTIMIZATION)
            i += 1
            continue

        # 保留调试信息但使用标准格式
        if a.startswith("-g"):
            if a == "-g0":
                i += 1
                continue  # 跳过禁用调试信息
            if "-g" not in out:  # 避免重复调试标志
                out.append("-g")
            i += 1
            continue

        # 保留重要的编译选项
        if a.startswith(("-D", "-U", "-I", "-isystem", "-include", "-idirafter", "-iprefix",
                         "-nostdinc", "-f", "-m", "-W", "-std=", "-mcmodel=", "-march=", "-mtune=")):
            # 跳过一些不必要的警告选项
            if a.startswith(("-Wno-", "-Werror")):
                i += 1
                continue
            out.append(a)
            i += 1
            continue

        # 保留架构和平台相关选项
        if a.startswith(("-m32", "-m64", "-march", "-mtune", "-mcpu")):
            out.append(a)
            i += 1
            continue

        # 其他情况忽略
        i += 1

    # 添加静态分析友好的选项
    analysis_flags = [
        "-Wno-unknown-warning-option",
        "-Wno-unused-command-line-argument", 
        "-fno-discard-value-names",  # 保留变量名用于分析
        "-disable-llvm-passes",      # 禁用LLVM优化以保持IR清晰
    ]
    
    for flag in analysis_flags:
        if flag not in out:
            out.append(flag)
    
    return out


def compile_directly(entries):
    """直接编译生成干净的.bc文件"""
    total = 0
    success = 0
    failed = 0
    
    print("开始编译干净的bitcode文件用于静态分析...")
    print(f"编译器: {CLANG}")
    print(f"优化等级: {OPTIMIZATION}")
    print("移除标志: sanitizers, profiling, stack protection, coverage")
    print("模式: 强制重新编译所有文件")
    print("输出: .bc文件将保存在源文件相同目录下")
    print()
    
    for entry in entries:
        args = norm_args(entry)
        if not args or not is_c_compile(args):
            continue
            
        # 仅处理内核代码
        if "-D__KERNEL__" not in args:
            continue
            
        src = pick_src(args)
        if not src:
            continue
        
        cwd = Path(entry.get("directory", "."))
        
        # 计算输出 .bc 路径 (保持在源文件相同目录下)
        src_file = Path(src)
        if src_file.is_absolute():
            # 绝对路径：在同一目录下生成.bc文件
            bc_path = src_file.with_suffix('.bc')
        else:
            # 相对路径：在工作目录的相对路径下生成.bc文件
            bc_path = cwd / src_file.with_suffix('.bc')
        
        # 应用清理过滤
        flags = filter_flags_for_analysis(args)
        
        src_path = cwd / src if not Path(src).is_absolute() else Path(src)
        
        total += 1
        
        print(f"编译: {src} -> {bc_path.relative_to(cwd) if bc_path.is_relative_to(cwd) else bc_path}")
        
        # 检查源文件是否存在
        if not src_path.exists():
            print(f"  ✗ 源文件不存在: {src_path}")
            failed += 1
            continue
        
        # 创建输出目录
        bc_path.parent.mkdir(parents=True, exist_ok=True)
        
        # 构建编译命令
        cmd = [CLANG]
        if TARGET:
            cmd.append(f"--target={TARGET}")
        cmd.extend(["-emit-llvm", "-c", str(src), "-o", str(bc_path)])
        cmd.extend(flags)
        
        # 执行编译
        try:
            result = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=60)
            if result.returncode == 0:
                print("  ✓ 成功")
                success += 1
            else:
                print("  ✗ 编译失败")
                if result.stderr.strip():
                    # 只显示关键错误
                    error_lines = result.stderr.strip().split('\n')
                    important_errors = [line for line in error_lines 
                                      if any(keyword in line.lower() 
                                           for keyword in ['error:', 'fatal:', 'undefined'])]
                    if important_errors:
                        print(f"     {important_errors[0]}")
                failed += 1
        except subprocess.TimeoutExpired:
            print("  ✗ 编译超时")
            failed += 1
        except Exception as e:
            print(f"  ✗ 编译异常: {e}")
            failed += 1
    
    # 输出统计信息
    print()
    print("=== 编译统计 ===")
    print(f"总文件数: {total}")
    print(f"成功编译: {success}")
    print(f"编译失败: {failed}")
    print()
    
    if success > 0:
        print(f"✓ 成功生成 {success} 个干净的 .bc 文件")
        print("这些文件已移除插桩代码，适合静态分析")
    else:
        print("✗ 没有成功编译任何文件")
    
    return total, success, failed


def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} /path/to/compile_commands.json", file=sys.stderr)
        print("\n直接编译生成干净的bitcode文件用于静态分析", file=sys.stderr)
        print("环境变量:", file=sys.stderr)
        print("  CLANG=clang-版本号    指定clang版本", file=sys.stderr)
        print("  TARGET=架构-系统      指定目标架构", file=sys.stderr)
        print("  OPTIMIZATION=-O级别   优化级别 (默认: -O1)", file=sys.stderr)
        sys.exit(2)

    db = Path(sys.argv[1])
    
    if not db.exists():
        print(f"错误: 文件不存在: {db}", file=sys.stderr)
        sys.exit(1)
    
    try:
        entries = json.loads(db.read_text())
    except json.JSONDecodeError as e:
        print(f"错误: JSON解析失败: {e}", file=sys.stderr)
        sys.exit(1)

    print("直接编译生成干净的bitcode文件用于静态分析...")
    print(f"输入文件: {db}")
    print(f"编译器: {CLANG}")
    print(f"优化等级: {OPTIMIZATION}")
    print("文件将生成到对应的源文件目录下")
    print()

    # 直接编译生成.bc文件
    total, success, failed = compile_directly(entries)
    
    print()
    if success > 0:
        print("下一步:")
        print("1. 使用现有的compile_commands.json文件进行分析:")
        print(f"   ./irq_analyzer_simple --compile-commands={db.name} \\")
        print("                         --handlers=handler.json \\")
        print("                         --output=results.json --verbose")
        print()
        print("优点:")
        print("  • 生成的.bc文件已移除所有插桩代码")
        print("  • 保留调试信息，适合静态分析") 
        print("  • 使用-O1优化等级保持代码可读性")
        print("  • .bc文件生成在源文件相同目录下")
        print("  • 强制重新编译确保文件最新")
    else:
        print("⚠ 警告: 没有成功编译任何文件")
        print("请检查:")
        print("1. compile_commands.json 是否包含 -D__KERNEL__ 定义的文件")
        print("2. 文件是否是 .c 源文件")
        print("3. 编译命令是否包含 -c 标志")
        print("4. 源文件是否存在")
    
    return 0 if success > 0 else 1


if __name__ == "__main__":
    sys.exit(main())
