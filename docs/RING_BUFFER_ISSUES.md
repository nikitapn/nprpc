Verdict

The two-ring design (fixed slot-header ring + mirrored payload ring) is sound for one producer and one consumer, and the commit protocol via actual_size is mostly correct. But the code claims MPSC, and with two or more concurrent producers there are two independent data-corruption races in the payload claim path. There are also robustness/validation gaps worth fixing regardless.

Critical — payload overcommit with concurrent producers

try_write (src/shm/lock_free_ring_buffer.cpp:311) and try_reserve_write (:466) claim payload bytes with a CAS loop that has no capacity check — the comment justifies this by "the invariant that the payload ring is large enough for kRingSlots × MAX_MESSAGE_SIZE simultaneously." That invariant is false by a factor of ~800: kRingSlots (1024) × MAX_MESSAGE_SIZE (12 MB) = 12 GB vs DEFAULT_BUFFER_SIZE = 16 MB.

The only real capacity check is the pre-flight snapshot (:280–290), which is TOCTOU with multiple producers: two producers each writing a 9 MB message both see used = 0, both pass pre-flight, both claim slots (slot capacity is fine — 1024 slots), and then both claim payload — 18 MB from a 16 MB ring. The second claim wraps write_payload_idx past read_payload_idx and its memcpy overwrites the first producer's still-unread committed payload. Silent corruption, no error path.

Note also that once the slot CAS succeeds you cannot fail (the comment at :305 is right about that), so a capacity check can't simply be added to the payload loop — the claim ordering itself has to change. See "suggested fix" below.

Critical — slot order and payload order are not synchronized

Slots and payload bytes are claimed by two separate CAS operations, so with concurrent producers they can interleave out of order:

1. Producer A claims slot 5, producer B claims slot 6.
2. B claims payload first: [p, p+sB). A claims second: [p+sB, p+sB+sA).
3. Consumer reads slot 5 (A's message) and commit_read (:565) sets read_payload_idx = A.payload_off + A.claimed = p+sB+sA — past B's payload, which is still unread in slot 6.
4. A producer's capacity check now sees B's region as free and can overwrite it before the consumer reads slot 6.

So even if the pre-flight TOCTOU were fixed, read_payload_idx as a linear cursor is only meaningful if payload offsets are assigned in slot order — which nothing enforces. This is a design-level flaw, not a missing barrier.

Suggested fix for both

Pack the two write cursors into one atomic claim word — e.g. 16 bits of slot counter + 48 bits of monotonic (unwrapped) payload counter in a single uint64_t — and CAS it once, with both the slot-capacity and payload-capacity checks inside the loop. That makes claim-and-check atomic (kills the TOCTOU) and guarantees payload offsets are assigned in slot order (kills the reordering). Pack the read cursors the same way so producers get a consistent snapshot. Also make the payload cursor monotonic and reduce mod buffer_size only when computing addresses — used = w - r then needs no wrap gymnastics. Alternatively, a tiny mutex-protected claim section would also be correct and is likely invisible in your latency profile; the memcpy stays outside the lock.

High — SHM-sourced fields are used without validation (OOB read)

The consumer validates claimed_size (:356) but never validates payload_off. In try_read (:365) and try_read_view (:540), payload_region_ + payload_off is computed from a uint64_t read straight out of shared memory. A buggy or compromised peer process (or the corruption from the bugs above) can set payload_off to anything, and the consumer will memcpy from an unmapped address — crash at best, cross-mapping info leak at worst. Also, try_read checks actual_size > buffer_size (the caller's buffer) but not actual_size <= claimed_size — try_read_view does check this (:533), try_read doesn't. Validate payload_off < buffer_size and actual_size <= claimed_size in both paths.

Medium — a stalled or dead producer wedges the consumer forever

- try_read (:350) and try_read_view (:523) spin indefinitely on actual_size != 0 once a slot is claimed. A producer that is preempted, dies, or throws between claim and commit blocks the consumer permanently — inside functions documented as "non-blocking." Since read_loop in the channel is the consumer, one crashed peer wedges the whole channel thread. Consider a bounded spin that returns 0 (treat "claimed but uncommitted" as "empty for now"), which is safe because read_slot_idx hasn't advanced.
- commit_write(reservation, 0) is accepted (:495 only rejects > max_size) but stores 0 — which is the "uncommitted" sentinel, so the consumer spins on that slot forever. Either reject 0, or store actual_size + 1 as the commit signal, and add an explicit abort_write that commits a skip marker for producer error paths.
- The corrupt-slot error paths (:356, :529) return without advancing read_slot_idx, so every subsequent read hits the same slot — permanent wedge with no recovery and no way for the caller to distinguish "empty" from "stuck" (both return 0).

Low / notes

- File size vs mapping size: the segment is created with total_size = page_size + hdr_ring_sz + buffer_size (:123), but the payload is mapped as ring_window = round_up_page(buffer_size) (:122) — when buffer_size isn't page-aligned, the mapping extends past EOF and wrapping messages write into the partial page beyond EOF, which POSIX leaves undefined for MAP_SHARED (it happens to work on Linux tmpfs). Size the segment with ring_window instead and the mirror math becomes exactly right.
- Dekker-pattern ordering: the lost-wakeup argument in wait_for_readable (:582) needs seq_cst on both critical accesses; the producer's write_slot_idx CAS is acq_rel and the consumer's is_empty() load is acquire. On x86/ARMv8 this works in practice, but per the C++ memory model it isn't guaranteed — a seq_cst fence before each side's read would make it airtight. Worst case is a bounded latency blip (one timeout period), not a deadlock.
- boost::interprocess_mutex is not robust: a process dying while holding it (in the notify path or read_with_timeout) deadlocks everyone. Consider pthread_mutex_t with PTHREAD_MUTEX_ROBUST + pthread_condattr_setpshared, or an eventfd/futex-based wakeup.
- The raw regions at [page_size, ...) overlap memory that Boost's segment manager believes it owns. Today there's exactly one construct<> call so nothing collides (and the page-0 fit check at :135 helps), but any future shm.construct could hand out memory inside your slot ring. A comment forbidding further managed allocations — or dropping managed_shared_memory for a plain header struct — would remove the trap.
- Minor: the doc comment on try_reserve_write says max_size is "full available space" but the code sets max_size = min_size; calculate_shm_size is dead code and disagrees with create(); nothing documents/enforces that all consumer functions must be called from exactly one thread across all processes (in the channel they are — read_loop only — which is good).

Bottom line

If, in practice, each ring only ever has one producer thread (one channel direction = one writer), bugs 1 and 2 are latent and the code is close to correct — but then the MPSC claim and the multi-producer CAS machinery are misleading and should be simplified to SPSC (plain load/store cursors, no CAS at all, faster too). If multiple threads can call SharedMemoryChannel::send/reserve_write concurrently on one channel — which the public API allows — the payload claim path needs the packed-cursor redesign before this is safe. I'd fix the validation gap (high) and the wedge scenarios (medium) either way. Happy to implement the packed-cursor fix or the SPSC simplification if you tell me which contract you actually want.