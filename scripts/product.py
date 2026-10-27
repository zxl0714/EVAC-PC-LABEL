"""Current product identity and frozen pre-v0.4 release names."""

NAME = 'EVAC PC LABEL'
SLUG = 'EVAC-PC-LABEL'
LAUNCHER = NAME + '.exe'
LEGACY_LAUNCHER = 'CAVE-PC-Collection.exe'
VERSION = '0.8.0'
REPOSITORY = 'zxl0714/EVAC-PC-LABEL'
REMOTE = 'git@github.com:' + REPOSITORY + '.git'
REPOSITORY_URL = 'https://github.com/' + REPOSITORY


def renamed(version):
    return bool(version) and tuple(map(int, version.split('-', 1)[0].split('.')[:2])) >= (0, 4)


def slug(version):
    return SLUG if renamed(version) else 'CAVE-PC-Collection'


def launcher(version):
    return LAUNCHER if renamed(version) else LEGACY_LAUNCHER
