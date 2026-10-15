// Applies project symbol names from a CSV (address,name[,kind[,source]]) to the current program.
// kind = "func" (default) creates the function if Ghidra missed it, "label" only names the address.
// Lines starting with # are comments. Used by tools/pipeline.py analyze before ExportPS2Functions.java.
// @category PS2Recomp

import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.List;

public class ApplySymbols extends GhidraScript {

    private int created = 0;

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            println("ApplySymbols: no CSV path given, nothing to do");
            return;
        }
        File csv = new File(args[0]);
        if (!csv.isFile()) {
            println("ApplySymbols: " + csv + " not found, nothing to do");
            return;
        }

        List<String> lines = Files.readAllLines(csv.toPath(), StandardCharsets.UTF_8);
        int applied = 0;
        for (String raw : lines) {
            String line = raw.trim();
            if (line.isEmpty() || line.startsWith("#") || line.toLowerCase().startsWith("address,")) {
                continue;
            }
            String[] parts = line.split(",");
            if (parts.length < 2) {
                printerr("ApplySymbols: bad line: " + raw);
                continue;
            }
            Address addr = toAddr(Long.decode(parts[0].trim()));
            String name = parts[1].trim();
            String kind = parts.length > 2 ? parts[2].trim() : "func";
            try {
                if (applyOne(addr, name, kind)) {
                    applied++;
                }
            } catch (Exception e) {
                printerr("ApplySymbols: " + name + " at " + addr + ": " + e.getMessage());
            }
        }
        println(String.format("ApplySymbols: applied %d names (%d new functions) from %s", applied, created, csv));
    }

    private boolean applyOne(Address addr, String name, String kind) throws Exception {
        if (!kind.equals("func")) {
            createLabel(addr, name, true, SourceType.USER_DEFINED);
            return true;
        }
        Function func = getFunctionAt(addr);
        if (func == null) {
            disassemble(addr);
            if (new CreateFunctionCmd(addr).applyTo(currentProgram, monitor)) {
                created++;
                func = getFunctionAt(addr);
            }
        }
        if (func == null) {
            printerr("ApplySymbols: could not create function at " + addr + " for " + name);
            return false;
        }
        func.setName(name, SourceType.USER_DEFINED);
        return true;
    }
}
