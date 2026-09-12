# Installing MkLinux

MkLinux is a Mach 3.0 based Linux for Old World Power Macs, started by Apple and
later carried on by the MkLinux Developers Association. It isn't booted directly:
a Mac OS extension loads the Mach kernel and hands off to it, so a working Mac OS
install has to come first.

Most of this describes DR3, Apple's last release. The community's later R1 and R2
releases install much the same way; see [MkLinux R2](#mklinux-r2) at the end for
what differs.

The whole install runs off the MkLinux CD, so leave the network unconfigured
throughout. Networking can be set up afterwards with `--enet_backend=slirp`.

## What you need

* A 4 MB "TNT" Boot ROM (Power Mac 7200/7500/8500/9500). CRC32 `9630c68b` works.
* A bootable Mac OS install CD. 7.6.1 is contemporary with DR3; avoid 8.5 and
  newer, which postdate the DR3 booter.
* The MkLinux DR3 CD image.

Use `-m pm7200` or `-m pm7500` with the same TNT ROM. The rebuilt Mach kernel
and Linux 2.0.40 server also boot on `pm7500`; see the
[self-hosted build instructions](../../mklinux-selfhost/README.md).

On the 7500, the additional MESH controller is SCSI bus 0; `--hdd_img` and
`--cdr_img` attach to the other controller, bus 1. Thus the Mac OS `pdisk`
names below become `/dev/scsi1.0` and `/dev/scsi1.1`. The Linux disk remains
`sdb`, and the installed root device remains `/dev/sdb2`. Leave `--hdd_img2`
and `--cdr_img2` empty for this configuration.

The 7500's Control video also passed Xpmac/GNOME at 640×480 with 32-bit pixels,
using `--mon_id=VGA-SVGA --gfxmem_size=4`. Run `startx` from the guest console;
the installed Xpmac uses the mode inherited from Mac OS. Checks covered keyboard
input, colored terminal text and scrolling, overlapping windows, dragging and
repainting, switching between console VT 1 and X on VT 7 with `chvt`, and
exiting X back to a usable color console. The guest then shut down cleanly.
No additional emulator, Mach or Linux changes were needed. This desktop check
does not establish X coverage on the other models with Control video.

## Power Mac 8500

`pm8500` boots the same rebuilt Mach kernel and Linux 2.0.40 server with its
default 604 CPU, the `9630C68B` TNT ROM, universal Mac OS 7.6.1 installation
and R2 booter. Use separate copies of the working disks:

```
chungusppc -r -m pm8500 -b "9630C68B - Power Mac 7200&7500&8500&9500 v2.ROM" \
    --rambank1_size 128 --mon_id=VGA-SVGA \
    --hdd_img "universal-macos.img:mklinux.img" \
    --cdr_img "MkLinux R2 RC5.toast" --cdr_img2 "MkLinux R2 RC5.toast" \
    --enet_backend=slirp --enet_hostfwd=tcp:2324:23
```

As on the 7500, the disks above are on Curio SCSI (Mach bus 1), preserving
`rootdev=/dev/sdb2`. With a CD on each controller, MESH's CD appears first as
`/dev/scd0`, followed by Curio's as `/dev/scd1`. The configuration uses
instruction timing, without `--realtime`.

The 8500 passed login with a color Control-video console, 4 MiB checksum
comparisons for both SCSI CD paths and filesystem writes, DHCP, DNS, gateway
ping, a forwarded telnet connection, and checksum-verified 2 MiB Ethernet
transfers in both directions. A guest reboot returned to login with DHCP and
the saved file checksums intact; clean shutdown also passed. No additional
Mach or Linux changes are needed.
MESH hard-disk writes, the extra composite/S-video hardware, X, audio playback
and floppy I/O remain untested on this model.

## Later TNT ROM revisions

The later TNT ROMs are both 4 MiB images. Their Apple checksums, firmware
versions and file CRC32 values distinguish them:

| ROM | Firmware version | File CRC32 | Intended model family |
| --- | --- | --- | --- |
| `960E4BE9` (v1) | `077D.34F2` | `7910cdf9` | 7300, 7600, original 8600/9600 |
| `960FC647` (v2) | `077D.34F5` | `14d126f4` | Enhanced 8600/9600 |

Apple's [enhanced 8600/9600 developer note](https://manualzz.com/doc/1294509/apple-power-macintosh-8600-250--8600-300--9600-300--9600-...)
describes the later ROM's support for the Mach 5 processor card, its inline
cache and Brick controller, and higher clock frequencies and multipliers.
Those models shipped with System 7.6.1. ChungusPPC currently selects an ordinary
604e for these TNT models; loading v2 does not add a Mach 5 processor or model
its inline-cache hardware.

The `pm7300`, `pm7600` and `pm8600` configurations passed boot, 4 MiB SCSI filesystem
and CD checksum checks on both controllers, DHCP, DNS, ping, telnet, 2 MiB
Ethernet transfers in both directions, reboot persistence and clean shutdown
with v1. Use the 8500 command above with the corresponding model selector and the
`960E4BE9` ROM. The guest disks, root device, R2 booter and rebuilt Mach/Linux
binaries remain the same.
The `pm8600` configuration also passed the same checks with v2, including a
color console, guest reboot and clean shutdown, using its default 604e CPU.

The current `pm7300` and `pm7600` definitions instantiate the same devices and
default 604e CPU. Selector tests therefore do not establish distinct board or
AV-capture implementations for those machines.

The 9500 and 9600 have no built-in Control video. The emulated `AtiMach64Gx`
PCI card expects `113-32900-004_Apple_MACH64.bin` in the working directory;
the motherboard ROM does not replace that separate graphics-card ROM.
The examined `-004` image is byte-for-byte identical to `113-32900-104 Apple
MACH64.BIN` (MD5 `e6375717e5514ff1f609d3200a7d116c`) and identifies itself
internally as `113-32900-104`, with `ATY,Mem#=100-31602-00` and
`ATY,Card#=102-329XX-XX`. The `-101` image is
different (MD5 `ee7dac510963e74d10f10b15a26d63f5`) and has placeholder internal
ROM, memory and card numbers (`000-00000-000`). Both distinct images are
32 KiB and identify the same Mach64 GX PCI device; their filenames do not
establish different GPU models. Mac OS 7.6.1 was checked with both distinct
images at 640×480 in 256, Thousands and Millions of colors. The `-101` driver
also exercises eight-bit host bitmap uploads with an OR raster operation
when returning to 256 colors; the GX emulation implements that path.

## Power Mac 9500 and 9600 with Mach64 GX

The GX emulation needs a valid chip ID, the Macintosh big-endian framebuffer
aperture, DAC byte-register readback and the 2D drawing operations used by Mac
OS. These fixes restore ordinary Mac OS 7.6.1 graphics as well as the MkLinux
boot path; they do not require changing Mach or Linux. Mac OS on the 9500 was
checked at 640×480 in 256, Thousands and Millions of colors, including text,
icons, depth changes and window movement/repainting. See the
[GX implementation notes](../developers/atimach64gx.md) for remaining limits.

For MkLinux, use 832×624 in 256 colors with the fixed-frequency 16-inch monitor:

```
chungusppc -r -m pm9500 -b "9630C68B - Power Mac 7200&7500&8500&9500 v2.ROM" \
    --rambank1_size 128 --mon_id=MacRGB16in --pci_A1=AtiMach64Gx \
    --hdd_img "universal-macos.img:mklinux.img" \
    --cdr_img "MkLinux R2 RC5.toast" --cdr_img2 "MkLinux R2 RC5.toast" \
    --enet_backend=slirp --enet_hostfwd=tcp:2324:23
```

Place the separate card ROM under the expected filename above in the working
directory. Use private copies of the universal Mac OS and MkLinux disks; the
disk order and `rootdev=/dev/sdb2` are unchanged. Start without `--realtime`.

At 640×480 in 256 colors, the tested driver supplies framebuffer address
`0x81800200`. The existing Linux console mapping omits that address's page
offset, so clearing the end of the framebuffer crosses the mapping and faults.
832×624 leaves enough padding in the final mapped page for this offset. This
is a video-mode workaround for the retained guest binaries, separate from the
GX emulator fixes; other MkLinux resolutions and direct-color modes remain
unvalidated.

Xpmac/GNOME also passed on the 9500 with the `-104` GX card ROM at 832×624 in
256 colors. Run `startx` from the console. Keyboard input, colored terminal
text and scrolling, overlapping windows, dragging and repainting, switching
between console VT 1 and X on VT 7 with `chvt`, and exiting X back to the color
console all worked, followed by clean shutdown. No additional emulator or guest
changes were needed. The eight-bit desktop can exhaust its colormap: an extra
xterm reported color-allocation warnings. X remains untested on the 9600 and
with the `-101` card ROM.

The 9500 with its default 604 CPU and the `-104` card ROM passed a color login,
4 MiB filesystem and both SCSI CD checksum checks, DHCP, DNS, gateway ping,
telnet, 2 MiB Ethernet transfers in both directions, persistence across a
cold start and guest reboot, and clean shutdown.
The distinct `-101` card ROM also passed MkLinux login on the 9500 at
832×624 in 256 colors, the filesystem and both CD checksums, DHCP, DNS, ping,
telnet, guest-reboot persistence and clean shutdown.

The 9600 with its default 604e CPU also passed the storage, network,
reboot-persistence and shutdown checks with both motherboard ROM revisions:

| Motherboard ROM | Graphics card ROM | Slot | Framebuffer BAR base |
| --- | --- | --- | --- |
| v1 `960E4BE9` | `-104` | `pci_A1` (first Bandit bus) | `0x81000000` |
| v2 `960FC647` | `-104` | `pci_D2` (second Bandit bus) | `0x90000000` |

Use `-m pm9600`, the corresponding motherboard ROM filename and the tested
slot flag in the command above. The v2 run exercises the second PCI bus;
it does not add the enhanced machine's Mach 5/Brick processor-card hardware.

## Power Mac 6100, 7100 and 8100

The rebuilt Mach kernel and Linux 2.0.40 server also boot on `pm6100`, `pm7100`
and `pm8100` with a universal Mac OS 7.6.1 installation and the R2 booter.
The tested ROM has Apple
checksum `9FEB69B3` (file CRC32 `a43fadbc`). The newer install disc used here is
`MkLinux R2 RC5.toast`.

```
chungusppc -r -m pm6100 -b "9FEB69B3 - Power Mac 6100 & 7100 & 8100.ROM" \
    --rambank1_size 64 --rambank2_size 64 --mon_id=VGA-SVGA \
    --hdd_img "universal-macos.img:mklinux.img" \
    --cdr_img "MkLinux R2 RC5.toast" \
    --enet_backend=slirp --enet_hostfwd=tcp:2324:23
```

Keep `rootdev=/dev/sdb2` for this disk order. To prepare a fresh Mac OS disk,
choose **Custom Install → Universal system for any supported computer** in the
7.6 installer, apply the 7.6.1 update, then copy the R2 booter files and rebuilt
Mach kernel into its System Folder as described below.

Use `-m pm7100` or `-m pm8100` in the same command to test those models.
Start all three **without `--realtime`**. Their ROM measures CPU speed to select a
Mac model. With host elapsed time, a fast emulator can measure above the ROM's
100 MHz limit and select an unsupported model, causing 7.6.1 to reject even a
universal startup disk. The emulator selects an instruction period for each
model: 16 ns for the 6100/60, 13 ns for the 7100/66, and 11 ns for the 8100/80.
Using the 6100's timing on the 7100 produces prototype Gestalt ID 111, which the
R2 booter rejects with `err=-111`. No additional launch flag is needed for these
model-specific defaults.

The 8100 also needs its second SCSI controller, including its DMA channel and
separate VIA2 interrupt. Leaving that register bank unimplemented hangs Mac OS
during its SCSI scan. `--hdd_img2` and `--cdr_img2` attach to this controller,
which MkLinux enumerates as bus 0; the usual `--hdd_img` and `--cdr_img` are on
bus 1. Keep the second bus's hard-disk list empty to retain `/dev/sdb2` with the
disk order above. A CD on the second bus works with `--cdr_img2`.

These models also rely on the earlier fixes for cyclic timer phase preservation,
AWACS codec busy-bit readback, and AMIC native interrupt delivery. The same
rebuilt Mach and Linux binaries used on the 7200/7500 work unchanged. AMIC
Ethernet uses the MACE controller with a 48 KiB receive ring and two fixed
transmit buffers. Its address ROM supplies `08:00:07:61:00:01`.

With `--enet_backend=slirp`, configure `eth0` for DHCP; the guest receives
`10.0.2.15`, gateway `10.0.2.2`, and DNS `10.0.2.3`. The example forwards host
`127.0.0.1:2324` to the guest's telnet port. No additional Mach or Linux patches
are needed for networking. Samba and AppleTalk can remain enabled at startup;
the earlier workaround disabling their `S91` boot links is no longer needed.

The 7100/8100 validation covered booting the rebuilt kernels, DHCP, DNS, ping,
telnet, checksum-verified 2 MiB Ethernet transfers in both directions, disk
persistence across shutdown/startup or reboot, CD reads, and clean shutdown.
Both also completed a guest-initiated reboot with Samba and AppleTalk enabled.
The 8100's second SCSI bus passed a separate 4 MiB CD checksum comparison;
hard-disk writes on that bus remain untested. NuBus cards, floppy I/O and audio
playback are outside this pass. The 6100 was rechecked for boot and Ethernet
after these emulator changes.

The 6100's built-in video also passed Xpmac/GNOME at 640×480 in 256 colors,
started with `startx` from the console. Checks covered keyboard input, colored
terminal text and scrolling, overlapping windows, dragging and repainting,
`chvt` between console VT 1 and X on VT 7, and exiting X back to a working color
console. Colors were restored correctly when switching consoles. The guest
then halted; the emulator was closed afterward. No additional emulator or
guest changes were needed.
An extra xterm reported colormap-allocation warnings at this eight-bit depth.
X remains untested on the 7100/8100 and with expansion graphics cards.

## Power Mac 5400 and Performa / Power Mac 6400

`pm5400` and `pm6400` boot the same rebuilt Mach kernel and Linux 2.0.40 server,
using the universal Mac OS 7.6.1 installation and R2 booter above. Use copies of the
working disks and the `6F5724C0` Performa 6400 ROM:

```
chungusppc -r -m pm6400 -b "6F5724C0 - Performa 6400.ROM" \
    --rambank1_size 32 --rambank2_size 32 \
    --rambank3_size 32 --rambank4_size 32 --mon_id=VGA-SVGA \
    --hdd_img2 "universal-macos.img:mklinux.img" \
    --cdr_img2 "MkLinux R2 RC5.toast"
```

Use `-m pm5400` with the same Alchemy ROM to run the 5400. Both models use
MESH for SCSI, so both boot disks belong in `--hdd_img2` and the
CD in `--cdr_img2`. This retains `rootdev=/dev/sdb2`. Its default `--hdd_img`
attachment is IDE; a separate scratch image there appears as `/dev/hda` in
MkLinux. The command above uses instruction timing, without `--realtime`.

MESH needs working DMA writes, accurate transferred-byte counts on reads, and
a retry when the data command follows DMA startup. Without these, Mac OS can
hang at Happy Mac on a pending disk write. Valkyrie's 16-bit display also needs
the CLUT applied separately to each RGB component for console colors. These
are emulator fixes; the Mach and Linux binaries need no additional patches.

Both models passed 4 MiB checksum comparisons for SCSI filesystem writes,
raw IDE writes and reads, and MESH CD reads, plus a guest-initiated reboot.
The 5400 retained its test data after reboot; the 6400 also passed persistence
across a clean shutdown and cold start. Both shut down cleanly.
Mac OS's booter and MkLinux's 16-bit console display in color.

The 6400 also passed Xpmac/GNOME at 640×480 with 16-bit pixels, started with
`startx` from the console. Checks covered keyboard input, colored terminal text
and scrolling, overlapping windows, dragging and repainting, `chvt` between
console VT 1 and X on VT 7, and exiting X back to a working color console.
The guest shut down cleanly. No additional emulator or guest changes were
needed. X remains untested on the 5400. Booting from IDE, audio playback and
floppy I/O remain untested; the 6400 desktop run logged audio DMA errors.

The [6400 specifications](https://support.apple.com/en-ca/112092) list two PCI
slots and a Comm Slot II, with no built-in Ethernet. Period upgrades included
Farallon PCI and Comm Slot II 10/100 cards
([Farallon's announcement](https://www.mactech.com/1998/09/28/npl-farallon-ships-10-100-for-comm-slot-ii/)).
ChungusPPC currently emulates neither kind of Ethernet expansion card, so
`--enet_backend=slirp` alone cannot provide networking on this model.

## Power Mac 6500, 5500 and Twentieth Anniversary Macintosh

The Gazelle models use the `6E92FE08` ROM (file CRC32 `084646f4`) and onboard
ATI Rage graphics. MkLinux's [hardware list](https://www.mklinux.org/getting_started/machine.html)
includes all three models as supported. Keep the universal Mac OS 7.6.1
installation, R2 booter and rebuilt Mach/Linux binaries. Use separate copies
of the disks for each model:

```
chungusppc -r -m pm6500 -b "6E92FE08 - Power Mac 6500.ROM" \
    --rambank1_size 64 --rambank2_size 64 --mon_id=VGA-SVGA \
    --hdd_img2 "universal-macos.img:mklinux.img" \
    --cdr_img2 "MkLinux R2 RC5.toast"
```

Use `-m pm5500` or `-m tam` for the other Gazelle models. As on the 6400, these
attachments use MESH SCSI and preserve `rootdev=/dev/sdb2`; a separate scratch
disk in `--hdd_img` appears as IDE `/dev/hda`. Start without `--realtime`.

Before booting MkLinux, select **Thousands** in the Mac OS **Monitors** control
panel and restart. At 640×480 in 256 colors, this ROM supplies framebuffer
address `0x81800480`. The retained Linux console code maps the pixel bytes but
does not include that page offset, and faults clearing the last part of the
screen. Thousands works with the existing binaries. This is a tested video-mode
workaround; it does not establish that all real Gazelle machines failed in
256-color mode. No guest kernel patch is included for it.

The emulator must retain Gazelle's `pci_F1=AtiRageGT` board default when
registering generic PCI-host settings. It also needs MESH PIO writes to drain
each full FIFO burst, and the ATI DAC lookup table applied in RGB555 mode.
The latter restores MkLinux's console colors. The ATI draw engine now handles
the monochrome glyph uploads, pattern fills and raster operations used by Mac
OS 7.6.1. On the 6500, text, icons and window repainting were checked at 640×480
in 256, Thousands and Millions of colors. The 5500 and TAM also passed desktop
and window-repainting checks at 640×480 in Thousands. See the
[ATI drawing notes](../developers/atirage.md#mac-os-drawing) for remaining limits.

The 6500, 5500 and TAM passed login, 4 MiB SCSI filesystem and raw IDE checksum
comparisons, CD reads, persistence after a guest reboot, and clean shutdown.
The 6500 also retained its test data across a cold start. These runs use
640×480 VGA output; they do not cover the TAM's native LCD mode, every other
built-in display mode, IDE boot, audio playback or floppy I/O. X/GNOME was
also checked on the 6500 at 640×480 in Thousands, including terminal output
and scrolling; X remains untested on the 5500 and TAM. As on the 6400, there
is currently no emulated Ethernet controller for these models.

## 1. Create the disks

```
dd if=/dev/zero of=macos.img   bs=1 count=0 seek=1g
dd if=/dev/zero of=mklinux.img bs=1 count=0 seek=2g
```

Hard disks land on SCSI IDs 0 and 1 in the order given, CD-ROMs start at ID 3.
That puts the DR3 CD on ID 3, which is `/dev/scd0` — DR3's default `rootdev`, so
no editing is needed to install.

## 2. Install Mac OS

```
chungusppc -r -b bootrom.bin -m pm7200 --rambank1_size 128 \
    --hdd_img "macos.img:mklinux.img" \
    --cdr_img "MacOS761.iso:MkLinux-DR3.iso"
```

Initialize `macos.img` before installing. Apple's **Drive Setup** will refuse it:
ChungusPPC's disks report as `QUANTUM / Emulated Disk`, and Drive Setup only
touches Apple-branded drives. Use **Apple HD SC Setup 7.3.5** from the DR3 CD's
`MacOS Utilities` folder instead, which partitions any SCSI drive.

Then run the Mac OS installer and target the new volume.

In Mac OS's **Date & Time** control panel, set the time zone and daylight saving
setting to match the host, then check the displayed date and time. MkLinux's
Mach kernel uses the Mac's saved time zone to convert its local hardware clock
to UTC. An unspecified Mac time zone can leave MkLinux hours off even when the
Mac's menu clock looks right. Set Linux's time zone to match as well.

## 3. Install the Mac OS side of MkLinux

From `Mac Files` on the DR3 CD, into your System Folder:

| File | Destination |
| --- | --- |
| `MkLinux Booter`, `Mach Kernel` | Extensions |
| `MkLinux` | Control Panels |
| `lilo.conf`, `MkLinux.prefs` | Preferences |

## 4. Give the Linux disk a partition map

Still in Mac OS, run `pdisk` from the CD's `MacOS Utilities`:

```
Top level command: e
Name of device:    /dev/scsi0.1
Command:           i         (initialize the map)
Command:           w         (answer y)
Command:           q
Top level command: q
```

Check the device name — `/dev/scsi0.1` is `mklinux.img`; `/dev/scsi0.0` is your
Mac OS disk. `L` at the top level lists every device if you want to confirm.

This step is not optional busywork. The installer picks its partitioner by
reading block 0 and testing for the `ER` Driver Descriptor Record. Without it the
installer silently runs `fdisk`, whose PC-style partitions MkLinux cannot use.

## 5. Run the installer

Restart and pick **MkLinux** at the splash screen (10 second countdown, Mac OS by
default). The installer boots from the CD.

Choose **Local CD-ROM**, then **Install**. At *Partition Disks* select `sdb` and
**Edit** to get pdisk, then create a root and a swap partition:

```
Command: c
First block:       64
Length in blocks:  3669952
Name of partition: root

Command: c
First block:       3670016
Length in blocks:  524287
Name of partition: swap

Command: w        (answer y)
Command: q
```

Lowercase `c` creates `Apple_UNIX_SVR2` partitions, the only type the installer
displays. The name `swap` is what marks the second one as swap and keeps it out
of the root partition list.

Choose **Done**, pick `root` at *Select Root Partition*, and let *Setup
filesystems* format it. Decline network configuration — in particular BOOTP and
DHCP, which will stall on retries that nothing can answer.

## 6. Point the booter at the installed system

`lilo.conf` still reads `rootdev=/dev/scd0`, so the machine keeps booting the
installer from CD. Back in Mac OS, open the **MkLinux** control panel, click
**Custom...** to edit `lilo.conf` in SimpleText, and set what the installer told
you — for the layout above:

```
rootdev=/dev/sdb2
```

`sdb` because the disk is the second one, and `2` because Linux numbers every
partition map entry, including `Apple_partition_map` itself.

`MkLinux.prefs` alongside it holds `bootos` (`MacOS` or `MkLinux`) and
`bootdelay` in seconds. Raising the delay is worth it if you switch often.

## MkLinux R2

R2 ("MkLinux Release 2.0") is Linux 2.0.38 on the same microkernel, with a Red
Hat 6.2 userland: gcc 2.95.4, glibc 2.1.3, perl 5.6.1. It installs like DR3, with
these differences.

Its CD carries its own `MkLinux Booter`, `Mach Kernel` and `MkLinux` control
panel, and its Mac OS installer places them for you. Steps 1 through 4 are
otherwise unchanged, and step 6 still applies — the booter ships pointing at
`/dev/scd0`.

The installer is Red Hat's `newt` one rather than DR3's, so step 5 differs:

* Pick **Local CDROM**, then **Install**, then **fdisk** at *Disk Setup* — Disk
  Druid is offered but refuses to run. Press **Done** without partitioning; the
  Apple partition map from step 4 is already in place and the Mach server, not
  Linux, owns it.
* At *Current Disk Partitions* the root partition arrives with an **empty Mount
  Point**. Select it, press **F3**, and enter `/`. Nothing is formatted or
  mounted until this is set.
* Check the root partition at *Partitions To Format*.

Afterwards, three things are worth fixing from a console login:

* `PROMPT=no` in `/etc/sysconfig/init`. Otherwise every boot stops for good after
  `Enabling swap space` — `rc.sysinit` runs its body in a background subshell
  while the foreground waits on `/sbin/getkey`, and the `kill -TERM $(pidof
  getkey)` meant to release it doesn't find the process here.
* `nameserver 10.0.2.3` in `/etc/resolv.conf`. The slirp backend answers DNS
  there but its DHCP lease doesn't carry the server.
* A telnet line in `/etc/inetd.conf` and `ttyp0`..`ttyp9` in `/etc/securetty`, if
  you want to reach a root shell from the host over `--enet_hostfwd=tcp:2323:23`.
  R2 enables neither by default.

### Getting X running

`startx` fails with `execve failed for /etc/X11/X (errno 2)`, followed by a run of
`_X11TransSocketUNIXConnect` errors as `xinit` retries against a server that never
started. Xconfigurator runs during the install and reports `Probing found a:
Xpmac`, but nothing installs that server: the component list leaves out every
display server, so only `Xnest`, `Xvfb`, `Xprt` and `Xvnc` are present.
`/usr/X11R6/bin/X` is a symlink to the setuid `Xwrapper`, which execs
`/etc/X11/X`, and that is what does not exist.

The server is on the CD. Install it and point `/etc/X11/X` at it:

```
rpm -ivh /mnt/cdrom/RedHat/RPMS/XFree86-Xpmac-3.3.6-8a.ppc.rpm
ln -sf /usr/X11R6/bin/Xpmac /etc/X11/X
```

Take `Xpmac`, not the `XFree86-FBDev` sitting beside it: FBDev wants a Linux
framebuffer device, and on `osfmach3` the Mach server owns the display. No
`XF86Config` is needed — Xpmac takes its geometry from the Mach framebuffer — and
`startx` then brings up GNOME. `id:5:initdefault:` in `/etc/inittab` makes that
the default, since gdm is installed.

### Changing the screen resolution

MkLinux has no video mode of its own. The booter reads whatever mode Mac OS is
in and hands it over, so the resolution is set from Mac OS and inherited:
`/proc/cmdline` is only `ro`, and there is no `vmode=` anywhere in
`/mach_servers/vmlinux`.

Start the emulator with a monitor that offers more than 640x480, and enough
video memory for it:

```
--mon_id=VGA-SVGA --gfxmem_size=4
```

`VGA-SVGA` is the only monitor in `displayid.cpp` advertising 800x600; it offers
1024x768 and higher too. 1 MB of video memory is uncomfortably tight for 800x600
at 16bpp, hence the 4.

Then boot Mac OS rather than MkLinux, open Monitors, and pick the mode. Two
things to know: the confirmation dialog's default button is **Cancel**, so
Return reverts rather than keeps, and the setting backs out on its own after a
few seconds, which looks exactly like the click not having registered. Select
and confirm in one go. Booting MkLinux afterwards reports the new mode:

```
using video mode 9 (800x600 at 56Hz), 16 bits/pixel
```

### Browsing the web

Dillo 0.8.5 is installed and is the only graphical browser on either CD. DR3's
CD carries KDE launchers and app-defaults for Netscape, but not Netscape itself;
its one graphical browser is `arena`.

Nothing of that vintage can negotiate a modern TLS connection. R2's OpenSSL is
0.9.6m, which predates SNI and everything else a current server expects. The way
through is [Crypto Ancienne](https://github.com/classilla/cryanc), a TLS library
written for pre-C99 compilers and old machines, which speaks TLS 1.2 and 1.3 and
builds with gcc 2.5 or newer. Its `carl` tool has a proxy mode meant for browsers
that predate `CONNECT`, so it terminates TLS itself and hands plaintext back:

```
gcc -O2 -o carl carl.c        # about six minutes on an emulated 601
cp carl /usr/local/bin/
echo 'carl 8080/tcp' >> /etc/services
echo 'carl stream tcp nowait root /usr/local/bin/carl carl -p -t -u' >> /etc/inetd.conf
killall -HUP inetd
```

`-u` sends every request over TLS whether or not the URL said `https`, which is
what lets a browser with no TLS of its own reach a modern site; the cost is that
a plain HTTP only site no longer loads. `-t` drops the ten second transaction
timeout. Then set the proxy in `~/.dillo/dillorc`, or in `http_proxy` for `wget`
and `lynx`:

```
http_proxy=http://127.0.0.1:8080/
```

Crypto Ancienne warns off machines slower than about 40 MHz. The emulated 601
reports 61.64 BogoMIPS and turns a TLS 1.2 fetch around in roughly a second.

Note that `inetd` here does not accept the `address:service` form in
`/etc/inetd.conf`, so `carl` ends up bound to every interface rather than
loopback. That is an open proxy for anything that can reach the guest. It is out
of reach from outside the host as long as the only inbound path is an explicit
`--enet_hostfwd`, but on a bridged or tap backend it would be exposed.

## Troubleshooting

**"mount failed: Invalid argument" while installing R2.** The installer skipped
its own `mke2fs` and is mounting an unformatted partition. Check the mount point
first (above). If it still happens, make the filesystem from the host, at the
partition's byte offset within the image, and press **Retry**:

```
mke2fs -t ext2 -b 4096 -I 128 -O none -E offset=32768 -F mklinux.img 983032
```

`-O none -I 128` keeps it to features Linux 2.0 and e2fsck 1.32 understand.

**Arrow keys print `^[[A` and screens jump backwards.** The tty has been left in
line mode by a subprocess, so newt reads the `ESC` that begins every cursor key
as its cancel key — each arrow press cancels a step. Navigate with Tab, Space and
Enter until you're past it.

**"You don't have any Linux native partitions defined."** The disk has no
`Apple_UNIX_SVR2` partitions; see steps 4 and 5.

**The installer offers fdisk rather than pdisk.** Block 0 has no `ER` signature.
Do step 4.
