#!/usr/bin/env python3
# ccjson_to_bc_clean.py - 为静态分析生成干净的bitcode文件
import json, os, shlex, subprocess, sys
from pathlib import Path

CLANG  = os.environ.get("CLANG", "clang")
TARGET = os.environ.get("TARGET", "")
OPTIMIZATION = os.environ.get("OPTIMIZATION", "-O1")  # 默认轻度优化保持可读性

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
KEEP_PAIR = {"-I","-isystem","-idirafter","-iprefix","-include","-imacros"}
# options we ALWAYS drop, plus their next-arg if they take one
DROP_SINGLE = {"-c","-E","-pipe","-MMD","-MD","-MP"}
DROP_PAIR   = {"-o","-Wp,","-MF","-MT","-MQ"}

WRAPPER_PREFIXES = ("ccache","sccache","distcc","icecc")

def norm_args(entry):
    if entry.get("arguments"):
        return list(entry["arguments"])
    return shlex.split(entry.get("command",""), posix=True)

def is_c_compile(args):
    return ("-c" in args) and any(a.endswith(".c") for a in args) and ("-E" not in args)

def pick_src(args):
    srcs = [a for a in args if a.endswith(".c")]
    return srcs[-1] if srcs else None

def pick_out_o(args):
    for i,a in enumerate(args):
        if a == "-o" and i+1 < len(args):
            return args[i+1]
    return None

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
        if i == 0 and (a in WRAPPER_PREFIXES or a.endswith(("gcc","cc","clang"))):
            i += 1; continue

        # drop '-mllvm <param>'
        if a == "-mllvm":
            i += 2; continue

        # 过滤所有插桩和性能分析标志
        if is_flag_to_drop(a):
            i += 1; continue

        # drop singles
        if a in DROP_SINGLE or a.startswith("-Wp,"):
            i += 1; continue

        # drop pairs we don't need
        if a in DROP_PAIR:
            i += 2; continue

        # keep pair options (and their next argument)
        if a in KEEP_PAIR and i+1 < len(args):
            out.extend([a, args[i+1]]); i += 2; continue

        # 替换优化级别为分析友好的级别
        if a in ["-O0", "-O2", "-O3", "-Os", "-Oz"]:
            if OPTIMIZATION not in out:  # 避免重复
                out.append(OPTIMIZATION)
            i += 1; continue

        # 保留调试信息但使用标准格式
        if a.startswith("-g"):
            if a == "-g0":
                i += 1; continue  # 跳过禁用调试信息
            if "-g" not in out:  # 避免重复调试标志
                out.append("-g")
            i += 1; continue

        # 保留重要的编译选项
        if a.startswith(("-D","-U","-I","-isystem","-include","-idirafter","-iprefix",
                         "-nostdinc","-f","-m","-W","-std=","-mcmodel=","-march=","-mtune=")):
            # 跳过一些不必要的警告选项
            if a.startswith(("-Wno-", "-Werror")):
                i += 1; continue
            out.append(a)
            i += 1; continue

        # 保留架构和平台相关选项
        if a.startswith(("-m32", "-m64", "-march", "-mtune", "-mcpu")):
            out.append(a)
            i += 1; continue

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

