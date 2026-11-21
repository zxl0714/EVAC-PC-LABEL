"""Read Death Label present evidence from an ordinary launcher report ZIP."""
import argparse
import io
from pathlib import Path
import re
import zipfile


def summarize(lines):
    images, ranges, calls, frames = {}, {}, {}, []
    create = []
    render_fault, stage, generation = None, None, None
    for raw in lines:
        line = raw.strip()
        if line.startswith("[ps2-dl-wsi] "):
            message = line.removeprefix("[ps2-dl-wsi] ")
            fields = dict(word.split("=", 1) for word in message.split() if "=" in word)
            if "image" in fields:
                if fields["image"] == "runtime":
                    images.clear()
                    ranges.clear()
                    calls.clear()
                    frames.clear()
                    create.clear()
                    render_fault, stage, generation = None, None, None
                images[fields["image"]] = message
            elif match := re.fullmatch(r"loader_export=(\w+) begin_rva=0x([0-9a-fA-F]+) end_rva=0x([0-9a-fA-F]+)", message):
                ranges[match[1]] = (int(match[2], 16), int(match[3], 16))
            elif message.startswith("vkCreateSwapchainKHR enter "):
                create = [message]
            elif message.startswith("vkCreateSwapchainKHR return "):
                create.append(message)
            elif message.startswith("vkQueuePresentKHR ") and {"generation", "phase"} <= fields.keys():
                if fields["generation"] != generation:
                    calls.clear()
                    generation = fields["generation"]
                phase = fields["phase"]
                if message.startswith("vkQueuePresentKHR enter "):
                    calls[phase] = [message]
                elif message.startswith("vkQueuePresentKHR return "):
                    calls.setdefault(phase, []).append(message)
        elif line == "[ps2_graphics]":
            frames.clear()
            render_fault, stage = None, None
        elif line.startswith("fault_on_render_thread="):
            render_fault = line
        elif re.fullmatch(r"event=\d+ frame=\d+ stage=[\w-]+", line):
            stage = line
        elif match := re.fullmatch(r"unwind\[(\d+)\]=([^+]+)\+0x([0-9a-fA-F]+)", line):
            frames.append((int(match[1]), match[2], int(match[3], 16)))

    yield from images.values()
    for pair in (create, *calls.values()):
        yield from pair
        if len(pair) == 1 and " enter " in pair[0]:
            yield "No return was recorded for this call."
    if render_fault:
        yield render_fault
    if stage:
        yield stage
    for depth, module, rva in frames:
        entry = f"unwind[{depth}]={module}+0x{rva:x}"
        if module.casefold() == "vulkan-1.dll":
            # Older frames contain the return address; frame zero contains the fault PC.
            pc = rva - int(depth != 0)
            matches = [name for name, (start, end) in ranges.items() if start <= pc < end]
            if len(matches) == 1:
                entry += f" ({matches[0]}, matched in the loaded Vulkan DLL)"
        yield entry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    args = parser.parse_args()
    with zipfile.ZipFile(args.report) as archive:
        names = [name for name in archive.namelist() if name.removeprefix("./") == "logs/stderr.txt"]
        if len(names) != 1:
            parser.exit(1, "The report needs one logs/stderr.txt.\n")
        with archive.open(names[0]) as source, io.TextIOWrapper(source, encoding="utf-8", errors="replace") as lines:
            output = list(summarize(lines))
        for filename, keys in (
            ("launcher-exit.txt", {"exit_code", "exit_code_hex"}),
            ("process-at-upload.txt", {"process_state", "exit_code", "windows_fault", "windows_event_error",
                                       "windows_exception_code_hex", "windows_fault_module",
                                       "windows_fault_module_version", "windows_fault_offset_hex"}),
        ):
            matches = [member for member in archive.infolist() if member.filename.removeprefix("./") == "logs/" + filename]
            if len(matches) == 1 and matches[0].file_size <= 8192:
                for line in archive.read(matches[0]).decode("utf-8", errors="replace").splitlines():
                    if line.partition("=")[0] in keys:
                        output.append(f"{filename}: {line}")
    print("\n".join(output) if output else "No Death Label present or fault records in this report.")


if __name__ == "__main__":
    main()
