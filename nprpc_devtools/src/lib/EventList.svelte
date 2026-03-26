<svelte:options runes={true} />

<script lang="ts">
  import type { DebugEntry } from '../types';

  type SortKey = 'seq' | 'direction' | 'method' | 'duration' | 'bytes' | 'status';

  let {
    events = [],
    selected = $bindable<DebugEntry | null>(null)
  }: {
    events?: DebugEntry[];
    selected?: DebugEntry | null;
  } = $props();

  let tbody: HTMLElement | undefined = $state();
  let userScrolled = $state(false);
  let prevLen = $state(0);
  let sortKey = $state<SortKey>('seq');
  let sortDir = $state<'asc' | 'desc'>('asc');
  let scrollTop = $state(0);
  let viewportHeight = $state(0);

  const ROW_HEIGHT = 21;
  const OVERSCAN_ROWS = 12;

  function syncViewport() {
    if (!tbody) return;
    scrollTop = tbody.scrollTop;
    viewportHeight = tbody.clientHeight;
  }

  $effect(() => {
    if (!tbody) return;

    syncViewport();

    const resizeObserver = new ResizeObserver(() => {
      syncViewport();
    });

    resizeObserver.observe(tbody);

    return () => {
      resizeObserver.disconnect();
    };
  });

  // Auto-scroll to newest row unless the user has scrolled up manually.
  $effect(() => {
    if (!tbody) return;
    if (displayedEvents.length === prevLen) return;
    prevLen = displayedEvents.length;
    if (!userScrolled) {
      tbody.scrollTop = tbody.scrollHeight;
      syncViewport();
    }
  });

  function onScroll() {
    if (!tbody) return;
    syncViewport();
    const atBottom = tbody.scrollHeight - tbody.scrollTop - tbody.clientHeight < 8;
    userScrolled = !atBottom;
  }

  function select(ev: DebugEntry) {
    selected = ev;
  }

  // Keyboard navigation
  function onKeyDown(e: KeyboardEvent) {
    if (!displayedEvents.length) return;
    const selectedId = selected?.id;
    const idx = selectedId === undefined ? -1 : displayedEvents.findIndex(x => x.id === selectedId);
    if (e.key === 'ArrowDown') {
      const next = displayedEvents[Math.min(idx + 1, displayedEvents.length - 1)];
      if (next) selected = next;
      e.preventDefault();
    } else if (e.key === 'ArrowUp') {
      const prev = displayedEvents[Math.max(idx - 1, 0)];
      if (prev) selected = prev;
      e.preventDefault();
    }
  }

  function onRowKeyDown(e: KeyboardEvent, ev: DebugEntry) {
    if (e.key === 'Enter' || e.key === ' ') {
      selected = ev;
      e.preventDefault();
    }
  }

  // ── Formatting helpers ──────────────────────────────────────────

  function short_class(class_id: string): string {
    // "nprpc_nameserver/nprpc.common.Nameserver" → "nprpc.common.Nameserver"
    const slash = class_id.lastIndexOf('/');
    return slash >= 0 ? class_id.slice(slash + 1) : class_id;
  }

  function method_label(ev: DebugEntry): string {
    const cls = short_class(ev.class_id);
    const fn  = ev.method_name ?? `fn#${ev.func_idx}`;
    return ev.entry_kind === 'stream'
      ? `${cls}.${fn} [${ev.stream_kind}]`
      : `${cls}.${fn}`;
  }

  function fmt_duration(ev: DebugEntry): string {
    if (ev.status === 'pending') return '…';
    if (ev.duration_ms === undefined) return '—';
    if (ev.duration_ms < 1000) return `${ev.duration_ms} ms`;
    return `${(ev.duration_ms / 1000).toFixed(2)} s`;
  }

  function fmt_bytes(b?: number): string {
    if (b === undefined) return '';
    if (b < 1024) return `${b} B`;
    return `${(b / 1024).toFixed(1)} K`;
  }

  function fmt_entry_bytes(ev: DebugEntry): string {
    if (ev.entry_kind === 'stream') {
      const out = fmt_bytes(ev.sent_bytes);
      const incoming = fmt_bytes(ev.received_bytes);
      if (!out && !incoming) return '';
      return `${out || '0 B'} / ${incoming || '0 B'}`;
    }

    if (ev.request_bytes === undefined) return '';
    const request = fmt_bytes(ev.request_bytes);
    if (ev.response_bytes !== undefined) {
      return `${request} / ${fmt_bytes(ev.response_bytes)}`;
    }
    return request;
  }

  function bytes_value(ev: DebugEntry): number {
    if (ev.entry_kind === 'stream') {
      return ev.sent_bytes + ev.received_bytes;
    }
    return (ev.request_bytes ?? 0) + (ev.response_bytes ?? 0);
  }

  function status_rank(ev: DebugEntry): number {
    switch (ev.status) {
      case 'pending':
        return 0;
      case 'success':
        return 1;
      case 'cancelled':
        return 2;
      case 'error':
        return 3;
    }
  }

  function direction_label(ev: DebugEntry): string {
    if (ev.entry_kind === 'stream') return 'stream';
    return ev.direction;
  }

  function compareEntries(left: DebugEntry, right: DebugEntry): number {
    switch (sortKey) {
      case 'seq':
        return left.seq - right.seq;
      case 'direction':
        return direction_label(left).localeCompare(direction_label(right));
      case 'method':
        return method_label(left).localeCompare(method_label(right));
      case 'duration':
        return (left.duration_ms ?? -1) - (right.duration_ms ?? -1);
      case 'bytes':
        return bytes_value(left) - bytes_value(right);
      case 'status':
        return status_rank(left) - status_rank(right);
    }
  }

  function setSort(key: SortKey) {
    if (sortKey === key) {
      sortDir = sortDir === 'asc' ? 'desc' : 'asc';
      return;
    }

    sortKey = key;
    sortDir = key === 'seq' ? 'asc' : 'desc';
  }

  function sortIndicator(key: SortKey): string {
    if (sortKey !== key) return '';
    return sortDir === 'asc' ? ' ▲' : ' ▼';
  }

  const displayedEvents = $derived.by(() => {
    const sorted = [...events].sort((left, right) => {
      const result = compareEntries(left, right);
      if (result !== 0) return sortDir === 'asc' ? result : -result;
      return left.seq - right.seq;
    });
    return sorted;
  });

  const windowRange = $derived.by(() => {
    const total = displayedEvents.length;
    if (!total) {
      return {
        start: 0,
        end: 0,
        topPadding: 0,
        bottomPadding: 0
      };
    }

    const visibleRows = Math.max(1, Math.ceil(viewportHeight / ROW_HEIGHT));
    const start = Math.max(0, Math.floor(scrollTop / ROW_HEIGHT) - OVERSCAN_ROWS);
    const end = Math.min(total, start + visibleRows + OVERSCAN_ROWS * 2);

    return {
      start,
      end,
      topPadding: start * ROW_HEIGHT,
      bottomPadding: Math.max(0, (total - end) * ROW_HEIGHT)
    };
  });

  const visibleEvents = $derived.by(() =>
    displayedEvents.slice(windowRange.start, windowRange.end)
  );

  function row_class(ev: DebugEntry): string {
    const parts = ['row'];
    if (ev.direction === 'server') parts.push('server');
    if (ev.status === 'error')     parts.push('error');
    if (ev.status === 'pending')   parts.push('pending');
    if (ev.status === 'cancelled') parts.push('cancelled');
    if (ev.entry_kind === 'stream') parts.push('stream');
    if (selected && selected.id === ev.id)
      parts.push('selected');
    return parts.join(' ');
  }
