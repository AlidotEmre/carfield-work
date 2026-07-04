# TitanSSL Analysis — inputs for Mock OpenTitan (MOCK_OT_SPEC.md)

Source repo: `alsaqr-fpga-ecs` (GitLab, private), local clone `~/alsaqr-fpga-ecs`
(WSL). All paths below are relative to that repo root. Read-only, nothing in
that repo was modified.

**Source availability caveat (flag first, affects §2):** `supt-openssl-ecslab/`
does not exist anywhere on this machine — not in `alsaqr-fpga-ecs`, not on the
WSL root filesystem, not under the Windows-side project folders. It is not a
submodule of `alsaqr-fpga-ecs` either (checked `.gitmodules`, none). So
`supt-openssl-ecslab/driver/kthread.c` (their own software mock of OT) and
`utils.h`/`driver_core.c` could **not** be read for this analysis. §2 below is
built entirely from `titanssl_firmware` (the real OT-side consumer), which the
brief already flagged as the most important source — so the gap is a missing
second data point, not a missing primary one. Confirm whether that repo is
meant to exist and just isn't cloned here, or whether it lives somewhere else
entirely before assuming it's unreachable.

---

## 1. Contract inventory

### 1.1 `titanssl_mbox_header_t` (`titanssl_driver/titanssl.h:82-105`, packed, 19×`u32`)

One instance, three regions back-to-back — input/output/meta triple-buffer,
not the mock's single in-place buffer:

| Word | Field | Width | Meaning | Why (inferred) | Ref |
|---|---|---|---|---|---|
| W00 | `magic_number` | 32 | `0xEC5FBEC5` | sanity check the header page is really initialized/ours before trusting `header_phy[1..]` | `titanssl.h:83`, `driver.c:463` |
| W01-06 | `input_dsz/nop/fpo/fps/lps/map` | 32 ea | input buffer geometry + map-page phys addr | encrypted operand(s) OT reads | `titanssl.h:85-90` |
| W07-12 | `output_*` | 32 ea | output buffer geometry + map | where OT writes its result — **separate** buffer from input | `titanssl.h:92-97` |
| W13-18 | `meta_*` | 32 ea | metadata buffer geometry + map | out-of-band operation parameters (keys, IV, RSA modulus — see 1.3) | `titanssl.h:99-104` |

Geometry quintuple (`dsz/nop/fpo/fps/lps`) is identical in shape to
`MOCK_OT_SPEC.md`'s single header, just repeated ×3. `map` is the phys addr of
a page of `u32` phys page addresses — same "map page" concept.

### 1.2 5-word mailbox message (`driver.c:16-23`, built at `driver.c:545-550`)

| Word | Field | Meaning | Ref |
|---|---|---|---|
| W0 | `TITANSSL_MAGIC_NUMBER` | wire-level magic, distinct instance from header's `magic_number` (same value, different memory) | `driver.c:546` |
| W1 | `header_phys & 0xFFFFFFFF` | **masked**, not range-checked (see §4 row 1) | `driver.c:547` |
| W2 | `(cmd&0xFFFF)<<16 \| (bitfield&0xFFFF)` | CMD+BITFIELD packed in one word | `driver.c:548` |
| W3 | `(session_id&0xFFFF)<<16 \| (session_cnt&0xFFFF)` | SESSION_ID+SESSION_CNT packed | `driver.c:549` |
| W4 | `current->pid` | PID, intended for multiplexing (see §5) | `driver.c:550` |

Doc comment (`driver.c:13-23`) claims 7 words / 28 bytes with W5/W6 as
doorbell/completion "reserved" slots **inside** the mailbox region — the real
code sends only 5 words (`MBOX_SIZE` = 20, `driver.c:123`, `memcpy_toio(...,
MBOX_SIZE)` at `driver.c:376`) and DOORBELL/COMPLETION are two entirely
separate `ioremap`'d 4-byte registers (`driver.c:120-121,364-366`). **Comment
and code disagree** — flagged in §6.

### 1.3 BITFIELD (`driver.c:28-47` doc vs `titanssl_utils.h:82-91` code)

