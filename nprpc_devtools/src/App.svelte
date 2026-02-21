<script lang="ts">
  import { onMount } from 'svelte';
  import type { RpcEvent, DebugMsg } from './types';
  import EventList   from './lib/EventList.svelte';
  import EventDetail from './lib/EventDetail.svelte';

  // ── State ────────────────────────────────────────────────────────
  let events   : RpcEvent[]   = [];
  let selected : RpcEvent | null = null;
  let filter   : string = '';
  let recording: boolean = true;
  let seq      : number = 0;

  // Pending events keyed by id (status=pending, awaiting call_end).
  const pending = new Map<number, RpcEvent>();

  // ── Message relay from background ────────────────────────────────
  onMount(() => {
    const port = chrome.runtime.connect({ name: 'panel' });

    port.postMessage({
      type : 'panel_init',
      tabId: chrome.devtools.inspectedWindow.tabId,
    });

    port.onMessage.addListener((msg: DebugMsg) => {
      if (!recording) return;

      if (msg.type === 'nprpc_call_start') {
        const ev: RpcEvent = { ...msg.data, seq: ++seq };
        pending.set(ev.id, ev);
        events = [...events, ev];
      } else if (msg.type === 'nprpc_call_end') {
        const ev = pending.get(msg.data.id);
        if (ev) {
          Object.assign(ev, msg.data);
          pending.delete(ev.id);
          events = [...events]; // trigger reactivity
          if (selected?.id === ev.id) selected = ev;
        }
      }
    });

    port.onDisconnect.addListener(() => {
      // background service-worker restarted; reconnect on next interaction
    });

    return () => port.disconnect();
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
  $: total    = events.length;
  $: errors   = events.filter(e => e.status === 'error').length;
  $: filtered = filter
    ? events.filter(ev => {
        const q = filter.toLowerCase();
        return (ev.method_name ?? '').toLowerCase().includes(q)
          || ev.class_id.toLowerCase().includes(q)
          || `${ev.endpoint.hostname}:${ev.endpoint.port}`.includes(q);
      })
    : events;

  // ── Resizable split ──────────────────────────────────────────────
  let container  : HTMLElement;
  let splitFrac   = 0.5;   // fraction of container height for list pane
  let dragging    = false;

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

<svelte:window on:mousemove={onMouseMove} on:mouseup={onMouseUp} />

<div class="shell">
  <!-- ── Toolbar ───────────────────────────────────────────────── -->
  <div class="toolbar">
    <button
      class="rec-btn"
      class:recording
      title={recording ? 'Pause recording' : 'Resume recording'}
      on:click={toggleRecording}
    >
      <span class="rec-dot">●</span>
      {recording ? 'Recording' : 'Paused'}
    </button>

    <button class="icon-btn" title="Clear all calls" on:click={clear}>
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
        <!-- svelte-ignore a11y-click-events-have-key-events -->
        <!-- svelte-ignore a11y-no-static-element-interactions -->
        <span class="clear-filter" on:click={() => filter = ''}>✕</span>
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
        {selected}
        on:select={e => selected = e.detail}
      />
    </div>

    <!-- Drag handle -->
    <!-- svelte-ignore a11y-no-static-element-interactions -->
    <div class="divider" on:mousedown={onDividerDown}>
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
