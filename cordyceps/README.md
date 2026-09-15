# cordyceps/ — investigation artifacts

Everything here was moved out of `/tmp` (which is scratch and gets cleared) so
the Cordyceps from-source-kernel investigation can be resumed. See the full
write-up in `../CORDYCEPS_HANDOFF.md`. Nothing here is committed to git.

## Layout

### `kernels/` — Mach kernels + symbol map
- `MK_performa` — **stock** Performa kernel (dgatwood Aug-2000, 1612796 B). Boots
  the 6200 to a login prompt. This is the current default installed on `macos.img`.
- `MK_new` — **from-source** kernel (Dec-1999 osfmk, PRODUCTION, 1355024 B).
  Reaches userland, then deadlocks before login (the bug under investigation).
- `MK_fixed` — from-source + **fix attempt #1** (slot retry-drain). Built cleanly
  but REGRESSED (wedges earlier, at Mach hw probe). Kept for reference.
- `MK.map` — symbol map for `MK_new` (from the build).
- `MK_new.elf`, `MK_performa.elf` — the ELFs with the 32-byte `MACH_BOOT_IMAGE`
  header stripped, for Ghidra (`PowerPC:BE:32:default` @ `0x00200000`).

Install a kernel on the 6200 boot disk:
`hmount macos.img; hcopy -r cordyceps/kernels/<file> ":System Folder:Extensions:Mach Kernel"; hattrib -t zsys -c MACS ":System Folder:Extensions:Mach Kernel"; humount`

### `server/` — Linux server binary + symbols (from the R2 image)
- `srv-vmlinux`, `srv-System.map` — the MkLinux server (loaded independently of
  the Mach kernel). Blocked thread at the hang is `fake_interrupt_thread`
  (waits on `fake_interrupt_cond`; woken by the kernel's `serv_callback_fake_interrupt`).

### `ghidra/` — static-diff tooling + outputs
- `gh_analyze.py` — Ghidra headless post-script: decompiles addrs from
  `gh_addrs_<PROG>.txt`, hunts strings/constants, writes `gh_out_<PROG>.txt`
  (paths updated to this dir).
- `gh_addrs_MK_new.txt` — addresses to decompile (currently the interrupt-path
  functions). `gh_addrs_MK_performa.txt` — empty (stock is stripped; located by
  matching).
- `gh_out_MK_new.txt`, `gh_out_MK_performa.txt` — the decompile results
  (interrupt-forwarding path). `mk_symbols.txt` — symbol-import file for `MK_new`.
- Run: `/opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless <proj> <name> -import cordyceps/kernels/MK_new.elf -processor PowerPC:BE:32:default -scriptPath cordyceps/ghidra -postScript gh_analyze.py MK_new`

### `osfmk-src/` — osfmk source reference tree (read-only; matches the build host)
Kernel source at `osfmk-src/osfmk/src/mach_kernel`. Interrupt code:
`ppc/POWERMAC/interrupt_performa.c` (`performa_via2_slot_interrupt` ~line 512).

### `scripts/`
- `run6200r2.sh` — launch the 6200 test (add `-d` for the built-in debugger).
- `run7500.sh` — launch the pm7500 **build host** (ODE tree in
  `mklinux-selfhost/debug/pm7500/mklinux.img`).
- `tn.py` — telnet helper for the build host: `python3 cordyceps/scripts/tn.py root dingusppc '<cmd>' <timeout>`.

### `patches/`
- `interrupt_performa.c.attempt1` — the fix-attempt-#1 source (slot retry-drain).
  The **original** is at `osfmk-src/osfmk/src/mach_kernel/ppc/POWERMAC/interrupt_performa.c`.
- `fstab.orig`, `rc.sysinit.orig` — R2 image originals before edits.

## Kernel rebuild pipeline (proven)
Edit `osfmk-src/.../interrupt_performa.c` (or start from the original), write it
into the build-host image with debugfs while the host is halted, boot with
`run7500.sh`, run `/tmp/prod-build.sh` on the guest via `tn.py`, `sync`, kill,
and extract the built `Mach_Kernel` with debugfs. Full recipe in
`../CORDYCEPS_HANDOFF.md` §6.
