#!/usr/bin/env python3
"""Metropolismania PC build pipeline (Windows, MSVC).

Steps, in order:
  bootstrap   sync the pinned PS2Recomp submodule (+ patches/) into work/PS2Recomp
  tools       build ps2_analyzer and ps2_recomp
  extract     pull SLUS_203.77, SYSTEM.CNF and SYS/ from your disc image into original/
  symbols     merge ps2_analyzer's SDK signature names into config/symbols.csv
  analyze     Ghidra headless: import + analyze, apply config/symbols.csv, export config/funcmap.csv
  script      run one Ghidra script from tools/ghidra against the project (read-only by default)
  recompile   render config/metropolismania.template.toml and run ps2_recomp into generated/
  stage       copy generated code and src/overrides into the runtime tree
  build       build ps2EntryRunner
  run         start the runner with a timeout and capture a log in logs/
  all         bootstrap, tools, extract, analyze, recompile, stage, build

Everything under original/, generated/, work/, build/ and logs/ is git-ignored.
"""

from __future__ import annotations

import argparse
import datetime as dt
import filecmp
import json
import os
import shutil
import subprocess
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
UPSTREAM = ROOT / "ext" / "PS2Recomp"
WORK = ROOT / "work"
STAGE = WORK / "stage"
RUNTIME_TREE = WORK / "PS2Recomp"
BUILD = ROOT / "build" / "ps2recomp"
ORIGINAL = ROOT / "original"
GENERATED = ROOT / "generated"
LOGS = ROOT / "logs"
CONFIG = ROOT / "config"
ELF_NAME = "SLUS_203.77"
GHIDRA_PROJECT = WORK / "ghidra"
GHIDRA_PROJECT_NAME = "metropolismania"
GHIDRA_LANGUAGE = "r5900:LE:32:default"
GHIDRA_EXPORT_TOML = WORK / "ghidra_export.toml"
RENDERED_TOML = WORK / "metropolismania.toml"
BUILD_TYPE = "RelWithDebInfo"


def log(msg: str) -> None:
    print(f"[pipeline] {msg}", flush=True)


def run(cmd: list[str], env: dict[str, str] | None = None, cwd: Path | None = None) -> None:
    log("$ " + " ".join(f'"{c}"' if " " in c else c for c in cmd))
    subprocess.run(cmd, env=env, cwd=cwd, check=True)


# ---------------------------------------------------------------- local settings

def settings() -> dict:
    """Machine-specific paths live in work/local.json (git-ignored). Missing keys are
    auto-detected where possible."""
    path = WORK / "local.json"
    data = json.loads(path.read_text()) if path.exists() else {}
    return data


def ghidra_dir() -> Path:
    value = settings().get("ghidra_install_dir") or os.environ.get("GHIDRA_INSTALL_DIR")
    if not value:
        sys.exit("Set ghidra_install_dir in work/local.json or GHIDRA_INSTALL_DIR")
    return Path(value)


def msvc_env() -> dict[str, str]:
    """Environment of a vcvars64 shell, so cl.exe, the Windows SDK and Ninja are on PATH."""
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    install = subprocess.run(
        [str(vswhere), "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
         "-property", "installationPath"], capture_output=True, text=True, check=True).stdout.strip()
    if not install:
        sys.exit("Visual Studio with the C++ x64 tools was not found")
    vcvars = Path(install) / "VC/Auxiliary/Build/vcvars64.bat"
    out = subprocess.run(f'cmd /d /c ""{vcvars}" >nul && set"', capture_output=True, text=True, shell=True, check=True).stdout
    env = dict(os.environ)
    for line in out.splitlines():
        key, sep, value = line.partition("=")
        if sep:
            env[key] = value
    return env


# ---------------------------------------------------------------- bootstrap

def upstream_lock() -> dict:
    return json.loads((ROOT / "upstream.lock.json").read_text())


def sync_tree(src: Path, dst: Path, keep: set[str] | None = None) -> int:
    """Make dst match src, touching only files whose bytes differ so incremental builds stay incremental.
    Paths in `keep` (relative, posix) are never deleted from dst."""
    keep = keep or set()
    changed = 0
    src_files = {p.relative_to(src).as_posix() for p in src.rglob("*") if p.is_file() and ".git" not in p.parts}
    for rel in sorted(src_files):
        s, d = src / rel, dst / rel
        if not d.exists() or not filecmp.cmp(s, d, shallow=False):
            d.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(s, d)
            changed += 1
    if dst.exists():
        for p in dst.rglob("*"):
            rel = p.relative_to(dst).as_posix()
            if p.is_file() and rel not in src_files and not any(rel.startswith(k) for k in keep):
                p.unlink()
                changed += 1
    return changed


