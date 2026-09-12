The MESH is a SCSI controller used in Power Mac machines. Its register and
interrupt semantics are specified in chapter 12 of the
[CHRP I/O Device Reference](https://www.bitsavers.org/pdf/apple/powerpc/CHRP/chrp_io.pdf).

The ROM-independent regression test exercises interrupt masking, command
completion, exception acknowledgement, and repeated selection timeouts on an
empty bus. A memory-backed target also checks DBDMA reads and writes at 16 bytes,
512 bytes and 64 KiB, split descriptors that partially drain the FIFO, and
starting DMA before or after the MESH data command. PIO writes cover full FIFO
bursts, a partial final burst, and the zero-encoded 64 KiB transfer count:

```
cmake -S . -B build -DDPPC_BUILD_DEVICE_TESTS=ON
cmake --build build --target testmesh
ctest --test-dir build -R '^mesh$' --output-on-failure
```

The DMA callbacks return the number of bytes actually transferred. Returning
zero after a read stalls DBDMA; inheriting the default write callback consumes
the DMA buffer without delivering anything to the SCSI target. Mac OS startup
on the 6400 exercises these writes before MkLinux loads. When the data command
arrives after DBDMA has started, MESH must retry the waiting channel.

The Gazelle ROM also writes through the FIFO using PIO. A full FIFO must drain
without requiring another write: the driver polls FIFOCount for space first.
Deferring the transfer until a seventeenth byte arrives deadlocks that poll
and would discard the extra byte.

# Registers

| Register Name    | Number |
|:----------------:|:------:|
| R_COUNT0         | 0x0    |
| R_COUNT1         | 0x1    |
| R_FIFO           | 0x2    |
| R_CMD            | 0x3    |
| R_BUS0STATUS     | 0x4    |
| R_BUS1STATUS     | 0x5    |
| FIFO_CNT         | 0x6    |
| EXCPT            | 0x7    |
| ERROR            | 0x8    |
| INTMASK          | 0x9    |
| INTERRUPT        | 0xA    |
| SOURCEID         | 0xB    |
| DESTID           | 0xC    |
| SYNC             | 0xD    |
| MESHID           | 0xE    |
| SEL_TIMEOUT      | 0xF    |

# Commands

| Command Name     | Number |
|:----------------:|:------:|
| NOP              | 0x0    |
| ARBITRATE        | 0x1    |
| SELECT           | 0x2    |
| COMMAND          | 0x3    |
| STATUS           | 0x4    |
| DATAOUT          | 0x5    |
| DATAIN           | 0x6    |
| MSGOUT           | 0x7    |
| MSGIN            | 0x8    |
| BUSFREE          | 0x9    |
| ENABLE_PARITY    | 0xA    |
| DISABLE_PARITY   | 0xB    |
| ENABLE_RESELECT  | 0xC    |
| DISABLE_RESELECT | 0xD    |
| RESET_MESH       | 0xE    |
| FLUSH_FIFO       | 0xF    |
| SEQ_DMA          | 0x80   |
| SEQ_TARGET       | 0x40   |
| SEQ_ATN          | 0x20   |