</script>

<div
  class="list-wrap"
  role="grid"
  aria-rowcount={displayedEvents.length}
  tabindex="0"
  onkeydown={onKeyDown}
>
  <div class="thead">
    <button type="button" class="head-btn col-seq" onclick={() => setSort('seq')}>#{sortIndicator('seq')}</button>
    <button type="button" class="head-btn col-dir" onclick={() => setSort('direction')}>Dir{sortIndicator('direction')}</button>
    <button type="button" class="head-btn col-method" onclick={() => setSort('method')}>Entry{sortIndicator('method')}</button>
    <button type="button" class="head-btn col-dur" onclick={() => setSort('duration')}>Duration{sortIndicator('duration')}</button>
    <button type="button" class="head-btn col-bytes" onclick={() => setSort('bytes')}>Bytes{sortIndicator('bytes')}</button>
    <button type="button" class="head-btn col-status" onclick={() => setSort('status')}>St{sortIndicator('status')}</button>
  </div>

  <div
    class="tbody"
    bind:this={tbody}
    onscroll={onScroll}
  >
    {#if displayedEvents.length}
      <div
        class="spacer"
        aria-hidden="true"
        style={`height: ${windowRange.topPadding}px;`}
      ></div>

      {#each visibleEvents as ev (ev.id)}
        <div
          class={row_class(ev)}
          role="row"
          tabindex="-1"
          onclick={() => select(ev)}
          onkeydown={(e) => onRowKeyDown(e, ev)}
        >
          <span class="col-seq mono">{ev.seq}</span>

          <span class="col-dir">
            {#if ev.entry_kind === 'stream'}
              <span class="badge badge-stream" title="Stream session">⇄</span>
            {:else if ev.direction === 'client'}
              <span class="badge badge-client" title="Client → Server">↑</span>
            {:else}
              <span class="badge badge-server" title="Server → Client">↓</span>
            {/if}
          </span>

          <span class="col-method" title={method_label(ev)}>{method_label(ev)}</span>

          <span class="col-dur mono dur-{ev.status}">{fmt_duration(ev)}</span>

          <span class="col-bytes mono" title={fmt_entry_bytes(ev)}>{fmt_entry_bytes(ev)}</span>

          <span class="col-status">
            {#if ev.status === 'success'}
              <span class="dot dot-ok" title="Success">●</span>
            {:else if ev.status === 'error'}
              <span class="dot dot-err" title={ev.error ?? 'Error'}>●</span>
            {:else if ev.status === 'cancelled'}
              <span class="dot dot-cancel" title="Cancelled">●</span>
            {:else}
              <span class="dot dot-pending" title="Pending">●</span>
            {/if}
          </span>
        </div>
      {/each}

      <div
        class="spacer"
        aria-hidden="true"
        style={`height: ${windowRange.bottomPadding}px;`}
      ></div>
    {:else}
      <div class="empty">No RPC calls recorded yet.</div>
    {/if}
  </div>
</div>

<style>
  .list-wrap {
    display: flex;
    flex-direction: column;
    height: 100%;
    overflow: hidden;
    outline: none;
  }

  /* ─ header ─ */
  .thead {
    display: flex;
    align-items: center;
    padding: 0 4px;
    height: 22px;
    background: #292b2f;
    border-bottom: 1px solid #3c4043;
    font-size: 11px;
    color: #9aa0a6;
    flex-shrink: 0;
    user-select: none;
  }

  .head-btn {
    background: transparent;
    border: 0;
    color: inherit;
    font: inherit;
    height: 100%;
    padding: 0;
    cursor: pointer;
    text-align: inherit;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .head-btn:hover { color: #e8eaed; }

  /* ─ body ─ */
  .tbody {
    flex: 1;
    overflow-y: auto;
    overflow-x: hidden;
  }

  /* ─ rows ─ */
  .row {
    display: flex;
    align-items: center;
    padding: 0 4px;
    box-sizing: border-box;
    height: 21px;
    font-size: 11px;
    color: #e8eaed;
    border-bottom: 1px solid #2d2f33;
    cursor: pointer;
    white-space: nowrap;
  }
  .row:hover    { background: rgba(255,255,255,0.04); }
  .row.selected { background: #1a3a5c !important; }
  .row.server   { background: rgba(255,140,0,0.05); }
  .row.stream   { background: rgba(74, 158, 255, 0.05); }
  .row.error    { background: rgba(244,67,54,0.08); color: #ef9a9a; }
  .row.cancelled { background: rgba(158, 158, 158, 0.08); color: #c7c7c7; }
  .row.pending:not(.stream) { opacity: 0.6; }

  /* ─ column widths ─ */
  .col-seq    { width: 32px;  flex-shrink: 0; overflow: hidden; text-overflow: ellipsis; }
  .col-dir    { width: 28px;  flex-shrink: 0; overflow: hidden; text-overflow: ellipsis; }
  .col-method { flex: 1; overflow: hidden; text-overflow: ellipsis; min-width: 80px; }
  .col-dur    { width: 72px;  flex-shrink: 0; text-align: right; overflow: hidden; text-overflow: ellipsis; }
  .col-bytes  {
    width: 96px;
    flex-shrink: 0;
    text-align: right;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .col-status { width: 24px;  flex-shrink: 0; text-align: center; overflow: hidden; }

  .mono { font-family: Menlo, Monaco, 'Courier New', monospace; }

  /* ─ direction badges ─ */
  .badge {
    display: inline-block;
    width: 14px;
    height: 14px;
    border-radius: 3px;
    text-align: center;
    line-height: 14px;
    font-size: 10px;
    font-weight: 700;
  }
  .badge-client { background: #1a4775; color: #7ab8f5; }
  .badge-server { background: #4a2c00; color: #ffb347; }
  .badge-stream { background: #233347; color: #8fd0ff; }

  /* ─ status dots ─ */
  .dot { font-size: 9px; }
  .dot-ok      { color: #4caf50; }
  .dot-err     { color: #f44336; }
  .dot-cancel  { color: #b0bec5; }
  .dot-pending { color: #ff9800; animation: pulse 1s ease-in-out infinite; }

  @keyframes pulse {
    0%, 100% { opacity: 1; }
    50%       { opacity: 0.3; }
  }

  /* ─ duration colour ─ */
  .dur-success { color: #b5cea8; }
  .dur-error   { color: #ef9a9a; }
  .dur-cancelled { color: #c7c7c7; }
  .dur-pending { color: #888; }

  .empty {
    padding: 16px;
    color: #555;
    font-size: 12px;
    text-align: center;
  }

  .spacer {
    flex: 0 0 auto;
  }
</style>
