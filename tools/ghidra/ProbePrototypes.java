// Records the decompiler's own view of every function's prototype, ignoring prototypes applied by
// ApplyTypes: each function is reset to an unlocked default signature in memory, decompiled, and the
// inferred parameters (name, type, storage) and return type are written as JSON lines.
// Run it read-only so the resets are never saved: pipeline.py script ProbePrototypes.java <out file>.
// Script args: <out file>
// @category PS2Recomp

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.DataType;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Function.FunctionUpdateType;
import ghidra.program.model.listing.Variable;
import ghidra.program.model.pcode.FunctionPrototype;
import ghidra.program.model.pcode.HighFunction;
import ghidra.program.model.pcode.HighSymbol;
import ghidra.program.model.symbol.SourceType;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;

public class ProbePrototypes extends GhidraScript {

    private static String json(String s) {
        return "\"" + s.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            printerr("ProbePrototypes: usage: <out file>");
            return;
        }
        for (Function func : currentProgram.getFunctionManager().getFunctions(true)) {
            if (func.getSignatureSource() != SourceType.DEFAULT || func.hasCustomVariableStorage()) {
                func.setCustomVariableStorage(false);
                func.replaceParameters(new ArrayList<Variable>(), FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS,
                    true, SourceType.DEFAULT);
                func.setReturnType(DataType.DEFAULT, SourceType.DEFAULT);
                func.setSignatureSource(SourceType.DEFAULT);
            }
        }

        DecompInterface decompiler = new DecompInterface();
        decompiler.setOptions(new DecompileOptions());
        decompiler.toggleCCode(false);
        decompiler.toggleSyntaxTree(true);
        decompiler.openProgram(currentProgram);
        int written = 0;
        int failed = 0;
        try (PrintWriter out = new PrintWriter(new File(args[0]), "UTF-8")) {
            for (Function func : currentProgram.getFunctionManager().getFunctions(true)) {
                if (monitor.isCancelled()) {
                    break;
                }
                DecompileResults results = decompiler.decompileFunction(func, 120, monitor);
                HighFunction high = results.decompileCompleted() ? results.getHighFunction() : null;
                StringBuilder line = new StringBuilder();
                line.append("{\"entry\":").append(json(String.format("0x%08X", func.getEntryPoint().getOffset())));
                line.append(",\"name\":").append(json(func.getName()));
                if (high == null) {
                    line.append(",\"error\":").append(json(String.valueOf(results.getErrorMessage())));
                    failed++;
                } else {
                    FunctionPrototype proto = high.getFunctionPrototype();
                    line.append(",\"ret\":").append(json(proto.getReturnType().getDisplayName()));
                    line.append(",\"params\":[");
                    for (int i = 0; i < proto.getNumParams(); i++) {
                        HighSymbol p = proto.getParam(i);
                        if (i > 0) {
                            line.append(",");
                        }
                        line.append("{\"name\":").append(json(p.getName()));
                        line.append(",\"type\":").append(json(p.getDataType().getDisplayName()));
                        line.append(",\"size\":").append(p.getDataType().getLength());
                        line.append(",\"storage\":").append(json(p.getStorage().toString())).append("}");
                    }
                    line.append("]");
                }
                line.append("}");
                out.println(line);
                written++;
            }
        } finally {
            decompiler.dispose();
        }
        println("ProbePrototypes: " + written + " function(s), " + failed + " failed");
    }
}
