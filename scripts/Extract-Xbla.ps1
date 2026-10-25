#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$InputPackage,
    [Parameter(Mandatory)][string]$OutputDir,
    [ValidateSet('Xbla','Dlc')][string]$ContentKind='Xbla'
)
# Read-only STFS extraction for LIVE/PIRS XBLA or DLC packages (one hash-table
# copy, up to 512 MiB). ZIP/RAR must be unpacked first. No guest execution,
# license modification or package signature authentication is performed.
# Layout reference: ReXGlue/Xenia's BSD-licensed stfs_container_device.cpp.
$ErrorActionPreference = 'Stop'
if (-not ('Evac.XblaExtractor' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;
using System.Text;

namespace Evac {
public sealed class XblaFile {
    public string Path;
    public int Bytes;
    public string Sha256;
    internal bool Directory;
    internal List<int> Blocks;
}
public sealed class XblaResult {
    public string OutputDirectory;
    public string TitleId;
    public string ContentType;
    public string PackageSha256;
    public int CheckedBlocks;
    public long TotalFileBytes;
    public List<XblaFile> Files = new List<XblaFile>();
}
public sealed class XblaExtractor {
    const int BlockSize = 4096, End = 0xFFFFFF, LevelOne = 170 * 170;
    const int MaxBytes = 512 * 1024 * 1024, MaxBlocks = MaxBytes / BlockSize;
    readonly byte[] data;
    readonly int first, blocks;
    readonly HashSet<int> used = new HashSet<int>();
    readonly SHA1 sha1 = SHA1.Create();
    readonly List<XblaFile> entries = new List<XblaFile>();
    readonly Dictionary<int,XblaFile> slots = new Dictionary<int,XblaFile>();
    readonly HashSet<string> paths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
    readonly XblaResult result = new XblaResult();

    static void Need(bool ok, string message) { if (!ok) throw new InvalidDataException(message); }
    static string Hex(byte[] value) { return BitConverter.ToString(value).Replace("-", "").ToLowerInvariant(); }
    uint Number(int offset, int count, bool little = false) {
        Need(offset >= 0 && count > 0 && offset <= data.Length-count, "Truncated package.");
        uint value = 0;
        for (int i=0; i<count; ++i) value = (value << 8) | data[offset+(little ? count-i-1 : i)];
        return value;
    }
    int HashOffset(int block) {
        Need(block >= 0 && block < blocks, "Block index is outside the package.");
        int table = block < 170 ? 0 : (block / 170) * 171 + block / LevelOne + 1 + (block >= LevelOne ? 1 : 0);
        int offset = first + table * BlockSize + (block % 170) * 24;
        Need(offset <= data.Length-24, "Truncated hash table.");
        return offset;
    }
    int DataOffset(int block) {
        HashOffset(block);
        int physical = block;
        for (int level=0, span=170; level<3; ++level, span*=170) {
            physical += (block+span)/span;
            if (block<span) break;
        }
        int offset = first + physical * BlockSize;
        Need(offset <= data.Length-BlockSize, "Truncated data block.");
        return offset;
    }
    int CheckBlock(int block) {
        Need(used.Add(block), "A block is reused or its chain contains a cycle.");
        int offset = DataOffset(block), hash = HashOffset(block);
        byte[] actual = sha1.ComputeHash(data, offset, BlockSize);
        for (int i=0; i<20; ++i) Need(actual[i]==data[hash+i], "Data-block checksum mismatch at block " + block + ".");
        return offset;
    }
    int Next(int block) { return (int)Number(HashOffset(block)+21, 3); }
    static void NoLinks(string path) {
        for (string current=path; !String.IsNullOrEmpty(current); current=System.IO.Path.GetDirectoryName(current)) {
            FileAttributes attributes;
            try { attributes=File.GetAttributes(current); }
            catch (FileNotFoundException) { continue; }
            catch (DirectoryNotFoundException) { continue; }
            Need((attributes & FileAttributes.ReparsePoint)==0, "Linked input/output paths are not supported.");
        }
    }
    static void Name(string name) {
        Need(name.Length>0 && name!="." && name!=".." && !name.EndsWith(".") && !name.EndsWith(" "), "Invalid package filename.");
        foreach (char c in name) Need(c>=32 && c<=126 && "<>:\"/\\|?*".IndexOf(c)<0, "Unsafe package filename.");
        string stem=name.Split('.')[0].TrimEnd(' ').ToUpperInvariant();
        Need(stem!="CON" && stem!="PRN" && stem!="AUX" && stem!="NUL" &&
            !(stem.Length==4 && (stem.StartsWith("COM") || stem.StartsWith("LPT")) && stem[3]>='0' && stem[3]<='9'),
            "Reserved Windows filename.");
    }
    XblaExtractor(byte[] bytes, uint expectedType) {
        data=bytes;
        Need(data.Length>=0x400 && data.Length<=MaxBytes, "Expected an STFS package no larger than 512 MiB.");
        string magic=Encoding.ASCII.GetString(data,0,4);
        Need(magic=="LIVE" || magic=="PIRS", "Expected a LIVE/PIRS package. Unpack ZIP/RAR first; ISO/CON is not supported by this helper.");
        uint header=Number(0x340,4);
        Need(header>=0x400 && header<=1024*1024 && header<=data.Length, "Invalid STFS header size.");
        first=(int)((header+4095)&~4095u);
        Need((expectedType==0xD0000 || expectedType==2) && Number(0x344,4)==expectedType, "Package content type does not match ContentKind (Xbla/Dlc).");
        Need(Number(0x3A9,4)==0 && data[0x379]==0x24 && (data[0x37B]&1)!=0,
            "Unsupported STFS layout: this helper requires one hash-table copy.");
        uint allocated=Number(0x395,4);
        Need(allocated>0 && allocated<MaxBlocks, "STFS block count exceeds the extraction limit.");
        blocks=(int)allocated;
        DataOffset(blocks-1);
        int count=(int)Number(0x37C,2,true), current=(int)Number(0x37E,3,true);
        Need(count>0 && count<256, "Invalid file-table block count.");
        var table=new List<int>();
        for (int n=0; n<count; ++n) { table.Add(CheckBlock(current)); current=Next(current); }
        Need(current==End, "File-table chain length mismatch.");
        for (int n=0; n<table.Count; ++n) for (int slot=0; slot<64; ++slot) {
            int at=table[n]+slot*64, length=data[at+40]&63;
            if (length==0) continue;
            Need(length<=40, "Invalid filename length.");
            for (int i=0; i<length; ++i) Need(data[at+i]>=32 && data[at+i]<=126, "Unsupported filename encoding.");
            string name=Encoding.ASCII.GetString(data,at,length); Name(name);
            int parent=(int)Number(at+50,2);
            string relative=name;
            if (parent!=65535) {
                XblaFile directory;
                Need(slots.TryGetValue(parent,out directory) && directory.Directory, "Invalid parent directory.");
                relative=directory.Path+"/"+name;
            }
            Need(relative.Length<=200 && paths.Add(relative), "Duplicate or excessively long package path.");
            var entry=new XblaFile { Path=relative, Directory=(data[at+40]&128)!=0, Blocks=new List<int>() };
            entries.Add(entry); slots.Add(n*64+slot,entry);
            if (entry.Directory) continue;
            uint lengthBytes=Number(at+52,4);
            Need(lengthBytes<=MaxBytes, "Unsupported file size.");
            entry.Bytes=(int)lengthBytes;
            int expected=(entry.Bytes+4095)/4096;
            Need(Number(at+44,3,true)==expected, "File size and block count disagree.");
            current=(int)Number(at+47,3,true);
            for (int b=0; b<expected; ++b) { entry.Blocks.Add(CheckBlock(current)); current=Next(current); }
            Need(expected==0 || current==End, "File chain length mismatch.");
            result.TotalFileBytes+=entry.Bytes;
            result.Files.Add(entry);
        }
        Need(result.Files.Count>0, "Package contains no files.");
        using (var hash=SHA256.Create()) result.PackageSha256=Hex(hash.ComputeHash(data));
        result.TitleId=Number(0x360,4).ToString("X8");
        result.ContentType=expectedType.ToString("X8");
        result.CheckedBlocks=used.Count;
    }
    public static XblaResult Extract(string input, string output, uint expectedType) {
        input=System.IO.Path.GetFullPath(input); output=System.IO.Path.GetFullPath(output);
        Need(File.Exists(input), "InputPackage is not a file.");
        NoLinks(input); NoLinks(output);
        Need(!File.Exists(output) && !Directory.Exists(output), "OutputDir must not already exist; choose a new folder.");
        string parent=System.IO.Path.GetDirectoryName(output);
        Need(!String.IsNullOrEmpty(parent) && Directory.Exists(parent), "OutputDir's parent folder must already exist.");
        byte[] bytes;
        using (var stream=new FileStream(input,FileMode.Open,FileAccess.Read,FileShare.Read)) {
            Need(stream.Length>=0x400 && stream.Length<=MaxBytes, "Expected an STFS package no larger than 512 MiB.");
            bytes=new byte[(int)stream.Length];
            int at=0;
            while (at<bytes.Length) { int read=stream.Read(bytes,at,bytes.Length-at); Need(read>0,"Truncated input."); at+=read; }
        }
        XblaExtractor archive=null;
        try {
            archive=new XblaExtractor(bytes,expectedType); // Validate every path/chain/hash before creating output.
            NoLinks(output);
            Need(!File.Exists(output) && !Directory.Exists(output), "OutputDir appeared during validation.");
            string staging=System.IO.Path.GetFullPath(output+".extract-"+Guid.NewGuid().ToString("N"));
            Need(String.Equals(System.IO.Path.GetDirectoryName(staging),parent,StringComparison.OrdinalIgnoreCase), "Invalid staging path.");
            Directory.CreateDirectory(staging);
            try {
                foreach (XblaFile entry in archive.entries) {
                    string target=System.IO.Path.GetFullPath(System.IO.Path.Combine(staging,entry.Path.Replace('/',System.IO.Path.DirectorySeparatorChar)));
                    Need(target.StartsWith(staging+System.IO.Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase), "Path escaped output directory.");
                    NoLinks(target);
                    if (entry.Directory) { Directory.CreateDirectory(target); continue; }
                    Directory.CreateDirectory(System.IO.Path.GetDirectoryName(target));
                    using (var file=new FileStream(target,FileMode.CreateNew,FileAccess.Write,FileShare.None)) {
                        int remaining=entry.Bytes;
                        foreach (int block in entry.Blocks) { int size=Math.Min(remaining,BlockSize); file.Write(bytes,block,size); remaining-=size; }
                    }
                    using (var file=File.OpenRead(target)) using (var hash=SHA256.Create()) entry.Sha256=Hex(hash.ComputeHash(file));
                }
                NoLinks(staging); NoLinks(output);
                Directory.Move(staging,output); // Atomic publish; never merge into existing user data.
            } catch (Exception error) {
                throw new IOException("Extraction failed. Partial output was retained at " + staging + ".",error);
            }
            archive.result.OutputDirectory=output;
            return archive.result;
        } finally { if (archive!=null) archive.sha1.Dispose(); }
    }
}
}
'@
}
$inputPath = (Resolve-Path -LiteralPath $InputPackage).ProviderPath
$outputPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDir)
$contentType = if ($ContentKind -eq 'Dlc') { [uint32]2 } else { [uint32]0xD0000 }
[Evac.XblaExtractor]::Extract($inputPath, $outputPath, $contentType) | ConvertTo-Json -Depth 5
