# ChungusPPC User Manual

## Implemented Features

* Interpreter (with 601, FPU, and MMU support)
* IDE and SCSI
* Floppy disk image reading (Raw, Disk Copy 4.2, WOZ v1 and v2)
* ADB mouse, keyboard, and AppleJack (Pippin) controller
* Some audio support
* Basic video output support (i.e. ATI Rage, Control, Platinum)

## Known Working OSes

* Disk Tools (7.1.2 - 8.5)
* Mac OS 7.1.2 - 9.2.2 (from CD or Hard Disk)
* Mac OS X 10.0 - 10.3
* BeOS DR9 - 5.0 (Using a Power Mac; BeBox is currently not supported)
* OpenDarwin 6.6.2
* MkLinux DR3 and R2 (see mklinux.md for the install procedure)

## Disk Initialization

To initialize a disk, you'll first need to boot Disk Tools from a floppy disk image along with a blank hard disk image you want to initialize.

Once booted, start up the Hard Disk Setup application and initialize the drive listed. It may be listed as not mounted.

Once the disk has been initialized, you will need to reboot the machine with the appropriate installation media, such as a CD-ROM image.

Make sure the disk is appropriately sized for the OS you want to install and follow the instructions from the installer carefully.

You may also want to use a third-party program like BlueSCSI to convert a raw HFS image from an emulator like SheepShaver.

## Windows

ChungusPPC uses two windows when booted up; a command line window and a monitor window to display the machine.

## Commands

ChungusPPC is operated using the command line interface. As such, we will list the commands as required. These commands are separated by spaces.

```
-r, --run
```

Run the emulator (using the interpreter).

```
--realtime
```

