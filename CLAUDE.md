@AGENTS.md

## Claude Code notes

- Scope tracker: `LOCAL-AI/GOALS.md` (git-ignored). Read it before non-trivial work and update it when finishing.
- The `ps2-recomp-Agent-SKILL` skill (in `.claude/skills/`, git-ignored, pinned at a1cafc23) is installed unmodified. Where it disagrees with this repo, this file wins:
  - Its state file `PS2_PROJECT_STATE.md` lives at `LOCAL-AI/PS2_PROJECT_STATE.md`, not in the repo root.
  - Its tool names come from another agent: `find_by_name` means Glob, `grep_search` means Grep, `view_file` means Read, `list_dir` means a directory listing, `command_status` means reading a background task's output.
  - Never run its `scripts/install_ghydramcp.py` (unpinned download that edits MCP config). This project uses LaurieWired GhidraMCP 2.0 instead of GhydraMCP.
  - Its "never edit runner/*.cpp" rule maps to: never edit `generated/` or `work/PS2Recomp/ps2xRuntime/src/runner/`. Fixes go in `config/`, `src/overrides/` or `patches/`.
  - Its build rules map to `python tools/pipeline.py build` (the pipeline loads the vcvars64 environment itself).
  - Never copy anything from the skill's `resources/pdfs` or `resources/images` into the repo.
- Ghidra 12.1.4 location comes from `work/local.json` (`ghidra_install_dir`, git-ignored). It needs JDK 21. The headless GhidraMCP server opens `work/ghidra`; it cannot run while `pipeline.py analyze` holds the project lock, and the other way round.