def cmd_bootstrap(_: argparse.Namespace) -> None:
    lock = upstream_lock()
    head = subprocess.run(["git", "-C", str(UPSTREAM), "rev-parse", "HEAD"], capture_output=True, text=True, check=True).stdout.strip()
    if head != lock["commit"]:
        sys.exit(f"ext/PS2Recomp is at {head}, upstream.lock.json pins {lock['commit']}. Run: git submodule update --init")
    if STAGE.exists():
        shutil.rmtree(STAGE)
    shutil.copytree(UPSTREAM, STAGE, ignore=shutil.ignore_patterns(".git"))
    patches = sorted((ROOT / "patches").glob("*.patch"))
    for patch in patches:
        run(["git", "apply", "--whitespace=nowarn", str(patch)], cwd=STAGE)
    # Generated code and overrides are owned by the stage step; keep them.
    changed = sync_tree(STAGE, RUNTIME_TREE, keep={"ps2xRuntime/src/runner/", "ps2xRuntime/include/ps2_recompiled_"})
    shutil.rmtree(STAGE)
    log(f"upstream {head[:8]} + {len(patches)} patch(es) -> {RUNTIME_TREE} ({changed} file(s) changed)")


# ---------------------------------------------------------------- build helpers

def configure(env: dict[str, str]) -> None:
    if (BUILD / "CMakeCache.txt").exists():
        return
    run(["cmake", "-S", str(RUNTIME_TREE), "-B", str(BUILD), "-G", "Ninja", f"-DCMAKE_BUILD_TYPE={BUILD_TYPE}",
         "-DPS2X_BUILD_TEST=OFF", "-DPS2X_BUILD_STUDIO=OFF"], env=env)


def build_targets(targets: list[str]) -> None:
    env = msvc_env()
    configure(env)
    run(["cmake", "--build", str(BUILD), "--target", *targets], env=env)


def tool(name: str) -> Path:
    for sub in (f"ps2xRecomp/{name}.exe", f"ps2xAnalyzer/{name}.exe", f"ps2xRuntime/{name}.exe"):
        if (BUILD / sub).exists():
            return BUILD / sub
    sys.exit(f"{name}.exe not built yet; run: pipeline.py tools")


def cmd_tools(_: argparse.Namespace) -> None:
    build_targets(["ps2_analyzer", "ps2_recomp"])


# ---------------------------------------------------------------- extract / analyze

def default_image() -> Path:
    cues = sorted(ROOT.glob("*.cue")) + sorted(ROOT.glob("*.iso"))
    if not cues:
        sys.exit("No disc image given and none found in the repo root (pass a .cue, .bin or .iso)")
    return cues[0]


def cmd_extract(args: argparse.Namespace) -> None:
    image = Path(args.image) if args.image else default_image()
    # The runtime's cdrom0: root defaults to the ELF's folder, so the game needs the whole disc
    # (DATA/...) next to SLUS_203.77 to run. --boot-only skips the data for analysis-only setups.
    extra = [] if getattr(args, "boot_only", False) else ["--all"]
    run([sys.executable, "-I", str(ROOT / "tools/extract_disc.py"), str(image), "-o", str(ORIGINAL), *extra])


def cmd_analyze(args: argparse.Namespace) -> None:
    elf = ORIGINAL / ELF_NAME
    if not elf.exists():
        sys.exit("original/SLUS_203.77 missing; run: pipeline.py extract")
    GHIDRA_PROJECT.mkdir(parents=True, exist_ok=True)
    headless = ghidra_dir() / "support" / "analyzeHeadless.bat"
    script_path = ";".join(str(p) for p in (ROOT / "tools/ghidra", RUNTIME_TREE / "ps2xRecomp/tools/ghidra"))
    imported = (GHIDRA_PROJECT / f"{GHIDRA_PROJECT_NAME}.rep").exists() and not args.reimport
    cmd = [str(headless), str(GHIDRA_PROJECT), GHIDRA_PROJECT_NAME]
    if imported:
        cmd += ["-process", ELF_NAME, "-noanalysis"]
    else:
        cmd += ["-import", str(elf), "-overwrite", "-processor", GHIDRA_LANGUAGE, "-cspec", "default"]
    cmd += ["-scriptPath", script_path,
            "-postScript", "ApplySymbols.java", str(CONFIG / "symbols.csv"),
            "-postScript", "ExportPS2Functions.java", str(GHIDRA_EXPORT_TOML), str(CONFIG / "funcmap.csv")]
    run(cmd)
    filter_function_map()


