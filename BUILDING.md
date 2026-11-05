# Building the Vita client

This is the native client source for beta 0.81, with the production leaderboard configuration removed. It includes the modified graphics/audio libraries and the source needed to relink the client. The existing downloadable release has not been replaced by this source publication.

## Requirements

- Python 3.9 or newer (standard library only).
- A working [VitaSDK](https://vitasdk.org/) toolchain and its host runtime dependencies on your `PATH`.
- ARM **softfp** builds of the dependencies below. Do not mix hard-float and softfp libraries.

The checked build uses GCC 10.3.0 and the VitaSDK toolchain recorded in [toolchain-reference.txt](toolchain-reference.txt). Install matching libraries and headers under `$VITASDK/arm-vita-eabi`. [vitasdk-softfp/packages](https://github.com/vitasdk-softfp/packages) provides package recipes and binaries. The reference archive hashes and audio library versions are recorded in [dependency-reference.json](dependency-reference.json).

Required SDK libraries: `sndfile`, `mp3lame`, `mpg123`, `opus`, `vorbis`, `ogg`, `vorbisenc`, `vorbisfile`, `FLAC`, `vitashark`, `SceShaccCgExt`, `taihen_stub`, `kubridge_stub`, `mathneon`, `pthread` and `z`, plus the standard VitaSDK C/C++ libraries and SCE import stubs. See `build.py` for the complete link command. vitaGL and OpenSL ES are built from this repository, not taken from precompiled SDK archives.

## Build

```sh
python build.py --vitasdk /path/to/vitasdk --package
```

Or set `VITASDK` in your environment and run `python build.py --package`. On Windows, use the Windows VitaSDK and a Python command available on your `PATH`; quote paths containing spaces. Make sure the SDK's required host DLLs are also on `PATH`.

The output is `build/ac-pirates.vpk`. Without `--package`, the script compiles and links `build/game/ac-pirates.elf`. `--jobs 4` limits parallel compilation. Object files, the link map and the library archives stay under `build/` for inspection and relinking. Nothing is uploaded by this script.

No APK, OBB, extracted game library, server checkout or database is needed to build the native client. The installer table, manifest names and OBB ordering file are metadata for Android 2.9.1 / main.253, not game payloads. Players supply their own supported game files on the Vita as described in the README.

## Optional leaderboard service

The full leaderboard client is in `native/online.c` and `support/vita_online.lua`. Its production service address and signing key are intentionally absent. Default builds do not make leaderboard requests, including when a runtime URL override exists; trophy jobs continue independently.

For your own compatible service, copy `config/online.example.h` to a file outside the repository, or to an ignored `*.local.h` file, and set `ACP_LEADERBOARD_URL` (base URL ending in `/`) and `ACP_LEADERBOARD_KEY`. Then build with:

```sh
python build.py --vitasdk /path/to/vitasdk --online-config /private/online.local.h --package
```

The native client defines the `submit.php` and `top.php` request formats and HMAC-SHA256 signing. The server implementation and database are separate projects. Do not commit populated configuration headers. Values supplied at compile time are embedded in the resulting executable, so they are not a substitute for server-side validation.

## Modified dependencies and relinking

- `vendor/vitagl`: modified vitaGL based on `9c23758ff17893db63887f95e9a4d9350c986d88`, including shader caching, texture readback/transcoding, render-pass and error-handling changes. The source is already patched; no private patching step is needed.
- `vendor/opensles`: OpenSL ES based on `e35b0630ac4c9091db63374d5c111d719887dde9`, including the Vita output backend and object/worker lifetime changes.
- `vendor/falso_ndk`: Android activity, looper, input, sensor and descriptor compatibility, with the port's changes applied.
- `vendor/tlsf`: TLSF 3.1 based on `deff9ab509341f264addbd3c8ada533678591905`, with alignment changes.
- `vendor/so_util` and `vendor/android-jni`: the Android loader and JNI ABI support.

`build.py` compiles these sources and links them with the client. To use a modified library, edit its source here (or replace its SDK archive for an external dependency), then rerun the build. Compiler options, object files and the link map are retained in `build/`. There is no signature check tying this client to a particular build of these libraries. Install your resulting VPK through VitaShell on a homebrew-enabled Vita.

The external libraries retain their upstream licences and build systems. Their source locations are listed in [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt). This public build deliberately differs from the distributed binary in its empty service configuration; it is not a byte-identical reproduction of that binary.
