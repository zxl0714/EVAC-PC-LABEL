// Export direct flow references into another statically compiled module.
// @category EVAC.PS2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.address.Address;
import java.io.PrintWriter;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.TreeSet;

public class ExportExternalCalls extends GhidraScript {
    @Override public void run() throws Exception {
        String[] args=getScriptArgs();
        if(args.length!=3 || Files.exists(Path.of(args[0])))
            throw new IllegalArgumentException("Expected new output CSV, range start and exclusive end");
        long start=Long.decode(args[1]),end=Long.decode(args[2]);
        if(start<0 || end<=start)throw new IllegalArgumentException("Invalid target range");
        TreeSet<Long> targets=new TreeSet<>();
        try(PrintWriter out=new PrintWriter(args[0],"UTF-8")) {
            InstructionIterator instructions=currentProgram.getListing().getInstructions(true);
            while(instructions.hasNext()) {
                Instruction instruction=instructions.next();
                if(!instruction.getFlowType().isCall() && !instruction.getFlowType().isJump())continue;
                for(Address target:instruction.getFlows()) {
                    long pc=target.getOffset();
                    if(pc>=start && pc<end) {
                        out.printf("%08x,%08x,%s%n",instruction.getAddress().getOffset(),pc,
                                   instruction.getMnemonicString());
                        targets.add(pc);
                    }
                }
            }
        }
        println("External static targets: "+targets.size());
    }
}
