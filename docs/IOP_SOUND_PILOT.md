# Optional native IOP MIDI tempo pilot

This contribution implements the two absolute-tempo services used by YYSOUND.
It is a first, independently expressed replacement of a small MODMIDI surface,
not a replacement for the whole MIDI sequencer or Sony's other sound modules.
All other module services continue to use the existing translated code.

The pilot is disabled by default. It adds four files under `src/port/iop/`,
one header include and one install call in `iop_port.cpp`, and a standalone
differential test. The existing staging script already picks up those files.
It does not change the AOT translator, runtime patches, YYSOUND, game logic,
renderer, save system or the existing capture files.

## Integration and rollback

Apply the contribution commit(s) on the desired integration branch. Applying
them alone does not enable the replacements. To enable them for a scheduled
validation run, set `MMPC_IOP_MIDI_TEMPO=1` before launching the runner. No other
value enables the pilot. Remove the variable to restore the previous behavior;
the global `PS2X_IOP_NATIVE=0` switch also disables these natives.

Build it with the normal `pipeline.py stage` and `pipeline.py build`.

The registry uses module name `MODMIDI.IRX`, relocated base `0x49900`, image size
15,348 and relocated-image FNV-1a `3A298BC1E83A4D7F`. The existing runtime's
module fingerprint gate must match before it arms these replacements. Different
module revisions or relocation addresses are unsupported and must not reuse
these constants without a new reference comparison.

Before enabling this in a normal build, perform an attended game pass at a safe
checkpoint with `PS2X_IOP_VERIFY=1`. For a focused verifier window, use
`PS2X_IOP_VERIFY_ALL=Midi_GetAbsoluteTempo,Midi_SetAbsoluteTempo` (with the time
window settings documented by the existing verifier). Check new-game/load,
music start/stop and a tempo-changing path. Compare with the switch off.

The verifier only checks a replacement that is called at native depth 0, and
YYSOUND's own native port is what calls the getter, so with every replacement on
the tempo calls run unverified. Verify them with
`PS2X_IOP_NATIVE=Midi_GetAbsoluteTempo,Midi_SetAbsoluteTempo`, which leaves
YYSOUND as its original code. Checked that way on 2026-11-11 (explore autoplay
from a save): 9 getter calls, all verified, no mismatches, 50 instructions each
like the original. The setter was not called in that run.

## Inventory of the direct YYSOUND sound-library surface

Source: `src/port/iop/yysound_imports.h`, its existing function metadata, and the
local modules' import/export tables. This is a direct-call inventory; library
internals introduce further dependencies.

| Module | Imported ordinals | Behavior | This contribution |
|---|---|---|---|
| MODMIDI | 4, 5, 6 | Initialize, tick, load | Original |
| MODMIDI | 14, 15, 16 | Select sequence, play/stop, seek | Original |
| MODMIDI | 18 | Channel volume | Original; updates sequencer state and emits control messages |
| MODMIDI | 19, 20 | Set/get absolute tempo | Optional native replacement |
| MODHSYN | 4, 5, 6, 7 | Initialize, tick, load bank, transfer voices | Original |
| MODHSYN | 10, 14, 15, 19, 22 | Volume, reset controllers, voice-state buffer, voice limit, voice lookup | Original |
| MODSEIN | 4, 5, 7 | Initialize, tick, enqueue sound-effect message | Original |
| LIBSD | 4, 5, 9, 11, 13, 23 | Initialize, parameter/address/core attributes, note-to-pitch, effect attributes | Original |

That is 27 direct imports, of which two are covered here. SDRDRV is a potential
transitive dependency, not a direct import in this list. PADMAN, SIO2MAN, MCMAN
and MCSERV are outside this contribution.

The existing game driver calls the getter while loading a sequence to remember
its default tempo. Its tempo command calls the setter with the default or the
requested adjusted tempo. Those call sites remain unchanged.

## Behavioral contract

The shared context points to a group of paired buffer descriptors. Port `p`
uses descriptor `2*p+1` as its sequencer environment. The environment's public
layout is already present in `config/iop/yysound_types.h`; the small private
layout facts needed here remain confined to `modmidi_tempo.cpp`.

