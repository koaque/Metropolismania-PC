#!/usr/bin/env python3
"""Metropolismania PC build pipeline (Windows, MSVC).

Steps, in order:
  bootstrap   sync the pinned PS2Recomp submodule (+ patches/) into work/PS2Recomp
  tools       build ps2_analyzer and ps2_recomp
  extract     pull SLUS_203.77, SYSTEM.CNF and SYS/ from your disc image into original/
  symbols     merge ps2_analyzer's SDK signature names into config/symbols.csv
  analyze     Ghidra headless: import + analyze, apply config/symbols.csv, config/types/ and
              config/signatures.txt, export config/funcmap.csv
  map         analyze, then export the call graph (work/map/graph.json) and decompile every
              function (work/decomp/), then status
  status      rebuild the per-function inventory: config/port_status.csv, docs/PORT_STATUS.md, docs/map/
  pointers    add functions reached only through pointers (vtables, ctor/callback tables), re-analyze
  script      run one Ghidra script from tools/ghidra against the project (read-only by default)
  recompile   render config/metropolismania.template.toml and run ps2_recomp into generated/
  stage       copy generated code, src/overrides and src/port (after tools/port_gen.py) into the runtime tree
  build       build ps2EntryRunner
  run         start the runner with a timeout and capture a log in logs/
  capture     visible run that records calls of chosen functions into work/difftest/
  difftest    replay those records through ported and generated code, headless
  vu1-capture visible run that records VU1 program runs (PS2X_VU1_CAPTURE) into work/vu1_aot/records/
  vu1-test    replay those VU1 runs through the interpreter and compare, headless (plan P4); --aot runs them
              through the pre-timed programs (run), checks their timing model (timing) or fuzzes their ops (fuzz)
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
TYPES_DIR = CONFIG / "types"
SIGNATURES = CONFIG / "signatures.txt"
THIS_TYPES = CONFIG / "this_types.txt"
GRAPH_JSON = WORK / "map" / "graph.json"
DECOMP_DIR = WORK / "decomp"
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
    Paths under a `keep` prefix (relative, posix) are owned by another step: never copied, never deleted."""
    keep = keep or set()
    changed = 0
    src_files = {p.relative_to(src).as_posix() for p in src.rglob("*") if p.is_file() and ".git" not in p.parts}
    src_files = {rel for rel in src_files if not any(rel.startswith(k) for k in keep)}
    for rel in sorted(src_files):
        s, d = src / rel, dst / rel
        if not d.exists() or not filecmp.cmp(s, d, shallow=False):
            d.parent.mkdir(parents=True, exist_ok=True)
            # copyfile, not copy2: a changed file must get a fresh mtime or Ninja may skip it
            # (files from git archive carry the old commit time).
            shutil.copyfile(s, d)
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
    # Stage the exact committed bytes (git archive), not the working tree: on Windows checkouts
    # core.autocrlf rewrites line endings, and patches/ are made against the committed files.
    import io
    import tarfile

    # core.autocrlf=false: git archive would otherwise convert line endings too.
    archive = subprocess.run(["git", "-c", "core.autocrlf=false", "-C", str(UPSTREAM), "archive", "--format=tar",
                              lock["commit"]], capture_output=True, check=True).stdout
    with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
        tar.extractall(STAGE, filter="data")
    patches = sorted((ROOT / "patches").glob("*.patch"))
    stage_rel = STAGE.relative_to(ROOT).as_posix()
    for patch in patches:
        # work/stage lives inside this repository, and git apply resolves patch paths from the
        # repository root, so point it at the stage explicitly. --check first: fail loudly.
        cmd = ["git", "apply", "--whitespace=nowarn", f"--directory={stage_rel}", str(patch)]
        run(cmd[:2] + ["--check"] + cmd[2:], cwd=ROOT)
        run(cmd, cwd=ROOT)
    # Generated code and overrides are owned by the stage step; keep them.
    changed = sync_tree(STAGE, RUNTIME_TREE, keep={"ps2xRuntime/src/runner/", "ps2xRuntime/include/ps2_recompiled_"})
    shutil.rmtree(STAGE)
    log(f"upstream {head[:8]} + {len(patches)} patch(es) -> {RUNTIME_TREE} ({changed} file(s) changed)")


