"""Build/test the optional MIDI tempo pilot without building or launching the game.

Requires an existing patched PS2Recomp build for its IOP interpreter library.
--reference-root may name another checkout; it is read only. Runtime inputs are
snapshotted into this checkout's ignored work/iop_midi_tempo_test directory.
Only the two native services and their adapter/test are linked into the harness.
"""
import argparse
import hashlib
import json
import shutil
import struct
import subprocess
from pathlib import Path

import irx_inspect
import pipeline

ROOT = Path(__file__).resolve().parent.parent
WORK = ROOT / "work/iop_midi_tempo_test"
MODULE_BASE = 0x49900
MODULE_SIZE = 15348
MODULE_HASH = 0x3A298BC1E83A4D7F


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command, env):
    completed = subprocess.run(command, cwd=WORK, env=env, capture_output=True, text=True)
    if completed.stdout:
        print(completed.stdout, end="", flush=True)
    if completed.stderr:
        print(completed.stderr, end="", flush=True)
    completed.check_returncode()
    return completed.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-root", type=Path, default=ROOT)
    args = parser.parse_args()
    reference = args.reference_root.resolve()
    module = reference / "original/SYS/MODMIDI.IRX"
    runtime = reference / "work/PS2Recomp/ps2xIOP"
    library = reference / "build/ps2recomp/ps2xIOP/ps2_iop.lib"
    if not module.is_file() or not library.is_file() or not (runtime / "src").is_dir():
        parser.error("reference checkout needs original/SYS/MODMIDI.IRX and an existing patched IOP build")
    WORK.mkdir(parents=True, exist_ok=True)
    # A failing retry must not leave an old success receipt looking current.
    receipt = WORK / "result.json"
    receipt.write_text(json.dumps({"status": "running"}) + "\n", encoding="utf-8")
    snapshot = WORK / "reference"
    snapshot.mkdir(exist_ok=True)
    inputs = {}
    for source, target in [(module, snapshot / "MODMIDI.IRX"), (library, snapshot / "ps2_iop.lib")]:
        before = sha256(source)
        shutil.copyfile(source, target)
        if sha256(source) != before or sha256(target) != before:
            raise RuntimeError(f"reference changed while copying: {source}")
        inputs[str(source)] = before
    for folder in ["include", "src"]:
        # Only headers are required; no runtime implementation is copied into the contribution.
        for source in (runtime / folder).rglob("*.h"):
            target = snapshot / folder / source.relative_to(runtime / folder)
            target.parent.mkdir(parents=True, exist_ok=True)
            before = sha256(source)
            shutil.copyfile(source, target)
            if sha256(source) != before or sha256(target) != before:
                raise RuntimeError(f"reference header changed while copying: {source}")
            inputs[str(source)] = before

    irx = irx_inspect.Irx(snapshot / "MODMIDI.IRX")
    image, unresolved = irx_inspect.relocated_image(irx, MODULE_BASE)
    if unresolved or irx_inspect.fnv1a64(image[:MODULE_SIZE]) != MODULE_HASH:
        raise RuntimeError("MODMIDI fingerprint mismatch; this pilot only supports the recorded USA-disc module")
    for entry in [0x1F78, 0x1FDC]:
        if struct.unpack_from("<I", image, entry)[0] != 0x27BDFFE0:
            raise RuntimeError("fallback stack-prologue contract changed")
    image_path = WORK / "modmidi-relocated.bin"
    image_path.write_bytes(image)

    env = pipeline.msvc_env()
    # vcvars may emit Path while the parent environment had PATH. Normalize the
    # case-insensitive Windows keys, keeping the last (vcvars) value.
    env = {key.upper(): value for key, value in env.items()}
    compiler = shutil.which("cl", path=env["PATH"])
    linker = shutil.which("link", path=env["PATH"])
    if compiler is None or linker is None:
        raise RuntimeError("MSVC compiler/linker not found in the vcvars environment")
    flags = ["/nologo", "/std:c++20", "/EHsc", "/O2", "/MD", "/W4", "/WX",
             f"/I{ROOT / 'src/port/iop'}", f"/I{snapshot / 'include'}", f"/I{snapshot / 'src'}"]
    objects = []
    for source in [ROOT / "src/port/iop/modmidi_tempo.cpp",
                   ROOT / "src/port/iop/modmidi_tempo_native.cpp",
                   ROOT / "tools/iop_midi_tempo_test.cpp"]:
        obj = WORK / f"{source.stem}.obj"
        run([compiler, *flags, "/c", str(source), f"/Fo{obj}"], env)
        objects.append(obj)
        inputs[str(source)] = sha256(source)
    # Check the actual optional registration adapter against the runtime API too.
    installer = ROOT / "src/port/iop/modmidi_tempo_install.cpp"
    run([compiler, *flags, "/D_CRT_SECURE_NO_WARNINGS", "/c", str(installer), f"/Fo{WORK / 'installer.obj'}"], env)
    inputs[str(installer)] = sha256(installer)
    executable = WORK / "iop_midi_tempo_test.exe"
    run([linker, "/nologo", *map(str, objects), str(snapshot / "ps2_iop.lib"), f"/OUT:{executable}"], env)
    output = run([str(executable), str(image_path)], env)
    inputs[str(ROOT / "src/port/iop/modmidi_tempo.h")] = sha256(ROOT / "src/port/iop/modmidi_tempo.h")
    receipt.write_text(json.dumps({"status": "passed", "summary": output.strip(), "sha256": inputs,
                                   "limits": "Standalone interpreter comparison; no game run or full integration build."}, indent=2) + "\n",
                       encoding="utf-8")
    print(f"Receipt: {receipt}")


if __name__ == "__main__":
    main()