| Bits | Driver-comment meaning | Firmware enum (`titanssl_utils.h`) meaning | Ref |
|---|---|---|---|
| [0-2] | `PAGE_SIZE_SHIFT` | same | `driver.c:29-38`, `titanssl_utils.h:363` |
| 3/4/5 | INPUT/OUTPUT/META_REQUIRED | same | `titanssl_utils.h:364-366` |
| 6/7/8 | "RESERVED" | INPUT/OUTPUT/META_**HYPERPAGE** (two-tier map, see §1.5) | `titanssl_utils.h:367-369` |
| [9-11] | `ERRORS` (0=OK, 0b111=GENERIC) | `OPERATION_STATUS`, same slot/size, renamed | `titanssl_utils.h:370` |

Bits 6-8 are stale in the driver-file's own doc comment — the firmware
repurposed them for hyperpages and the comment was never updated. Real drift,
not just stale prose, because the two files are independently maintained.

### 1.4 CMD (`titanssl.h:21-29` host vs `titanssl_utils.h:58-67` firmware)

Both enums are implicit-value (0,1,2,...), no shared header, and use
**different names for the same ordinal**: `RSA_DOCIPHER` (host) vs
`RSA_DOCYPHER` (firmware, note the swapped I/Y — `titanssl.h:28` vs
`titanssl_utils.h:65`). Numeric agreement (0-6) is accidental/positional, not
enforced by a shared symbol. Firmware also defines `TIMEOUT=7`
(`titanssl_utils.h:66`) that the host driver never sends (no `cmd=7` anywhere
in `driver.c`) — dead branch, `titanssl_main.c:91-93`.

### 1.5 SESSION_ID / SESSION_CNT / PID

- `session_id` is **hardcoded to `1`** for every session
  (`titanssl_engine/tee_client_api.c:70`) — not actually used to distinguish
  concurrent sessions, despite the 16-bit width implying up to 65,536.
- `session_cnt` increments per invocation on the *same* session object
  (`tee_client_api.c:170,213,244`, `session->session_cnt++`) — a sequence
  number, not a demux key.
- `pid` (`driver.c:550`) is the only value that actually varies per calling
  process. Firmware has a placeholder for checking it that was **never
  implemented**: `titanssl_main.c:66-71` is a comment block — `/* HERE check
  for process id */ /* ------------------------- */` — with no code between
  the markers. So even PID-based multiplexing exists as an ABI field with no
  consumer-side logic behind it. Section 5 treats this as informative, not as
  something to blindly inherit.

### 1.6 Map pages — Masterpage vs Hyperpage (`titanssl_utils.c:196-256`)

Two addressing modes selected by the HYPERPAGE bitfield bit (§1.3):
- **Masterpage** (single-tier, the mode actually used — see §4 row 6):
  `((titanssl_batch_t*)map)[i].data` — treats each `u32` map entry as if it
  *were itself* a `titanssl_batch_t` (a single-pointer struct), i.e. reads the
  phys-addr bit pattern stored by the producer and reinterprets it directly as
  a native pointer. Only correct because OT is a flat/identity-mapped 32-bit
  core (no MMU translation) and `sizeof(titanssl_batch_t) == sizeof(uint32_t)`
  on that build — an implicit, fragile coupling (`titanssl_utils.c:206-213`).
- **Hyperpage** (two-tier, `map` points to an array of pointers to
  second-level map pages, `titanssl_utils.c:215-225`): built for `nop` beyond
  one page of `u32` entries (>1024). This is the *exact* problem
  `MOCK_OT_SPEC.md §5.3`'s `nop > 1024 → ERR_NOP` sidesteps by rejecting
  instead of chaining — see §5 RESERVE.

---

## 2. Consumer-side walk (titanssl_firmware — the only available OT-side source)

Step-by-step, host doorbell → firmware reply, all in
`titanssl_firmware/sw/tests/opentitan/titanssl/`:

1. **Wait for doorbell (interrupt-driven, not polled):** `utils_irq_enable()`
   configures the PLIC and enables external interrupts (`titanssl_utils.c:464-476`),
   then `main()` just does `wfi` in a loop (`titanssl_main.c:41-42`). The
   doorbell write on the host side (`driver.c:382`) is what wakes the core.
2. **ISR entry, ack the doorbell:** `external_irq_handler()`
   (`titanssl_main.c:47-103`) is the actual consumer entry point. First
   `utils_irq_check()` busy-polls the PLIC claim register until it matches
   `TITANSSL_MBOX_IRQ_ID` (159) (`titanssl_utils.c:478-482`), then
   `utils_irq_reset_door()` clears the doorbell register
   (`titanssl_utils.c:444-447`, `titanssl_main.c:54`).
