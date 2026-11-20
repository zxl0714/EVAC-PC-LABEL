"""Build the pinned MPEG-2-only FFmpeg SDK used by R-Type Final."""

import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
from urllib.request import urlopen

from project import ROOT, digest, reject_link, write_json_new
from toolchain import tool_path

VERSION = '7.1.3'
SOURCE_NAME = f'ffmpeg-{VERSION}.tar.xz'
SOURCE_URL = 'https://ffmpeg.org/releases/' + SOURCE_NAME
SOURCE_SHA256 = 'f0bf043299db9e3caacb435a712fc541fbb07df613c4b893e8b77e67baf3adbe'
MAKE_NAME = 'make-4.4.1-3-x86_64.pkg.tar.zst'
MAKE_URL = 'https://repo.msys2.org/msys/x86_64/' + MAKE_NAME
MAKE_SHA256 = 'af0bdba17f06fe037f0194069adaa31a8fe45f1a11381501896aea1fae37bd5d'
DLLS = ('avcodec-61.dll', 'avutil-59.dll', 'swscale-8.dll')
LIBS = ('avcodec.lib', 'avutil.lib', 'swscale.lib')
OPTIONS = (
    '--toolchain=msvc', '--arch=x86_64', '--disable-everything', '--disable-autodetect',
    '--disable-network', '--disable-programs', '--disable-doc', '--disable-static',
    '--enable-shared', '--disable-avformat', '--disable-avdevice', '--disable-avfilter',
    '--disable-swresample', '--disable-postproc', '--enable-avcodec', '--enable-avutil',
    '--enable-swscale', '--enable-decoder=mpeg2video', '--enable-parser=mpegvideo',
    '--disable-x86asm', '--enable-small', '--disable-debug', '--prefix=../sdk',
)


def download(name, url, expected):
    directory = ROOT / 'tools/downloads'
    path = directory / name
    reject_link(path)
    directory.mkdir(parents=True, exist_ok=True)
    if not path.exists():
        with tempfile.TemporaryDirectory(prefix='ps2-movie-', dir=directory) as temporary:
            partial = Path(temporary) / name
            with urlopen(url, timeout=60) as source, partial.open('xb') as output:
                shutil.copyfileobj(source, output)
            if digest(partial) != expected:
                raise ValueError('PS2 movie dependency checksum differs: ' + name)
            partial.replace(path)
    if digest(path) != expected:
        raise ValueError('PS2 movie dependency checksum differs: ' + name)
    return path


def source_archive():
    path = ROOT / 'tools/downloads' / SOURCE_NAME
    reject_link(path)
    if not path.is_file() or digest(path) != SOURCE_SHA256:
        raise ValueError('Missing or changed FFmpeg source archive. Rebuild the PS2 movie SDK.')
    return path


def source_notice():
    text = (f'The R-Type Final runtime uses FFmpeg {VERSION} under the GNU Lesser General\n'
            'Public License, version 2.1 or later. The original FFmpeg source archive\n'
            'and license text are included in this directory.\n\n'
            f'Upstream source: {SOURCE_URL}\n'
            f'SHA-256: {SOURCE_SHA256}\n\n'
            'Build with the Visual Studio 2022 x64 tools and GNU Make. From a build\n'
            'directory alongside the unpacked source, run the following commands\n'
            'in a shell that can invoke the Visual Studio tools.\n\n'
            f'../ffmpeg-{VERSION}/configure {" ".join(OPTIONS)}\n'
            'make -j2\n'
            'make install\n\n'
            'The collection build automates these commands in scripts/ps2_movie.py.\n')
    return text


def license_text(path):
    with tarfile.open(path, 'r:xz') as source:
        member = source.extractfile(f'ffmpeg-{VERSION}/COPYING.LGPLv2.1')
        return member.read().decode('utf-8')