# ---------------------------------------------------------------- build helpers

# RelWithDebInfo with full inlining and AVX2. Not /GL (link-time codegen over ~14k generated
# functions is very slow) and not /fp:fast (the runtime emulates exact PS2 float semantics).
MSVC_FLAGS = "/Zi /O2 /Ob2 /Oi /DNDEBUG /arch:AVX2"


def configure(env: dict[str, str]) -> None:
    cache = BUILD / "CMakeCache.txt"
    text = cache.read_text(errors="replace") if cache.exists() else ""
    if (f"CMAKE_CXX_FLAGS_RELWITHDEBINFO:STRING={MSVC_FLAGS}" in text
            and "PS2X_ENABLE_AGRESSIVE_LOGS:BOOL=OFF" in text):
        return
    # The runtime's "aggressive" logs write several lines per frame and add a log hook to every
    # generated function; profiling showed ~20% of the game thread in NtWriteFile. Keep the normal
    # runtime log, drop the aggressive one and the per-RPC trace.
    run(["cmake", "-S", str(RUNTIME_TREE), "-B", str(BUILD), "-G", "Ninja", f"-DCMAKE_BUILD_TYPE={BUILD_TYPE}",
         "-DPS2X_BUILD_TEST=OFF", "-DPS2X_BUILD_STUDIO=OFF",
         "-DPS2X_ENABLE_AGRESSIVE_LOGS=OFF", "-DPS2X_ENABLE_IOP_RPC_TRACE=OFF",
         f"-DCMAKE_CXX_FLAGS_RELWITHDEBINFO={MSVC_FLAGS}", f"-DCMAKE_C_FLAGS_RELWITHDEBINFO={MSVC_FLAGS}"], env=env)


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
            "-postScript", "ApplyTypes.java", str(TYPES_DIR), str(SIGNATURES), str(THIS_TYPES),
            "-postScript", "ExportPS2Functions.java", str(GHIDRA_EXPORT_TOML), str(CONFIG / "funcmap.csv")]
    if getattr(args, "map", False):
        GRAPH_JSON.parent.mkdir(parents=True, exist_ok=True)
        cmd += ["-postScript", "ExportGraph.java", str(GRAPH_JSON),
                "-postScript", "DecompileAll.java", str(DECOMP_DIR)]
    run(cmd)
    filter_function_map()


def cmd_map(args: argparse.Namespace) -> None:
    """analyze plus the call graph and a fresh decompilation of every function, then status.
    work/decomp/ is overwritten in place (files are named by address), so it stays readable meanwhile."""
    cmd_analyze(argparse.Namespace(reimport=False, map=True))
    cmd_status(args)


def cmd_status(_: argparse.Namespace) -> None:
    """Rebuild config/port_status.csv, docs/PORT_STATUS.md (tools/port_status.py) and docs/map/ (tools/map_docs.py)."""
    run([sys.executable, "-I", str(ROOT / "tools/port_status.py"), "--root", str(ROOT)])
    run([sys.executable, "-I", str(ROOT / "tools/map_docs.py"), "--root", str(ROOT)])


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
    path.write_text("\n".join(kept) + "\n", encoding="utf-8", newline="\n")
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

    merge_symbols("ps2_analyzer", detected)


# Higher wins when two sources name the same address.
SYMBOL_PRIORITY = {"manual": 3, "ps2_analyzer": 2, "pointer_sweep": 1}


def merge_symbols(source: str, names: dict[int, str]) -> None:
    """Replace all rows of `source` in config/symbols.csv with `names`, keeping higher-priority rows."""
    import csv

    path = CONFIG / "symbols.csv"
    text = path.read_text().splitlines()
    header = [line for line in text if line.startswith("#")]
    rows: dict[int, list[str]] = {}
    for row in csv.reader(line for line in text if line and not line.startswith("#")):
        if row[0].lower() == "address":
            continue
        row_source = row[3] if len(row) > 3 else "manual"
        if row_source != source:
            rows[int(row[0], 16)] = row[:3] + [row_source]
    for addr, name in names.items():
        existing = rows.get(addr)
        if existing is None or SYMBOL_PRIORITY.get(existing[3], 0) < SYMBOL_PRIORITY.get(source, 0):
            rows[addr] = [f"0x{addr:08X}", name, "func", source]
    lines = header + ["address,name,kind,source"] + [",".join(rows[a]) for a in sorted(rows)]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    counts: dict[str, int] = {}
    for row in rows.values():
        counts[row[3]] = counts.get(row[3], 0) + 1
    log("config/symbols.csv: " + ", ".join(f"{n} {s}" for s, n in sorted(counts.items())))