3. **Read letters / header fields:** `utils_mbox_read(&t_param)`
   (`titanssl_utils.c:93-152`) copies `cmd/bitfield/session/cnt/pid` straight
   out of the live MMIO mailbox struct, computes `page_size` from the
   PAGE_SIZE_SHIFT bitfield, then **only if
   `titanssl_mbox->magic_num == titanssl_mbox->header_phy[0]`**
   (`titanssl_utils.c:103`) does it read `input_ds/nop/fpo/fps/lps/map`
   (and output/meta, gated per-region by the REQUIRED bits). Note:
   `header_phy` is declared `uint32_t *` and dereferenced directly
   (`titanssl_utils.h:149`) — same flat/identity-address assumption as §1.6.
   **If the magic check fails, the function silently returns with
   `t_param`'s buffer fields simply never populated — no error status is
   set, no rejection is signaled.** This is the ceiling of “validation” on
   the consumer side; contrast with `MOCK_OT_SPEC.md §5`'s 8 explicit
   distinct-code rejections.
4. **Command dispatch:** `external_irq_handler` branches on `mb_cmd` into
   `hmac_run`/`aes_run`/`rsa_run` (`titanssl_main.c:78-93`). Each of those
   (e.g. `aes_run`, `titanssl_aes.c:21-45`) calls a `_xxx_validate()` first —
   for AES this is `_aes_validate()` at `titanssl_aes.c:47-50`, whose entire
   body is `// check also bitfield` `return true;`. **It never actually
   validates anything.** The only place a `GENERAL_ERROR` status can be set
   is the `else` branch when validate returns false (`titanssl_aes.c:24,42-44`)
   — a branch that, for AES, is unreachable.
5. **Payload walk / buffer indirection:** `utils_dram_readpage()` /
   `utils_dram_writepage()` (`titanssl_utils.c:196-256`) pick
   Masterpage/Hyperpage per §1.6, index the map by
   `bytes_processed_so_far / page_size` (page index) and add `fpo` only on
   page 0 (`titanssl_utils.c:210-211,241-242`) — i.e. the same
   "first page gets an offset, rest don't" rule as
   `MOCK_OT_SPEC.md §3`'s payload layout. Sizes per page are **not**
   passed in explicitly here; the caller (each crypto routine) is
   responsible for calling these page-at-a-time in `page_size`-sized (or
   smaller, on the last page) chunks and knowing when it's on page 0 or the
   last page — `fps`/`lps` are consulted by the callers, not by the
   read/write helpers themselves.
6. **Result write-back:** writes go through `utils_dram_writepage()` directly
   into the OUTPUT map's pages — i.e. into the pinned user pages the host
   handed over via `output_map`/`output_pages`, not into a separate kernel
   staging buffer. Confirms `MOCK_OT_SPEC.md §1`'s "write result back into the
   pinned user pages" is exactly this pattern.
7. **Reply + completion:** `utils_mbox_set(&t_param)`
   (`titanssl_utils.c:154-164`) writes `cmd/bitfield/session/cnt/pid` back to
   the mailbox — see §6 for a real bug in this function — then
   `titanssl_main.c:96-98` calls `utils_irq_reset_door()` **again** (comment:
   "patch reset doorbell for openssl speed"), `utils_irq_end()` (clears the
   PLIC claim, `titanssl_utils.c:484-488`), then
   `utils_irq_trig_comp()` (`titanssl_utils.c:459-462`) sets the completion
   register — this is the actual "doorbell" the host's ISR is waiting on.
8. **Host wakes:** `titanssl_mbox_completion_handler()` (`driver.c:298-303`)
   just sets a flag and `wake_up_interruptible()` — it does **not** reset the
   completion register itself. That happens after the host's
   `wait_event_interruptible()` returns, back in `mbox_send()`
   (`driver.c:385-388`, `writel(0x0, completion)`).

**Loop:** back to `wfi` (`titanssl_main.c:41-42`) — there is no persistent
kthread-style loop object on the OT side, the "loop" is interrupt re-arm, not
a `while(1)` around the handler body. `MOCK_OT_SPEC.md §2.3`'s kthread
service-loop (wait → ack → read → validate → process → reply → signal →
loop) is a reasonable Linux-side translation of this same shape, since a
kthread has no hardware interrupt to re-arm.

---

## 3. Sync & memory-ordering notes

