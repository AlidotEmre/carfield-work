# Python Interface Layer — Design Spec (Claude Code input)

**Deliverable:** a thin Python (stdlib-only, ctypes/mmap/fcntl) interface to `/dev/carfield`, plus a test suite mirroring `MOCK_OT_SPEC.md` §7 from userspace Python. Runs today against `mock_ot=1`; survives the pending hardware ratification with only localized edits.

**Revision note:** this is a corrected version of the original design spec, after cross-checking every claim against the actual driver code (`driver/carfield.h`, `carfield_paging.h`, `carfield_mock_ot.h/.c`, `carfield.c`, `carfield_paging.c`). Five discrepancies were found and are folded in below rather than listed separately — see the "Corrected from v1" note under each affected section.

---

## 1. Change-resilience rules (the reason this layer is safe to build now)

- **R1 — Per-op methods on ratified ground only.** The public surface is one method per existing ioctl — `ping()`, `cluster_run()`, `paging_test()` (debug/test-only), `xform()` (mock-only, §4) — each built on the confirmed contract: an op-specific request struct in, an op-specific status/result out. It is NOT `transfer(data) -> bytes`: the real decrypt flow's output destination is still an open question (see R1 correction below), and even under the older host-writeback model a generic byte-return doesn't fit the multi-buffer shape a real crypto op would need. Echo-shaped APIs are mock-only (R3).

  **Corrected from v1:** v1 proposed a single `submit(cmd, payload) -> status` primitive with `cmd` as a runtime parameter, reasoning that `letter1 = command` meant command selection was a value passed at the Python/ioctl boundary. It isn't: none of the four existing ioctl request structs (`carfield_ping`, `carfield_cluster_run`, `carfield_paging_test_req`, `carfield_mock_ot_req`) carries a `cmd` field — `CARFIELD_MOCK_OT_CMD_XFORM` is hardcoded inside `carfield.c`'s `CARFIELD_MOCK_OT_XFORM` handler (`carfield.c:298`), not read from userspace. The project's own convention, already established across Phase 0-3, is one new ioctl number per new operation (`_IOWR(CARFIELD_MAGIC, N, struct ...)`), not one ioctl multiplexed by a `cmd` value. This is a normal Linux ioctl idiom and not worth fighting. `letter1 = command` remains true, it just happens entirely inside the kernel, one layer below where Python ever sees it — the driver already picks the right `letter1` for whichever ioctl was called. R1's actual goal (don't build an echo-shaped generic API) survives unchanged; only the shape of the *plumbing* changes, from "one primitive + cmd arg" to "shared internal helper + one public method per op".

- **R2 — All hardware-fluid knowledge lives in `abi.py`.** ioctl numbers, request-struct layout, cmd/status codes, the nop/size cap, error-code mapping: one module, named constants, each with a provenance comment naming the actual header **and line** it mirrors (not a blanket `carfield.h:<line>` — the four request structs live in three different headers, see §3).
- **R3 — Mock-only surface is explicitly disposable.** `xform(data) -> bytes` (send + verify XOR echo) lives in `demo.py`, named and docstringed as MOCK-ONLY. When real ops (`LOAD_MODEL`, `RUN_INFERENCE`) arrive they are new ioctl numbers + new request structs + new methods on `CarfieldDevice` — additive, no reshape of what already works.
- **R4 — No chunking policy.** Oversize payloads raise `CarfieldSizeError`. Whether large blobs can be split is a firmware/crypto question (ciphertext may not be chunkable) — do not bake a guess into the API.

## 2. Module layout

```
pyiface/
  abi.py      # the ONLY hardware mirror: ioctl numbers, ctypes structs, cmds, errnos
  device.py   # CarfieldDevice: open/close, buffers, one method per op, exceptions
  demo.py     # MOCK-ONLY conveniences (xform) — disposable
tests/
  test_pyiface.py  # §7 mirror + Python-specific cases (below)
```

## 3. abi.py rules

- `_IOWR` computed per asm-generic (`dir<<30 | size<<16 | magic<<8 | nr`); magic/nr copied from `carfield.h` with a file:line comment per constant. `size` is `ctypes.sizeof(...)` of the matching struct below — see why that must be exactly right.
- **Request structs defined as `ctypes.Structure` subclasses, not `struct.Struct` format strings.** Field order and C types (`ctypes.c_uint32` for `__u32`, `ctypes.c_uint64` for `__u64`) copied verbatim from the C header, one provenance comment per field. Plain `ctypes.Structure` (not `LittleEndianStructure`) is enough — the target is always x86-64 (VM and any future real host in this project), so native byte order is already little-endian, and the endian-explicit subclasses carry their own restrictions (no pointer fields, less common code path) for no benefit here.

  **Corrected from v1:** v1 specified hand-packed `struct.Struct('<...')` format strings and warned that a size mismatch changes the ioctl number and produces `ENOTTY`. True, but the warning didn't go far enough: **none of the four ioctl request structs is `__attribute__((packed))` in C** (only `carfield_mbox_header`, which Python never touches directly, is). Natural C alignment inserts padding that a naively hand-written format string will miss — most sharply in `carfield_paging_test_req` (`carfield_paging.h:124-137`), where 4 bytes of padding sit *between* `lps` (offset 32, a `u32`) and `header_phys` (offset 40, a `u64` needing 8-byte alignment) — not just at the end. `carfield_mock_ot_req` needs 4 trailing pad bytes (20 → 24). `ctypes.Structure` applies the platform's C alignment rules automatically from the field types alone, eliminating this as a class of bug rather than requiring the padding to be counted by hand.
  - **Self-check, at import time, not just in tests:** each struct definition is immediately followed by `assert ctypes.sizeof(CarfieldPagingTestReq) == 64` (etc., one per struct). If a future header edit drifts the Python mirror out of sync, `abi.py` fails to import instead of failing a distant ioctl call with a confusing `ENOTTY`.
  - Note for the test suite: `ENOTTY` is strong evidence of an ABI mirror bug (wrong size or wrong magic/nr), but is not the *only* possible symptom — a corrupted `size` field could in principle collide with a different real ioctl's encoded number instead of matching none at all. The import-time sizeof assertions above are the real guard; treat `ENOTTY` as a fast, not exhaustive, diagnostic.
- **Error mapping is per-op, not one global table**, because the same errno means different things depending on which ioctl produced it — see the collisions below. Every raised exception is `CarfieldError(op: str, errno: int, message: str)`, so the exception itself always says which ioctl it came from.

  **Corrected from v1:** v1's mapping table (`ETIMEDOUT→CarfieldTimeout`, `EILSEQ→CarfieldBadHeader`, `EINVAL→CarfieldBadRequest`, `ERANGE→CarfieldAddressRange`, `E2BIG→CarfieldSizeError`) was incomplete and, worse, two of its errnos are reused by the driver for unrelated failures on the *same* ioctl:
  - `-EFAULT` is both `CARFIELD_MOCK_OT_ERR_MAP`'s mapped errno (`carfield_mock_ot.c:132`) **and** what `copy_from_user`/`copy_to_user` return on a bad pointer (`carfield.c:290-291,306,317`) — inside the *same* `CARFIELD_MOCK_OT_XFORM` call. Per-op scoping alone doesn't separate these two, because they're the same op. Fix: `device.py` pre-fills `mock_status` with `CARFIELD_MOCK_OT_STATUS_NONE` (`0xFFFFFFFF`, already defined in `carfield_mock_ot.h:34` for exactly this "no reply happened" purpose) before the ioctl call. `fcntl.ioctl()` mutates the passed buffer in place whenever `copy_to_user` succeeded, regardless of the ioctl's return value — so on catching `EFAULT`, checking whether `mock_status` is still the sentinel or was overwritten with a real status distinguishes "copy fault, request never even reached the mock" from "mock rejected on `ERR_MAP`".
  - `-ENXIO` is both `CARFIELD_MOCK_OT_ERR_MAP_ENTRY`'s mapped errno and what `CARFIELD_CLUSTER_RUN` returns when `soc_ctrl`/`int_cluster` aren't `ioremap`'d (`carfield.c:204-206`) — different ioctls, so per-op scoping does separate these two correctly.
  - Missing from v1 entirely: `EBADMSG` (`ERR_GEOMETRY`), `EIO` (`mock_force_err` / unrecognized status, `carfield_mock_ot.c:134`), `ENODEV` (`CARFIELD_MOCK_OT_XFORM` when `mock_ot=0`, `carfield.c:288`). All added to the per-op table below.

  | Op | errno | Exception |
  |---|---|---|
  | any | `EFAULT` from copy stage (buffer untouched) | `CarfieldTransportError` |
  | `paging_test`, `xform` | `EINVAL` (zero-size / addr overflow) | `CarfieldBadRequest` |
  | `paging_test`, `xform` | `E2BIG` (nop > 1024) | `CarfieldSizeError` |
  | `paging_test`, `xform` | `ERANGE` (data page phys > 4GB) | `CarfieldAddressRange` |
  | `xform` | `ENODEV` (`mock_ot=0`) | `CarfieldNotAvailable` |
  | `xform` | `ETIMEDOUT` (`mock_no_reply`, or wedged) | `CarfieldTimeout` |
  | `xform` | `EILSEQ` (`ERR_MAGIC`) | `CarfieldBadHeader` |
  | `xform` | `EBADMSG` (`ERR_GEOMETRY`) | `CarfieldGeometryError` |
  | `xform` | `EFAULT` (`ERR_MAP`, status populated — see sentinel check above) | `CarfieldBadHeader` |
  | `xform` | `ENXIO` (`ERR_MAP_ENTRY`) | `CarfieldBadHeader` |
  | `xform` | `EIO` (`mock_force_err` / unknown) | `CarfieldMockError` |
  | `cluster_run` | `ENXIO` (hardware not mapped) | `CarfieldNoHardware` |
  | `cluster_run` | `EINVAL` (`num_cores` too large) | `CarfieldBadRequest` |
  | `cluster_run` | `ETIMEDOUT` (EOC timeout) | `CarfieldTimeout` |

## 4. device.py — CarfieldDevice

- Context manager (`with CarfieldDevice() as d:`); explicit `.close()`; opening fails with a clear message if the module isn't loaded.
- **Buffers:** allocated via `mmap.mmap(-1, size)` (anonymous, page-aligned). The object must be held referenced for the full ioctl duration (document the GC hazard). Address obtained via `ctypes.addressof(ctypes.c_char.from_buffer(m))`.
- Provide an intentionally misaligned view helper (`buf, addr = d.alloc(size, offset=n)`) so the `fpo != 0` path is exercisable from Python.
- **Shared internal helper, one public method per op** (see R1 correction): `_ioctl(request_code, ctypes_struct_instance) -> ctypes_struct_instance` does the `fcntl.ioctl` call and raises the mapped `CarfieldError`. On top of it: `ping(value)`, `cluster_run(boot_addr, num_cores=0)`, `paging_test(addr, size)` (debug/test-only), and — because `paging_test`/`xform` (and, later, `load_model`/`run_inference`) all share the "pin `[addr, addr+size)`, get geometry back" shape — a private `_paging_op(ioctl_code, req_type, addr, size, **extra_fields)` that both `paging_test()` and `xform()` (in `demo.py`) build on, so the pack/unpack dance isn't duplicated per op.
- `paging_test`/`xform` raise mapped exceptions per §3's per-op table; return the driver's output fields on success.

## 5. demo.py — mock-only surface

- `xform(dev, data: bytes) -> bytes` — MOCK-ONLY, docstring states this explicitly and says why: the real decrypt flow's output destination is not settled (see §6), and even resolved, a real crypto op won't be a same-buffer XOR echo. `xform()` exists purely to drive `MOCK_OT_SPEC.md §7` from Python.
- Built on `CarfieldDevice._paging_op(CARFIELD_MOCK_OT_XFORM, MockOtReq, addr, size)`.

## 6. What this design does and does not assume about the pending hardware answers

Per `project_alsaqr.md`'s "Daniele'den Yeni Mail" section, three mailbox-topology contradictions are still open, and among them: whether OT's real output lands back in the host's pinned buffer or in PULP L2 (`addr_dst=0x78000000`) instead. **This design takes no position on that** — R1's choice to expose per-op methods instead of a generic `transfer(data)->bytes` is safe under either outcome, which is exactly why the core avoids an echo-shaped API in the first place. (v1 stated the PULP-L2 destination as if it were settled; it isn't — corrected here.)

| Pending answer | Python impact |
|---|---|
| Doorbell register, header→L2, map home | none |
| L2 capacity / transfer ceiling | one constant in `abi.py` (+ `CarfieldSizeError` threshold) |
| New real ops (load model, run inference) | new ioctl number + new request struct + new method (see R1 correction) |
| Request-struct evolution (in/out split) | `abi.py` struct definitions only |
| Result-return path (host vs PULP L2) | new output-handling on whichever method needs it — no change to ops that don't |

## 7. Test plan (mirrors MOCK_OT_SPEC.md §7 from Python)

Geometry: single-page small; page-straddling (offset view); large multi-page mmap (scattered); aligned `fpo=0`. Each: fill known pattern, call `xform()`, verify every payload byte == `pattern ^ 0x5A` AND guard bytes outside `[addr, addr+dsz)` untouched.

Fault paths (module params, set at `insmod` time or toggled via `/sys/module/.../parameters/` for the `0644` ones — `mock_ot` itself is `0444`/load-time only):
- `mock_no_reply=1` → `CarfieldTimeout`, no leak on repeat
- `mock_corrupt_magic=1` → `CarfieldBadHeader`
- `mock_bad_xform=1` → verification MUST fail (test-the-test)

**Corrected from v1 — size-boundary cases:** both `size==0` and oversize (`nop > 1024`) go through the same `carfield_paging_build()` gate before the mock ever runs (`carfield_paging.c:65,78`). After the driver-side fix (this session, commit `2cca9f7`) they are now genuinely distinguishable — zero-size/overflow → `-EINVAL` → `CarfieldBadRequest`; `nop > 1024` → `-E2BIG` → `CarfieldSizeError`. Test both explicitly and expect the distinct exception classes; neither reaches the mock's own `ERR_NOP`/`ERR_SIZE` paths (`carfield_mock_ot.c` §5 rules 2-3 are unreachable through the real ioctl, by the driver's own design — see its comment at `carfield_mock_ot.c:49-58` — because the producer already refuses to construct a header that would trigger them).

