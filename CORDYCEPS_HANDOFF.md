# Cordyceps Bring-up — Handoff

**Goal:** boot a from-source (Dec-1999 osfmk) MkLinux Mach kernel to a working
login on the emulated Performa 6200 (`pm6200`) in ChungusPPC, validated against
the known-good MkLinux R2 RC5 shipped kernel.

**Status in one line:** the emulator work is **done and validated** (the correct
stock Performa kernel boots to a login prompt + working root shell on our five
committed fixes). The **from-source** kernel reaches userland but deadlocks
before login; we root-caused it to **hardware-interrupt forwarding to the Linux
server** and have three concrete leads (below). One kernel-fix attempt was built
and tested; it regressed and, in doing so, cast useful doubt on the theory.

---

## 1. What is DONE and VALIDATED

### 1a. Emulator fixes (committed + pushed to `chungus/main`, `e96ff536..a091a2ba`)
1. **`032fc8e3`** — Valkyrie VBL is a periodic pulse, not a held level
   (`devices/video/valkyrie.cpp`). A permanently-latched VBL masked the
   edge-triggered VIA2 "any slot" cascade.
2. **`e6aabe11`** — an aborted ATA multi-block command still asserts INTRQ
   (`devices/common/ata/atahd.cpp`).
3. **`58f6e2e9`** — ATA drives default to their max multi-block size
   (`devices/common/ata/atahd.h`; `sectors_per_int = SECTORS_PER_INT`). Fixes a
   `read_ata_mac_label` null-deref panic.
4. **`c51a09f3`** — back the unmodeled PrimeTime II sub-block registers with a
   register file so the ROM POST read-back passes (`primetime.cpp/.h` io_stub).
5. **`a091a2ba`** — **the big interrupt fix**: re-deliver a Performa interrupt
   that asserts during its own handler (`primetime.cpp/.h` `int_since_read`,
   `f108.cpp` `note_int_level_read`). MkLinux acks Capella at the *end* of its
   ISR and chains the next disk command from *inside* the ISR; the new INTRQ
   re-asserts the level before the ack, so the interrupt would otherwise be lost.

### 1b. Image-side fix
- **R2 `/etc/fstab` `sda`→`sdb`** (via debugfs). `macos.img` at SCSI ID 0 = Linux
  `sda`, `mklinux-r2.img` at ID 1 = `sdb` (root = `sdb2`, Mach `sd1b`). Backup at
  `cordyceps/patches/fstab.orig`.