| Location | Operation | Stated/apparent reason | Ref |
|---|---|---|---|
| `driver.c:379` | `asm volatile ("fence.i")`, comment "IMPORTANT! Clean cache!" | Make header/map/data page writes (done earlier in `titanssl_core`) visible to OT before the doorbell fires | `driver.c:378-379` |
| `driver.c:388` | `writel(0x0, completion)` | Reset completion register after wake, so the next request's wait doesn't fire spuriously on a stale value | `driver.c:385-388` |
| `titanssl_main.c:96` | second `utils_irq_reset_door()` call, comment "patch reset doorbell for openssl speed" | Unexplained "patch" — looks like a workaround for a doorbell race discovered empirically, not derived from a documented invariant | `titanssl_main.c:96` |

**Correctness concern (flag, not smoothed over):** `fence.i` on RISC-V
orders/flushes the **instruction** fetch stream against memory — it is the
primitive for self-modifying-code coherency, not a data-memory barrier. It
does not, by itself, guarantee that an external agent reading through a
different physical path (OT via mailbox-triggered DMA-style access) observes
the CVA6 core's prior stores to the header/map/data pages. If what's actually
needed here is a data memory fence (`fence rw,rw` / a cache-management
op for a non-coherent interconnect), `fence.i` is very likely the wrong
instruction, kept only because it happens to work when the interconnect is
in fact coherent (or when timing hides the race). This is the same
"`fence.i` zorunlu" rule already recorded in `carfield-work/CLAUDE.md` line 32
and `project_alsaqr.md`'s "Kritik Teknik Notlar" #1 — i.e. **this project may
have inherited a RISC-V memory-model misconception from titanssl rather than
an independently verified requirement.** This does not block the mock (which
runs entirely in-process, no real cache/interconnect involved) but it is a
live correctness question for the real-hardware phase and belongs in the
Daniele meeting alongside the mailbox topology and EU questions already
queued in `QUESTIONS_FOR_DANIELE.md`.

