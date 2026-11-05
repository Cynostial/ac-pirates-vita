#!/usr/bin/env python3
"""Build the Vita client and its modified libraries from this source tree."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / 'build'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vitasdk', type=Path, default=os.environ.get('VITASDK'))
    parser.add_argument('--jobs', type=int, default=min(8, os.cpu_count() or 1))
    parser.add_argument('--online-config', type=Path, help='Private C header defining the optional leaderboard URL and key')
    parser.add_argument('--package', action='store_true', help='Also produce build/ac-pirates.vpk')
    args = parser.parse_args()
    if not args.vitasdk:
        parser.error('Set VITASDK or pass --vitasdk PATH')
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    sdk = args.vitasdk.resolve()
    env = dict(os.environ)
    env['PATH'] = str(sdk / 'bin') + os.pathsep + env['PATH']
    BUILD.mkdir(exist_ok=True)
    game = BUILD / 'game'
    game.mkdir(exist_ok=True)

    def tool(name):
        return sdk / 'bin' / (name + ('.exe' if os.name == 'nt' else ''))

    def run(command):
        result = subprocess.run(list(map(str, command)), env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if result.stdout:
            print(result.stdout, end='', flush=True)
        if result.returncode:
            raise RuntimeError(f'{Path(command[0]).name} failed ({result.returncode})')

    def write_changed(path, value):
        if not path.exists() or path.read_text(encoding='utf-8') != value:
            path.write_text(value, encoding='utf-8', newline='\n')

    scripts = ['controls', 'render_queue', 'economy', 'entity_state', 'battle', 'sailing', 'hitches', 'runner', 'online']
    lua = '\n'.join((ROOT / f'support/vita_{name}.lua').read_text(encoding='utf-8') for name in scripts)
    for name, symbol, value in [
        ('vita_scripts.inc', 'acp_vita_lua', lua),
        ('vita_profile.inc', 'acp_profile_lua', (ROOT / 'support/vita_profile.lua').read_text(encoding='utf-8')),
    ]:
        write_changed(game / name, f'static const char {symbol}[]=\n' + ''.join(json.dumps(line + '\n') + '\n' for line in value.splitlines()) + ';\n')

    def compile_set(name, sources, flags, cxx_flags, own=False):
        directory = BUILD / name
        directory.mkdir(exist_ok=True)
        sources = sorted(sources)
        objects = [directory / (str(p.relative_to(ROOT)).replace('/', '_').replace('\\', '_') + '.o') for p in sources]
        headers = [p for folder in ('src', 'native', 'installer', 'vendor') for p in (ROOT / folder).rglob('*') if p.suffix in ('.h', '.inc', '.hpp')]
        headers += list(game.glob('*.inc'))
        if args.online_config:
            headers.append(args.online_config.resolve())
        newest = max(p.stat().st_mtime_ns for p in headers)
        stamp = directory / 'flags.json'
        config = json.dumps([flags, cxx_flags])
        changed = not stamp.exists() or stamp.read_text() != config

        def compile_one(pair):
            src, obj = pair
            if not changed and obj.exists() and obj.stat().st_mtime_ns > max(newest, src.stat().st_mtime_ns):
                return
            cxx = src.suffix == '.cpp'
            strict = ['-Werror'] if own and src.parent.name in ('src', 'native', 'installer') else []
            lang = cxx_flags if cxx else (['-std=gnu17'] if src.suffix == '.c' else [])
            print(f'Compile {src.relative_to(ROOT)}', flush=True)
            run([tool('arm-vita-eabi-g++' if cxx else 'arm-vita-eabi-gcc'), *flags, *lang, *strict, '-c', src, '-o', obj])

        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            list(pool.map(compile_one, zip(sources, objects)))
        write_changed(stamp, config)
        return objects

    gl = ROOT / 'vendor/vitagl/source'
    gl_sources = [p for folder in (gl, gl / 'utils', gl / 'utils/preprocessor') for p in folder.iterdir() if p.suffix in ('.c', '.cpp')]
    gl_flags = ['-g', '-O3', '-ffast-math', '-mtune=cortex-a9', '-mfpu=neon', '-mfp16-format=ieee', '-mfloat-abi=softfp',
                '-DHAVE_SOFTFP_ABI', '-DHAVE_SHARK_LOG', '-DLOG_ERRORS', '-DSKIP_SPLASHSCREEN', '-DVGL_GIT_HASH="9c23758-acp"',
                '-Wno-incompatible-pointer-types', '-Wno-stringop-overflow', '-I' + str(gl), '-I' + str(ROOT / 'src')]
    gl_objects = compile_set('vitagl', gl_sources, gl_flags, ['-fexceptions', '-std=gnu++11', '-Wno-write-strings'])
    gl_archive = BUILD / 'vitagl/libvitaGL.a'
    run([tool('arm-vita-eabi-gcc-ar'), 'rcsD', gl_archive, *gl_objects])

    sl = ROOT / 'vendor/opensles/libopensles'
    makefile = (sl / 'Makefile').read_text()
    sl_sources = [sl / (name + '.c') for name in re.findall(r'\b(\w+)\.o\b', makefile.split('INCLUDES')[0])]
    sl_flags = ['-g', '-O3', '-Wall', '-mfloat-abi=softfp', '-mfpu=neon', '-DUSE_OUTPUTMIXEXT', '-DUSE_SDL', '-DUSE_SNDFILE',
                '-I' + str(sl), '-I' + str(sl.parent / 'include')]
    sl_objects = compile_set('opensles', sl_sources, sl_flags, [])
    sl_archive = BUILD / 'opensles/libOpenSLES.a'
    run([tool('arm-vita-eabi-gcc-ar'), 'rcsD', sl_archive, *sl_objects])

    flags = ['-O2', '-g', '-Wall', '-Wextra', '-Wno-unused-parameter', '-mfloat-abi=softfp', '-mfpu=neon', '-mcpu=cortex-a9',
             '-ffunction-sections', '-fdata-sections', '-DACP_UPDATE_SAMPLES', '-DACP_CLASS_COUNTERS']
    flags += ['-I' + str(ROOT / name) for name in ('src', 'native', 'installer', 'vendor/android-jni', 'vendor', 'vendor/falso_ndk', 'vendor/vitagl/source', 'vendor/opensles/include', 'build/game')]
    if args.online_config:
        flags += ['-include', str(args.online_config.resolve())]
    sources = [p for folder in ('src', 'native', 'installer') for p in (ROOT / folder).iterdir()
               if p.suffix in ('.c', '.cpp', '.S') and p.name not in ('frustum.S', 'emulator_io_guard.c', 'render_profile.c')]
    ndk = ROOT / 'vendor/falso_ndk'
    sources += [ROOT / 'vendor/so_util/so_util.c', ROOT / 'vendor/tlsf/tlsf.c', ndk / 'FalsoNDK_Utils.cpp']
    sources += [ndk / 'android' / f'{name}.cpp' for name in ('AConfiguration', 'AInput', 'ALooper', 'ASensor')]
    sources += list((ndk / 'linux').glob('*.cpp')) + list((ndk / 'shim').glob('*.cpp'))
    objects = compile_set('game', sources, flags, ['-std=gnu++20'], own=True)
    libraries = '''sndfile mp3lame mpg123 opus vorbis ogg vorbisenc vorbisfile FLAC vitashark SceShaccCgExt SceShaccCg_stub taihen_stub kubridge_stub mathneon
        stdc++ m z pthread SceDisplay_stub SceGxm_stub SceCtrl_stub SceTouch_stub SceMotion_stub ScePower_stub SceAppMgr_stub SceAppUtil_stub
        SceAudio_stub SceAudioIn_stub SceKernelDmacMgr_stub SceSysmodule_stub SceVshBridge_stub SceRtc_stub SceCommonDialog_stub SceHttp_stub
        SceNpTrophy_stub SceHid_stub SceFios2_stub SceNet_stub SceNetCtl_stub'''.split()
    elf = game / 'ac-pirates.elf'
    run([tool('arm-vita-eabi-gcc'), '-mfloat-abi=softfp', '-mfpu=neon', '-mcpu=cortex-a9', '-Wl,-q', '-Wl,--gc-sections',
         '-Wl,--wrap=abort', '-Wl,--wrap=__assert_func', '-Wl,-Map=' + str(game / 'ac-pirates.map'), *objects,
         '-Wl,--start-group', gl_archive, sl_archive, *['-l' + name for name in libraries], '-Wl,--end-group', '-o', elf])
    run([tool('arm-vita-eabi-size'), elf])
    if args.package:
        sys.path.insert(0, str(ROOT / 'tools'))
        from build_trophies import build as build_trophies
        run([tool('vita-elf-create'), elf, game / 'ac-pirates.velf'])
        run([tool('vita-make-fself'), '-c', '-na', game / 'ac-pirates.velf', game / 'eboot.bin'])
        run([tool('vita-mksfoex'), '-s', 'TITLE_ID=ACPR00001', '-s', 'APP_VER=00.81', '-d', 'ATTRIBUTE2=12', 'AC Pirates', game / 'param.sfo'])
        with zipfile.ZipFile(BUILD / 'ac-pirates.vpk', 'w', zipfile.ZIP_DEFLATED) as package:
            for src, target in [(game / 'eboot.bin', 'eboot.bin'), (game / 'param.sfo', 'sce_sys/param.sfo'),
                                (ROOT / 'THIRD-PARTY-NOTICES.txt', 'THIRD-PARTY-NOTICES.txt'),
                                (ROOT / 'installer/obb-order.bin', 'setup/obb-order.bin'),
                                (ROOT / 'support/metadata/manifest.names', 'manifest.names'),
                                (ROOT / 'support/ui/controller-icons.bin', 'ui/controller-icons.bin'),
                                (build_trophies(ROOT, game / 'TROPHY.TRP'), 'sce_sys/trophy/ACPR00001_00/TROPHY.TRP')]:
                package.write(src, target)
            for file in (ROOT / 'licenses').glob('*.txt'):
                package.write(file, 'licenses/' + file.name)
            for name in ('cpuinfo', 'meminfo'):
                package.write(ROOT / 'support/extras' / name, name)
            for name in ('icon0.png', 'pic0.png'):
                package.write(ROOT / 'support/livearea' / name, 'sce_sys/' + name)
            for name in ('bg0.png', 'startup.png'):
                package.write(ROOT / 'support/livearea' / name, 'sce_sys/livearea/contents/' + name)
            package.write(ROOT / 'support/extras/livearea/template.xml', 'sce_sys/livearea/contents/template.xml')
        print('Built', BUILD / 'ac-pirates.vpk')
    write_changed(BUILD / 'build-report.json', json.dumps({
        'elf_sha256': hashlib.sha256(elf.read_bytes()).hexdigest(),
        'leaderboard_configured': bool(args.online_config),
        'source_files': len(sources),
    }, indent=2) + '\n')
    print('Built', elf)


if __name__ == '__main__':
    main()