| Entry | Offset | Contract |
|---|---|---|
| Get absolute tempo | `0x1FDC` | Return zero for a null context, missing port, no selected sequence, or zero stored period. Otherwise divide 60,000,000 by the stored microsecond period and clamp the result to 20..255 BPM. |
| Set absolute tempo | `0x1F78` | Return -1 for a null context or missing port. Use the low byte of the requested tempo. Store `60,000,000 / tempo` as the quarter-note period and update the fixed-point tick step. Return zero on success. |

The tick-step calculation has two 32-bit shift/divide stages. Widening the
whole expression to 64 bits changes observable overflow behavior. The setter
does not clamp low nonzero input to 20; only the getter clamps its report.
Null context and port bounds are handled before validating the requested tempo.

Zero divisors, misaligned/non-RAM accesses, invalid stacks and descriptor/state
accesses overlapping the original's stack frame defer to the original function.
The native performs no RAM writes before deciding to defer. Both fingerprinted
entries begin with a 32-byte stack reservation: the adapter performs that one
instruction and resumes at entry+4. This avoids recursively re-entering the
replacement and preserves the original's exceptions and partial writes. It
does not change the guest's erroneous input into a successful result.

Costs are charged per path in the interpreter's instruction-count model. They
are not a claim about physical PS2 cycle timing. The normal native return uses
the same ABI-preserved registers as the existing IOP verifier; scratch registers
and dead stack bytes are not part of the native service's contract.

## Standalone verification

From the contribution checkout on Windows with MSVC:

```powershell
python tools/test_iop_midi_tempo.py --reference-root 'PATH TO EXISTING BUILT CHECKOUT'
```

In an integrated, already built checkout, omit `--reference-root`. The reference
checkout supplies the user's `original/SYS/MODMIDI.IRX` and its existing patched
`ps2_iop.lib` plus headers. The tool copies these to this checkout's ignored
`work/iop_midi_tempo_test/`, checking each copy's hash, then builds only the small
native services, adapter and harness. It also compiles the registration file
against the real IOP API. It never launches or builds the game, writes into the
reference checkout, or modifies captures/saves.

The harness executes the original module in the existing R3000A interpreter and
the actual native entry points from identical initial RAM and registers. It
compares return values, ABI-preserved registers, persistent RAM, control flow
and instruction counts. Fallback cases additionally compare scratch registers,
HI/LO, COP0 and the full RAM including stack. For successful native calls only,
the existing verifier's dead-stack range is excluded. RAM ownership bookkeeping
is not compared, consistent with the current direct-RAM native interface.

Coverage includes all 256 low-byte tempo inputs with high-bit variants, getter
clamp boundaries, zero/unselected state, invalid ports, index wraparound,
KSEG0/KSEG1 aliases, unaligned pointers, stack aliasing, end-of-RAM accesses,
division exceptions, fixed-point overflow/divisor boundaries, and 4,096 seeded
random state/input cases (both entries per case). This is a synthetic contract
test, not evidence that every state occurs during gameplay.

The tool writes `work/iop_midi_tempo_test/result.json` with the outcome and input
hashes. No original binary, relocated image, capture or translated module is
included in the commit. The test needs no Capstone or Ghidra installation.

Validation on 2026-11-10: **9,432 calls passed**, including 40 original-code
fallbacks and 36 original exceptions. Persistent RAM, ABI registers and
instruction counts agreed in every case. The three service/adapter translation
units and the harness compiled under MSVC with `/W4 /WX` (the registration unit uses
the standard CRT deprecation-warning suppression for `getenv`).

## Provenance and limits

The new implementation expresses the measured interface, integer arithmetic
and memory layout in new C++. Local original instructions were inspected to
identify the contract and fallback boundary; no Sony SDK source/manual text,
raw disassembly or decompiler body is included. This is not a separated-team
clean-room process and is not a legal clearance of distribution.

No complete runner build, live music comparison, performance measurement or
runtime loader/fingerprint activation test was performed for this contribution.
The standalone check uses the project's interpreter, so it is not independent
proof of that interpreter's accuracy on real hardware. The opt-in default stays
in place until the integration checks above pass. Existing zero-interpretation
and YYSOUND verification claims are not expanded by this pilot.

## Next independent slice

After integration validates the two tempo entries, inventory the MIDI output
message queue before replacing channel volume. The original volume operation
both updates channel state and emits control messages, so a field-only setter
would be incomplete. Preserve the original implementation as the comparison
reference until the queue contract and timing are verified. Synthesis and
hardware-effect record/replay remain further work; this pilot does not depend
on taking ownership of the renderer, save drift or native-control roadmap.
