#!/usr/bin/env python3
# ccjson_to_bc_min.py — replay compile_commands.json to emit .bc (robust & short)
import json, os, shlex, subprocess, sys
from pathlib import Path

CLANG  = os.environ.get("CLANG", "clang")            # e.g. CLANG=clang-18
TARGET = os.environ.get("TARGET", "")                # e.g. TARGET=x86_64-linux-gnu
DROP_SAN = os.environ.get("DROP_SAN", "0") == "1"    # set DROP_SAN=1 to drop -fsanitize* flags

# options that take a separate next-arg and we want to KEEP both
KEEP_PAIR = {"-I","-isystem","-idirafter","-iprefix","-include"}
# options we ALWAYS drop, plus their next-arg if they take one
DROP_SINGLE = {"-c","-E","-pipe"}
DROP_PAIR   = {"-o","-Wp,","-MF","-MT","-MQ"}  # -Wp,前缀作为单记号出现时也丢
# special: drop '-mllvm' and its next token together (避免裸 -mllvm)
DROP_MLLVM = True

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

def filter_flags(args):
    out = []
    i = 1  # skip compiler name at args[0]
    while i < len(args):
        a = args[i]

        # drop wrappers at argv0 层面已处理，这里仅防御性处理
        if i == 0 and (a in WRAPPER_PREFIXES or a.endswith(("gcc","cc","clang"))):
            i += 1; continue

        # drop '-mllvm <param>'
        if DROP_MLLVM and a == "-mllvm":
            i += 2; continue

        # drop sanitizer flags if requested
        if DROP_SAN and (a.startswith("-fsanitize") or a.startswith("-fno-sanitize")):
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

        # generic keep rules
        if (a.startswith(("-D","-U","-I","-isystem","-include","-idirafter","-iprefix",
                          "-nostdinc","-f","-m","-O","-g","-W","-std=","-mcmodel="))):
            out.append(a); i += 1; continue

        # otherwise ignore quietly
        i += 1

    # de-noise unknown/unused flag diagnostics
    out += ["-Wno-unknown-warning-option","-Wno-unused-command-line-argument"]
    return out

def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} /path/to/compile_commands.json", file=sys.stderr); sys.exit(2)

    db = Path(sys.argv[1])
    entries = json.loads(db.read_text())
    total=ok=skip=fail=0

    for e in entries:
        args = norm_args(e)
        if not args or not is_c_compile(args): continue
        # 仅处理目标侧（排除 host 工具）。如要包含 host，把这行注释掉：
        if "-D__KERNEL__" not in args: continue

        # 去掉前置包装，如 ccache/sccache
        while args and (args[0] in WRAPPER_PREFIXES or args[0].endswith(("gcc","cc","clang")) and args[0]!=CLANG):
            # 若你希望强制使用 CLANG 指定版本，可直接替换为 CLANG
            args = [CLANG] + args[1:]
            break
        if not args or not args[0]: continue

        src = pick_src(args); out_o = pick_out_o(args)
        if not src: continue

        # 计算输出 .bc 路径（相对 entry['directory']）
        if out_o:
            out_bc = Path(os.path.splitext(out_o)[0] + ".bc")
        else:
            base = os.path.splitext(os.path.basename(src))[0]
            out_bc = Path(base + ".bc")

        flags = filter_flags(args)

        cmd = [CLANG]
        if TARGET: cmd += [f"--target={TARGET}"]
        cmd += ["-emit-llvm","-c", src, "-o", str(out_bc)]
        cmd += flags

        cwd = Path(e.get("directory","."))  # **关键：在每条 entry 的目录下执行**
        out_path = (cwd / out_bc).resolve()
        src_path = (cwd / src).resolve() if not Path(src).is_absolute() else Path(src)
        out_path.parent.mkdir(parents=True, exist_ok=True)

        try:
            if out_path.exists() and out_path.stat().st_mtime >= src_path.stat().st_mtime:
                skip += 1; total += 1; continue
        except FileNotFoundError:
            pass

        res = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        total += 1
        if res.returncode == 0:
            ok += 1
        else:
            fail += 1
            sys.stderr.write(f"[FAIL] {out_bc}\ncmd: {' '.join(shlex.quote(x) for x in cmd)}\n{res.stderr}\n")

    print(f"[done] total={total} ok={ok} skip={skip} fail={fail}")
    sys.exit(1 if fail else 0)

if __name__ == "__main__":
    main()

