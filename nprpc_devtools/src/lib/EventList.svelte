<script lang="ts">
  import { afterUpdate } from 'svelte';
  import type { RpcEvent } from '../types';
  import { createEventDispatcher } from 'svelte';

  export let events  : RpcEvent[] = [];
  export let selected: RpcEvent | null = null;

  const dispatch = createEventDispatcher<{ select: RpcEvent }>();

  let tbody: HTMLElement;
  let userScrolled = false;
  let prevLen = 0;

  // Auto-scroll to newest row unless the user has scrolled up manually.
  afterUpdate(() => {
    if (!tbody) return;
    if (events.length === prevLen) return;
    prevLen = events.length;
    if (!userScrolled) {
      tbody.scrollTop = tbody.scrollHeight;
    }
  });

  function onScroll() {
    if (!tbody) return;
    const atBottom = tbody.scrollHeight - tbody.scrollTop - tbody.clientHeight < 8;
    userScrolled = !atBottom;
  }

  function select(ev: RpcEvent) {
    dispatch('select', ev);
  }

  // Keyboard navigation
  function onKeyDown(e: KeyboardEvent) {
    if (!events.length) return;
    const idx = selected ? events.findIndex(x => x.id === selected!.id) : -1;
    if (e.key === 'ArrowDown') {
      const next = events[Math.min(idx + 1, events.length - 1)];
      if (next) dispatch('select', next);
      e.preventDefault();
    } else if (e.key === 'ArrowUp') {
      const prev = events[Math.max(idx - 1, 0)];
      if (prev) dispatch('select', prev);
      e.preventDefault();
    }
  }

  // ── Formatting helpers ──────────────────────────────────────────

  function short_class(class_id: string): string {
    // "nprpc_nameserver/nprpc.common.Nameserver" → "nprpc.common.Nameserver"
    const slash = class_id.lastIndexOf('/');
    return slash >= 0 ? class_id.slice(slash + 1) : class_id;
  }

  function method_label(ev: RpcEvent): string {
    const cls = short_class(ev.class_id);
    const fn  = ev.method_name ?? `fn#${ev.func_idx}`;
    return `${cls}.${fn}`;
  }

  function fmt_duration(ev: RpcEvent): string {
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

  function row_class(ev: RpcEvent): string {
    const parts = ['row'];
    if (ev.direction === 'server') parts.push('server');
    if (ev.status === 'error')     parts.push('error');
    if (ev.status === 'pending')   parts.push('pending');
    if (selected?.id === ev.id)    parts.push('selected');
    return parts.join(' ');
  }
</script>

<div
  class="list-wrap"
  role="grid"
  tabindex="0"
  on:keydown={onKeyDown}
>
  <div class="thead">
    <span class="col-seq">#</span>
    <span class="col-dir">Dir</span>
    <span class="col-method">Method (Class)</span>
    <span class="col-dur">Duration</span>
    <span class="col-bytes">Bytes ↑↓</span>
    <span class="col-status">St</span>
  </div>

  <div
    class="tbody"
    bind:this={tbody}
    on:scroll={onScroll}
  >
    {#each events as ev (ev.id)}
      <!-- svelte-ignore a11y-click-events-have-key-events -->
      <div
        class={row_class(ev)}
        role="row"
        tabindex="-1"
        on:click={() => select(ev)}
      >
        <span class="col-seq mono">{ev.seq}</span>

        <span class="col-dir">
          {#if ev.direction === 'client'}
            <span class="badge badge-client" title="Client → Server">↑</span>
          {:else}
            <span class="badge badge-server" title="Server → Client">↓</span>
          {/if}
        </span>

        <span class="col-method" title={method_label(ev)}>{method_label(ev)}</span>

        <span class="col-dur mono dur-{ev.status}">{fmt_duration(ev)}</span>

        <span class="col-bytes mono">
          {#if ev.request_bytes !== undefined}
            {fmt_bytes(ev.request_bytes)}
            {#if ev.response_bytes !== undefined}
              / {fmt_bytes(ev.response_bytes)}
            {/if}
          {/if}
        </span>

        <span class="col-status">
          {#if ev.status === 'success'}
            <span class="dot dot-ok" title="Success">●</span>
          {:else if ev.status === 'error'}
            <span class="dot dot-err" title={ev.error ?? 'Error'}>●</span>
          {:else}
            <span class="dot dot-pending" title="Pending">●</span>
          {/if}
        </span>
      </div>
    {:else}
      <div class="empty">No RPC calls recorded yet.</div>
    {/each}
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
    height: 20px;
    font-size: 11px;
    color: #e8eaed;
    border-bottom: 1px solid #2d2f33;
    cursor: pointer;
    white-space: nowrap;
  }
  .row:hover    { background: rgba(255,255,255,0.04); }
  .row.selected { background: #1a3a5c !important; }
  .row.server   { background: rgba(255,140,0,0.05); }
  .row.error    { background: rgba(244,67,54,0.08); color: #ef9a9a; }
  .row.pending  { opacity: 0.6; }

  /* ─ column widths ─ */
  .col-seq    { width: 28px;  flex-shrink: 0; }
  .col-dir    { width: 22px;  flex-shrink: 0; }
  .col-method { flex: 1; overflow: hidden; text-overflow: ellipsis; min-width: 80px; }
  .col-dur    { width: 64px;  flex-shrink: 0; text-align: right; }
  .col-bytes  { width: 80px;  flex-shrink: 0; text-align: right; }
  .col-status { width: 18px;  flex-shrink: 0; text-align: center; }

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

  /* ─ status dots ─ */
  .dot { font-size: 9px; }
  .dot-ok      { color: #4caf50; }
  .dot-err     { color: #f44336; }
  .dot-pending { color: #ff9800; animation: pulse 1s ease-in-out infinite; }

  @keyframes pulse {
    0%, 100% { opacity: 1; }
    50%       { opacity: 0.3; }
  }

  /* ─ duration colour ─ */
  .dur-success { color: #b5cea8; }
  .dur-error   { color: #ef9a9a; }
  .dur-pending { color: #888; }

  .empty {
    padding: 16px;
    color: #555;
    font-size: 12px;
    text-align: center;
  }
</style>