def artifact_hashes(directory):
    paths = [directory / 'bin' / name for name in (*DLLS, *LIBS)]
    paths += sorted((directory / 'include').rglob('*.h'))
    paths.append(directory / 'COPYING.LGPLv2.1')
    if not (directory / 'include/libavcodec/avcodec.h').is_file():
        raise ValueError('FFmpeg SDK headers are missing')
    if {path.name for path in (directory / 'bin').glob('*.dll')} != set(DLLS):
        raise ValueError('Unexpected FFmpeg runtime DLL set')
    hashes = {}
    for path in paths:
        reject_link(path)
        hashes[path.relative_to(directory).as_posix()] = digest(path)
    return hashes


def verify(directory):
    directory = Path(directory)
    certificate = directory / 'build.json'
    reject_link(certificate)
    value = json.loads(certificate.read_text(encoding='utf-8'))
    if (value.get('sourceSha256') != SOURCE_SHA256 or value.get('configure') != list(OPTIONS) or
            value.get('files') != artifact_hashes(directory)):
        raise ValueError('FFmpeg SDK differs from its recorded source build')
    source_archive()
    return value


def prepare(jobs):
    git = shutil.which('git')
    shell = Path(git).parent.parent / 'bin/bash.exe' if git else None
    compiler = shutil.which('cl.exe')
    if not shell or not shell.is_file() or not compiler:
        raise ValueError('Building the PS2 movie SDK requires Git for Windows and the VS x64 tools')

    inputs = dict(sourceSha256=SOURCE_SHA256, configure=list(OPTIONS), msvcSha256=digest(Path(compiler)))
    key = hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()[:16]
    directory = ROOT / 'tools/apps' / f'ffmpeg-ps2-{VERSION}-{key}'
    reject_link(directory)
    source = download(SOURCE_NAME, SOURCE_URL, SOURCE_SHA256)
    if directory.exists():
        verify(directory)
        return directory

    make = download(MAKE_NAME, MAKE_URL, MAKE_SHA256)
    directory.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='ffmpeg-ps2-', dir=directory.parent) as temporary:
        work = Path(temporary)
        with tarfile.open(source, 'r:xz') as archive:
            archive.extractall(work, filter='data')
        subprocess.run(['tar.exe', '-xf', str(make), '-C', str(work), 'usr/bin/make.exe'], check=True)
        build = work / 'build'
        build.mkdir()
        bash = [str(shell), '--noprofile', '--norc']
        output = io.BytesIO()
        for arguments in (
            [f'../ffmpeg-{VERSION}/configure', *OPTIONS],
            ['-c', 'exec "$@"', 'sh', '../usr/bin/make.exe', f'-j{jobs}'],
            ['-c', 'exec "$@"', 'sh', '../usr/bin/make.exe', 'install'],
        ):
            result = subprocess.run(bash + arguments, cwd=build, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            output.write(result.stdout)
            if result.returncode:
                raise RuntimeError(output.getvalue().decode(errors='replace')[-8000:])
        sdk = work / 'sdk'
        shutil.copyfile(work / f'ffmpeg-{VERSION}' / 'COPYING.LGPLv2.1', sdk / 'COPYING.LGPLv2.1')
        for name in DLLS:
            imports = subprocess.check_output([str(tool_path('readobj', ROOT)), '--coff-imports',
                                               str(sdk / 'bin' / name)], text=True)
            dependencies = set(re.findall(r'^\s+Name: (\S+)', imports, re.MULTILINE))
            if not dependencies or {name.lower() for name in dependencies} - {*DLLS, 'kernel32.dll', 'bcrypt.dll'}:
                raise ValueError('Unexpected DLL dependency in ' + name)
        write_json_new(sdk / 'build.json', dict(inputs, files=artifact_hashes(sdk)))
        try:
            sdk.rename(directory)
        except FileExistsError:
            pass
    verify(directory)
    return directory


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jobs', type=int, choices=range(1, 9), default=2)
    print(prepare(parser.parse_args().jobs).as_posix())