def cmd_pointers(args: argparse.Namespace) -> None:
    """Find functions reached only through pointers (tools/find_code_pointers.py) and add them to
    config/symbols.csv so Ghidra creates them. Repeats analyze until no new targets appear."""
    import csv

    for round_no in range(1, args.rounds + 1):
        out = WORK / "pointer_candidates.csv"
        run([sys.executable, "-I", str(ROOT / "tools/find_code_pointers.py"), str(ORIGINAL / ELF_NAME),
             str(CONFIG / "funcmap.csv"), "--layout", str(CONFIG / "layout.toml"), "--write", str(out)])
        found = {int(r["address"], 16) for r in csv.DictReader(line for line in out.read_text().splitlines() if not line.startswith("#"))}
        previous = set()
        for row in csv.reader(line for line in (CONFIG / "symbols.csv").read_text().splitlines() if line and not line.startswith("#")):
            if len(row) > 3 and row[3] == "pointer_sweep":
                previous.add(int(row[0], 16))
        new = found - previous
        log(f"pointer sweep round {round_no}: {len(found)} candidate(s), {len(new)} new")
        if not new:
            break
        merge_symbols("pointer_sweep", {a: f"fn_{a:08X}" for a in previous | found})
        cmd_analyze(argparse.Namespace(reimport=False))


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


def todo_handlers() -> set[str]:
    """Runtime stub names whose handler is still TODO_NAMED in the pinned PS2Recomp runtime."""
    import re

    names: set[str] = set()
    for path in (RUNTIME_TREE / "ps2xRuntime/src/lib").rglob("*.cpp"):
        names.update(re.findall(r'TODO_NAMED\("([A-Za-z0-9_]+)"', path.read_text(errors="replace")))
    return names


