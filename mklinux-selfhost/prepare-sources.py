#!/usr/bin/env python3
"""Verify and package the two native builds without keeping extracted trees."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parent
PORT = ROOT / 'rebase-2.0.40'


def digest(path, algorithm='sha256'):
    h = hashlib.new(algorithm)
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def prepare(target, check_only):
    archive_name = 'osfmk.tar.gz' if target == 'mach' else 'linux-2.0.40.tar.xz'
    metadata = json.loads((ROOT / 'sources/manifest.json').read_text())[archive_name]
    archive = ROOT / 'sources' / archive_name
    if not archive.is_file():
        raise SystemExit(f'Missing {archive}; see README.md for restoring source archives.')
    if digest(archive) != metadata['sha256']:
        raise SystemExit(f'Checksum mismatch: {archive}')

    work = ROOT / 'work'
    work.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=target + '-', dir=work) as temporary:
        temporary = Path(temporary)
        # These exact, checksum-verified archives contain only relative paths.
        with tarfile.open(archive) as source:
            members = [m for m in source.getmembers() if 'CVS' not in Path(m.name).parts]
            kwargs = {'filter': 'data'} if hasattr(tarfile, 'data_filter') else {}
            source.extractall(temporary, members=members, **kwargs)
        tree = temporary / ('osfmk' if target == 'mach' else 'linux-2.0.40')
        # Do not let git apply inherit the enclosing DingusPPC repository;
        # it would silently skip Git-format patch paths outside its subdirectory.
        environment = dict(os.environ, GIT_CEILING_DIRECTORIES=str(temporary))
        subprocess.run(['git', 'apply', '--no-index', '--whitespace=nowarn',
                        str(ROOT / metadata['patch'])], cwd=tree, env=environment, check=True)

        if target == 'mach':
            changed = tree / metadata['patched_file']
            if digest(changed) != metadata['patched_file_sha256']:
                raise SystemExit('Patched Mach source differs from the boot-tested source')
            for name in ['build_world', 'sandboxrc', 'rebuild-mach.sh']:
                shutil.copy2(ROOT / 'tools' / name, tree / name)
            print('mach: archive and patched ppc_init.c verified', flush=True)
        else:
            manifest = json.loads((PORT / 'source-manifest.json').read_text())
            actual = {str(p.relative_to(tree)): digest(p)
                      for p in sorted(tree.rglob('*')) if p.is_file()}
            if actual != manifest['files']:
                differences = sorted(name for name in actual.keys() | manifest['files'].keys()
                                     if actual.get(name) != manifest['files'].get(name))
                raise SystemExit(f'Linux source verification failed ({len(differences)} files): '
                                 f'{differences[:20]}')
            bundle = temporary / 'mklinux-2.0.40'
            bundle.mkdir()
            tree.rename(bundle / 'src')
            tree = bundle
            for name in ['build-server.sh', 'resume-build.sh', 'install-server.sh',
                         'run-runtime-checks.sh', 'runtime-check.c', 'formatter-check.c',
                         'server.config']:
                shutil.copy2(PORT / name, tree / name)
            sums = ''.join(f'{digest(tree / "src" / name, "md5")}  {name}\n'
                           for name in sorted(actual))
            (tree / 'source-md5sums.txt').write_text(sums)
            print(f'linux: archive and all {len(actual)} patched source files verified', flush=True)

        if not check_only:
            dist = ROOT / 'dist'
            dist.mkdir(exist_ok=True)
            output = dist / (tree.name + '.tar.gz')
            incoming = output.with_suffix('.incoming')
            # GNU headers work with MkLinux's old tar. Discard changing host
            # ownership/timestamps so packaging the same inputs is repeatable.
            def normalize(info):
                info.uid = info.gid = info.mtime = 0
                info.uname = info.gname = ''
                return info
            try:
                with incoming.open('wb') as raw:
                    with gzip.GzipFile(filename='', mode='wb', fileobj=raw, mtime=0) as zipped:
                        with tarfile.open(fileobj=zipped, mode='w', format=tarfile.GNU_FORMAT) as out:
                            out.add(tree, arcname=tree.name, filter=normalize)
                incoming.replace(output)
            finally:
                if incoming.exists():
                    incoming.unlink()
            print(f'{output.relative_to(ROOT)}: {output.stat().st_size} bytes', flush=True)
    work.rmdir()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='verify without saving transfer archives')
    parser.add_argument('targets', nargs='*', help='mach and/or linux (default: both)')
    args = parser.parse_args()
    targets = args.targets or ['mach', 'linux']
    if any(target not in ('mach', 'linux') for target in targets):
        parser.error('targets must be mach and/or linux')
    for target in targets:
        prepare(target, args.check)
