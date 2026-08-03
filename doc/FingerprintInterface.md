# Fingerprint scanner driver (`FingerprintInterface`)

Technical notes about the fingerprint part of the AccessControl module: which scanners are supported,
how the driver is structured, which console commands are available for hardware triage and what to
expect from the multi-device synchronisation.

Source files: `src/FingerprintTypes.h` (enums, results, callbacks), `src/FingerprintProtocol.h/.cpp`
(wire framing and the incremental receive parser), `src/FingerprintInterface.h/.cpp` (the driver
itself), `src/AccessControl.h/.cpp` (module integration).

## Supported scanners and ETS selection

The driver speaks the complete GROW command set of the R503 series. The model is **not** detected at
runtime, it is taken from the ETS parameter **"Fingerprint Scanner"**:

| ETS value | Selection | Template size | Library | Notes |
|---|---|---|---|---|
| 0 | `R503` | 1536 bytes | 200 slots | `GetImageEx`, `CheckSensor`, `ReadProdInfo`, `SoftRst` available |
| 1 | `R503S` | 1536 bytes | 200 slots | handled with the R503 profile |
| 2 | `R503Pro` | 512 bytes | 1500 slots | extended LED colours (0x20 / 0x30), no R503-only commands |
| 3 | `Kein Fingerprint` | – | – | no bring-up at all |

* The model profile decides the template size used for `UpChar`/`DownChar`, the library capacity, the
  encoding of `AutoEnroll`/`AutoIdentify` parameters (1 byte on the R503, 2 bytes on the R503Pro) and
  which commands are answered with "not supported by this model" without any wire traffic.
* At bring-up the library size reported by the sensor is checked against the selected profile. A
  mismatch (wrong selection in ETS, or a variant with a smaller library) is logged as an error and the
  bring-up continues - the profile stays in charge.
* **"Kein Fingerprint"** skips the scanner bring-up completely: the power pin stays off, the UART is
  never opened, there is no health check and no UART traffic at all. The scanner status group object
  is set to `false`, all fingerprint operations answer with a failure code and the cached getters
  answer "empty". NFC readers and keypads are unaffected.
* The R503Pro answers the index table pages 4 and 5 (slots 1024-1499) although its manual documents
  pages 0-3 only. If a unit ever refuses them, the bring-up logs which page failed, keeps the index
  cache valid up to the last successful page and continues.

## Non-blocking architecture

The driver never blocks: every operation is started with a `startX(...)` call which returns
immediately, and its result is delivered exactly once through a callback which is only ever invoked
from `FingerprintInterface::loop()` - never from an interrupt and never re-entrantly from `startX()`.
`loop()` runs the four layers in order: transport (bounded UART read/write budgets), command lifecycle
(`TxFrame`/`WaitAck`/`RxData`/`TxData`/`PostDataGuard`/`WaitMoreAcks` with per-command timeouts,
verified checksums and retries for transport errors), the active composite operation and finally the
LED latch. Composites such as bring-up, search, enrollment, template export/import, delete, empty
database, set password and health check are state machines which are advanced one step per loop pass,
so even a six-capture enrollment or a 1536-byte template transfer costs well below a millisecond of
loop time per pass. Exactly one operation is in flight at a time; the only fire-and-forget path is
`setLed()`, which latches the latest LED state and flushes it at the next command boundary so that a
burst of LED group objects can never starve an operation. On the module side an ownership arbiter
(`FpOwner`) decides who may drive the driver - the scan pipeline, the enrollment, the two sync
template transfers, the ETS maintenance operations or the console test mode - in the priority order
sync-import > enroll > sync-export > maintenance > scan > health check. Completion callbacks only
latch flags, group objects and LEDs; the next scanner operation is always started from the module
`loop()` through the arbiter. Ownership is released between the individual finger-removal probes, so a
finger resting on the sensor cannot starve any other activity. As a result KNX stays fully responsive
during enrollment, template synchronisation and library maintenance, and every wait (finger placement,
finger removal, waiting for the scanner to become free) is bounded by a timeout instead of hanging the
device.

## Console diagnostics

Useful when a unit is suspected to be defective. They run against the productive driver, so no reboot
is needed afterwards.

| Command | Effect |
|---|---|
| `acc fpi test` | Small sequencer: claims the driver through the arbiter (`FpOwner::Test`), brings a powered off or faulted scanner up (on an unconfigured device with the factory default password 0), dumps powered/ready/busy plus the system parameters, then waits 5 s for a finger and reports match location and score, "no match" or "no finger detected". Releases the driver afterwards; normal scanning simply defers while it runs. |
| `acc fpi info` | Powered/ready/busy, module power state and current arbiter owner, plus the full system parameter dump (status register, system identifier, library size, security level, device address, packet size, baud rate, profile template size, algorithm and firmware version, product info on the R503, number of stored templates). Answered from live driver state, no wire traffic. |
| `acc fpi idx` | Lists every occupied location from the index cache, up to which location the cache is valid and the next free location. No wire traffic. |
| `acc fpi led <0-9>` | Sets an LED state directly: 0 off, 1 scan, 2 match, 3 match without action, 4 no match, 5 create model, 6 wait for finger, 7 remove finger, 8 delete/not found, 9 success. Goes through the LED latch, so a productive activity may overwrite it afterwards. |

Related commands: `acc pwr on` / `acc pwr off` toggle the raw scanner power pin on boards which have one
(the driver detects the resulting desynchronisation and recovers by itself), `acc test mode` runs the
full device test sequence (scanner, LEDs, relay, optionally NFC and keypad).

## Synchronisation between devices

* The KNX sync wire format is **frozen**: the uncompressed payload is always
  `FP_TEMPLATE_SIZE_MAX + 29` = 1565 bytes and the person data always starts at offset 1536, also for
  a 512-byte R503Pro template. Only the number of bytes requested from or sent to the sensor follows
  the model profile (`templateSize()`): asking an R503Pro for 1536 bytes would run into the data phase
  timeout.
* Both uncompressed buffers are zeroed before every fill. The unused tail behind a short template (or
  behind a 38-byte NFC/keypad record) therefore compresses away instead of being random memory
  content, which keeps R503Pro broadcasts about as short as R503 ones.
* The control packet log line reports both sizes, e.g.
  `Sync-Send (syncTypeCode=0, 1/56): control packet: bufferLength=698, ..., uncompressed=1565, compressed=698`,
  so the compression ratio of a broadcast can be judged from the log alone.
* A finger template export needs the scanner and therefore goes through the arbiter; a broadcast which
  cannot get it stays armed and is retried, bounded by `SYNC_SEND_START_TIMEOUT` (90 s). The same
  bound exists for a received template waiting to be written (`SYNC_IMPORT_START_TIMEOUT`).
* A received delete is queued (`BUS_DELETE_QUEUE_SIZE`) instead of being executed inside the group
  object callback, and is never broadcast again.