def render_config() -> None:
    text = (CONFIG / "metropolismania.template.toml").read_text()
    for key, path in {"@ELF@": ORIGINAL / ELF_NAME, "@MAP@": CONFIG / "funcmap.csv", "@OUT@": GENERATED}.items():
        text = text.replace(key, path.as_posix())
    data = tomllib.loads(text)
    options = data.pop("pipeline", {})
    general = data.setdefault("general", {})
    if GHIDRA_EXPORT_TOML.exists():
        export = tomllib.loads(GHIDRA_EXPORT_TOML.read_text()).get("general", {})
        for key in ("stubs", "untracked_stubs"):
            merged = list(dict.fromkeys(general.get(key, []) + export.get(key, [])))
            general[key] = merged
    # Prefer the game's own (recompiled) SDK code over runtime handlers that are still TODO:
    # a TODO handler only logs and returns -1, while the original code at least does the real work
    # against the emulated hardware. [pipeline].keep_todo_stubs forces a binding anyway.
    # [pipeline].force_recompile (fnmatch patterns) also keeps matching SDK functions as recompiled
    # code even when the runtime has a working handler, e.g. to compare an HLE library with the original.
    import fnmatch

    todo = todo_handlers()
    keep = set(options.get("keep_todo_stubs", []))
    forced = options.get("force_recompile", [])

    def unbind(name: str) -> bool:
        if any(fnmatch.fnmatchcase(name, pattern) for pattern in forced):
            return True
        return name in todo and name not in keep

    unbound = [s for s in general.get("stubs", []) if unbind(s.split("@")[0])]
    general["stubs"] = [s for s in general.get("stubs", []) if s not in unbound]
    # ps2_recomp also binds a function to a runtime handler whenever its *name* matches one, so
    # dropping it from [general].stubs is not enough: render a copy of the function map in which
    # those functions get an "_lle" suffix (original code, low-level).
    unbound_names = {s.split("@")[0] for s in unbound}
    unbound_addrs = {int(s.split("@")[1], 16) for s in unbound if "@" in s}
    # A function the analyzer named with a leading underscore (an SDK internal) is bound by the same
    # name match without being listed in [general].stubs: _sceVu0ecossin (0x104E88, the sin/cos step inside
    # sceVu0RotMatrixX/Y/Z, which hands its result back in vf4 and vf5) was bound to the runtime's unrelated
    # public sceVu0ecossin(dst, angle), so every rotation matrix was built from stale registers.
    hidden = set()
    for line in (CONFIG / "funcmap.csv").read_text().splitlines()[1:]:
        name, start, _ = line.split(",", 2)
        if name.startswith("_") and any(fnmatch.fnmatchcase(name.lstrip("_"), pattern) for pattern in forced):
            hidden.add((name, int(start, 16)))
    unbound_addrs |= {addr for _, addr in hidden}
    rendered_map = WORK / "funcmap.rendered.csv"
    map_lines = (CONFIG / "funcmap.csv").read_text().splitlines()
    out_lines = [map_lines[0]]
    for line in map_lines[1:]:
        name, start, rest = line.split(",", 2)
        if (name in unbound_names and int(start, 16) in unbound_addrs) or (name, int(start, 16)) in hidden:
            name = f"{name}_lle"
        out_lines.append(f"{name},{start},{rest}")
    rendered_map.write_text("\n".join(out_lines) + "\n")
    general["ghidra_output"] = rendered_map.as_posix()
    general["entry_points"] = list(dict.fromkeys(general.get("entry_points", []) + [f"0x{a:08X}" for a in sorted(unbound_addrs)]))
    if unbound:
        log(f"left {len(unbound)} SDK function(s) as recompiled code (TODO handler or force_recompile): "
            + ", ".join(s.split("@")[0] for s in unbound))
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
    # ps2_recomp names files after the functions, so a rename leaves the old file behind; stale files then
    # reference functions the new header no longer declares. Start from an empty output set.
    for old in list(GENERATED.glob("*.cpp")) + list(GENERATED.glob("*.h")):
        old.unlink()
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
    # The readable port: headers regenerated from config/, sources named to sort after the generated
    # files (unity batches stay put) and built outside unity batches (patches/0131).
    run([sys.executable, "-I", str(ROOT / "tools/port_gen.py")])
    for src in (ROOT / "src/port").glob("*"):
        if src.suffix == ".cpp":
            shutil.copy2(src, staging / f"zz_port_{src.name}")
        elif src.suffix == ".h":
            shutil.copy2(src, staging / src.name)
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


BUILD_TESTS = ROOT / "build" / "ps2recomp-tests"
# Unoptimized: MSVC 14.38 hits an internal compiler error on ps2_runtime_kernel_tests.cpp at /O2.
# The tests check results, not speed (the runtime keeps exact float semantics either way).
TEST_FLAGS = "/Zi /Od /DNDEBUG"


def cmd_test(_: argparse.Namespace) -> None:
    """Build and run PS2Recomp's own test suite (ps2xTest) against the patched runtime, in a
    separate build directory so the game build keeps its settings."""
    env = msvc_env()
    if not (BUILD_TESTS / "CMakeCache.txt").exists():
        run(["cmake", "-S", str(RUNTIME_TREE), "-B", str(BUILD_TESTS), "-G", "Ninja", f"-DCMAKE_BUILD_TYPE={BUILD_TYPE}",
             "-DPS2X_BUILD_TEST=ON", "-DPS2X_BUILD_STUDIO=OFF",
             "-DPS2X_ENABLE_AGRESSIVE_LOGS=OFF", "-DPS2X_ENABLE_IOP_RPC_TRACE=OFF",
             f"-DCMAKE_CXX_FLAGS_RELWITHDEBINFO={TEST_FLAGS}", f"-DCMAKE_C_FLAGS_RELWITHDEBINFO={TEST_FLAGS}"], env=env)
    run(["cmake", "--build", str(BUILD_TESTS), "--target", "ps2x_tests"], env=env)
    exe = next(BUILD_TESTS.rglob("ps2x_tests.exe"))
    LOGS.mkdir(exist_ok=True)
    out = LOGS / f"tests-{dt.datetime.now():%Y%m%d-%H%M%S}.log"
    # The GS tests read the VRAM buffer directly right after drawing, which only works when the GS
    # rasterizes synchronously (the runtime itself always goes through the backend's sync points).
    test_env = dict(env, PS2X_GS_THREAD="0")
    with out.open("w", encoding="utf-8", errors="replace") as fh:
        code = subprocess.run([str(exe)], cwd=exe.parent, stdout=fh, stderr=subprocess.STDOUT, env=test_env).returncode
    log(f"ps2x_tests exit code {code}; log {out}")


