<svelte:options runes={true} />

<script lang="ts">
  import { onMount } from 'svelte';
  import type { RpcEvent, DebugMsg } from './types';
  import EventList   from './lib/EventList.svelte';
  import EventDetail from './lib/EventDetail.svelte';

  // ── State ────────────────────────────────────────────────────────
  let events = $state<RpcEvent[]>([]);
  let selected = $state<RpcEvent | null>(null);
  let filter = $state('');
  let recording = $state(true);
  let nextId = $state(0);
  let seq = $state(0); // 1-based display sequence number (reset on Clear)

  // Pending events keyed by id -> index in the reactive events array.
  const pending = new Map<number, number>();

  // ── Direct connection to content script (no background SW) ─────────
  onMount(() => {
    let port: chrome.runtime.Port | undefined;
    const tabId = chrome.devtools.inspectedWindow.tabId;

    const onMessage = (msg: DebugMsg) => {
      if (!recording) return;

      if (msg.type === 'nprpc_call_start') {
        const ev: RpcEvent = { ...msg.data, seq: ++seq };
        const index = events.length;
        events = [...events, ev];
        pending.set(ev.id, index);
        nextId = nextId + 1;
      } else if (msg.type === 'nprpc_call_end') {
        const index = pending.get(msg.data.id);
        if (index !== undefined) {
          const updated = { ...events[index], ...msg.data };
          events[index] = updated;
          pending.delete(msg.data.id);
          if (selected && selected.id === updated.id)
            selected = updated;
        }
      }
    };

    const connect = () => {
      port = chrome.tabs.connect(tabId, { name: 'nprpc_devtools' });
      port.onMessage.addListener(onMessage);
      port.onDisconnect.addListener(() => {
        // page navigated/reloaded — re-inject happens automatically,
        // reconnect after a short delay to let the content script settle.
        pending.clear();   // drop any calls that were in-flight before reload
        setTimeout(connect, 500);
      });
      port.postMessage({ type: 'nprpc_devtools_setup', nextId });
    };

    connect();
    return () => port?.disconnect();
  });

  // ── Actions ──────────────────────────────────────────────────────
  function clear() {
    events   = [];
    selected = null;
    seq      = 0;
    pending.clear();
  }

  function toggleRecording() { recording = !recording; }

  // ── Stats ────────────────────────────────────────────────────────
  const total = $derived(events.length);
  const errors = $derived(events.filter((e) => e.status === 'error').length);
  const filtered = $derived.by(() => {
    if (!filter) return events;

    const q = filter.toLowerCase();
    return events.filter((ev) => {
      return (ev.method_name ?? '').toLowerCase().includes(q)
        || ev.class_id.toLowerCase().includes(q)
        || `${ev.endpoint.hostname}:${ev.endpoint.port}`.includes(q);
    });
  });

  // ── Resizable split ──────────────────────────────────────────────
  let container = $state<HTMLElement | undefined>();
  let splitFrac = $state(0.5);   // fraction of container height for list pane
  let dragging = $state(false);

  function onDividerDown(e: MouseEvent) {
    dragging = true;
    e.preventDefault();
  }

  function onMouseMove(e: MouseEvent) {
    if (!dragging || !container) return;
    const rect = container.getBoundingClientRect();
    splitFrac = Math.min(0.85, Math.max(0.15,
      (e.clientY - rect.top) / rect.height
    ));
  }

  function onMouseUp() { dragging = false; }
</script>

<svelte:window onmousemove={onMouseMove} onmouseup={onMouseUp} />

