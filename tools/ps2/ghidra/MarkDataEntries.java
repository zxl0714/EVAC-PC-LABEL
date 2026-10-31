// Refine a stripped ELF analysis using verified data-only references.
// @category EVAC.PS2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.symbol.Reference;

public class MarkDataEntries extends GhidraScript {
    @Override public void run() throws Exception {
        String[] args=getScriptArgs();
        if(args.length<2 || !args[0].equalsIgnoreCase(currentProgram.getExecutableSHA256()))
            throw new IllegalArgumentException("Data annotations do not match this ELF");
        // Validate the whole set before altering analysis. A code reference or
        // an existing function requires review, rather than silent removal.
        for(int i=1;i<args.length;i++) {
            Address address=toAddr(args[i]);
            if(getFunctionContaining(address)!=null)
                throw new IllegalArgumentException("Data annotation overlaps a function: "+address);
            boolean referenced=false;
            for(Reference reference:getReferencesTo(address)) {
                referenced=true;
                if(reference.getReferenceType().isFlow())
                    throw new IllegalArgumentException("Data annotation has a code reference: "+address);
            }
            if(!referenced)throw new IllegalArgumentException("Data annotation has no references: "+address);
        }
        for(int i=1;i<args.length;i++) {
            Address address=toAddr(args[i]);
            clearListing(address,address.add(3));
            createDWord(address);
        }
        println("Applied "+(args.length-1)+" verified data annotations");
    }
}
