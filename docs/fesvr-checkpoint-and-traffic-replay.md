# FESVR checkpoints and device traffic record & replay

This guide covers how spike and its front-end server (FESVR) record the traffic between a simulated program and the host. It then covers how that recording is replayed, and how checkpoints built on top of it are created and loaded.

The first half is for users: what the mechanism does, and how to run it. The second half is for developers and tool authors: how it works, the HTIF protocol it adds, the on-disk formats and the limitations.

Contents:

1. [Overview](#1-overview)
2. [Quick start](#2-quick-start)
3. [Options reference](#3-options-reference)
4. [Checkpoint description file](#4-checkpoint-description-file)
5. [How record & replay works](#5-how-record--replay-works)
6. [How checkpoints work](#6-how-checkpoints-work)
7. [Main and mirror FESVR (721sim)](#7-main-and-mirror-fesvr-721sim)
8. [HTIF protocol additions](#8-htif-protocol-additions)
9. [File formats](#9-file-formats)
10. [Limitations](#10-limitations)
11. [Troubleshooting](#11-troubleshooting)
12. [Source map](#12-source-map)


## 1. Overview

A program running on spike under the proxy kernel (pk) reaches the outside world only through the FESVR. pk writes a command to the `tohost` CSR. The FESVR services it, for example by running the `read` system call on the host and copying the result into target memory, and answers through `fromhost`. This guide calls everything the FESVR does for one command (the command, the target memory it reads and writes, and its response) the **device traffic** of that command.

The mechanism has three parts:

- **Record.** In a normal run, the FESVR records the device traffic of every command into a *recording* folder.
- **Replay.** A later run is given the recording instead of the real host environment. Each command is answered from the recording: the recorded writes are applied to target memory, the recorded reads are checked against target memory, and the recorded response is returned. Nothing runs on the host. The replayed run needs neither the program's files nor its sysroot, nor its command-line arguments: pk itself fetches the arguments through a system call, which is also replayed.
- **Checkpoints.** A replaying run can stop at chosen instruction counts and save a checkpoint. A checkpoint holds the hart's architectural state, all of target memory, and the position in the recording. Loading one restores that state, moves the recording to the same position, and continues from there.

Because the recording supplies everything the program needs, a checkpoint can be simulated without recreating the program's environment. This mechanism replaces spike's former `-c` and `--make-checkpoint` options, which have been removed.

Typical workflow:

```
real run + record  ──>  recording folder  ──>  replay (+ create checkpoints)  ──>  load a checkpoint, simulate an interval
```


## 2. Quick start

The examples below use SPEC's `473.astar` running under pk with 256 MB of memory. Adjust the paths, the memory size and the program for your workload.

### Where options go

FESVR options start with `+`. Put spike's own `-` options first, then the `+` options, then the target program and its arguments:

```
spike [-m<MB> -e<n> ...] [+fesvr-options ...] pk [program args ...]
```

spike's option parser stops at the first argument that doesn't start with `-`. A `-` option placed after a `+` option is therefore no longer seen by spike.

### Record a run

The recording folder must exist before the run, because the recorder doesn't create it.

```
mkdir fesvr-recording
spike -m256 \
    +chroot=473.astar_test +target-cwd=/app \
    +dev-traffic-record=fesvr-recording \
    +final-state-dump=final-state-record.json \
    pk astar_base.rv64 lake.cfg
```

This is a normal run: the program does its real system calls through the FESVR (inside `+chroot`, starting in `+target-cwd`), and every command is also written to `fesvr-recording/`. A run stopped early with `-e<n>` still produces a valid recording, but only of the commands issued before it stopped.

### Replay it

```
spike -m256 \
    +dev-traffic-replay=fesvr-recording \
    +final-state-dump=final-state-replay.json \
    pk
```

- Use the same `-m` and the same pk binary. The recording stores both, and the replay checks them (the "ELF" whose SHA-256 is compared is the binary spike loads, which is pk).
- Don't pass the program, its arguments, `+chroot` or `+target-cwd`. pk gets all of them through replayed system calls. Real-device options are ignored in replay mode.
- A replay prints nothing and writes no files: system calls aren't executed again. Console output and output files exist only from the recording run.
- To confirm the replay reproduced the run, compare the `instret` in the two `+final-state-dump` files. They must be equal.

### Create checkpoints

Write a checkpoint description file (see [section 4](#4-checkpoint-description-file)). Each line gives a checkpoint name and the instruction count where it is taken:

```
astar.1G: 1000000000
astar.5G: 5000000000
```

Then replay with `+create-checkpoint`:

```
spike -m256 +dev-traffic-replay=fesvr-recording +create-checkpoint=ckpts.txt pk
```

The checkpoints are written to the current directory as files named `astar.1G` and `astar.5G`. The run stops, with exit status 0, right after the last checkpoint is written.

### Load a checkpoint

```
spike -m256 -e100000000 \
    +dev-traffic-replay=fesvr-recording \
    +load-checkpoint=astar.1G \
    pk
```

This restores `astar.1G`, then simulates 100M instructions (`-e`). A checkpoint must be loaded together with the recording it was created from: the checkpoint stores the recording's SHA-256 and the load checks it. spike still requires a target program argument, but nothing is loaded from it; the program is already in the restored memory.


## 3. Options reference

FESVR options, parsed in `htif_t::htif_t` (`riscv-fesvr/fesvr/htif.cc`). "Real" is a normal run with real devices; "replay" is a run with `+dev-traffic-replay`. "Mirror" is a second FESVR in the same process; see [section 7](#7-main-and-mirror-fesvr-721sim).

| Option | Applies in | Effect |
|---|---|---|
| `+dev-traffic-record=<dir>` | real, replay | Record every command's device traffic into `<dir>`, which must exist. Recording while replaying is allowed and re-records the replayed traffic. |
| `+dev-traffic-replay=<dir>` | — | Replay mode: no host devices are created, and commands are answered from the recording in `<dir>`. The recording is opened and validated at startup. |
| `+create-checkpoint=<file>` | replay | Create checkpoints as listed in the description file `<file>`, which is parsed at startup. Single hart only. |
| `+load-checkpoint=<file>` | replay, mirror | Restore checkpoint `<file>` before execution starts. |
| `+final-state-dump=<file>` | real, replay | At the end, write JSON `{"hart_state": [{"instret": N}, ...]}`. N is the hart's retired-instruction count since the original boot, including the part before a restored checkpoint. |
| `+signature=<file>` | real, replay | Write the memory between the ELF's `begin_signature` and `end_signature` symbols (torture tests). There is no signature after `+load-checkpoint`, because no ELF is loaded. |
| `+strace=<file>` | real only | Write a trace of the proxied system calls. |
| `+std-dump=<base>` | real only | Also copy the target's stdout and stderr to `<base>.stdout` and `<base>.stderr`. |
| `+chroot=<dir>` | real only | Resolve the target's absolute paths under `<dir>`. |
| `+target-cwd=<dir>` | real only | Initial working directory of the target. |
| `+disk=<file>`, `+rfb[=<n>]` | real only | Add a disk or frame-buffer device. |
| `+verbose` | all | Log each serviced command and the target spec to stderr. |

"Real only" options are accepted but have no effect in replay mode or on a mirror FESVR. spike's own help (`spike -h`) lists its `-` options.


## 4. Checkpoint description file

`+create-checkpoint=<file>` reads one checkpoint per line (`riscv-fesvr/fesvr/ckpt_desc_reader.cc`):

```
<name>: <instruction count>
```

- Leading and trailing whitespace is ignored, and blank lines are skipped. There is no comment syntax.
- The line is split at its last `:`. The name may contain only letters, digits, `-`, `_` and `.`, so it can't contain a directory. Each checkpoint is written to a file of that name in the current directory.
- The count is a non-negative decimal integer: the number of instructions retired since the start of this run, before which the checkpoint is taken. With `+load-checkpoint`, counting starts at the restored point.
- Entries may appear in any order; they are taken in increasing count order.
- Names must be unique, and so must counts. A malformed line (no `:`, an empty name, a signed or non-numeric count) is an error when the file is read.
- The run ends right after the last checkpoint.
- Checkpoints can be created for a single hart only. With a mirror FESVR, create only one checkpoint per run; see [section 7](#7-main-and-mirror-fesvr-721sim).


## 5. How record & replay works

### Components

```
  spike (riscv/)                              FESVR (riscv-fesvr/fesvr/), runs as a coroutine of spike
 +-----------------------+    HTIF packets    +--------------------------------------------------------+
 | hart                  | <----------------> | htif_t::run()                                          |
 |  csrw tohost  ------> |                    |   tohost -> command_t -> device_composition_t          |
 |  csrr fromhost <----- |                    |     real_composition_t:  syscall proxy, bcd, disk, rfb |
 |  memory               |                    |     recorded_composition_t: answers from a recording   |
 +-----------------------+                    |   memif_tap_t: every target memory access by a device  |
                                              |   traffic listeners: recorder, mirror buffer, +verbose |
                                              +--------------------------------------------------------+
```

- `htif_t::run()` loops over the harts. It reads `tohost`, wraps a non-zero value in a `command_t`, and passes it to the FESVR's `device_composition_t`.
- `real_composition_t` dispatches commands to real devices.
- `recorded_composition_t` answers them from a recording.
- Devices access target memory only through `htif_t::memif()`, a `memif_tap_t` that reports each read and write to the composition.

### What is recorded

When at least one traffic listener is registered (the recorder, a mirror's buffer, or the `+verbose` logger), `device_composition_t::handle_command` captures each command as a **command service sequence** (`cmd_service_sequence_t`):

1. the command: device, command number, 48-bit payload, hart;
2. every target memory read and write a device made while servicing it, in order, with the data;
3. whether the device responded, the full 64-bit `fromhost` value, and the FESVR exit code after the command.

`device_traffic_recorder_t` writes these sequences to the recording folder ([section 9](#9-file-formats)). For a pk system call, the sequence is:
- a 64-byte read of pk's argument block;
- the call's own reads and writes;
- a 64-byte write of the result back;
- the response.

### What replay does

`recorded_composition_t::do_handle_command` takes the hart's next recorded sequence, then:

1. **Command fields.** It checks that device, command, hart and payload match the incoming command. A mismatch means the run diverged.
2. **Memory, in recorded order.** Recorded writes are applied to target memory. Recorded reads are performed and compared byte for byte with the recorded data. A difference means the program computed something different from the recording run.
3. **Exit code.** A non-zero recorded exit code is applied, which ends the run as the program's `exit` did. A recorded 0 never cancels a stop the FESVR itself has requested, such as the one after the last checkpoint.
4. **Response.** The recorded response, if any, is delivered.

Any mismatch stops the run with an error ([section 11](#11-troubleshooting)) rather than continuing on diverged state.

### Determinism

Replay works only if the replayed program issues exactly the same commands with the same data. Under pk on a single hart, this holds, including for programs that read time or instruction counters and send the values to the host.

**All target-visible time is derived from the instruction count.**
- spike returns `state.count`, the retired-instruction count, for every counter CSR: `cycle`, `time`, `instret` and `count` (`processor_t::get_pcr`, `riscv/processor.cc`).
- User code may read them directly.
- pk computes `time`, `gettimeofday`, `clock_gettime` and `times` from `rdcycle()` with a fixed 1 GHz `CLOCK_FREQ`; they are never sent to the host. pk's `sysinfo` uptime and its `-s` cycle report use `rdcycle()` as well.
- `state.count` is part of the hart state, so a checkpoint restores it.

**The host can't change the instruction stream.**
- pk talks to the host only through `frontend_syscall`: write `tohost`, then poll `fromhost`.
- spike holds the hart inside the `tohost` write until the FESVR has responded (`processor_t::set_pcr(CSR_TOHOST)`). pk's poll therefore always succeeds the first time, and no instructions retire while the host works.
- pk enables no interrupts, so timer and host interrupts never redirect execution.
- A checkpoint freeze retires no instructions.

**The remaining host input is recorded.** `getrandom` is proxied to the FESVR, which uses a fixed seed. Like everything else, the result is replayed from the recording.

So values a program prints or writes (counters, times, anything derived from them) are identical in the recording run and the replay. On replay they show up as recorded reads, for example the buffer of a `write` call, and pass the byte-for-byte check.

The guarantee depends on these conditions:
- **Synchronous host communication only.** The program talks to the host only through the system call device, as pk does. Devices that answer later, such as console input (bcd), disk and rfb, make the number of polling iterations depend on HTIF timing. A response that arrives after its command was serviced can't be recorded; the FESVR prints a warning when that happens.
- **No extra interrupts.** The guest enables no interrupts beyond what pk does.
- **One hart.**
- **Same spike build, memory size and pk binary.**


## 6. How checkpoints work

### Hart execution controller

Checkpoints are timed by a per-hart **execution controller** in spike (`riscv/processor_execution_controller.h`). The FESVR drives it through new HTIF commands (`riscv-fesvr/fesvr/hart_execution_controller.cc`).

It provides three breakpoint types:
- **unconditional** (freeze at the next instruction);
- **instruction count-down** (freeze after N more retired instructions);
- **PC breakpoints** (up to four; freeze before executing a given PC).

Checkpointing uses the first two.

Registers (`riscv-fesvr/fesvr/hart_execution_reg.h`). Bit masks: unconditional `1<<0`, count-down `1<<1`, PC0–PC3 `1<<2` to `1<<5`.

| # | Register | Read | Write |
|---|---|---|---|
| 0 | `NULL` | 0 | ignored |
| 1 | `RESET` | 0 | clears the given bits in ENABLE and FROZEN |
| 2 | `ENABLE` | enabled breakpoints | sets bits; enabling count-down or a PC breakpoint loads its staged value |
| 3 | `FROZEN` | which breakpoints froze the hart | clears the given bits; the hart runs again when all are clear |
| 4 | `INSTR_CNT_DOWN` | remaining count | stages a new count |
| 5–8 | `PC0_BREAK`–`PC3_BREAK` | loaded PC | stages a PC |

Freeze semantics:
- A hart freezes at an instruction boundary, before the next instruction executes. While frozen, it keeps servicing HTIF packets, so the FESVR can read and replace its state and memory.
- The count-down decrements once per retired instruction: every instruction that completes, plus `scall` and `sbreak`, which retire by trapping.
- These don't count: instructions that fault, interrupts, and instructions restarted after serialization.
- The FESVR notices a freeze by polling `FROZEN` in its loop. It polls only harts with a breakpoint armed.

### Creating checkpoints

With `+create-checkpoint` and `+dev-traffic-replay` (`htif_t::start`, `setup_trap_for_next_checkpoint`, `create_checkpoint`):

1. At startup, the hart is reset and frozen by an unconditional breakpoint. The FESVR arms the count-down for the first checkpoint's count and releases the hart.
2. The program runs, replaying its commands, until the count-down reaches 0. The hart freezes before the next instruction.
3. The FESVR writes the checkpoint file:
   - the header;
   - the hart's state, downloaded from spike;
   - the current position in the recording (the number of commands serviced so far) and the recording's SHA-256;
   - all of target memory, streamed from spike as a zlib stream.
4. It arms the count-down with the distance to the next checkpoint and releases the hart.
5. After the last checkpoint, the FESVR stops the simulation.

### Loading a checkpoint

With `+load-checkpoint` and `+dev-traffic-replay` (`htif_t::start`, `load_checkpoint`):

1. No ELF is loaded. The hart is reset and frozen at its first instruction.
2. The checkpoint header is checked: magic, hart count, memory size.
3. The checkpoint's recording SHA-256 is compared with the replayed recording's.
4. The hart state is uploaded. spike checks its size against this build's `state_t`, then re-applies the status register and flushes its TLB and instruction cache.
5. The recording is moved to the stored position.
6. The memory image is streamed into target memory. spike flushes every hart's TLB and instruction cache again.
7. The recording's target spec (memory size, hart count, pk SHA-256) is checked against the restored system.
8. The hart is released and continues from the restored PC.

### What a checkpoint contains

**Saved:**
- each hart's `state_t`: PC, integer and FP registers, the CSRs spike keeps there, `count`, `tohost`/`fromhost`, the load reservation;
- all of target memory;
- per hart: the number of commands already serviced, and the SHA-256 of the recording it belongs to;
- the SHA-256 of the ELF the original run loaded.

**Not saved:** see [section 10](#10-limitations).


## 7. Main and mirror FESVR (721sim)

721sim runs two simulators in one process: a functional spike that leads, and a timing model whose checker has its own FESVR. Their device traffic must be identical, so the second FESVR doesn't talk to the host. It replays the traffic the first one produces (`riscv-fesvr/fesvr/device_traffic_bypass.cc`).

**Roles:**
- **Main FESVR:** the first `htif_t` created in the process. It registers its device composition process-wide, and works in real-device or replay mode as configured.
- **Mirror FESVR:** every later `htif_t`.
  - Its composition replays from a `cmd_service_sequence_buffer_t`. That buffer registers as a traffic listener on the main FESVR and queues every sequence the main services.
  - It applies the same memory writes to its own target, checks the same reads against its own memory, and delivers the same responses.
  - It ignores the device, recorder, strace, std-dump, chroot, target-cwd, signature and final-state-dump options.

**Ordering requirements:**
- The mirror must be created before the main FESVR starts, so it receives the target spec.
- The main must service a command before the mirror asks for it. Otherwise the mirror fails with "the recorded traffic source depleted …".

**Checkpoints with a mirror:**
- Only the main FESVR creates checkpoints.
- On `+load-checkpoint`, the mirror restores its hart and memory and resets its command counter. It doesn't verify the recording SHA-256 or seek, because the main FESVR does both.
- A mirror still needs `+dev-traffic-replay=` to accept checkpoint options, although it doesn't read the path.
- **Create only one checkpoint per run while a mirror exists.** A checkpoint freezes the leading hart while the FESVR transfers its state and memory, and spike keeps ticking the HTIF during that time. The ticks after the checkpoint then no longer fall at the same instruction counts as before. According to the design note in `setup_trap_for_next_checkpoint()`, more than one checkpoint therefore makes 721sim's checker fail. The rule is not enforced in code.


## 8. HTIF protocol additions

Packet header (`riscv-fesvr/fesvr/packet.h`): `cmd:4 | data_size:12 (8-byte words) | seqno:8 | addr:40`. Commands:

| Value | Command | Added |
|---|---|---|
| 0 | `READ_MEM` | |
| 1 | `WRITE_MEM` | |
| 2 | `READ_CONTROL_REG` | |
| 3 | `WRITE_CONTROL_REG` | |
| 4 | `DOWNLOAD_HART_FULL_STATE` | yes |
| 5 | `UPLOAD_HART_FULL_STATE` | yes |
| 6 | `READ_HART_EXEC_CONTROL_REG` | yes |
| 7 | `WRITE_HART_EXEC_CONTROL_REG` | yes |
| 8 | `DOWNLOAD_MEM_DUMP` | yes |
| 9 | `UPLOAD_MEM_DUMP` | yes |
| 10 | `ACK` | moved from 4 |
| 11 | `NACK` | moved from 5 |

`ACK` and `NACK` moved, so this FESVR and spike only work with each other, not with an upstream counterpart.

The transport (`htif_pthread_t`) carries at most 16384 bytes per packet. On the spike side, each `tick()` handles exactly one host packet (`riscv/htif.cc`).

**Execution control registers.** `addr = hart << 20 | register`.
- Read: request with no payload; reply with the 8-byte value.
- Write: request with the 8-byte new value; reply with the old value.

**Hart state download.** Request `addr = hart`. The reply is a raw copy of spike's `state_t`, zero-padded to 8 bytes; its `addr` field carries `sizeof(state_t)`.

**Hart state upload.**
- The request carries the padded state.
- spike rejects a size other than its own padded `sizeof(state_t)`.
- Otherwise it copies the state, re-applies the status register, flushes the TLB and instruction cache, and acknowledges.

**Memory dump download** (checkpoint creation):

```
host                                     spike
DOWNLOAD_MEM_DUMP(addr=0)  ------------>
                           <------------ ACK(chunk, addr=chunk length)      repeated: zlib stream of all memory,
DOWNLOAD_MEM_DUMP(addr=chunk length) ->                                     16384-byte chunks, the last one shorter
                           <------------ ACK(no payload, addr=total bytes)  end of stream; the host checks the total
```

**Memory dump upload** (checkpoint loading):

```
host                                     spike
UPLOAD_MEM_DUMP(addr=0)    ------------>
                           <------------ ACK(addr=16384)                    spike's receive capacity
UPLOAD_MEM_DUMP(chunk, addr=length) --->                                    repeated, never an empty chunk
                           <------------ ACK(addr=length)
UPLOAD_MEM_DUMP(no payload, addr=total) ->                                  end of stream
                           <------------ ACK(addr=decompressed size)        must equal the memory size
```

spike decompresses each chunk into memory as it arrives (`riscv/stream_compression.cc`). It rejects a stream that ends early, holds trailing bytes, or would overflow memory.


## 9. File formats

All integers are little-endian (host order on x86), and all structures are packed. Offsets are in bytes.

### Recording folder

`+dev-traffic-record=<dir>` writes these files, and `+dev-traffic-replay=<dir>` reads them:

| File | Content |
|---|---|
| `target_spec.data` | the system the recording was made on |
| `device_traffic_hart<N>.packets` | the command service sequences of hart N |
| `device_traffic_hart<N>.index` | the offset of each command in `.packets` |
| `device_traffic_hart<N>.sha256` | the SHA-256 of `.packets`; it identifies the recording |

Recordings made before the spec file was renamed have `target_spec` instead of `target_spec.data`.

**Compression.** In a CMake build, `FESVR_TRAFFIC_RECORD_COMPRESSED_OUTPUT` is defined (`riscv-fesvr/fesvr/CMakeLists.txt`). The `.packets`, `.index` and `.sha256` files are then each a gzip stream. An autotools build writes them uncompressed. All offsets below refer to the uncompressed bytes. A gzip build can read an uncompressed recording, because zlib passes plain files through; an autotools build can't read a compressed one. `target_spec.data` is never compressed.

**`target_spec.data`** (40 bytes):

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | memory size in MB |
| 4 | 4 | number of harts |
| 8 | 32 | SHA-256 of the loaded ELF (pk) |

**`.packets`.** `u32` magic `0x38837587` (bytes `87 75 83 38`), then the packets. Each command is a `COMMAND_BEGIN` packet, zero or more `PHYSICAL_MEMORY_ACCESS` packets in the order the accesses happened, and a `COMMAND_END` packet. Every packet starts with a 4-byte type:

| Type | Value | Payload | Packet size |
|---|---|---|---|
| `COMMAND_BEGIN` | 0 | `u8` device, `u8` command, `u64` payload | 14 |
| `PHYSICAL_MEMORY_ACCESS` | 1 | `u64` physical address, `u32` length L, `u8` is_write (0 read, 1 write), L data bytes | 17 + L |
| `COMMAND_END` | 128 | `u8` responded, `u64` response (the full `fromhost` value), `i64` exit code, `u32` CRC32 | 25 |

**CRC32.** The `CRC32` in each `COMMAND_END` is the standard CRC-32 (zlib's `crc32`). It covers all bytes from offset 4, just after the magic, up to the field itself. It is cumulative over the whole file and includes earlier commands' CRC fields. The reader verifies it for every command, and after a seek it restarts the computation from the previous command's stored CRC. The file ends after the last `COMMAND_END`, with no terminator.

**`.index`.** `u32` magic `0x38834639` (bytes `39 46 83 38`), then one `u64` per command: the offset of its `COMMAND_BEGIN` in `.packets`. The first entry is always 4. The number of commands is `(size - 4) / 8`.

**`.sha256`.** `u32` magic `0x38833478` (bytes `78 34 83 38`), then the 32-byte SHA-256 of the whole uncompressed `.packets` stream, magic included.
- It is written when the recording is finalized: at the end of the run, or when the FESVR is destroyed after a run stopped by `-e`.
- A write failure leaves only the magic, and replay then rejects the recording.
- Replay doesn't recompute the SHA-256. It is used to tie checkpoints to their recording.

### Checkpoint file

`riscv-fesvr/fesvr/checkpoint_format.h`. Header:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `0x78642578` (bytes `78 25 64 78`) |
| 4 | 4 | number of harts H |
| 8 | 4 | memory size in MB |
| 12 | 32 | SHA-256 of the ELF loaded by the original run (pk) |
| 44 | 8236 × H | one record per hart |

Per-hart record (8236 bytes):

| Offset | Size | Field |
|---|---|---|
| 0 | 32 | SHA-256 of the recording (its `.sha256` digest) |
| 32 | 8 | commands already serviced: the replay resumes at this command index |
| 40 | 4 | size of the saved hart state |
| 44 | 8192 | the hart state: spike's `state_t`, raw, zero-padded to 8 bytes; the rest is zero |

The header is followed by the zlib stream (`deflate`, level 9) of all target memory, running to the end of the file. It decompresses to exactly memory size × 2^20 bytes. A one-hart checkpoint's header is 8280 bytes. The file has no version field.


## 10. Limitations

**Checkpoint creation:**
- **Single hart only:** "Currently HTIF doesn't support to create checkpoint for multi-HART system!".
- **Replay mode required:** checkpoints can only be created or loaded with `+dev-traffic-replay`. A program has to be recorded first.
- **Exact match on load:** loading needs the same hart count, memory size, recording and pk binary as the run that created the checkpoint.

**Hart state and build compatibility:**
- **The hart state is a raw copy of spike's `state_t`.** A checkpoint is valid only for a spike build with the same `state_t` layout. A different size is rejected; a same-size layout change isn't detected.
- **No version field** exists in the checkpoint or the recording formats.

**Not saved in a checkpoint:**
- accelerator (RoCC) state;
- cache-simulator, tracer and SimPoint state;
- the phase of spike's HTIF tick interval;
- a response still queued in the FESVR.

None of these matters for pk programs: pk uses no accelerator, and spike holds the hart until every system call has been answered.

**Other:**
- **Every checkpoint compresses all of target memory,** however much the program uses.
- **No `+signature` output after `+load-checkpoint`,** since no ELF is loaded.
- **A truncated recording can't resume past its end.** If the recording was stopped by `-e`, a checkpoint taken after its last command can't be loaded, because the recording has no command at that position to seek to.
- **Asynchronous device responses can't be recorded.** These include console input; see [Determinism](#determinism).
- **A mirror FESVR allows one checkpoint per run.** This rule is not enforced; see [section 7](#7-main-and-mirror-fesvr-721sim).


## 11. Troubleshooting

Errors end the run with `terminate called after throwing an instance of 'std::runtime_error'` and the message below.

**Command line:**

| Message | Cause |
|---|---|
| `to create checkpoint(s), FESVR must operate in device traffic replay mode.` / `to load a checkpoint, …` | `+create-checkpoint` or `+load-checkpoint` without `+dev-traffic-replay`. |
| `Cannot open '<file>'.` | The checkpoint description file can't be read. |
| `Configuration '…' has no ':' between …`, `… has an empty checkpoint name.`, `… contains invalid skip amount.` | A malformed line in the description file. |
| `Configurations has same skip amount: …` / `… same checkpoint name: …` | Two description lines share a count or a name. |
| `Configuration "…" contains invalid char …` | A checkpoint name with characters other than letters, digits, `-`, `_`, `.`. |

**Recording:**

| Message | Cause |
|---|---|
| `Cannot open file to write device traffic packets: <dir>/…` | The `+dev-traffic-record` folder doesn't exist or isn't writable. |
| `failed to write the device traffic …` / `Failed to finalize the device traffic recording …` | A write failed, for example because the disk is full. The recording is incomplete and can't be replayed. |

**Starting a replay or loading a checkpoint:**

| Message | Cause |
|---|---|
| `fail to load pre-recorded FESVR device traffic: cannot open target spec information.` | Wrong `+dev-traffic-replay` path, or an old recording whose spec file is named `target_spec`. |
| `malformed device traffic … file: bad magic number` | Not a recording file, or a compressed recording read by an autotools (uncompressed) build. |
| `malformed device traffic checksum file: cannot read the sha256 of the recording` | The recording was never finalized; see the `failed to write` row above. |
| `incompatible FESVR device traffic recording: … expects target to have <X>MB memory …` / `… HART(s) …` / `… load an ELF with SHA256 …` | Different `-m`, `-p` or pk binary from the recording run. |
| `Cannot load the checkpoint file … bad magic number.` / `… created for system with …` | Not a checkpoint, or a different hart count or memory size. |
| `Cannot use the checkpoint file … requires HART <i> to load a device traffic recording with SHA256 …` | The checkpoint belongs to a different recording. |
| `Error happened while loading the full state of HART <i>: … The state was probably dumped by a different build.` | The checkpoint comes from a spike build with a different `state_t`. |
| `Failed to load the checkpoint file … cannot seek to the <n>th command …` | The recording has fewer commands than the checkpoint expects (for example, a truncated recording). |
| `The compressed chunk supplier stopped providing data …` / `… has extra data after the compressed stream is ended.` / `zlib::inflate failed …` | The checkpoint file is truncated, has trailing data, or is corrupt. |

**During a replay** (the run diverged from the recording):

| Message | Cause |
|---|---|
| `'device' field …`, `'cmd' field …`, `'payload' field … doesn't match the recorded servicing sequence.` | The replayed program issued a different command than the recording run. |
| `the data read from target system doesn't match the recorded servicing sequence.` | The program passed different data to the host, for example different output. |
| `the recorded traffic source depleted when handling the <n>th command for HART <h>` | The program issued more commands than were recorded: the recording was truncated (`-e`), or the program diverged. |
| `malformed device traffic input: …` (CRC32, unexpected EOF, early termination) | The recording files are damaged. |

**When a replay diverges, look for:**
- a different pk binary or spike build;
- different `-m`/`-p`;
- a recording made with asynchronous devices (see [Determinism](#determinism)).

`+verbose` logs each command as it is serviced, which helps find where the divergence starts.


## 12. Source map

| Topic | Files |
|---|---|
| FESVR options, run loop, checkpoint create/load, transfers | `riscv-fesvr/fesvr/htif.cc`, `riscv-fesvr/fesvr/htif.h` |
| Command capture, real vs recorded composition, replay | `riscv-fesvr/fesvr/device_composition.{h,cc}`, `riscv-fesvr/fesvr/device_traffic_capture.{h,cc}` |
| Recording files: writer, reader, recorder, replayer | `riscv-fesvr/fesvr/device_traffic_persistent.{h,cc}` |
| Target memory tap | `riscv-fesvr/fesvr/memif_tap.{h,cc}` |
| Main/mirror FESVR | `riscv-fesvr/fesvr/device_traffic_bypass.{h,cc}` |
| Checkpoint file and description file | `riscv-fesvr/fesvr/checkpoint_format.h`, `riscv-fesvr/fesvr/ckpt_desc_reader.{h,cc}` |
| Execution controller (host side), register map | `riscv-fesvr/fesvr/hart_execution_controller.{h,cc}`, `riscv-fesvr/fesvr/hart_execution_reg.h` |
| HTIF packet format and commands | `riscv-fesvr/fesvr/packet.h` |
| CRC32, SHA-256, gzip streams with seek | `riscv-fesvr/fesvr/checksum.{h,cc}`, `riscv-fesvr/fesvr/crypto_digest.{h,cc}`, `riscv-fesvr/fesvr/gzstream.{h,cc}` |
| spike's HTIF handlers (state, memory dump, execution control) | `riscv/htif.cc` |
| Memory dump compression | `riscv/stream_compression.{h,cc}` |
| Execution controller (spike side), freeze and count-down | `riscv/processor_execution_controller.h`, `riscv/execute_insn_hook.h` |
| Blocking system call wait, step loop, counter CSRs | `riscv/processor.cc` |
