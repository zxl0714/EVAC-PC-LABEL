"""Keep investigation outputs local, new, and separate from every input."""
from pathlib import Path
import re

from project import ROOT, reject_link, config_path


def private_output(path, *inputs):
    path = Path(path).absolute()
    reject_link(path)
    output = path.resolve()
    local = (ROOT / '.local').resolve()
    if output == local or not output.is_relative_to(local):
        raise ValueError('Keep analysis outputs in a new path under collection/.local')
    protected = [(ROOT / 'game-data').resolve(), *(Path(item).resolve() for item in inputs)]
    config = config_path(ROOT)
    if config.exists():
        reject_link(config)
        if not config.is_file() or config.stat().st_size > 1024 * 1024:
            raise ValueError('Cannot check configured game-data boundaries')
        text = config.read_text(encoding='utf-8-sig')
        if '\0' in text:
            raise ValueError('Cannot check configured game-data boundaries')
        # This is a conservative output guard, not another settings parser:
        # protect every data_dir declaration, including unknown game sections.
        for value in re.findall(r'(?im)^\s*data_dir\s*=\s*([^\r\n]*)', text):
            value = value.strip()
            if not value:
                raise ValueError('Cannot check an empty configured data_dir')
            item = Path(value)
            if item.anchor and not item.is_absolute():
                raise ValueError('Cannot check a drive-relative data_dir')
            protected.append((item if item.is_absolute() else ROOT / item).resolve())
    if any(output.is_relative_to(item) or item.is_relative_to(output) for item in protected):
        raise ValueError('Analysis output overlaps read-only game data or an input')
    if output.exists():
        raise FileExistsError(output)
    return output


def analysis_child(directory, relative):
    from generate_catalogue import relative as safe_relative
    directory = Path(directory).resolve()
    path = directory / safe_relative(relative)
    reject_link(path)
    result = path.resolve()
    if not result.is_relative_to(directory):
        raise ValueError('Census path escapes analysis directory')
    return result
