// Exports the program's call graph and data references as JSON, one record per function:
//   entry, end, size, name, prototype, instructions
//   callees      direct calls and tail jumps {entry: count}; callers is the reverse
//   indirect_calls  calls through a register (CALLIND in the decompiled p-code)
//   globals      memory the function reads or writes directly {address: r|w|rw}
//   strings      string constants it uses {address: text}
//   func_refs    functions whose address it takes (callbacks, vtables) {entry: name}; taken_by is the reverse
//   data_refs    other addresses it takes {address: 1}
// Calls come from Ghidra's references. Everything else comes from the decompiler's p-code, which
// resolves lui/addiu pairs and gp-relative accesses that the stored references miss.
// Read-only. Script args: <out.json>
// Used by tools/pipeline.py map; the inventory (pipeline.py status) and the mapping work read it.
// @category PS2Recomp

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.pcode.HighFunction;
import ghidra.program.model.pcode.PcodeOp;
import ghidra.program.model.pcode.PcodeOpAST;
import ghidra.program.model.pcode.Varnode;
import ghidra.program.model.symbol.Reference;

import java.io.PrintWriter;
import java.util.Iterator;
import java.util.Map;
import java.util.TreeMap;
import java.util.TreeSet;

public class ExportGraph extends GhidraScript {

    private Memory memory;
    private AddressSpace ram;

    private static String hex(long a) {
        return String.format("0x%08X", a);
    }

    private static String json(String s) {
        StringBuilder b = new StringBuilder("\"");
        for (char c : s.toCharArray()) {
            switch (c) {
                case '"': b.append("\\\""); break;
                case '\\': b.append("\\\\"); break;
                case '\n': b.append("\\n"); break;
                case '\r': b.append("\\r"); break;
                case '\t': b.append("\\t"); break;
                default:
                    if (c < 0x20 || c > 0x7E) {
                        b.append(String.format("\\u%04x", (int) c));
                    } else {
                        b.append(c);
                    }
            }
        }
        return b.append('"').toString();
    }

    private static void object(StringBuilder r, String key, Map<String, String> map, boolean quoteValues) {
        r.append(",\"").append(key).append("\":{");
        boolean first = true;
        for (Map.Entry<String, String> e : map.entrySet()) {
            r.append(first ? "" : ",").append(json(e.getKey())).append(':')
                .append(quoteValues ? json(e.getValue()) : e.getValue());
            first = false;
        }
        r.append('}');
    }

    // A defined string, or at least 3 printable ASCII bytes followed by a NUL.
    private String stringAt(Address to) {
        Data data = getDataAt(to);
        if (data != null && StringDataInstance.isString(data)) {
            String value = StringDataInstance.getStringDataInstance(data).getStringValue();
            if (value != null) {
                return value.length() > 120 ? value.substring(0, 120) + "..." : value;
            }
        }
        StringBuilder s = new StringBuilder();
        try {
            for (int i = 0; i < 121; i++) {
                int c = memory.getByte(to.add(i)) & 0xFF;
                if (c == 0) {
                    return s.length() >= 3 ? s.toString() : null;
                }
                if ((c < 0x20 || c > 0x7E) && c != '\n' && c != '\t' && c != '\r') {
                    return null;
                }
                s.append((char) c);
            }
        } catch (Exception e) {
            return null;
        }
        return s.substring(0, 120) + "...";
    }

    // Any address in the EE's 32 MB of RAM above the kernel area. Not limited to Ghidra's memory
    // blocks: the ELF's uninitialized data (0x1BDA00 up) has none, and most globals live there.
    private Address ramAddress(long offset) {
        if (offset < 0x00100000L || offset >= 0x02000000L) {
            return null;
        }
        return ram.getAddress(offset);
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            printerr("ExportGraph: usage: <out.json>");
            return;
        }
        memory = currentProgram.getMemory();
        ram = currentProgram.getAddressFactory().getDefaultAddressSpace();

        DecompInterface decompiler = new DecompInterface();
        decompiler.setOptions(new DecompileOptions());
        decompiler.toggleSyntaxTree(true);
        decompiler.toggleCCode(false);
        decompiler.setSimplificationStyle("decompile");
        decompiler.openProgram(currentProgram);

        Map<Long, TreeSet<String>> callers = new TreeMap<>();
        Map<Long, TreeSet<String>> takenBy = new TreeMap<>();
        Map<Long, String> bodies = new TreeMap<>();
        int failed = 0;

