// Lists the functions that call (or reference) each given function address. Read-only.
// Script args: <func address> [<func address> ...]
// @category PS2Recomp

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

import java.util.Map;
import java.util.TreeMap;

public class ReportCallers extends GhidraScript {

    @Override
    public void run() throws Exception {
        for (String arg : getScriptArgs()) {
            Address addr = toAddr(Long.decode(arg));
            Function target = getFunctionAt(addr);
            String targetName = target == null ? "?" : target.getName();
            Map<String, Integer> callers = new TreeMap<>();
            for (Reference ref : getReferencesTo(addr)) {
                Function caller = getFunctionContaining(ref.getFromAddress());
                String key = (caller == null ? "-" : caller.getEntryPoint() + " " + caller.getName())
                    + " [" + ref.getReferenceType() + "]";
                callers.merge(key, 1, Integer::sum);
            }
            println("ReportCallers: " + targetName + " @ " + addr + " referenced by " + callers.size() + ":");
            for (Map.Entry<String, Integer> e : callers.entrySet()) {
                println("ReportCallers:   " + e.getKey() + " x" + e.getValue());
            }
        }
    }
}
