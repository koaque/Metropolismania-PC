// Lists the call sites inside one or more functions: callee entry, callee name and how often it is
// called. Read-only. Script args: <func address> [<func address> ...]
// @category PS2Recomp

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;

import java.util.Map;
import java.util.TreeMap;

public class ReportCalls extends GhidraScript {

    @Override
    public void run() throws Exception {
        for (String arg : getScriptArgs()) {
            Address addr = toAddr(Long.decode(arg));
            Function func = getFunctionContaining(addr);
            if (func == null) {
                println("ReportCalls: no function at " + addr);
                continue;
            }
            Map<String, Integer> counts = new TreeMap<>();
            InstructionIterator it = currentProgram.getListing().getInstructions(func.getBody(), true);
            while (it.hasNext()) {
                Instruction insn = it.next();
                if (!insn.getFlowType().isCall()) {
                    continue;
                }
                for (Reference ref : insn.getReferencesFrom()) {
                    if (!ref.getReferenceType().isCall()) {
                        continue;
                    }
                    Function callee = getFunctionAt(ref.getToAddress());
                    String key = ref.getToAddress() + " " + (callee == null ? "?" : callee.getName());
                    counts.merge(key, 1, Integer::sum);
                }
            }
            println("ReportCalls: " + func.getName() + " @ " + func.getEntryPoint() + " calls:");
            for (Map.Entry<String, Integer> e : counts.entrySet()) {
                println("ReportCalls:   " + e.getKey() + " x" + e.getValue());
            }
        }
    }
}
