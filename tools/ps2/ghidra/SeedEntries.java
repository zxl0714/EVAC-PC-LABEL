// Add verified cross-module call targets without rerunning whole-program analysis.
// @category EVAC.PS2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.nio.file.StandardOpenOption;

public class SeedEntries extends GhidraScript {
    @Override public void run() throws Exception {
        int added=0;
        String[] args=getScriptArgs();
        boolean skipOwned=args.length==3 && args[1].equals("--skip-owned");
        if(args.length!=1 && !skipOwned)
            throw new IllegalArgumentException("Expected roots file, optionally --skip-owned new-report.csv");
        if(skipOwned && Files.exists(Path.of(args[2])))
            throw new IllegalArgumentException("The skipped-entry report already exists");
        ArrayList<String> skipped=new ArrayList<>();
        ArrayList<Address> entries=new ArrayList<>();
        for(String line:Files.readAllLines(Path.of(args[0]))) {
            if(line.isBlank())continue;
            Address address=toAddr(Long.parseUnsignedLong(line.strip(),16));
            Function existing=getFunctionContaining(address);
            if(existing!=null) {
                if(!existing.getEntryPoint().equals(address)) {
                    if(skipOwned) {
                        skipped.add(address+","+existing.getEntryPoint()+","+existing.getName());
                        continue;
                    }
                    throw new IllegalArgumentException("Target overlaps an existing function: "+address);
                }
                continue;
            }
            entries.add(address);
        }
        for(Address address:entries) {
            if(getInstructionAt(address)==null && !disassemble(address))
                throw new IllegalArgumentException("Cannot disassemble verified call target: "+address);
            if(createFunction(address,null)==null)
                throw new IllegalArgumentException("Cannot create verified function: "+address);
            added++;
        }
        if(skipOwned)Files.write(Path.of(args[2]),skipped,StandardOpenOption.CREATE_NEW);
        println("Added "+added+" verified cross-module entries");
        if(skipOwned)println("Left "+skipped.size()+" owned candidate entries for separate review");
    }
}