def cmd_run(args: argparse.Namespace) -> None:
    """Run the game in a normal, visible window, capture its log, optionally take screenshots
    (--shots) and feed scripted pad input (--pad, see patches/0002-pad-scripted-input.patch)."""
    import time

    LOGS.mkdir(exist_ok=True)
    stamp = f"{dt.datetime.now():%Y%m%d-%H%M%S}"
    log_path = LOGS / f"run-{stamp}.log"
    shot_dir = LOGS / "shots" / stamp
    exe = tool("ps2EntryRunner")
    env = dict(os.environ)
    # work/local.json "game_window_pos": [x, y] opens the game window there, unfocused (PS2X_WINDOW_POS).
    window_pos = settings().get("game_window_pos")
    if window_pos and "PS2X_WINDOW_POS" not in env:
        env["PS2X_WINDOW_POS"] = f"{int(window_pos[0])},{int(window_pos[1])}"
    # work/local.json "mute_audio": true keeps the SPU2 output off (PS2X_SPU2=0) for runs and captures.
    if settings().get("mute_audio") and "PS2X_SPU2" not in env:
        env["PS2X_SPU2"] = "0"
    if args.pad:
        env["PS2X_PAD_SCRIPT"] = str(Path(args.pad).resolve())
    for item in args.env or []:
        key, _, value = item.partition("=")
        env[key] = value
    log(f"running {exe.name} for up to {args.timeout}s, log: {log_path}")
    with log_path.open("w", encoding="utf-8", errors="replace") as fh:
        proc = subprocess.Popen([str(exe), str(ORIGINAL / ELF_NAME)], stdout=fh, stderr=subprocess.STDOUT,
                                cwd=exe.parent, env=env)
        start = time.monotonic()
        next_shot = args.shot_every
        shots = 0
        while proc.poll() is None:
            elapsed = time.monotonic() - start
            if elapsed >= args.timeout:
                break
            if shots < args.shots and elapsed >= next_shot:
                shots += 1
                next_shot += args.shot_every
                subprocess.run([sys.executable, "-I", str(ROOT / "tools/screenshot.py"),
                                str(shot_dir / f"t{int(elapsed):03d}s.png"), "--pid", str(proc.pid)],
                               capture_output=True)
            time.sleep(0.25)
        if proc.poll() is None:
            proc.kill()
        proc.wait()
    log(f"exit code {proc.returncode}; log {log_path}")
    if shots:
        log(f"screenshots: {shot_dir}")


DIFFTEST = WORK / "difftest"
VU1_RECORDS = WORK / "vu1_aot" / "records"


def cmd_capture(args: argparse.Namespace) -> None:
    """A visible run to the city (a new game, or the newest save with --autoplay load) that records the
    entry state of chosen functions (MMPC_CAPTURE) into work/difftest/ for pipeline.py difftest.
    Ported functions stay unreplaced during a capture."""
    args.env = [f"MMPC_CAPTURE={args.functions}", f"MMPC_CAPTURE_DIR={DIFFTEST}", f"MMPC_CAPTURE_MAX={args.max}",
                f"MMPC_CAPTURE_EVERY={args.every}", f"MMPC_AUTOPLAY={args.autoplay}", "MMPC_SKIP_MOVIES=1"] + (args.env or [])
    args.pad = None
    cmd_run(args)