### 1c. Validation
- The **stock Performa kernel** — the **1612796-byte** `Mach Kernel` inside
  `MkLinux-install/Place in Extensions Folder/Performas Use This!/` on
  `MkLinux R2 RC5.toast` (dgatwood's Aug-2000 DEBUG build) — **boots to a login
  prompt** and a **working root shell** (`root`/`dingusppc`; `uname -a` =
  `2.0.38-osfmach3 GENERIC_09`). Saved at `cordyceps/kernels/MK_performa`.
- Console login is driven via the SDL input-script mechanism (`SIGUSR2` reads
  `chungusppc-input.txt`; `SIGUSR1` dumps `chungusppc-screen.bmp`). **telnet does
  NOT reach the R2 guest** (it brings up only `lo`, no `eth0`).

---

## 2. The from-source bug — investigation chain (what we ruled out)

The from-source kernel (`cordyceps/kernels/MK_new`, 1355024 bytes, PRODUCTION, from the
Dec-1999 osfmk tree) reaches userland — mounts root over SCSI, runs INIT, prints
`Enabling swap space [OK]` — then, at ~23 s, wedges. On screen it loops the
server opening the Mach `scsi_info` device.

Each step below is backed by evidence gathered this session:

1. **`scsi_info` is a RED HERRING.** Ghidra-diffed both kernels'
   `scsi_info_getstatus`: functionally identical (`{controller,target_id,lun,
   flags}` + 0x100-byte inquiry copy, 0x44 stride). A live debugger trace of the
   from-source kernel showed `GET_DEVICE_COUNT` returns **2** and
   `GET_DEVICE_INFO` returns valid inquiry data (`"QUANTUM Emulated"` readable in
   the returned buffer). The server receives *identical* `scsi_info` data from
   both kernels. The `scsi_info` re-open spam is the server's *retry*, not the
   cause.

2. **The server is blocked in a wait-spin.** The stuck PC `0x100EE804` is a tight
   loop in the Linux server. Using the server binary + symbols we extracted from
   the R2 image (`cordyceps/server/srv-vmlinux`, `cordyceps/server/srv-System.map`), the blocked thread is
   **`fake_interrupt_thread`**, sleeping in
   `condition_wait(fake_interrupt_cond, uniproc_mutex)`. It wakes only when the
   Mach kernel forwards a HW interrupt via `serv_callback_fake_interrupt`
   (→ enqueue `fake_interrupt_queue` → signal the condition).

3. **The kernel stops FORWARDING interrupts.** Debugger at the hang:
   `performa_interrupt` (0x0026757c, the kernel's HW handler) is *still entered*
   (interrupts arrive), but `create_fake_interrupt` (0x002644c0, the forward-to-
   server call) is **never called**. So the server waits forever.

4. **Emulator interrupt logging (temporary, reverted):** at the hang the *only*
   interrupt firing is `id=0x400000` = the Valkyrie video VBL on the **slot video
   line** (slot bit 6), ~250 Hz, which has **no forwarding handler**
   (`performa_via2_slot_interrupts[6] = "Video IRQ" {0,0,-1}`). SCSI (`id=0x800`)
   and IDE (`id=0x20000000`) stop firing at ~23 s when the server blocks. All
   SCSI commands **completed cleanly** (not a stuck command).

5. **RED HERRING cleared:** the VIA1 "HZ tick" (`id=0x2`) stops at ~9.9 s on
   from-source **and ~17 s on stock**, and stock's idle-login steady-state is
   *also* video-VBL-only. So "VIA1 stopped / video-only" is normal quiescence,
   not the bug. Same emulator delivers identical HW interrupts to both kernels;
   **stock forwards them and boots to login, from-source stops forwarding after
   enumeration.**

---

## 3. Current root-cause understanding (and the honest uncertainty)

We Ghidra-diffed the interrupt-forwarding path (`cordyceps/ghidra/gh_out_MK_new.txt`,
`cordyceps/ghidra/gh_out_MK_performa.txt`). `create_fake_interrupt` is byte-identical; the
divergences are in the VIA handlers:

- **PRIMARY divergence — `performa_via2_slot_interrupt`** (handles the cascaded
  slot sources F108→IDE, video, PDS on the edge-triggered VIA2 "any slot" line):
  - **Stock** clears the any-slot bit, then **drains the slot IFR in a retry
    loop** (re-read `~*SLOT_IFR` + re-dispatch, ~3× while pending).
  - **From-source** reads `~*SLOT_IFR` **once**, dispatches once, no re-read
    (the IFR write-back is even commented out). `interrupt_performa.c` ~line 512.
- **SECONDARY divergence — IER masking:** the from-source VIA1/VIA2 handlers have
  the `intbits &= *IER` masking `#if 0`'d out (`interrupt_performa.c` ~426-432,
  ~486-492), so they clear/dispatch *all* pending IFR bits, not just enabled
  ones. Stock masks by IER.
- **DO NOT TOUCH — `performa_interrupt` tail:** from-source stores
  `*(capella+0x18)=0`; stock stores `*(base+0x18)=1; *(base+0x20)=7` and omits
  the `via1_portb` call. This is a *different Capella ack protocol* our emulator's
  Capella model is built around — changing it risks breaking interrupt ack
  entirely.

dgatwood's Aug-2000 README-PERFORMA explicitly mentions fixing a "lost IDE
interrupt" bug, which is this class.

**The uncertainty (important):** our emulator's `ack_via2_int`
(`primetime.cpp` ~448-457) **already re-edges "any slot" whenever a new slot
source asserts**. So in *our* emulator a re-asserted cascaded interrupt is **not
actually lost** — which means the single-pass slot handler may **not** be the
true deadlock cause, even though it's a real divergence from the working kernel.
This is why fix attempt #1 (below) is not conclusive.

---

## 4. Kernel fix attempt #1 — built cleanly, REGRESSED

- **Change:** patched `performa_via2_slot_interrupt` with a 3-pass retry-drain
  (each pass: `via_reg(PERFORMA_VIA2_IFR)=0x02` to clear any-slot, re-read
  `~*SLOT_IFR`, re-dispatch). The patched source is in the build-host image and
  at `cordyceps/patches/interrupt_performa.c.attempt1`; the original is at `cordyceps/osfmk-src/.../interrupt_performa.c`.
- **Build:** clean on the pm7500 build host (`prod-build.sh`, `KERNEL_RC=0`,
  `interrupt_performa.o` 6556→6644 bytes). Extracted kernel: `cordyceps/kernels/MK_fixed`
  (1355024 bytes, verified different from `cordyceps/kernels/MK_new`).
- **Result on the 6200:** **regressed earlier than the original bug** — wedges
  during Mach hardware probe (`wd0: udunwedge failed`, `Controller never
  recovered`, frozen at "Cuda probe point 7"), never reaching userland.
- **Why:** the retry loop re-clears VIA2 any-slot every pass while the emulator's
  video VBL keeps re-asserting that slot line → an **interrupt storm** that
  starves the probe. The unconditional 3-pass loop (I couldn't see dgatwood's
  exact break condition) also spuriously re-invokes the F108/IDE handler, wedging
  the ATA controller.
- **Takeaway:** slot handling is sensitive and genuinely involved, but this exact
  patch is both harmful in our emulator and unproven against the original
  deadlock (it regressed before reaching that point).

---

## 5. Three opportunities going forward

### Opportunity A — Re-verify the diagnosis before more build cycles (recommended first)
Because our emulator already re-edges "any slot", confirm *what forwardable
interrupt the server actually needs* right after enumeration, cheaply:
- Instrument the emulator (temporary `LOG_F`) at `create_fake_interrupt`-adjacent
  points is not possible (kernel), so instead: in the debugger, on the **stock**
  kernel find its `create_fake_interrupt` (locate via the same Ghidra pass) and
  log/trace which forwardable source it forwards in the seconds *after*
  enumeration that from-source never does. The delta is the missing interrupt.
- Cross-check the **VBL line** question (Opportunity C) as part of this — if the
  server's post-enumeration heartbeat is `PMAC_DEV_VBL` (F108 Keystone), and our
  emulator never fires that line, that alone explains the deadlock and no slot
  retry-drain is needed.
- Cheapest, highest-information; avoids burning ~15-min kernel build cycles on an
  unproven theory.

### Opportunity B — Iterate the kernel retry-drain (the dgatwood-parity path)
If we keep the kernel-fix approach:
- **Drop the `via_reg(PERFORMA_VIA2_IFR)=0x02` re-clear** inside the loop (it
  causes the storm with the perpetual video VBL).
- Re-read `~*SLOT_IFR` and re-dispatch in a **bounded** loop that **breaks when no
  *handled* (non-video) source remains** — i.e. mask out slot bit 6 (video, no
  handler) from the "still pending" test so the perpetual VBL can't keep the loop
  (or a storm) alive.
- Optionally also restore the IER masking (secondary divergence) so only enabled
  interrupts are cleared/dispatched — but beware interactions with our
  `int_since_read` logic and the "clear whole IFR" assumption our emulator fixes
  were built around; change one variable at a time.
- Slower (each cycle ≈ patch → write to image → boot build host → build →
  extract → install → 6200 test ≈ 15-20 min), and still on a theory the
  re-edging analysis partly undermines. Do Opportunity A first.

### Opportunity C — The VBL line mismatch (possible emulator-side "our bug")
Our emulator asserts the Valkyrie VBL on the **slot video line** (slot bit 6,
`id=0x400000`), which the kernel leaves **unhandled** (`{0,0,-1}`). But the
kernel's *forwardable* VBL is **`PMAC_DEV_VBL` on the F108 "Keystone" line**
(F108 IFR bit 6, `id` would be `0x40000000`), which **has** a handler and is
forwarded to the server. Our emulator never fires the F108 Keystone VBL.
- If the Linux server uses the forwarded VBL as a periodic heartbeat/timer, and
  our emulator routes VBL to the wrong (unforwarded) line, the server never gets
  woken post-enumeration → deadlock. This would be a genuine *emulator* bug
  (per the project rule, fixes belong emulator-side where they're ours).
- Investigate: on the real Performa/Valkyrie, does the VBL drive the F108
  Keystone interrupt (f108 bit 6) or the VIA2 slot video line (slot bit 6), or
  both? Check `PowerMac5200-6200 Developer Note.pdf` and MkLinux's
  `powermac_performa.h` / the F108 interrupt table. If Keystone, wire the
  Valkyrie VBL to the F108 VBL line in `valkyrie.cpp`/`f108.cpp`/`primetime.cpp`
  and re-test — this is a **2-second emulator rebuild**, unlike a kernel cycle.
- Strongly worth doing alongside A; it may be the whole answer and needs no
  kernel change.

**Recommended order:** A (+ the C check) first to nail *which* interrupt is
missing; then C if it's the VBL line (fast, emulator-side), or B if it's genuinely
the slot-drain (kernel-side, dgatwood parity).

---

## 6. Tooling, state, and reproduction

### Disks / kernels
- 6200 test disks (repo root): `macos.img` (IDE + SCSI ID 0, HFS boots the Mach
  kernel), `mklinux-r2.img` (SCSI ID 1, ext2 root @ offset 32768, 2.0.38 server).
- Swap the Mach kernel on `macos.img`:
  `hmount macos.img; hcopy -r <file> ":System Folder:Extensions:Mach Kernel";
  hattrib -t zsys -c MACS ":System Folder:Extensions:Mach Kernel"; humount`
  (hfsutils in `/opt/homebrew/bin`).
- Kernels: `cordyceps/kernels/MK_performa` (stock, boots to login — current default on
  `macos.img`), `cordyceps/kernels/MK_new` (from-source, deadlocks post-enumeration),
  `cordyceps/kernels/MK_fixed` (attempt #1, regressed). Symbols: `cordyceps/kernels/MK.map`.
- After **every** hard-kill of the emulator, `e2fsck -fy` the ext2 image (the
  binary is `/opt/homebrew/Cellar/e2fsprogs/1.47.4/sbin/{e2fsck,debugfs}`):
  `e2fsck -fy "mklinux-r2.img?offset=32768"`.

### Run the 6200
- `cordyceps/scripts/run6200r2.sh` — pm6200, `--hdd_img macos.img`,
  `--scsi_hdd_img macos.img:mklinux-r2.img`, `--rambank1_size 32`, slirp.
  Add `-d` to enter the built-in debugger (reads commands from stdin).

### Built-in debugger (rebuild-free runtime tracing)
- Launch with `-d`; feed chained commands via a stdin pipe held open by a
  trailing `sleep`, e.g.
  `( echo "until 0xADDR"; echo "regs"; echo "disas 40,0xADDR"; echo "dump 16w,0xADDR"; sleep 1200 ) | <emulator> >log 2>&1 &`
- `until X` runs to effective PC=X (single-shot, chainable); `regs`, `disas N,X`,
  `dump NT,X` (T=b/w/d). The Mach kernel runs at its link addr `0x002xxxxx`; the
  server at `0x10xxxxxx`. Boot to the hang ≈ 90-115 s of wall time.
- Only `until` exists (no persistent/conditional breakpoints).

### Emulator interrupt/SCSI logging (how attempt-diagnostics were done)
- Temporary `LOG_F(INFO, "IRQLOG ...")` in `primetime.cpp`
  `ack_int`/`ack_cpu_int`/`clear_cpu_int` and `SCSILOG` in `sc53c94.cpp`
  `exec_command`/`update_irq`. `id` decode: `0x2`=VIA1/level1, `mask 0x04`=VIA2/
  level2, `0x800`=SCSI IRQ, `0x20000000`=F108 IDE0, `0x400000`=slot video.
  Reverted after use; `git checkout` the files, `make chungusppc` in `build/`
  (≈2 s incremental).

### Kernel rebuild on the pm7500 build host (proven this session)
- **Different** emulator instance + disks: `cordyceps/scripts/run7500.sh`
  (`-w mklinux-selfhost/debug/pm7500`, disks `macos.img:mklinux.img`; the 4 GB
  `mklinux.img` holds the ODE tree at `/usr/src/osfmk`). Reachable by telnet on
  `tcp:2324` via `python3 cordyceps/scripts/tn.py root dingusppc '<cmd>' <timeout>`.
- Edit source **while the build host is halted** with debugfs into the image
  (`IMG="mklinux-selfhost/debug/pm7500/mklinux.img?offset=32768"`):
  `debugfs -w -R "rm <path>" IMG; debugfs -w -R "write <hostfile> <path>" IMG`,
  then `e2fsck -fy IMG`. Source path:
  `/usr/src/osfmk/src/mach_kernel/ppc/POWERMAC/interrupt_performa.c`.
- Build: boot the host, then telnet-run `/tmp/prod-build.sh` detached
  (`(sh /tmp/prod-build.sh > /tmp/prod.out 2>&1 &)`), poll `/tmp/prod.out` for
  `PROD_DONE`. It removes `interrupt_performa.o` + `scsi_53C94_hdw.o`, builds
  `mach_kernel MACH_KERNEL_CONFIG=PRODUCTION`, `makeboot PRODUCTION`. `sync`
  before killing the host; extract with
  `debugfs -R "dump /usr/src/osfmk/obj/powermac/mach_kernel/PRODUCTION/Mach_Kernel cordyceps/kernels/MK_out" IMG`.
- The build-host `interrupt_performa.c` is currently the **patched (attempt #1)**
  version. To iterate, edit `cordyceps/patches/interrupt_performa.c.attempt1` (or start from
  `cordyceps/osfmk-src/.../interrupt_performa.c` original) and write it back.

### Ghidra (static binary diff — cracked scsi_info and the interrupt path)
- `analyzeHeadless` at `/opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless`
  (12.1.3). ELFs (32-byte `MACH_BOOT_IMAGE` header stripped): `cordyceps/kernels/MK_new.elf`,
  `cordyceps/kernels/MK_performa.elf` (both `PowerPC:BE:32:default` @ `0x00200000`). Symbol
  import file `cordyceps/ghidra/mk_symbols.txt`. Decompile/hunt script `cordyceps/ghidra/gh_analyze.py`
  (reads addrs from `cordyceps/ghidra/gh_addrs_<PROG>.txt`, writes `cordyceps/ghidra/gh_out_<PROG>.txt`);
  the fork also left `~/ghidra_scripts/GhAnalyze.java` for locating functions in
  the stripped stock kernel by constant/string.

### Source & symbol references
- osfmk source tree (read-only reference, matches the build host):
  `cordyceps/osfmk-src/osfmk/src/mach_kernel`. Interrupt code:
  `ppc/POWERMAC/interrupt_performa.c` (handler tables ~150-210; `performa_interrupt`
  376; `performa_via2_slot_interrupt` 512; `performa_f108_interrupt` 544).
- Server binary + symbols: `cordyceps/server/srv-vmlinux`, `cordyceps/server/srv-System.map`
  (`fake_interrupt_thread`, `fake_interrupt_cond`, `condition_wait`, etc.).
- Key kernel addrs (from `cordyceps/kernels/MK.map`): `create_fake_interrupt` 0x002644c0,
  `ihandler` 0x00264500, `performa_interrupt` 0x0026757c,
  `performa_via1_interrupt` 0x002675e4, `performa_via2_interrupt` 0x00267764,
  `performa_via2_slot_interrupt` 0x00267830, `performa_f108_interrupt` 0x002678f0.
- Server wait-loop regs at the hang: PC `0x100EE804`, wait object
  `r31=0x1012ED84` (`+0x2C=4` lock bit stuck, `+0x34=0` never signaled).

### Constraints (still in effect)
- Push ONLY to `chungus` (`git@github.com:timdoug/chungusppc.git`), NEVER
  `origin`. `main` is the only branch. Commit/push only when explicitly asked.
- Fixes belong emulator-side where they're *our* bug; authentic guest driver
  behavior stays authentic. Use the R2 root (`mklinux-r2.img`), not DR3.
