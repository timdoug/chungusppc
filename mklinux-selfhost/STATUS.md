# MkLinux status — 2026-09-13

Mach and Linux 2.0.40 have booted through Mac OS 7.6.1 on 16 model selectors.
This pass extended peripheral and display coverage and fixed RAM corruption
that only appeared under larger allocations. The latest Mach rebuild has
booted on the 5400; it has not been retested across all 16 models.

## Verified coverage

| Area | What passed | Changes in this pass |
| --- | --- | --- |
| Serial | Both ports: 4 KiB transfers on the 7500 and 16 KiB transfers on the 8100. The 7500 printer port also passed 16 KiB with rapid close/reopen. Serial login on 5400, 7500, 8100 and 9500. | ESCC interrupts, baud timing, FIFO/overrun and carrier status; independent sockets; four AMIC DMA rings. Linux readers now belong to the device port they opened, preventing an old read from hanging up a reopened tty. |
| Audio | All 24 format/channel/rate combinations on 7500 and 8100, including restart after a pause. Signed 16-bit stereo in both byte orders at 44.1/22.05 kHz on 6100, 5400, 6400 and 6500. | Correct 16-bit mono buffer bounds and preserve unsigned stereo input. Accept the DBDMA key used by Mach's AWACS completion counter. |
| Floppy | Ext2 reads/writes on 6100, 5400, 6400, 6500 and 7500, with written files checked from the host image. | Additional Alchemy checks. Mach's routine interrupt/sector traces are now disabled in production builds. I/O with that new Mach build is still pending. |
| SCSI | 16 MiB filesystem writes, host-image checks and cold-start persistence on 7500 MESH and the 8100's second bus. CD checks on both 8100 controllers. Disk/CD checks on 6100 and 7100. | Split AMIC DMA at host RAM allocation boundaries; the 6100's CD buffer crossed the 8 MiB boundary between onboard and expansion RAM. |
| RAM | 6100 with 72 MiB; 7100 and 8100 with 136 MiB, including larger disk transfers. | Separate the 7100/8100 physical banks. The old alias let Mach allocate the same storage twice. Preserve the 6100 ROM's compact RAM matrix. |
| Display | 9500 with the `-104` GX ROM: 640×480 console and X/GNOME with 8-bit, RGB555 and 32-bit pixels; terminal colors, scrolling and VT switching. Earlier X checks also cover 6100, 6400, 6500 and 7500. | Include the framebuffer's page offset in Linux's mapping length; rowbytes already accounts for depth. |

The [model notes](../zdocs/users/mklinux.md) contain launch commands and the
limits of each result. AMIC and DBDMA floppy paths are covered, as are the
emulated SCSI controller paths used by these Macs. This does not establish
support for every SCSI peripheral, NuBus card or model-specific display mode.

The host suite passed all 13 tests, including 61 serial, 1,691 HMC and 16,448
SCSI checks. The native Linux runtime suite passed on server build #7.
The extracted AWACS converters also passed buffer-boundary and input-preservation
checks under ASan/UBSan. Both source preparation checks pass, including all
2,875 Linux source files.

## Current builds and local images

These are the binaries from this pass; later native builds change timestamps
and build numbers. MD5 identifies the local test files, not their provenance;
source/archive verification uses the retained SHA-256 manifests.

| Binary | Size | MD5 |
| --- | ---: | --- |
| Linux `2.0.40-osfmach3` #7 | 1,552,524 bytes | `85af92aa1aa850bf5b4b796672a7e526` |
| Mach with quiet floppy traces | 1,354,944 bytes | `cd14947b0f7cdbf57dab6c213e3332b7` |
| Previous Mach used for the peripheral tests | 1,354,944 bytes | `0f86706bf852cc2f9f48e5b158356aaa` |

All paths below are relative to this directory. Disk images and temporary
build files are ignored by Git.

- `debug/pm7500/mklinux.img` holds the shared Linux #7 installation and native
  sources. It was used sequentially by the PCI, Alchemy and Gazelle checks.
- The new Mach data fork is installed in `debug/pm7500/macos.img`,
  `debug/pm5400/universal-macos.img` and `debug/pm6500/universal-macos.img`.
  The 5400 reached a root shell and then shut down cleanly. Other model images
  retain their previous kernels unless explicitly updated.
- The native Mach result is still in the shared guest at
  `/usr/src/osfmk/obj/powermac/mach_kernel/PRODUCTION/Mach_Kernel`.
  `/mach_servers/Mach_Kernel` is an older copy. Save the new build there with
  `/bin/cp` before cleaning the object directory.
- `debug/pm8100/mklinux.img` remains the older Linux #3 installation. It passed
  the final 6100/7100 disk checks and was shut down cleanly.
- `debug/pm6500/universal-macos.img` now has Mac OS set to **256 colors** at
  640×480 and `bootos=MkLinux`, ready for the pending eight-bit check.
  The 9500 boot volume is set to Millions at 640×480.
- `debug/audio-floppy/floppy.img` contains the ext2 test filesystem. The
  `/pm5400` file matched `4c122d0a8b5256ee9b23028f65fb82f8` on the host after
  unmounting; `/bash` matches `5bb93e827e9368bb26d7250a00f51a1e`.

No emulator or temporary HTTP server was left running. Native Mach/Linux
build intermediates remain for the next check. The archives, patches and
build scripts are sufficient to reproduce both builds after cleanup.

## Next checks

1. **Quiet floppy I/O on the 5400.** Boot with its Mac disk plus the shared
   Linux disk on `--hdd_img2`, an empty floppy drive and `--fdd_wr_prot=off`.
   Insert `debug/audio-floppy/floppy.img` after Linux boots. Mount
   `/dev/fd0H1440`, checksum `/bash`, write a file, sync and unmount. Confirm
   the console stays quiet and compare the written file from the host image.
   The binary check already confirms that `HALISR`, `HALGetNextAddr` and
   `DMASTATUS` trace strings are gone and `HALWrite failed` remains.
2. **Expanded audio on Alchemy/Gazelle.** Run `tools/mklinux-audio-test.c`
   on the 5400, then the 6500, using Linux #7. Each has passed the smaller
   signed-stereo test; the 24-case checks on these models are pending.
3. **6500 eight-bit console and X.** Use the prepared 256-color Mac volume
   and shared Linux #7 disk on MESH. Check console clearing/scrolling, start
   X, confirm root depth 8, exercise colors/window repainting and switch VTs.
   Do not mark this mode verified solely from the 9500 result.
4. **Clean build intermediates after verification.** Save the current Mach
   result with `/bin/cp`, then follow [the cleanup commands](README.md#clean-after-building).
   Remove disposable transfer archives and scratch investigation files. Keep
   original source archives, installed kernels, source/configuration and tools.

After those checks, the remaining opportunities are audio recording and mixer
controls, PPP traffic, more model-specific X checks, the TAM's native LCD mode,
and further floppy formats/operations. Audio input currently supplies synthetic
silence; it does not capture a host microphone. Disk Copy 4.2/GCR images remain
read-only, and low-level formatting is unimplemented. LocalTalk, expansion
Ethernet and NuBus cards are not covered. Keep Alchemy/Gazelle without Ethernet
and continue without `--realtime`; G3 models remain outside this pass.

For serial login, use the documented getty setup and `tools/chungusppc-serial.py`.
The old guest `strace` emitted `ptrace: umoven: Input/output error` during the
investigation; that process was stopped. Send scripted login commands one at a
time after each prompt, especially across terminal-mode changes.
