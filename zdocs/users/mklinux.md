# Installing MkLinux DR3

MkLinux DR3 is Apple's Mach 3.0 based Linux for Old World Power Macs. It isn't
booted directly: a Mac OS extension loads the Mach kernel and hands off to it, so
a working Mac OS install has to come first.

The whole install runs off the DR3 CD. DingusPPC has no networking, so leave the
network unconfigured throughout.

## What you need

* A 4 MB "TNT" Boot ROM (Power Mac 7200/7500/8500/9500). CRC32 `9630c68b` works.
* A bootable Mac OS install CD. 7.6.1 is contemporary with DR3; avoid 8.5 and
  newer, which postdate the DR3 booter.
* The MkLinux DR3 CD image.

Use `-m pm7200`. The TNT ROM also drives `pm7500`, but that machine instantiates
a second SCSI bus (MESH) with nothing attached to it, and disk utilities hang
scanning it.

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
dingusppc -r -b bootrom.bin -m pm7200 --rambank1_size 128 \
    --hdd_img "macos.img:mklinux.img" \
    --cdr_img "MacOS761.iso:MkLinux-DR3.iso"
```

Initialize `macos.img` before installing. Apple's **Drive Setup** will refuse it:
DingusPPC's disks report as `QUANTUM / Emulated Disk`, and Drive Setup only
touches Apple-branded drives. Use **Apple HD SC Setup 7.3.5** from the DR3 CD's
`MacOS Utilities` folder instead, which partitions any SCSI drive.

Then run the Mac OS installer and target the new volume.

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

## Troubleshooting

**Arrow keys print `^[[A` and screens jump backwards.** The tty has been left in
line mode by a subprocess, so newt reads the `ESC` that begins every cursor key
as its cancel key — each arrow press cancels a step. Navigate with Tab, Space and
Enter until you're past it.

**"You don't have any Linux native partitions defined."** The disk has no
`Apple_UNIX_SVR2` partitions; see steps 4 and 5.

**The installer offers fdisk rather than pdisk.** Block 0 has no `ER` signature.
Do step 4.