def filter_function_map() -> None:
    """Drop function-map rows that config/layout.toml marks as data (see that file for why)."""
    layout = tomllib.loads((CONFIG / "layout.toml").read_text())
    ranges = [(int(s, 16), int(e, 16)) for s, e in layout.get("data_ranges", [])]
    excluded = {int(a, 16) for a in layout.get("exclude_entries", [])}
    path = CONFIG / "funcmap.csv"
    lines = path.read_text().splitlines()
    kept, dropped = [lines[0]], []
    for line in lines[1:]:
        start = int(line.split(",")[1], 16)
        if start in excluded or any(s <= start < e for s, e in ranges):
            dropped.append(line.split(",")[0])
        else:
            kept.append(line)
    path.write_text("\n".join(kept) + "\n")
    log(f"config/funcmap.csv: {len(kept) - 1} entries ({len(dropped)} dropped by config/layout.toml: {', '.join(dropped) or 'none'})")


def cmd_symbols(_: argparse.Namespace) -> None:
    """Run ps2_analyzer's SDK signature scan and merge its names into config/symbols.csv.
    Rows with source 'manual' are kept and win over analyzer rows at the same address."""
    import csv
    import re

    elf = ORIGINAL / ELF_NAME
    out = WORK / "analyzer.toml"
    result = subprocess.run([str(tool("ps2_analyzer")), str(elf), str(out)], check=True, capture_output=True, text=True)
    # The analyzer also decodes data blobs (VU microcode, tables) as EE code; those show up as
    # "Unknown MMI function" lines and are expected noise.
    noise = sum(1 for line in result.stderr.splitlines() + result.stdout.splitlines() if "Unknown MMI function" in line)
    log(f"ps2_analyzer done ({noise} 'Unknown MMI function' lines from data decoded as code)")
    general = tomllib.loads(out.read_text()).get("general", {})
    detected: dict[int, str] = {}
    for key in ("stubs", "entry_points"):
        for selector in general.get(key, []):
            match = re.match(r"(.+)@(0x[0-9A-Fa-f]+)$", selector)
            if match and not match.group(1).startswith("sub_"):
                detected[int(match.group(2), 16)] = match.group(1)

    path = CONFIG / "symbols.csv"
    header = [line for line in path.read_text().splitlines() if line.startswith("#")]
    manual: dict[int, list[str]] = {}
    for row in csv.reader(line for line in path.read_text().splitlines() if line and not line.startswith("#")):
        if row[0].lower() == "address":
            continue
        source = row[3] if len(row) > 3 else "manual"
        if source != "ps2_analyzer":
            manual[int(row[0], 16)] = row[:3] + [source]
    rows = dict(manual)
    for addr, name in detected.items():
        rows.setdefault(addr, [f"0x{addr:08X}", name, "func", "ps2_analyzer"])
    lines = header + ["address,name,kind,source"]
    lines += [",".join(rows[a]) for a in sorted(rows)]
    path.write_text("\n".join(lines) + "\n")
    log(f"config/symbols.csv: {len(manual)} manual + {len(rows) - len(manual)} analyzer names")


def cmd_script(args: argparse.Namespace) -> None:
    """Run one Ghidra script from tools/ghidra against the analyzed project (read-only unless --write)."""
    headless = ghidra_dir() / "support" / "analyzeHeadless.bat"
    script_path = ";".join(str(p) for p in (ROOT / "tools/ghidra", RUNTIME_TREE / "ps2xRecomp/tools/ghidra"))
    cmd = [str(headless), str(GHIDRA_PROJECT), GHIDRA_PROJECT_NAME, "-process", ELF_NAME, "-noanalysis"]
    if not args.write:
        cmd.append("-readOnly")
    cmd += ["-scriptPath", script_path, "-postScript", args.name, *args.script_args]
    run(cmd)


# ---------------------------------------------------------------- recompile

def toml_value(value) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, str):
        return json.dumps(value)
    if isinstance(value, list):
        if not value:
            return "[]"
        return "[\n" + "".join(f"  {toml_value(v)},\n" for v in value) + "]"
    if isinstance(value, dict):
        return "{ " + ", ".join(f"{k} = {toml_value(v)}" for k, v in value.items()) + " }"
    raise TypeError(f"unsupported TOML value: {value!r}")


def render_config() -> None:
    text = (CONFIG / "metropolismania.template.toml").read_text()
    for key, path in {"@ELF@": ORIGINAL / ELF_NAME, "@MAP@": CONFIG / "funcmap.csv", "@OUT@": GENERATED}.items():
        text = text.replace(key, path.as_posix())
    data = tomllib.loads(text)
    general = data.setdefault("general", {})
    if GHIDRA_EXPORT_TOML.exists():
        export = tomllib.loads(GHIDRA_EXPORT_TOML.read_text()).get("general", {})
        for key in ("stubs", "untracked_stubs"):
            merged = list(dict.fromkeys(general.get(key, []) + export.get(key, [])))
            general[key] = merged
    lines = ["# Rendered by tools/pipeline.py from config/metropolismania.template.toml. Do not edit.", ""]
    for section, table in data.items():
        scalars = {k: v for k, v in table.items() if not (isinstance(v, list) and v and isinstance(v[0], dict))}
        lines.append(f"[{section}]")
        lines += [f"{k} = {toml_value(v)}" for k, v in scalars.items()]
        lines.append("")
        for k, v in table.items():
            if k not in scalars:
                for item in v:
                    lines.append(f"[[{section}.{k}]]")
                    lines += [f"{ik} = {toml_value(iv)}" for ik, iv in item.items()]
                    lines.append("")
    RENDERED_TOML.parent.mkdir(parents=True, exist_ok=True)
    RENDERED_TOML.write_text("\n".join(lines))