No barrier of any kind was found on the OT-firmware side between writing
output data (`utils_dram_writepage`) and triggering completion
(`utils_irq_trig_comp`) — consistent with the open "cache coherence CVA6↔OT"
question already tracked in this project's own memory
(`project_alsaqr.md` "Kritik Teknik Notlar" #1, `QUESTIONS_FOR_DANIELE.md` §4).

---

## 4. Delta table — titanssl vs `MOCK_OT_SPEC.md`

| Contract element | titanssl approach | Mock spec approach | Flag |
|---|---|---|---|
| Address range safety | Mask: `phys_header & 0xFFFFFFFF` (`driver.c:547`), pages from plain `GFP_KERNEL` (`driver.c:444,448,452,456`) — silently corrupts on real phys addr >4GB | Reject: `GFP_DMA32` alloc + explicit `-ERANGE` if a data-page phys addr >4GB (`carfield_paging.c:84-90,116-122`, already implemented in this project, not a mock-phase decision) | **Philosophy difference, already resolved in our favor** |
| Buffer model | Triple buffer: input/output/meta, separate geometries (`titanssl.h:82-104`) | Single in-place buffer (`MOCK_OT_SPEC.md §3-4`) | changed — see §5 |
| Validation | Effectively none: single magic check with silent no-op on mismatch (`titanssl_utils.c:103`), `_aes_validate()` hardcoded `true` (`titanssl_aes.c:49`) | Adversarial, 8 distinct rejection codes (`MOCK_OT_SPEC.md §5`) | changed, deliberately stricter |
| Geometry math | `page_lps = PAGE_SIZE - (PAGE_SIZE*nop - dsz - fpo)` (`driver.c:629`) | `lps = dsz - fps - (nop-2)*PAGE_SIZE` (`MOCK_OT_SPEC.md §5.5`) | **algebraically identical** — cross-checked by hand, good corroboration |
| Page pin API | `get_user_pages_fast(..., FOLL_GET, ...)` (`driver.c:661`) — no `FOLL_WRITE` even for the output buffer OT writes into | `pin_user_pages_fast` with `FOLL_WRITE` when `write` is requested (`carfield_paging.c:28,35`, already implemented) | already resolved in our favor, and directly the thing §8 of the mock spec exists to prove |
| Map addressing | Map entry reinterpreted as a native pointer via struct-layout coincidence (`titanssl_utils.c:209`) | Mock must independently `pfn_to_page`/`kmap` from the raw phys addr (`MOCK_OT_SPEC.md §2.2`) | changed, deliberately more explicit/robust |
| Session/PID multiplexing | `session_id` hardcoded `1` (`tee_client_api.c:70`); PID field exists but firmware-side check was never implemented (`titanssl_main.c:66-71`) | Not present — single global mock, one in-flight request | no — see §5 |
| CMD encoding | Two independently-maintained enums, same ordinals, different names (`titanssl.h:21-29` vs `titanssl_utils.h:58-67`) | One numeric `#define CMD_XFORM = 0x0001` (`MOCK_OT_SPEC.md §3`) | changed, avoids the drift class seen in §1.4 |
| Command reply path | `utils_mbox_set()` has a real bug writing to the wrong register (`titanssl_utils.c:157-158`, see §6) | Mock reply is a plain struct field write | n/a — mock has no equivalent bug surface, just don't copy the pattern |
| Growth path for large `nop` | Hyperpage two-tier map (`titanssl_utils.c:215-225`) | Hard cap `nop > 1024 → ERR_NOP` (`MOCK_OT_SPEC.md §5.3`) | changed — see §5 RESERVE |
| IRQ acquisition | `platform_get_irq()` from devicetree (`driver.c:157`) | N/A for mock (module param gate, no real IRQ) | see §5 RESERVE for later phase |
| Hardware bypass mechanism | Compile-time `#ifndef TITANSSL_TEST_QEMU` around `mbox_send()` (`driver.c:560-562`) | Runtime `insmod ... mock_ot=1` module param (`MOCK_OT_SPEC.md` deliverable line) | changed, runtime toggle is strictly better (no rebuild to flip) |

---

## 5. Recommendations

**ADOPT NOW**
- Nothing from titanssl's validation model is worth adopting as-is — it's
  the thing the mock spec correctly improves on (§4 row "Validation").
- The historical pitfall at `driver.c:574` (`__free_page()` required instead
  of `free_page()`, or kernel reports `BUG: Bad page stat`) is a concrete,
  narrow gotcha worth keeping in mind for the mock's own page teardown path,
  since the mock will also `alloc_page`+`kmap`+`kunmap`+free its own reply
  bookkeeping if any is needed.

**RESERVE** (add a field/knob now so it can come later without an ABI break)
- **Hyperpage-style chaining for `nop > 1024`:** titanssl already solved
  "more map entries than fit in one page" with a real, working two-tier
  scheme (`titanssl_utils.c:215-225`). `ERR_NOP` on `nop > 1024` is the right
  call for the mock phase (matches `carfield_paging.c:78-79`'s existing
  single-page-map cap), but if `carfield_paging.c` ever needs to lift that
  cap, the pattern to reach for already exists in this codebase — no need to
  invent one.
- **Session/PID multiplexing:** the user's own Python flow sends one blob and
  gets a *differently-sized* result back — titanssl's separate
  input/output/meta buffers exist precisely to support that (`titanssl.h:82-104`),
  and the mock's single in-place XOR buffer cannot represent a size-changing
  transform (encrypt/decrypt with padding, e.g.). In-place survives the mock
  phase (XOR is size-preserving by construction) but **will not survive**
  the first real crypto-shaped OpenTitan command. Recommend reserving a
  second `letter`-style pointer (or repurposing the existing 2-word doorbell
  as `[header_phys, cmd]` today, `[in_header_phys, out_header_phys]` later)
  rather than assuming in-place generalizes. Do not implement multi-session
  concurrency now — titanssl's own `session_id` is degenerate (always `1`,
  §1.5) and its PID check was never implemented, so there is no working
  reference pattern to copy; a single in-flight request (as the mock spec
  already assumes) matches the "tek kullanıcı/tek donanım" assumption already
  recorded elsewhere in this project's memory.
- **CMD as a real shared enum, not a `#define`:** fine for one command now;
  the moment a second command exists, put both in one header both sides
  `#include`, not two hand-maintained enums — titanssl's `DOCIPHER`/`DOCYPHER`
  spelling drift (§1.4) is a small, concrete example of what happens
  otherwise.
- **IRQ acquisition via devicetree/platform_device**
  (`ot_mbox_probe`/`platform_get_irq`, `driver.c:147-165,157`) is a resolved
  pattern for getting a real IRQ number instead of a hardcoded placeholder —
  relevant once `CARFIELD_EOC_IRQ`/mbox IRQs move from "0, unconfigured" to
  real numbers post-Daniele-meeting. Not applicable to the mock itself
  (no real IRQ in this phase).

