// Add verified cross-module call targets without rerunning whole-program analysis.
// @category EVAC.PS2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import java.nio.file.Files;
import java.nio.file.Path;

public class SeedEntries extends GhidraScript {
    @Override public void run() throws Exception {
        int added=0;
        for(String line:Files.readAllLines(Path.of(getScriptArgs()[0]))) {
            if(line.isBlank())continue;
            Address address=toAddr(Long.parseUnsignedLong(line.strip(),16));
            Function existing=getFunctionContaining(address);
            if(existing!=null) {
                if(!existing.getEntryPoint().equals(address))
                    throw new IllegalArgumentException("Target overlaps an existing function: "+address);
                continue;
            }
            if(getInstructionAt(address)==null && !disassemble(address))
                throw new IllegalArgumentException("Cannot disassemble verified call target: "+address);
            if(createFunction(address,null)==null)
                throw new IllegalArgumentException("Cannot create verified function: "+address);
            added++;
        }
        println("Added "+added+" verified cross-module entries");
    }
}
