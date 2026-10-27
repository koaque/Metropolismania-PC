// Applies a data type to each address of a list, so that variables decompile with their member and element names.
// Script args: <data types file>; lines "0xADDR <type>" ('#' starts a comment), the type written as Ghidra's parser reads it
// ("sceCslCtx", "ST_COMMAND_LIST [17]"). The types must exist in the program already (tools/ghidra/ApplyTypes.java).
// Used for the IOP modules (tools/irx_inspect.py ghidra). Missing inputs are not an error.
// @category PS2Recomp

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DataTypeManager;
import ghidra.util.data.DataTypeParser;
import ghidra.util.data.DataTypeParser.AllowedDataTypes;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.List;

public class ApplyDataTypes extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            println("ApplyDataTypes: no file given, nothing to do");
            return;
        }
        File file = new File(args[0]);
        if (!file.isFile()) {
            println("ApplyDataTypes: " + file + " not found, nothing to do");
            return;
        }
        DataTypeManager dtm = currentProgram.getDataTypeManager();
        DataTypeParser parser = new DataTypeParser(dtm, dtm, null, AllowedDataTypes.ALL);
        int applied = 0;
        int failed = 0;
        List<String> lines = Files.readAllLines(file.toPath(), StandardCharsets.UTF_8);
        for (String raw : lines) {
            String line = raw.trim();
            if (line.isEmpty() || line.startsWith("#")) {
                continue;
            }
            int space = line.indexOf(' ');
            if (space < 0) {
                continue;
            }
            try {
                Address address = toAddr(Long.decode(line.substring(0, space)));
                DataType type = parser.parse(line.substring(space + 1).trim());
                if (type == null || type.getLength() <= 0) {
                    throw new IllegalArgumentException("no usable type");
                }
                clearListing(address, address.add(type.getLength() - 1));
                createData(address, type);
                applied++;
            } catch (Exception e) {
                printerr("ApplyDataTypes: " + line + ": " + e.getMessage());
                failed++;
            }
        }
        println("ApplyDataTypes: " + applied + " variable(s) typed, " + failed + " failed");
    }
}