        try {
            for (Function func : currentProgram.getFunctionManager().getFunctions(true)) {
                if (monitor.isCancelled()) {
                    break;
                }
                long entry = func.getEntryPoint().getOffset();
                TreeMap<String, String> callees = new TreeMap<>();
                TreeMap<String, String> globals = new TreeMap<>();
                TreeMap<String, String> strings = new TreeMap<>();
                TreeMap<String, String> funcRefs = new TreeMap<>();
                TreeMap<String, String> dataRefs = new TreeMap<>();
                int instructions = 0;
                int indirect = 0;

                InstructionIterator it = currentProgram.getListing().getInstructions(func.getBody(), true);
                while (it.hasNext()) {
                    Instruction insn = it.next();
                    instructions++;
                    for (Reference ref : insn.getReferencesFrom()) {
                        // Calls, and tail jumps (j) to another function's entry, which hand that function the
                        // caller's return: both make it a callee (blocking flags follow callees).
                        boolean call = ref.getReferenceType().isCall();
                        boolean tailJump = ref.getReferenceType().isJump() && !func.getBody().contains(ref.getToAddress());
                        if (!(call || tailJump) || !ref.getToAddress().isMemoryAddress()) {
                            continue;
                        }
                        Function callee = getFunctionAt(ref.getToAddress());
                        if (callee != null && (call || callee != func)) {
                            long to = ref.getToAddress().getOffset();
                            callees.merge(hex(to), "1", (a, b) -> Integer.toString(Integer.parseInt(a) + 1));
                            callers.computeIfAbsent(to, k -> new TreeSet<>()).add(hex(entry));
                        }
                    }
                }

                DecompileResults results = decompiler.decompileFunction(func, 120, monitor);
                HighFunction high = results.decompileCompleted() ? results.getHighFunction() : null;
                if (high == null) {
                    failed++;
                } else {
                    Iterator<PcodeOpAST> ops = high.getPcodeOps();
                    while (ops.hasNext()) {
                        PcodeOpAST op = ops.next();
                        int opcode = op.getOpcode();
                        if (opcode == PcodeOp.CALLIND) {
                            indirect++;
                        }
                        // SSA bookkeeping (what a call may change, merges at joins), not accesses.
                        if (opcode == PcodeOp.INDIRECT || opcode == PcodeOp.MULTIEQUAL) {
                            continue;
                        }
                        Varnode out = op.getOutput();
                        if (out != null && out.isAddress() && ramAddress(out.getOffset()) != null) {
                            globals.merge(hex(out.getOffset()), "w", (a, b) -> a.equals(b) ? a : "rw");
                        }
                        for (int i = 0; i < op.getNumInputs(); i++) {
                            // Skip the call target itself; calls are taken from the references above.
                            if (i == 0 && (opcode == PcodeOp.CALL || opcode == PcodeOp.BRANCH || opcode == PcodeOp.CBRANCH)) {
                                continue;
                            }
                            Varnode in = op.getInput(i);
                            if (in.isAddress()) {
                                if (ramAddress(in.getOffset()) != null) {
                                    globals.merge(hex(in.getOffset()), "r", (a, b) -> a.equals(b) ? a : "rw");
                                }
                                continue;
                            }
                            if (!in.isConstant()) {
                                continue;
                            }
                            long value = in.getOffset() & 0xFFFFFFFFL;
                            Address target = ramAddress(value);
                            if (target == null) {
                                continue;
                            }
                            if ((opcode == PcodeOp.LOAD || opcode == PcodeOp.STORE) && i == 1) {
                                globals.merge(hex(value), opcode == PcodeOp.LOAD ? "r" : "w",
                                    (a, b) -> a.equals(b) ? a : "rw");
                                continue;
                            }
                            Function f = getFunctionAt(target);
                            if (f != null) {
                                funcRefs.put(hex(value), f.getName());
                                takenBy.computeIfAbsent(value, k -> new TreeSet<>()).add(hex(entry));
                                continue;
                            }
                            String s = stringAt(target);
                            if (s != null) {
                                strings.put(hex(value), s);
                            } else if (getFunctionContaining(target) == null) {
                                dataRefs.put(hex(value), "1");
                            }
                        }
                    }
                }

                StringBuilder r = new StringBuilder();
                r.append("{\"entry\":").append(json(hex(entry)));
                r.append(",\"end\":").append(json(hex(func.getBody().getMaxAddress().getOffset())));
                r.append(",\"size\":").append(func.getBody().getNumAddresses());
                r.append(",\"name\":").append(json(func.getName()));
                r.append(",\"prototype\":").append(json(func.getSignature().getPrototypeString()));
                r.append(",\"instructions\":").append(instructions);
                r.append(",\"indirect_calls\":").append(indirect);
                object(r, "callees", callees, false);
                object(r, "globals", globals, true);
                object(r, "strings", strings, true);
                object(r, "func_refs", funcRefs, true);
                object(r, "data_refs", dataRefs, false);
                bodies.put(entry, r.toString());
            }
        } finally {
            decompiler.dispose();
        }

        try (PrintWriter out = new PrintWriter(args[0], "UTF-8")) {
            out.println("{\"program\":" + json(currentProgram.getName()) + ",\"functions\":[");
            boolean first = true;
            for (Map.Entry<Long, String> e : bodies.entrySet()) {
                StringBuilder r = new StringBuilder(e.getValue());
                for (String key : new String[] {"callers", "taken_by"}) {
                    TreeSet<String> from = (key.equals("callers") ? callers : takenBy).get(e.getKey());
                    r.append(",\"").append(key).append("\":[");
                    if (from != null) {
                        boolean f = true;
                        for (String c : from) {
                            r.append(f ? "" : ",").append(json(c));
                            f = false;
                        }
                    }
                    r.append(']');
                }
                r.append('}');
                out.println((first ? "" : ",") + r);
                first = false;
            }
            out.println("]}");
        }
        println("ExportGraph: wrote " + bodies.size() + " function(s) to " + args[0] + " (" + failed + " not decompiled)");
    }
}