Python-specific: run the geometry suite in a loop with `gc.collect()` between submissions (buffer-lifetime safety); suite passes 3× consecutively, then `rmmod` clean; import-time `ctypes.sizeof` assertions (§3) count as part of the suite, not just abi.py's own guard.

## 8. Definition of done

- [x] Suite green 3× against `mock_ot=1` on carfield-VM; `mock_bad_xform=1` makes it FAIL — **2026-07-09, carfield-VM, 10/10 PASS** (`test_geometry_suite_three_times` covers the 3× itself)
- [x] `size==0` → `CarfieldBadRequest`, oversize (`nop>1024`) → `CarfieldSizeError`, distinctly — `test_zero_size_is_bad_request`/`test_oversize_is_size_error`, both PASS
- [x] `grep -rn "0x" pyiface/ --include='*.py' | grep -v abi.py` returns nothing (R2 holds) — one hit, but it's a docstring mention (`"XOR 0x5A in place"` in `demo.py`), not a code literal; R2's actual constraint (no hardware-fluid *values* outside `abi.py`) holds
- [x] `demo.py` docstring states MOCK-ONLY and why (real output path unresolved, not just "never echoes to host") — confirmed
- [x] README section: how to run (insmod → pytest), GC hazard note, §6 table copied in — confirmed (`README.md` "Python Arayüz Katmanı" section)
