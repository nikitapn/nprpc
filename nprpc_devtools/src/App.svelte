<svelte:options runes={true} />

<script lang="ts">
  import { onMount } from 'svelte';
  import type { DebugEntry, DebugMsg, RpcEvent, StreamEvent, StreamMessageEvent } from './types';
  import EventList   from './lib/EventList.svelte';
  import EventDetail from './lib/EventDetail.svelte';

  // ── State ────────────────────────────────────────────────────────
  let entries = $state<DebugEntry[]>([]);
  let selected = $state<DebugEntry | null>(null);
  let filter = $state('');
  let recording = $state(true);
  let nextId = $state(0);
  let seq = $state(0); // 1-based display sequence number (reset on Clear)

  // Pending RPC calls keyed by id -> index in the reactive entries array.
  const pending = new Map<number, number>();
  const openStreams = new Map<number, number>();

  // ── Direct connection to content script (no background SW) ─────────
  onMount(() => {
    let port: chrome.runtime.Port | undefined;
    const tabId = chrome.devtools.inspectedWindow.tabId;

    const onMessage = (msg: DebugMsg) => {
      if (!recording) return;

      if (msg.type === 'nprpc_call_start') {
        const ev: RpcEvent = { ...msg.data, entry_kind: 'rpc', seq: ++seq };
        const index = entries.length;
        entries = [...entries, ev];
        pending.set(ev.id, index);
        nextId = Math.max(nextId, ev.id + 1);
      } else if (msg.type === 'nprpc_call_end') {
        const index = pending.get(msg.data.id);
        if (index !== undefined) {
          const current = entries[index];
          if (!current || current.entry_kind !== 'rpc') return;
          const updated: RpcEvent = { ...current, ...msg.data };
          entries[index] = updated;
          pending.delete(msg.data.id);
          if (selected && selected.id === updated.id)
            selected = updated;
        }
      } else if (msg.type === 'nprpc_stream_start') {
        const ev: StreamEvent = {
          ...msg.data,
          entry_kind: 'stream',
          seq: ++seq,
          sent_bytes: 0,
          received_bytes: 0,
          message_count: 0,
          messages: [],
        };
        const index = entries.length;
        entries = [...entries, ev];
        openStreams.set(ev.id, index);
        nextId = Math.max(nextId, ev.id + 1);
      } else if (msg.type === 'nprpc_stream_message') {
        const index = openStreams.get(msg.data.id);
        if (index === undefined) return;

        const current = entries[index];
        if (!current || current.entry_kind !== 'stream') return;

        const message: StreamMessageEvent = {
          index: current.messages.length + 1,
          ...msg.data.message,
        };

        let sent_bytes = current.sent_bytes;
        let received_bytes = current.received_bytes;
        if (message.bytes !== undefined) {
          if (message.direction === 'outgoing') sent_bytes += message.bytes;
          else received_bytes += message.bytes;
        }

        let status = current.status;
        let duration_ms = current.duration_ms;
        let error = current.error;
        if (message.kind === 'complete') {
          status = 'success';
          duration_ms = message.timestamp - current.timestamp;
        } else if (message.kind === 'error') {
          status = 'error';
          duration_ms = message.timestamp - current.timestamp;
          error = message.error_code !== undefined
            ? `Stream error (code=${message.error_code})`
            : 'Stream error';
        } else if (message.kind === 'cancel') {
          status = 'cancelled';
          duration_ms = message.timestamp - current.timestamp;
        }

        const updated: StreamEvent = {
          ...current,
          status,
          duration_ms,
          error,
          sent_bytes,
          received_bytes,
          message_count: current.message_count + 1,
          messages: [...current.messages, message],
        };
        entries[index] = updated;
        if (status !== 'pending') {
          openStreams.delete(msg.data.id);
        }
        if (selected && selected.id === updated.id)
          selected = updated;
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
    entries  = [];
    selected = null;
    seq      = 0;
    nextId   = 0;
    pending.clear();
    openStreams.clear();
  }

  function toggleRecording() { recording = !recording; }

  // ── Stats ────────────────────────────────────────────────────────
  const total = $derived(entries.length);
  const errors = $derived(entries.filter((e) => e.status === 'error').length);
  const filtered = $derived.by(() => {
    if (!filter) return entries;

    const q = filter.toLowerCase();
    return entries.filter((ev) => {
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
      <span class="stat">{total} entr{total !== 1 ? 'ies' : 'y'}</span>
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

  :global(*) {
    scrollbar-width: thin;
    scrollbar-color: #5a6169 #202225;
  }

  :global(*::-webkit-scrollbar) {
    width: 10px;
    height: 10px;
  }

  :global(*::-webkit-scrollbar-track) {
    background: #202225;
  }

  :global(*::-webkit-scrollbar-thumb) {
    background: linear-gradient(180deg, #5d6670 0%, #4a525b 100%);
    border: 2px solid #202225;
    border-radius: 999px;
  }

  :global(*::-webkit-scrollbar-thumb:hover) {
    background: linear-gradient(180deg, #73808c 0%, #5c6670 100%);
  }

  :global(*::-webkit-scrollbar-corner) {
    background: #202225;
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
