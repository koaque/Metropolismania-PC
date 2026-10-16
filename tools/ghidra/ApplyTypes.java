// Applies the project's game types and function signatures to the current program.
// Script args: <types dir> <signatures file> [<this types file>]
//   <types dir>: every *.h file, in name order, is parsed with Ghidra's C parser into the
//     category /metropolismania. Plain C declarations only; preprocessor lines are skipped.
//   <signatures file>: lines "0xADDR <C prototype>;" ('#' starts a comment). The prototype's
//     types and parameter names are applied; the function keeps its name from symbols.csv.
//   <this types file>: lines "<name prefix> <struct> <parameter name> [except <name>,<name>...]".
//     Every function whose name starts with the prefix (and has no full signature) gets its first
//     parameter typed as a pointer to the struct, so class methods decompile with field names.
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
import ghidra.program.model.data.PointerDataType;
import ghidra.program.model.data.Structure;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Function.FunctionUpdateType;
import ghidra.program.model.listing.Parameter;
import ghidra.program.model.listing.ParameterImpl;
import ghidra.program.model.listing.Variable;
import ghidra.program.model.symbol.SourceType;

import java.io.ByteArrayInputStream;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

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
        Set<Address> typedBySignature = new HashSet<>();
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
                    typedBySignature.add(addr);
                } else {
                    printerr("ApplyTypes: " + addr + ": " + cmd.getStatusMsg());
                    failed++;
                }
            }
        }
        int thisTyped = 0;
        if (args.length >= 3 && new File(args[2]).isFile()) {
            thisTyped = applyThisTypes(dtm, new File(args[2]), typedBySignature);
        }
        println("ApplyTypes: " + headerCount + " header(s), " + applied + " signature(s) applied, " + failed
            + " failed, " + thisTyped + " method(s) given a typed first parameter");
        if (failed > 0) {
            throw new IllegalStateException("ApplyTypes: " + failed + " signature(s) failed");
        }
    }

    private int applyThisTypes(DataTypeManager dtm, File file, Set<Address> skip) throws Exception {
        int typed = 0;
        for (String raw : Files.readAllLines(file.toPath(), StandardCharsets.UTF_8)) {
            String line = raw.strip();
            if (line.isEmpty() || line.startsWith("#")) {
                continue;
            }
            String[] parts = line.split("\\s+");
            if (parts.length < 3) {
                throw new IllegalStateException("ApplyTypes: bad this-type line: " + line);
            }
            Set<String> except = new HashSet<>();
            if (parts.length >= 5 && parts[3].equals("except")) {
                except.addAll(Arrays.asList(parts[4].split(",")));
            }
            List<DataType> found = new ArrayList<>();
            dtm.findDataTypes(parts[1], found);
            Structure struct = null;
            for (DataType dt : found) {
                if (dt instanceof Structure) {
                    struct = (Structure) dt;
                    break;
                }
            }
            if (struct == null) {
                throw new IllegalStateException("ApplyTypes: struct " + parts[1] + " not found for: " + line);
            }
            DataType ptr = new PointerDataType(struct, dtm);
            for (Function func : currentProgram.getFunctionManager().getFunctions(true)) {
                String name = func.getName();
                if (!name.startsWith(parts[0]) || except.contains(name) || skip.contains(func.getEntryPoint())) {
                    continue;
                }
                List<Variable> params = new ArrayList<>();
                params.add(new ParameterImpl(parts[2], ptr, currentProgram));
                Parameter[] old = func.getParameters();
                for (int i = 1; i < old.length; i++) {
                    params.add(new ParameterImpl(old[i].getName(), old[i].getDataType(), currentProgram));
                }
                func.replaceParameters(params, FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS, true,
                    SourceType.USER_DEFINED);
                typed++;
            }
        }
        return typed;
    }
}