def cmd_difftest(args: argparse.Namespace) -> None:
    """Replay the records in work/difftest/ through each ported function and its generated original,
    headless (src/port/difftest.cpp). Exits non-zero when any record differs. --fuzz N also replays each
    record N times with mutated float and integer arguments for the --fuzz-functions named. --changes lists
    what each record's original run changed, for the functions named."""
    LOGS.mkdir(exist_ok=True)
    log_path = LOGS / f"difftest-{dt.datetime.now():%Y%m%d-%H%M%S}.log"
    exe = tool("ps2EntryRunner")
    env = dict(os.environ, MMPC_DIFFTEST=str(DIFFTEST), PS2X_HEADLESS="1")
    if args.fuzz:
        env["MMPC_DIFFTEST_FUZZ"] = str(args.fuzz)
        env["MMPC_DIFFTEST_FUZZ_FUNCTIONS"] = args.fuzz_functions or "all"
    if args.changes:
        env["MMPC_DIFFTEST_CHANGES"] = args.changes
    if args.only:
        env["MMPC_DIFFTEST_ONLY"] = args.only
    env.pop("PS2X_WINDOW_POS", None)
    with log_path.open("w", encoding="utf-8", errors="replace") as fh:
        code = subprocess.run([str(exe), str(ORIGINAL / ELF_NAME)], stdout=fh, stderr=subprocess.STDOUT, cwd=exe.parent,
                              env=env, timeout=1800).returncode
    lines = [l for l in log_path.read_text(encoding="utf-8", errors="replace").splitlines()
             if l.startswith(("[difftest]", "    ", "[port]"))]
    print("\n".join(lines))
    log(f"difftest exit code {code}; log {log_path}")
    if code != 0:
        sys.exit(code)


def cmd_vu1_capture(args: argparse.Namespace) -> None:
    """A visible run to the city (autoplay) that writes whole VU1 program runs (MSCAL) to work/vu1_aot/records/
    (PS2X_VU1_CAPTURE, patches/0132): up to --max files for each distinct (code, start PC)."""
    args.env = [f"PS2X_VU1_CAPTURE={VU1_RECORDS}", f"PS2X_VU1_CAPTURE_MAX={args.max}", f"MMPC_AUTOPLAY={args.autoplay}",
                "MMPC_SKIP_MOVIES=1"] + (args.env or [])
    args.pad = None
    cmd_run(args)


def cmd_vu1_test(args: argparse.Namespace) -> None:
    """Replay the VU1 runs in work/vu1_aot/records/ headless (src/port/vu1test.cpp): each through the interpreter,
    compared with what the game run recorded (state, data memory, cycles, XGKICK packets). Exits non-zero on any
    difference. Also prints the interpreter's speed on these runs."""
    LOGS.mkdir(exist_ok=True)
    log_path = LOGS / f"vu1test-{dt.datetime.now():%Y%m%d-%H%M%S}.log"
    exe = tool("ps2EntryRunner")
    env = dict(os.environ, MMPC_VU1_TEST=str(VU1_RECORDS), PS2X_HEADLESS="1")
    env.setdefault("MMPC_VU1_TEST_REPEAT", str(args.repeat))
    if args.aot:
        env["MMPC_VU1_AOT"] = args.aot
    if args.fuzz:
        env["MMPC_VU1_FUZZ"] = str(args.fuzz)
    env.pop("PS2X_WINDOW_POS", None)
    with log_path.open("w", encoding="utf-8", errors="replace") as fh:
        code = subprocess.run([str(exe), str(ORIGINAL / ELF_NAME)], stdout=fh, stderr=subprocess.STDOUT, cwd=exe.parent,
                              env=env, timeout=1800).returncode
    lines = [l for l in log_path.read_text(encoding="utf-8", errors="replace").splitlines()
             if l.startswith(("[vu1test]", "[vu1aot", "    "))]
    print("\n".join(lines))
    log(f"vu1-test exit code {code}; log {log_path}")
    if code != 0:
        sys.exit(code)


