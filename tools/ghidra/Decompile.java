// Prints Ghidra's decompilation of the functions containing the given addresses. Read-only.
// Script args: <address> [<address> ...] [@out.txt]  (an argument starting with @ names an output file)
// @category PS2Recomp

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;

public class Decompile extends GhidraScript {

    @Override
    public void run() throws Exception {
        List<String> addresses = new ArrayList<>();
        PrintWriter out = null;
        for (String arg : getScriptArgs()) {
            if (arg.startsWith("@")) {
                out = new PrintWriter(arg.substring(1), "UTF-8");
            } else {
                addresses.add(arg);
            }
        }

        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        StringBuilder text = new StringBuilder();
        for (String arg : addresses) {
            Address addr = toAddr(Long.decode(arg));
            Function func = getFunctionContaining(addr);
            if (func == null) {
                text.append("// no function contains ").append(addr).append("\n\n");
                continue;
            }
            DecompileResults results = decompiler.decompileFunction(func, 60, monitor);
            text.append("// ").append(func.getName()).append(" @ ").append(func.getEntryPoint())
                .append(" (asked for ").append(addr).append(")\n");
            if (results.decompileCompleted()) {
                text.append(results.getDecompiledFunction().getC());
            } else {
                text.append("// decompile failed: ").append(results.getErrorMessage()).append("\n");
            }
            text.append("\n");
        }
        decompiler.dispose();

        if (out != null) {
            out.print(text);
            out.close();
            println("Decompile: wrote " + addresses.size() + " function(s)");
        } else {
            println(text.toString());
        }
    }
}