<div class="shell">
  <!-- ── Toolbar ───────────────────────────────────────────────── -->
  <div class="toolbar">
    <button
      class="rec-btn"
      class:recording
      title={recording ? 'Pause recording' : 'Resume recording'}
      onclick={toggleRecording}
    >
      <span class="rec-dot">●</span>
      {recording ? 'Recording' : 'Paused'}
    </button>

    <button class="icon-btn" title="Clear all calls" onclick={clear}>
      🗑 Clear
    </button>

    <div class="filter-wrap">
      <input
        class="filter-input"
        type="text"
        placeholder="Filter by method, class, endpoint…"
        bind:value={filter}
      />
      {#if filter}
        <button class="clear-filter" type="button" onclick={() => filter = ''}>✕</button>
      {/if}
    </div>

    <div class="stats">
      <span class="stat">{total} call{total !== 1 ? 's' : ''}</span>
      {#if errors > 0}
        <span class="stat stat-err">{errors} error{errors !== 1 ? 's' : ''}</span>
      {/if}
    </div>
  </div>

  <!-- ── Main split pane ──────────────────────────────────────── -->
  <div
    class="content"
    bind:this={container}
    style:cursor={dragging ? 'ns-resize' : 'default'}
  >
    <!-- List pane (flex height driven by splitFrac) -->
    <div class="pane-top" style:height="{(splitFrac * 100).toFixed(1)}%">
      <EventList
        events={filtered}
        bind:selected
      />
    </div>

    <!-- Drag handle -->
    <!-- svelte-ignore a11y_no_static_element_interactions -->
    <div class="divider" onmousedown={onDividerDown}>
      <span class="divider-grip">⋯</span>
    </div>

    <!-- Detail pane (takes remaining height) -->
    <div class="pane-bottom">
      <EventDetail event={selected} />
    </div>
  </div>
</div>

<style>
  :global(*, *::before, *::after) { box-sizing: border-box; }
  :global(body) {
    margin: 0; padding: 0;
    background: #1e1e1e;
    color: #e8eaed;
    font-family: -apple-system, 'Segoe UI', system-ui, sans-serif;
    font-size: 12px;
    overflow: hidden;
  }

  .shell {
    display: flex;
    flex-direction: column;
    height: 100vh;
    width: 100vw;
    overflow: hidden;
  }

  /* ── Toolbar ── */
  .toolbar {
    display: flex;
    align-items: center;
    gap: 6px;
    padding: 4px 8px;
    background: #292b2f;
    border-bottom: 1px solid #3c4043;
    flex-shrink: 0;
    height: 32px;
  }

  .rec-btn {
    display: flex;
    align-items: center;
    gap: 4px;
    padding: 2px 8px;
    font-size: 11px;
    background: #2a2d30;
    color: #9aa0a6;
    border: 1px solid #3c4043;
    border-radius: 3px;
    cursor: pointer;
    white-space: nowrap;
  }
  .rec-btn:hover { background: #3a3d40; color: #e8eaed; }

  .rec-btn .rec-dot         { color: #555; }
  .rec-btn.recording .rec-dot {
    color: #f44336;
    animation: blink 1.2s ease infinite;
  }

  @keyframes blink {
    0%, 100% { opacity: 1; }
    50%       { opacity: 0.2; }
  }

  .icon-btn {
    padding: 2px 8px;
    font-size: 11px;
    background: #2a2d30;
    color: #9aa0a6;
    border: 1px solid #3c4043;
    border-radius: 3px;
    cursor: pointer;
    white-space: nowrap;
  }
  .icon-btn:hover { background: #3a3d40; color: #e8eaed; }

  .filter-wrap {
    position: relative;
    flex: 1;
    max-width: 380px;
  }
  .filter-input {
    width: 100%;
    padding: 2px 22px 2px 8px;
    font-size: 11px;
    background: #1e1e1e;
    color: #e8eaed;
    border: 1px solid #3c4043;
    border-radius: 3px;
    outline: none;
  }
  .filter-input:focus { border-color: #4a9eff; }

  .clear-filter {
    position: absolute;
    right: 5px;
    top: 50%;
    transform: translateY(-50%);
    color: #666;
    cursor: pointer;
    font-size: 10px;
    line-height: 1;
  }
  .clear-filter:hover { color: #e8eaed; }

  .stats {
    display: flex;
    align-items: center;
    gap: 10px;
    margin-left: auto;
    flex-shrink: 0;
  }
  .stat     { font-size: 11px; color: #9aa0a6; }
  .stat-err { color: #ef9a9a; font-weight: 600; }

  /* ── Split pane ── */
  .content {
    flex: 1;
    display: flex;
    flex-direction: column;
    overflow: hidden;
  }

  .pane-top {
    overflow: hidden;
    flex-shrink: 0;
  }

  .pane-bottom {
    flex: 1;
    overflow: hidden;
  }

  /* ── Drag divider ── */
  .divider {
    height: 6px;
    background: #292b2f;
    border-top: 1px solid #3c4043;
    border-bottom: 1px solid #3c4043;
    cursor: ns-resize;
    display: flex;
    align-items: center;
    justify-content: center;
    flex-shrink: 0;
    user-select: none;
  }
  .divider:hover     { background: #33363a; }
  .divider-grip      { color: #555; font-size: 10px; letter-spacing: 2px; }
</style>