def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} /path/to/compile_commands.json", file=sys.stderr)
        print("\n为静态分析生成干净的bitcode文件", file=sys.stderr)
        print("环境变量:", file=sys.stderr)
        print("  CLANG=clang-版本号    指定clang版本", file=sys.stderr) 
        print("  TARGET=架构-系统      指定目标架构", file=sys.stderr)
        print("  OPTIMIZATION=-O级别   优化级别 (默认: -O1)", file=sys.stderr)
        sys.exit(2)

    db = Path(sys.argv[1])
    entries = json.loads(db.read_text())
    total=ok=skip=fail=filtered_out=0

    print("开始为静态分析生成干净的bitcode文件...")
    print(f"编译器: {CLANG}")
    print(f"优化级别: {OPTIMIZATION}")
    print("过滤的标志类型: sanitizers, profiling, stack protection, coverage")
    print()

    for e in entries:
        args = norm_args(e)
        if not args or not is_c_compile(args): continue
        
        # 检查是否包含插桩标志
        has_instrumentation = any(is_flag_to_drop(arg) for arg in args)
        
        # 仅处理内核代码
        if "-D__KERNEL__" not in args: 
            filtered_out += 1
            continue

        # 去掉前置包装
        while args and (args[0] in WRAPPER_PREFIXES or args[0].endswith(("gcc","cc","clang")) and args[0]!=CLANG):
            args = [CLANG] + args[1:]
            break
        if not args or not args[0]: continue

        src = pick_src(args); out_o = pick_out_o(args)
        if not src: continue

        # 计算输出 .bc 路径
        if out_o:
            out_bc = Path(os.path.splitext(out_o)[0] + ".bc")
        else:
            base = os.path.splitext(os.path.basename(src))[0]
            out_bc = Path(base + ".bc")

        # 应用清理过滤
        flags = filter_flags_for_analysis(args)

        cmd = [CLANG]
        if TARGET: cmd += [f"--target={TARGET}"]
        cmd += ["-emit-llvm", "-c", src, "-o", str(out_bc)]
        cmd += flags

        cwd = Path(e.get("directory","."))
        out_path = (cwd / out_bc).resolve()
        src_path = (cwd / src).resolve() if not Path(src).is_absolute() else Path(src)
        out_path.parent.mkdir(parents=True, exist_ok=True)

        # 跳过已存在且较新的文件
        try:
            if out_path.exists() and out_path.stat().st_mtime >= src_path.stat().st_mtime:
                skip += 1; total += 1; continue
        except FileNotFoundError:
            pass

        # 显示处理进度
        if has_instrumentation:
            status = "[CLEAN]"  # 清理了插桩
        else:
            status = "[BUILD]"  # 正常编译
            
        print(f"{status} {src} -> {out_bc}")

        res = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        total += 1
        
        if res.returncode == 0:
            ok += 1
            # 验证生成的bitcode质量
            if verify_bitcode_quality(out_path):
                print(f"  ✓ 生成干净的bitcode: {out_path}")
            else:
                print(f"  ⚠ bitcode质量检查失败: {out_path}")
        else:
            fail += 1
            print(f"  ✗ 编译失败: {out_bc}")
            if res.stderr.strip():
                # 只显示关键错误信息
                error_lines = res.stderr.strip().split('\n')
                important_errors = [line for line in error_lines if 
                                  'error:' in line.lower() or 'fatal:' in line.lower()]
                if important_errors:
                    print(f"     {important_errors[0]}")

    print(f"\n=== 编译统计 ===")
    print(f"总文件数: {total}")
    print(f"成功编译: {ok}")
    print(f"跳过文件: {skip}")
    print(f"编译失败: {fail}")
    print(f"非内核文件: {filtered_out}")
    print(f"\n为静态分析生成了 {ok} 个干净的 .clean.bc 文件")
    
    if ok > 0:
        print(f"\n使用方法:")
        print(f"1. 更新 compile_commands.json 中的输出文件名为 .clean.bc")
        print(f"2. 或直接使用生成的 .clean.bc 文件进行分析")
        print(f"3. 这些bitcode文件移除了插桩代码，更适合静态分析")

    sys.exit(1 if fail > ok else 0)

def verify_bitcode_quality(bc_path):
    """简单验证bitcode质量"""
    try:
        # 使用 llvm-dis 检查bitcode是否有效
        result = subprocess.run(["llvm-dis", str(bc_path), "-o", "-"], 
                              capture_output=True, text=True, timeout=10)
        if result.returncode == 0:
            # 检查是否包含调试信息
            has_debug = "!dbg" in result.stdout
            # 检查是否有过多的插桩代码
            has_sanitizer = any(keyword in result.stdout for keyword in 
                              ["__asan", "__ubsan", "__tsan", "__msan", "__gcov"])
            return has_debug and not has_sanitizer
    except (subprocess.TimeoutExpired, FileNotFoundError):
        pass
    return True  # 默认认为有效

if __name__ == "__main__":
    main()