Advance emulated timers using host elapsed time from startup. This keeps guest
clocks from speeding up or slowing down with the instruction rate. Without
this flag, timing is based on the number of instructions executed.
Cannot be combined with `--deterministic`.
On the Power Mac 6100, leave this off during startup: the ROM measures CPU
speed, and host timing can select an unsupported Mac model. See
[the MkLinux 6100 notes](mklinux.md#power-mac-6100).

```
-d, --debugger
```

Enter the interactive debugger. The user may also enter the debugger at any point by pressing Control and C, when the command line window is selected.

```
-b, --bootrom filename
```

Specifies the Boot ROM path, where `filename` specifies the location of the ROM (optional; looks for bootrom.bin by default)

```
-m, --machine machineid
```

Specify machine ID, where `machineid` is a short string identifier for the machine (i.e. pm6100). (optional; will attempt to determine machine ID from the boot rom otherwise)

```
-w,--workingdir path
```

Specifies working directory, where `path` is a string for the directory the emulator will grab files from.

```
--setenv args
```

Set Open Firmware variables at startup, where `args` is a string where you enter the variables to change.

```
list machines
```

Shows the currently implemented machines within ChungusPPC.

```
list properties
```

Shows the configurable properties, such as the selected disc image and the ram bank sizes.

### Properties

```
--rambank1_size X
--rambank2_size X
--rambank3_size X
--rambank4_size X
```

Set the RAM sizes to use, where X is an integer of a power of 2 up to 512 (depending on the emulated machine).

```
gfxmem_size
```

Specifies the amount of available graphics memory.

```
--fdd_img filename
```

Set the floppy disk image. `filename` is the name of the floppy disk you want to insert into the emulator.

```
--fdd_wr_prot=1
```

Set the floppy to read-only. Raw 720 KiB and 1.44 MiB MFM images also support sector writes with
`--fdd_wr_prot=0`. Disk Copy 4.2 and GCR images remain read-only.

Drop a floppy image on the emulator window to insert it into the empty drive.
Eject the current disk in the guest before inserting another. Scripted input
also accepts `floppy /path/to/disk.img`; it uses the launch-time write-protection
setting.

```
--hdd_img filename
```

Set the hard disk image. `filename` is the name of the floppy disk you want to insert into the emulator. On machines that support SCSI hard disk, you can also use the colon (:) to add multiple hard disks.

```
--cdr_img filename
```

Set the CD ROM image. `filename` is the name of the CD ROM image you want to insert into the emulator.

```
hdd_config
cdr_config
```

These properties determine where in the bus the hard disk and CD ROM are set up in. For example, these can be `Ide0:0` or `CmdAta0:0`.

```
--cpu
```

Change which version of the PowerPC CPU to use

```
--emmo
```

Access the factory tests

```
--serial_backend=stdio
--serial_backend=socket
```

Change where the output of Open Firmware is directed to, either to the command line (with stdio) or a Unix socket (unavailable in Windows builds). Open Firmware 1.x outputs here by default.

```
mon_id
```

Allows users to specify what monitor they are using. This affects what resolutions and color modes are available. Current valid options include, but are not limited to, `MacRGB12in`, `MacColor21in`, `Multiscan17in`, and `VGA-SVGA`.

```
pci_A1
pci_B1
pci_C1
```

Specified what devices are connected to a particular PCI slot. Not supported on NuBus machines such as the Power Mac 6100.

```
--adb_devices device_name
```

Set the ADB devices to attach where `device_name` is the name of the device to attach, comma-separated.

### Command Line Examples

```
chungusppc -b bootrom-6100.bin --rambank1_size 64 --rambank2_size 64 --hdd_img "System_712.dsk"
```

The user has specified their own ROM file, which is for a Power Macintosh 6100 and has also set up two separate RAM banks to use 64 MB each. Note that if a second RAM bank is to be specified for the 6100, it should be the same size as the first RAM bank. With only a hard disk specified, the machine will immediately boot to the OS on the hard disk.

```
chungusppc -b "Power_Mac_G3_Beige.ROM" -r --rambank1_size 128 --fdd_img "DiskTools_8.5.img"
```

Here, the user has attached a floppy disk image. They've chosen to boot it from a G3 and the first RAM bank is set to 128 MB.

```
chungusppc -b "Power_Mac_G3_Beige.ROM" -d --rambank1_size 64 --rambank2_size 64 --cdr_img "OpenDarwin_662.cdr"
```

The debugger is enabled here, due to the presence of `-d`. The CD ROM image will be loaded in.

## Keyboard Shortcuts

You can use these keyboard commands while the emulator is running:

* Control-G: mouse grab
* Control-S: scale quality
* Control-F: fullscreen
* Control-Shift-F: fullscreen reverse
* Control-+: bigger
* Control--: smaller
* Control-Alt-R: toggle host real-time timing (Control-Option-R on macOS)
* Control-L: log toggle
* Control-D: debugger

The percentage in the window title is display magnification: `200%` means
each guest pixel is displayed at twice its original size in each direction.

By default, emulated timers advance with the number of instructions executed.
Guest clocks can therefore run faster or slower than host time as the workload
changes. Control-Alt-R switches those timers to host elapsed time; the log
reports `g_realtime: enabled` or `disabled`. This toggle lasts for the emulator
process. Pass `--realtime` to start with host elapsed time on every launch.
The `-r` command-line option only skips the debugger at startup.

## Driving the emulator from outside

On platforms with POSIX signals, the emulator can be looked at and typed into
without a human in front of the window. This is useful for scripting an install
or for reaching a guest that has no serial console or network yet.

Sending `SIGUSR1` writes the guest's screen, at the guest's own resolution, to
`chungusppc-screen.bmp` in the working directory:

```
kill -USR1 $(pgrep chungusppc)
```

Sending `SIGUSR2` reads `chungusppc-input.txt` from the working directory and
types it into the guest. Each line is either `text` followed by characters to
type, or `key` followed by the name of a single key, optionally prefixed with
`Shift+`, `Control+`, `Option+` or `Command+`. Lines starting with `#` are
ignored:

```
text root
key RETURN
key Control+C
```

Keys are handed to the guest one press or release per event poll, so its
keyboard driver sees each transition separately.

A `mouse` line moves the pointer or works its buttons, which is the only way to
reach anything a guest won't let you get to from the keyboard:

```
mouse to 120 48
mouse click
mouse down
mouse by -10 0
mouse up
```

`to` takes screen coordinates, `by` is relative, and `click`, `down` and `up`
take `left`, `right` or `middle`, defaulting to left.

Treat a position as approximate. `to` drives the pointer into the top left
corner first, because nothing on this side knows where the guest is drawing it,
and how far the guest moves for a given delta is the guest's business: an ADB
mouse reports a signed 7 bit delta per poll, and Mac OS scales small ones down
sharply. Measured against a target 320 pixels away, a 32 pixel step lands within
a few pixels while an 8 pixel step covers barely half the distance. X11 guests
damp it differently again. `mouse step` and `mouse rate` tune the pixels per
report and the gap between them, so the way to hit something small is to move,
take a screenshot, and correct:

```
mouse step 32
mouse rate 16
```

`tools/chungusppc-drive.sh` wraps both: it types its arguments and then captures the
screen. Run it from the emulator's working directory.

```
tools/chungusppc-drive.sh 'text root' 'key RETURN'
```

Set `CHUNGUSPPC_WAIT` to override how long it waits for the queue to drain, which a
pointer move needs since it is made of many small steps.

Once the guest has networking, `tools/chungusppc-shell.py` is easier still — it runs
commands over telnet and prints their output, given `--enet_hostfwd=tcp:2323:23`.

## Accessing Open Firmware

After booting from a PCI Power Mac ROM without any disk images, enter the debugger and change the NVRAM property `auto-boot?` to false. Exit out of the emulator and boot it back up to access it.

## Supported machines

The machines that currently work the best are the Power Mac 6100, the Power Mac 7500, and the Power Mac G3.

Early implementations of the iMac G3, Power Mac G3 Blue and White, and Apple Pippin are also present.

The Power Macintosh 5200 (`pm5200`) and Performa 6200 (`pm6200`) are new. They
boot Mac OS off an IDE image, and reach the "insert disk" screen without one.
Give them `--rambank1_size 32`; the 8 MB default is not enough to finish
booting. A SCSI CD-ROM does not work yet.

A second IDE disk can be attached to any machine with `--hdd2_img`, which puts
it on `Ide0:1`; `--hdd2_config` moves it to another bus or unit.
See the [Cordyceps notes](../developers/cordyceps.md) for the address map and
for the guesses that a full boot would confirm or disprove.

## Debugger

The debugger can be used to show what code is currently executing, the contents of memory as hex or 68K assembly or PowerPC assembly, NVRAM variables, memory regions, and CPU registers. It can also be used to change memory, registers, and nvram variables. It can step through instructions one at a time or many instructions at once.

## Quirks

### Mouse Grabbing

While the emulator display window is in focus, press Control-G to hide the host mouse cursor to control the guest mouse cursor directly. Press Control-G to show the host mouse cursor again and control the guest mouse cursor only when the host mouse cursor is within the emulator display window.

Press Control-S to toggle the scaling method of the emulator display window. The methods are nearest (best used when the scale factor is an integer value) and linear (or smooth mode which is not as sharp but helps display quality when the scale factor is not an integer value).

### CD ROM Images

Currently, ISO images are supported. However, support is not yet implemented for multi-mode CD images.

### Hard Disks

Because Sheepshaver, Basilisk II, and Mini vMac operate on raw disks, it is required to a program such as BlueSCSI to make their hard disk images work in an emulator like ChungusPPC. This is because the Mac OS normally requires certain values in the hard disks that these emulators don't normally insert into the images. You may also need a third-party utility to create an HFS or HFS+ disk image.

### OS Support

Currently, the Power Mac 6100 cannot boot any OS image containing Mac OS 9.0 or newer.

### ATI Mach Support

The GUI engine is currently not fully implemented. As such, UI elements might not render when trying to use this video card. To circumvent this, you may wish to move the ATI Accelerator extension to the Extensions (Disabled) folder.

### Currently Unimplemented Features

* JIT compiler
* AltiVec
* 3D acceleration support
* Additional ADB and USB peripherals
* Networking