def cmd_all(args: argparse.Namespace) -> None:
    for step in (cmd_bootstrap, cmd_tools, cmd_extract, cmd_symbols, cmd_analyze, cmd_pointers,
                 cmd_recompile, cmd_stage, cmd_build):
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
    sub.add_parser("map", help="analyze + call graph + decompile all + status").set_defaults(func=cmd_map)
    sub.add_parser("status", help="rebuild config/port_status.csv, docs/PORT_STATUS.md and docs/map/").set_defaults(func=cmd_status)
    p = sub.add_parser("pointers", help="add functions reached only through pointers, re-analyze until stable")
    p.add_argument("--rounds", type=int, default=4)
    p.set_defaults(func=cmd_pointers)
    p = sub.add_parser("script", help="run a Ghidra script from tools/ghidra on the project")
    p.add_argument("--write", action="store_true", help="save changes to the Ghidra project")
    p.add_argument("name")
    p.add_argument("script_args", nargs="*")
    p.set_defaults(func=cmd_script)
    sub.add_parser("recompile").set_defaults(func=cmd_recompile)
    sub.add_parser("stage").set_defaults(func=cmd_stage)
    sub.add_parser("build").set_defaults(func=cmd_build)
    sub.add_parser("test", help="build and run the PS2Recomp test suite").set_defaults(func=cmd_test)
    p = sub.add_parser("run")
    p.add_argument("--timeout", type=int, default=20)
    p.add_argument("--shots", type=int, default=0, help="number of window screenshots to take")
    p.add_argument("--shot-every", type=float, default=5.0, help="seconds between screenshots")
    p.add_argument("--pad", help="scripted pad input file (PS2X_PAD_SCRIPT)")
    p.add_argument("--env", action="append", help="extra environment variable KEY=VALUE (repeatable), e.g. MMPC_TRACE=1")
    p.set_defaults(func=cmd_run)
    p = sub.add_parser("capture", help="visible run that records function calls for difftest")
    p.add_argument("--functions", default="ported", help='comma-separated names or 0xADDRs, or "ported" (default)')
    p.add_argument("--max", type=int, default=8, help="records per function")
    p.add_argument("--every", default="1", help='record every Nth call, or "exp" for calls 1, 8, 64, 512 ... (spreads records over the run)')
    p.add_argument("--autoplay", default="city", choices=["city", "load", "explore"],
                   help="city: a new game; load: the newest save in original/mc0 (tools/mc_extract.py); "
                        "explore: that save, then random play in the city")
    p.add_argument("--timeout", type=int, default=200)
    p.add_argument("--shots", type=int, default=0)
    p.add_argument("--shot-every", type=float, default=45.0)
    p.add_argument("--env", action="append", help="extra environment variable KEY=VALUE (repeatable)")
    p.set_defaults(func=cmd_capture)
    p = sub.add_parser("difftest", help="replay work/difftest records through ported and generated code")
    p.add_argument("--fuzz", type=int, default=0, help="also replay each record N times with mutated arguments")
    p.add_argument("--fuzz-functions", default="", help="comma-separated names to fuzz (default: all)")
    p.add_argument("--changes", default="", help="comma-separated names (or all) whose records list the RDRAM ranges "
                   "the original run changed")
    p.add_argument("--only", default="", help="comma-separated names: replay only these (results.json is left as it was)")
    p.set_defaults(func=cmd_difftest)
    p = sub.add_parser("vu1-capture", help="visible run that records VU1 program runs for plan P4")
    p.add_argument("--max", type=int, default=12, help="files per distinct (code, start PC)")
    p.add_argument("--autoplay", default="explore", choices=["city", "load", "explore"],
                   help="city: a new game; load: the newest save in original/mc0; explore: that save, then random play")
    p.add_argument("--timeout", type=int, default=200)
    p.add_argument("--shots", type=int, default=0)
    p.add_argument("--shot-every", type=float, default=45.0)
    p.add_argument("--env", action="append", help="extra environment variable KEY=VALUE (repeatable)")
    p.set_defaults(func=cmd_vu1_capture)
    p = sub.add_parser("vu1-test", help="replay work/vu1_aot/records through the VU1 interpreter and compare")
    p.add_argument("--repeat", type=int, default=20, help="runs of every record (only the first is compared), for a steadier speed figure")
    p.add_argument("--aot", choices=["run", "timing", "fuzz"],
                   help="run: replay through the pre-timed programs; timing: check their schedule against the interpreter's pair trace; "
                        "fuzz: random pairs through the native ops and the interpreter's own functions")
    p.add_argument("--fuzz", type=int, default=0, help="cases for the fuzz checks (default 200,000)")
    p.set_defaults(func=cmd_vu1_test)
    p = sub.add_parser("all")
    p.add_argument("image", nargs="?")
    p.add_argument("--reimport", action="store_true")
    p.add_argument("--rounds", type=int, default=4)
    p.set_defaults(func=cmd_all)
    args = parser.parse_args()
    args.func(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
