// Writes Ghidra's decompilation of every function to <out dir>/<entry>.c, plus index.txt
// (entry, name, file). The output is a reading aid for mapping and porting: it is derived from
// the game's code, so it stays in work/ and is never committed (AGENTS.md rule 8).
// Read-only. Script args: <out dir>
// @category PS2Recomp

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;

import java.io.File;
import java.io.PrintWriter;

public class DecompileAll extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            printerr("DecompileAll: usage: <out dir>");
            return;
        }
        File dir = new File(args[0]);
        if (!dir.isDirectory() && !dir.mkdirs()) {
            printerr("DecompileAll: cannot create " + dir);
            return;
        }

        DecompInterface decompiler = new DecompInterface();
        decompiler.setOptions(new DecompileOptions());
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(false);
        decompiler.openProgram(currentProgram);

        int written = 0;
        int failed = 0;
        try (PrintWriter index = new PrintWriter(new File(dir, "index.txt"), "UTF-8")) {
            for (Function func : currentProgram.getFunctionManager().getFunctions(true)) {
                if (monitor.isCancelled()) {
                    break;
                }
                String entry = String.format("%08X", func.getEntryPoint().getOffset());
                String file = entry + ".c";
                DecompileResults results = decompiler.decompileFunction(func, 120, monitor);
                try (PrintWriter out = new PrintWriter(new File(dir, file), "UTF-8")) {
                    out.println("// " + func.getName() + " @ 0x" + entry + " (" + func.getBody().getNumAddresses() + " bytes)");
                    if (results.decompileCompleted()) {
                        out.print(results.getDecompiledFunction().getC());
                    } else {
                        out.println("// decompile failed: " + results.getErrorMessage());
                        failed++;
                    }
                }
                index.println("0x" + entry + "\t" + func.getName() + "\t" + file);
                written++;
            }
        } finally {
            decompiler.dispose();
        }
        println("DecompileAll: wrote " + written + " function(s) to " + dir + " (" + failed + " failed)");
    }
}
