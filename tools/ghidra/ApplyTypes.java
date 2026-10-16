// Applies the project's game types and function signatures to the current program.
// Script args: <types dir> <signatures file>
//   <types dir>: every *.h file, in name order, is parsed with Ghidra's C parser into the
//     category /metropolismania. Plain C declarations only; preprocessor lines are skipped.
//   <signatures file>: lines "0xADDR <C prototype>;" ('#' starts a comment). The prototype's
//     types and parameter names are applied; the function keeps its name from symbols.csv.
// Used by tools/pipeline.py analyze after ApplySymbols.java. Missing inputs are not an error.
// @category PS2Recomp

import ghidra.app.cmd.function.ApplyFunctionSignatureCmd;
import ghidra.app.cmd.function.FunctionRenameOption;
import ghidra.app.script.GhidraScript;
import ghidra.app.util.cparser.C.CParser;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DataTypeConflictHandler;
import ghidra.program.model.data.DataTypeManager;
import ghidra.program.model.data.FunctionDefinition;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

import java.io.ByteArrayInputStream;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.List;

public class ApplyTypes extends GhidraScript {

    private static String stripPreprocessor(String text) {
        StringBuilder out = new StringBuilder();
        for (String line : text.split("\n", -1)) {
            out.append(line.stripLeading().startsWith("#") ? "" : line).append('\n');
        }
        return out.toString();
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            printerr("ApplyTypes: usage: <types dir> <signatures file>");
            return;
        }
        DataTypeManager dtm = currentProgram.getDataTypeManager();
        CParser parser = new CParser(dtm, true, null);

        File typesDir = new File(args[0]);
        File[] headers = typesDir.isDirectory() ? typesDir.listFiles((d, n) -> n.endsWith(".h")) : null;
        int headerCount = 0;
        if (headers != null) {
            Arrays.sort(headers);
            for (File header : headers) {
                String text = stripPreprocessor(Files.readString(header.toPath(), StandardCharsets.UTF_8));
                parser.setParseFileName("metropolismania");
                try {
                    parser.parse(new ByteArrayInputStream(text.getBytes(StandardCharsets.UTF_8)));
                    headerCount++;
                } catch (Exception e) {
                    printerr("ApplyTypes: " + header.getName() + ": " + e.getMessage());
                    throw e;
                }
            }
        }

        File sigFile = new File(args[1]);
        int applied = 0;
        int failed = 0;
        if (sigFile.isFile()) {
            List<String> lines = Files.readAllLines(sigFile.toPath(), StandardCharsets.UTF_8);
            for (String raw : lines) {
                String line = raw.strip();
                if (line.isEmpty() || line.startsWith("#")) {
                    continue;
                }
                int space = line.indexOf(' ');
                if (space < 0) {
                    printerr("ApplyTypes: bad signature line: " + line);
                    failed++;
                    continue;
                }
                Address addr = toAddr(Long.decode(line.substring(0, space)));
                String proto = line.substring(space + 1).strip();
                if (!proto.endsWith(";")) {
                    proto += ";";
                }
                Function func = getFunctionAt(addr);
                if (func == null) {
                    printerr("ApplyTypes: no function at " + addr + " for: " + proto);
                    failed++;
                    continue;
                }
                DataType parsed;
                try {
                    parsed = parser.parse(proto);
                } catch (Exception e) {
                    printerr("ApplyTypes: " + addr + ": " + e.getMessage());
                    failed++;
                    continue;
                }
                if (!(parsed instanceof FunctionDefinition)) {
                    // The parser files function declarations under their name.
                    String head = proto.substring(0, Math.max(0, proto.indexOf('('))).strip();
                    String name = head.substring(Math.max(head.lastIndexOf(' '), head.lastIndexOf('*')) + 1);
                    parsed = parser.getFunctions().get(name);
                }
                if (!(parsed instanceof FunctionDefinition)) {
                    printerr("ApplyTypes: " + addr + ": not a function prototype: " + proto);
                    failed++;
                    continue;
                }
                ApplyFunctionSignatureCmd cmd = new ApplyFunctionSignatureCmd(addr, (FunctionDefinition) parsed,
                    SourceType.USER_DEFINED, true, true, DataTypeConflictHandler.DEFAULT_HANDLER,
                    FunctionRenameOption.NO_CHANGE);
                if (cmd.applyTo(currentProgram, monitor)) {
                    applied++;
                } else {
                    printerr("ApplyTypes: " + addr + ": " + cmd.getStatusMsg());
                    failed++;
                }
            }
        }
        println("ApplyTypes: " + headerCount + " header(s), " + applied + " signature(s) applied, " + failed + " failed");
        if (failed > 0) {
            throw new IllegalStateException("ApplyTypes: " + failed + " signature(s) failed");
        }
    }
}
