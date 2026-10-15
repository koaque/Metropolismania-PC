// Reports which functions reference strings matching a regex, to help name SDK functions in a
// stripped ELF from their own diagnostic messages. Read-only.
// Script args: <regex | @file> [output file]. With @file, each non-empty line of the file is one
// alternative (this avoids cmd.exe mangling | and other characters in .bat arguments).
// Output lines: string_addr|func_entry|func_name|ref_addr|string
// @category PS2Recomp

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;
import java.util.regex.Pattern;

public class ReportStringXrefs extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String regex = args.length > 0 ? args[0] : ".*";
        if (regex.startsWith("@")) {
            List<String> alternatives = new ArrayList<>();
            for (String line : Files.readAllLines(Paths.get(regex.substring(1)), StandardCharsets.UTF_8)) {
                if (!line.isBlank()) {
                    alternatives.add("(?:" + line.strip() + ")");
                }
            }
            regex = String.join("|", alternatives);
        }
        Pattern pattern = Pattern.compile(regex);
        PrintWriter out = args.length > 1 ? new PrintWriter(args[1], "UTF-8") : null;
        int hits = 0;

        DataIterator it = currentProgram.getListing().getDefinedData(true);
        while (it.hasNext() && !monitor.isCancelled()) {
            Data data = it.next();
            if (!data.hasStringValue()) {
                continue;
            }
            String value = String.valueOf(data.getValue());
            if (!pattern.matcher(value).find()) {
                continue;
            }
            Reference[] refs = getReferencesTo(data.getAddress());
            if (refs.length == 0) {
                emit(out, data.getAddress() + "|-|-|-|" + sanitize(value));
                hits++;
                continue;
            }
            for (Reference ref : refs) {
                Address from = ref.getFromAddress();
                Function func = getFunctionContaining(from);
                String entry = func == null ? "-" : func.getEntryPoint().toString();
                String name = func == null ? "-" : func.getName();
                emit(out, data.getAddress() + "|" + entry + "|" + name + "|" + from + "|" + sanitize(value));
                hits++;
            }
        }
        if (out != null) {
            out.close();
        }
        println("ReportStringXrefs: " + hits + " hit(s)");
    }

    private void emit(PrintWriter out, String line) {
        if (out != null) {
            out.println(line);
        } else {
            println(line);
        }
    }

    private static String sanitize(String s) {
        return s.replace("\n", "\\n").replace("\r", "\\r").replace("|", "/");
    }
}
