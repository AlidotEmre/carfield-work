# Mock OpenTitan — Design Spec (Claude Code input)

**Deliverable:** a kthread-based mock OpenTitan consumer inside the `carfield` driver, enabled with `insmod carfield.ko mock_ot=1`, plus an end-to-end userspace test. No behavior change when `mock_ot=0`.

**Role of this document:** this is the *contract*. Implement the mock from THIS spec only.

---

## 1. Purpose and scope

The mock plays the role OpenTitan will play on real hardware: it is the first ever **consumer** of the paging chain. It must independently walk `header_phys → header page → map page → data pages`, transform the payload, write the result back into the pinned user pages, and signal completion.

It validates: that the physical addresses published in the header/map are real and usable by a party that did not produce them; that `FOLL_WRITE` direction works (data written via phys addresses lands in the user buffer); that `fpo/fps/lps` describe the payload correctly; that the completion/timeout path works; that release/unpin after consumption doesn't leak.

It does **not** validate hardware realities (see §8). Label all results accordingly.

## 2. Implementation ground rules

1. **Clean-room discipline:** derive the mock's reading of the header/map from the struct definition in §3 and the rules in §5 — NOT from how `carfield_paging.c` happens to build them. The mock is a check on the producer; if it mirrors the producer's code, it checks nothing.
2. **Independent walk:** the mock must start from the raw `header_phys` value (`u32`) and resolve pages itself (`pfn_to_page(PHYS_PFN(...))` + `kmap_local_page`/`kmap`). It must NOT reuse the producer's in-kernel pointers (`h->header`, `h->map`, `h->data_pages`). Reusing them silently bypasses the whole point.
3. **Service-loop shape** (mirrors the team's own OT-side pattern from the titanssl `external_irq_handler`): wait for doorbell → acknowledge/reset doorbell → read letters → validate → process → write reply letters → signal completion → loop.
4. **Backend seam:** the host side must talk to the mock through the same minimal interface the real mailbox will use later — conceptually `send(letter0, letter1)` + `wait_completion(timeout)` + `read_reply(&letter0, &letter1)`. Mock backend = shared variables + waitqueues; hardware backend (later) = mailbox registers + IRQ. Swapping backends must not touch caller logic.
5. Kernel hygiene: kthread must be stoppable (`kthread_should_stop`), no busy-wait (sleep on a waitqueue), all `kmap`s paired with `kunmap`, and module unload must work cleanly with the thread running.

## 3. The contract being consumed

Header page: one 4 KB page, physically below 4 GB, containing little-endian `u32` fields at offset 0:

| Offset | Field  | Meaning |
|-------:|--------|---------|
| 0x00 | `magic` | must equal `0xCA4F1E1D` |
| 0x04 | `dsz`   | payload size in bytes |
| 0x08 | `nop`   | number of data pages |
| 0x0C | `fpo`   | offset of payload start inside the first data page |
| 0x10 | `fps`   | payload bytes in the first page |
| 0x14 | `lps`   | payload bytes in the last page |
| 0x18 | `map`   | 32-bit phys addr of the map page |

**Post-implementation addendum:** the header struct gained a trailing `version` (0x1C) + `reserved[2]` (0x20, 0x24) as of the actual implementation — offsets 0x00-0x18 above are unchanged, this is pure growing room (candidate use: session/PID multiplexing if that's ever needed, see `TITANSSL_ANALYSIS.md` §5 RESERVE). The mock reads and deliberately ignores these fields today; no §5 rule covers them yet.

Map page: one 4 KB page, physically below 4 GB, holding `nop` consecutive `u32` **physical page addresses** in payload order. Payload layout: bytes `[fpo, fpo+fps)` of page 0, then full pages 1..nop-2, then bytes `[0, lps)` of page nop-1. `PAGE_SIZE = 4096`.

Doorbell: `letter0 = header_phys`, `letter1 = command` (define `CMD_XFORM = 0x0001` for now). Reply: `letter1 = status` (0 = OK, else error code from §5), `letter0` echoed.

## 4. Happy-path behavior

On doorbell with `CMD_XFORM`: validate per §5; walk the payload exactly as §3 defines it; apply the transform **XOR each payload byte with `0x5A`** in place (writes go back through the mock's own mapping of the data pages — this is the write-back proof); reply `status=0`; signal completion.

XOR is chosen deliberately: self-inverse (running twice restores the input), so userspace can verify with a single comparison, and any byte processed twice/zero times is detectable.

## 5. Validation rules (mock must REJECT, with distinct error codes)

The mock is adversarial: a mock that accepts everything confirms instead of testing.

1. `magic != 0xCA4F1E1D` → `ERR_MAGIC`
2. `dsz == 0` → `ERR_SIZE`
3. `nop == 0` or `nop > 1024` → `ERR_NOP`
4. Recompute expected page count from `dsz` and `fpo`: `expected_nop = (fpo + dsz + PAGE_SIZE - 1) / PAGE_SIZE`; mismatch → `ERR_GEOMETRY`
5. Consistency: if `nop == 1` then `fps == dsz && lps == dsz`; if `nop > 1` then `fps == PAGE_SIZE - fpo` and `lps == dsz - fps - (nop-2)*PAGE_SIZE` → else `ERR_GEOMETRY`
6. `map == 0` or not 4 KB-aligned → `ERR_MAP`
7. Any `map[i] == 0` or not 4 KB-aligned → `ERR_MAP_ENTRY`
8. `fpo >= PAGE_SIZE` → `ERR_GEOMETRY`

On any rejection: consume nothing, reply with the error code, signal completion (the host must never hang on a rejected request).

## 6. Fault-injection modes (module params)

- `mock_delay_ms=N` — sleep N ms before replying (exercises the host's timeout margin)
- `mock_no_reply=1` — swallow the request silently (host MUST return `-ETIMEDOUT` cleanly and release pages without leaking)
- `mock_force_err=N` — reply with error N regardless of input (host error propagation path)
- `mock_bad_xform=1` — apply XOR with the WRONG constant (`0xFF`); the userspace test MUST fail. This tests the test: if verification still passes, the test is vacuous.

## 7. End-to-end userspace test requirements

Fill a buffer with a known pattern → full-transfer ioctl → verify **every payload byte** equals `pattern ^ 0x5A`, and — critically — **bytes outside `[addr, addr+dsz)` are untouched** (catches `fpo/fps/lps` mishandling; allocate guard bytes around the payload on the same pages). Cases: single-page small buffer; page-straddling offset buffer; large multi-page `malloc` (scattered); `mmap`-aligned buffer (`fpo=0`); `mock_no_reply` timeout case; one §5 rejection case (e.g. corrupt magic via a debug knob or a malformed direct request). Repeat-run the whole suite ≥3× then `rmmod` and check `dmesg` for leak/Bad-page warnings.

## 8. What this proves / does NOT prove

| Proves (software, real kernel) | Does NOT prove (FPGA only) |
|---|---|
| Phys addrs in header/map are independently resolvable and readable | OT's interconnect can reach those DDR addresses / address-view translation |
| Write-back through phys addrs lands in the pinned user buffer (`FOLL_WRITE`) | Cache coherence CVA6↔OT (flush-before-doorbell question) |
| `fpo/fps/lps` geometry is honored end to end | Real mailbox register semantics actually working on silicon (doorbell write observed, completion IRQ 58 fires and demuxes correctly) -- register map and IRQ number are confirmed (`docs/QUESTIONS_FOR_TEAM.md` item 8), just not yet exercised against real hardware |
| Timeout, error, release/unpin paths under a live consumer | 32-bit fit of real Carfield RAM; real OT firmware behavior/perf |

Report wording: "validated end-to-end against a software mock implementing the ratified contract" — never just "validated".

## 9. Definition of done

- [x] `mock_ot=0` path byte-identical to today (no regressions) — confirmed 2026-07-06, carfield-VM
- [x] All §7 cases pass 3× consecutively; `mock_bad_xform=1` makes the suite FAIL — confirmed 2026-07-06 (`tests/mock_ot_test`, 3 reps × 4 geometry cases PASS, `mock_bad_xform` sanity-fails as required)
- [x] `rmmod` clean under all modes incl. mid-`mock_no_reply` — confirmed, no Bad-page/BUG/leak warnings in `dmesg`
- [x] §5 rejections return distinct codes; host surfaces them as distinct errnos — confirmed (`mock_corrupt_magic` → `EILSEQ`, etc.)
- [x] §8 table copied into the repo's memory notes with results — see `memory/project_alsaqr.md` "Mock OpenTitan Consumer — Gerçek Kernel Testi" section