**REJECT**
- Address masking (`& 0xFFFFFFFF`) instead of rejecting out-of-range
  addresses — already correctly rejected in `carfield_paging.c`, do not
  regress toward titanssl's behavior.
- Reusing the producer's in-kernel pointers instead of an independent phys-addr
  walk — directly contradicts `MOCK_OT_SPEC.md §2.2`'s clean-room requirement,
  and titanssl's own Masterpage trick (§1.6) shows exactly the kind of
  implicit coupling that requirement is designed to prevent.
- A single magic-check-with-silent-no-op as the *only* validation
  (`titanssl_utils.c:103`) — the mock spec's 8-code adversarial validation is
  correctly a hard requirement, not a nice-to-have; titanssl's own
  `_aes_validate() { return true; }` (`titanssl_aes.c:49`) shows where "trust
  the producer" ends up.

---

## 6. Ratification questions (send to Daniele / team, not decided unilaterally)

1. **`fence.i` vs a real data-memory barrier** (§3): is the CVA6↔mailbox path
   actually cache-coherent, making `fence.i` a no-op-but-harmless leftover, or
   is a genuine data fence/CMO missing before the doorbell write? This affects
   `carfield-work/CLAUDE.md`'s rule #1 directly, not just titanssl.
2. **Is `supt-openssl-ecslab` supposed to be cloned locally?** It's referenced
   by name in this project's own memory (`project_alsaqr.md`, `CLAUDE.md`) as
   a second reference (their own software OT mock, `kthread.c`) but doesn't
   exist on either the WSL or Windows side of this machine, and isn't a
   submodule of `alsaqr-fpga-ecs`. Worth confirming its location/access
   before relying on "we already have a working mock reference" as an
   assumption in future sessions.
3. **BITFIELD bits 6-8:** driver.c's own header comment calls them
   "RESERVED" while `titanssl_utils.h` already uses them for HYPERPAGE flags
   (§1.3) — should Carfield's own bitfield layout (if one is ever needed for
   real mailbox messages, separate from the mock's 2-word letters) copy
   titanssl's numbering, or is this exactly the kind of doc/code drift to
   avoid by defining bits once, correctly, from scratch?
4. **`utils_mbox_set()` bug** (`titanssl_utils.c:157-158`): both lines write
   to `_mbox[2]` while masking against `_mbox[3]`:
   ```c
   _mbox[2] = (_mbox[3] & 0x0000FFFF) | t_param->mb_cmd;
   _mbox[2] = (_mbox[3] & 0xFFFF0000) | t_param->mb_bitfield;
   ```
   The second line's write clobbers the first entirely, and neither masks
   against its own prior value (`_mbox[2]`) — it reads `_mbox[3]` instead.
   Net effect: `mb_bitfield` (including any `OPERATION_STATUS`/error bits set
   during processing) ends up mixed with unrelated bits of the *session*
   word rather than being combined with the previously-written `mb_cmd`, and
   `mb_cmd` itself is immediately overwritten and never actually reaches the
   mailbox correctly. This looks like a copy-paste index error, not
   intentional. Not fixable by us (upstream code, different subsystem) but
   worth surfacing since it's the reply path our own mock's design is
   directly modeled on (§2.3/§4) — we should make sure our version doesn't
   carry the same mistake forward.
5. **`_utils_dram_getmetaHyperpage()` bug** (`titanssl_utils.c:290`): reads
   `t_param->output_mp` where every sibling function (`_utils_dram_getmetaMasterpage`
   at line 275, and the function's own name/purpose) implies it should be
   `t_param->meta_mp`. Same category as #4 — a genuine upstream bug, flagged
   for awareness, not something in our scope to patch.
6. **`mbox_send()`'s wait pattern** (`driver.c:369-374`): single `if
   (thread_wait_flag == 0) schedule();` after `prepare_to_wait_exclusive`,
   with the team's own inline comment "`// FB: use \`while\` instead
   \`if\`?`" — this is the classic missed-wakeup shape (should loop/recheck
   the condition after waking, not schedule once and assume). If Carfield's
   own future host-side "only one process may drive the mailbox at a time"
   logic is ever modeled after this function, it should use `while`, not
   `if` — worth a explicit decision rather than silently inheriting the
   questionable original.
