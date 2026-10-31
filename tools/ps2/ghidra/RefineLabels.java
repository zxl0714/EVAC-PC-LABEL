// Bound synthetic entries by the disassembled code, not the next distant label.
// @category EVAC.PS2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class RefineLabels extends GhidraScript {
    @Override public void run() throws Exception {
        String[] args=getScriptArgs();
        if(args.length!=2 || Path.of(args[0]).equals(Path.of(args[1])) || Files.exists(Path.of(args[1])))
            throw new IllegalArgumentException("Expected original CSV and a new output CSV");
        Pattern row=Pattern.compile("^(.*),(0x[0-9a-fA-F]+),(0x[0-9a-fA-F]+),([0-9]+)$");
        ArrayList<String> output=new ArrayList<>();
        int changed=0;
        for(String line:Files.readAllLines(Path.of(args[0]),StandardCharsets.UTF_8)) {
            Matcher match=row.matcher(line);
            if(!match.matches()) {output.add(line);continue;}
            long start=Long.decode(match.group(2)),end=Long.decode(match.group(3));
            Address address=toAddr(start);
            if(getFunctionContaining(address)==null) {
                long cursor=start;
                while(cursor<end) {
                    Instruction instruction=getInstructionAt(toAddr(cursor));
                    if(instruction==null)break;
                    cursor+=instruction.getLength();
                }
                if(cursor==start)throw new IllegalArgumentException("Synthetic entry is not code: "+address);
                if(cursor<end) {
                    end=cursor;changed++;
                    line=String.format("%s,0x%08X,0x%08X,%d",match.group(1),start,end,end-start);
                }
            }
            output.add(line);
        }
        Files.write(Path.of(args[1]),output,StandardCharsets.UTF_8);
        println("Bound "+changed+" synthetic code entries at instruction boundaries");
    }
}