def cmd_recompile(_: argparse.Namespace) -> None:
    render_config()
    GENERATED.mkdir(exist_ok=True)
    run([str(tool("ps2_recomp")), str(RENDERED_TOML)])
    cpp = len(list(GENERATED.rglob("*.cpp")))
    log(f"generated/: {cpp} .cpp file(s)")


# ---------------------------------------------------------------- stage / build / run

def cmd_stage(_: argparse.Namespace) -> None:
    runner = RUNTIME_TREE / "ps2xRuntime/src/runner"
    include = RUNTIME_TREE / "ps2xRuntime/include"
    staging = WORK / "runner_stage"
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)
    for cpp in GENERATED.rglob("*.cpp"):
        shutil.copy2(cpp, staging / cpp.name)
    for cpp in (ROOT / "src/overrides").glob("*.cpp"):
        shutil.copy2(cpp, staging / f"game_{cpp.name}")
    if not (staging / "register_functions.cpp").exists():
        sys.exit("generated/register_functions.cpp missing; run: pipeline.py recompile")
    changed = sync_tree(staging, runner)
    shutil.rmtree(staging)
    for header in GENERATED.rglob("*.h"):
        dest = include / header.name
        if not dest.exists() or not filecmp.cmp(header, dest, shallow=False):
            shutil.copy2(header, dest)
            changed += 1
    log(f"runtime tree staged ({changed} file(s) changed)")


def cmd_build(_: argparse.Namespace) -> None:
    build_targets(["ps2EntryRunner"])


def cmd_run(args: argparse.Namespace) -> None:
    LOGS.mkdir(exist_ok=True)
    log_path = LOGS / f"run-{dt.datetime.now():%Y%m%d-%H%M%S}.log"
    exe = tool("ps2EntryRunner")
    log(f"running {exe.name} for up to {args.timeout}s, log: {log_path}")
    with log_path.open("w", encoding="utf-8", errors="replace") as fh:
        proc = subprocess.Popen([str(exe), str(ORIGINAL / ELF_NAME)], stdout=fh, stderr=subprocess.STDOUT, cwd=exe.parent)
        try:
            proc.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
    log(f"exit code {proc.returncode}; last lines:")
    print("\n".join(log_path.read_text(errors="replace").splitlines()[-40:]))


def cmd_all(args: argparse.Namespace) -> None:
    for step in (cmd_bootstrap, cmd_tools, cmd_extract, cmd_symbols, cmd_analyze, cmd_recompile, cmd_stage, cmd_build):
        step(args)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("bootstrap").set_defaults(func=cmd_bootstrap)
    sub.add_parser("tools").set_defaults(func=cmd_tools)
    p = sub.add_parser("extract")
    p.add_argument("image", nargs="?", help=".cue, .bin or .iso (default: first .cue/.iso in the repo root)")
    p.add_argument("--boot-only", action="store_true", help="only SYSTEM.CNF, the boot ELF and SYS/ (enough for analysis)")
    p.set_defaults(func=cmd_extract)
    p = sub.add_parser("analyze")
    p.add_argument("--reimport", action="store_true", help="re-import and re-analyze the ELF from scratch")
    p.set_defaults(func=cmd_analyze)
    sub.add_parser("symbols", help="merge ps2_analyzer SDK names into config/symbols.csv").set_defaults(func=cmd_symbols)
    p = sub.add_parser("script", help="run a Ghidra script from tools/ghidra on the project")
    p.add_argument("--write", action="store_true", help="save changes to the Ghidra project")
    p.add_argument("name")
    p.add_argument("script_args", nargs="*")
    p.set_defaults(func=cmd_script)
    sub.add_parser("recompile").set_defaults(func=cmd_recompile)
    sub.add_parser("stage").set_defaults(func=cmd_stage)
    sub.add_parser("build").set_defaults(func=cmd_build)
    p = sub.add_parser("run")
    p.add_argument("--timeout", type=int, default=20)
    p.set_defaults(func=cmd_run)
    p = sub.add_parser("all")
    p.add_argument("image", nargs="?")
    p.add_argument("--reimport", action="store_true")
    p.set_defaults(func=cmd_all)
    args = parser.parse_args()
    args.func(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
