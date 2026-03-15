<svelte:options runes={true} />

<script lang="ts">
  import type { DebugEntry, StreamEvent, StreamMessageEvent } from '../types';
  import JsonTree from './JsonTree.svelte';

  let {
    event = null
  }: {
    event?: DebugEntry | null;
  } = $props();

  type Tab = 'general' | 'request' | 'response' | 'init' | 'messages';
  let active_tab = $state<Tab>('general');
  let selected_message = $state<StreamMessageEvent | null>(null);

  let previousEventId = $state<number | null>(null);
  let previousStreamMessageKey = $state('');

  $effect(() => {
    const eventId = event?.id ?? null;
    if (eventId !== previousEventId) {
      active_tab = 'general';
      previousEventId = eventId;
    }
  });

  $effect(() => {
    if (!event || event.entry_kind !== 'stream') {
      selected_message = null;
      previousStreamMessageKey = '';
      return;
    }

    const messageKey = `${event.id}:${event.message_count}`;
    if (messageKey !== previousStreamMessageKey) {
      selected_message = event.messages.at(-1) ?? null;
      previousStreamMessageKey = messageKey;
    }
  });

  // ── Formatting ──────────────────────────────────────────────────

  function fmt_time(ts: number): string {
    const d = new Date(ts);
    const h  = String(d.getHours()).padStart(2, '0');
    const m  = String(d.getMinutes()).padStart(2, '0');
    const s  = String(d.getSeconds()).padStart(2, '0');
    const ms = String(d.getMilliseconds()).padStart(3, '0');
    return `${h}:${m}:${s}.${ms}`;
  }

  function fmt_duration(ev: DebugEntry): string {
    if (ev.status === 'pending') return 'pending …';
    if (ev.status === 'cancelled') return 'cancelled';
    if (ev.duration_ms === undefined) return '—';
    if (ev.duration_ms < 1000) return `${ev.duration_ms} ms`;
    return `${(ev.duration_ms / 1000).toFixed(3)} s`;
  }

  function fmt_bytes(b?: number): string {
    if (b === undefined) return '—';
    if (b < 1024) return `${b} B`;
    return `${(b / 1024).toFixed(2)} KiB`;
  }

  function fmt_object_id(s: string): string {
    const n = BigInt(s);
    return '0x' + n.toString(16).padStart(16, '0').toUpperCase();
  }

  function endpoint_url(ev: DebugEntry): string {
    return `${ev.endpoint.transport}://${ev.endpoint.hostname}:${ev.endpoint.port}`;
  }

  function status_label(ev: DebugEntry): string {
    if (ev.status === 'success') return 'Success ✓';
    if (ev.status === 'error')   return 'Error ✕';
    if (ev.status === 'cancelled') return 'Cancelled';
    return 'Pending …';
  }

  function stream_message_label(message: StreamMessageEvent): string {
    switch (message.kind) {
      case 'chunk':
        return 'Chunk';
      case 'complete':
        return 'Complete';
      case 'error':
        return 'Error';
      case 'cancel':
        return 'Cancel';
      case 'window_update':
        return 'Window';
    }
  }

  function stream_message_meta(message: StreamMessageEvent): string {
    const parts: string[] = [];
    if (message.sequence !== undefined) parts.push(`#${message.sequence}`);
    if (message.bytes !== undefined) parts.push(fmt_bytes(message.bytes));
    if (message.credits !== undefined) parts.push(`${message.credits} cr`);
    if (message.error_code !== undefined) parts.push(`code ${message.error_code}`);
    return parts.join(' • ');
  }

  function copy_json(value: unknown) {
    navigator.clipboard?.writeText(JSON.stringify(value, null, 2));
  }

  // Has error tab?
  const has_error = $derived(event?.status === 'error' && !!event?.error);
  const is_stream = $derived(event?.entry_kind === 'stream');
</script>

{#if !event}
  <div class="no-selection">
    <span>Select a call to inspect it.</span>
  </div>
{:else}
  <div class="detail">
    <!-- ── Tabs ─────────────────────────────────────────────── -->
    <div class="tabs" role="tablist">
      <button
        type="button"
        class="tab"
        class:active={active_tab === 'general'}
        role="tab"
        tabindex="0"
        onclick={() => active_tab = 'general'}
      >General</button>
      {#if is_stream}
        <button
          type="button"
          class="tab"
          class:active={active_tab === 'init'}
          role="tab"
          tabindex="0"
          onclick={() => active_tab = 'init'}
        >Init</button>
        <button
          type="button"
          class="tab"
          class:active={active_tab === 'messages'}
          role="tab"
          tabindex="0"
          onclick={() => active_tab = 'messages'}
        >Messages</button>
      {:else}
        <button
          type="button"
          class="tab"
          class:active={active_tab === 'request'}
          role="tab"
          tabindex="0"
          onclick={() => active_tab = 'request'}
        >Request</button>
        <button
          type="button"
          class="tab"
          class:active={active_tab === 'response'}
          class:tab-error={has_error}
          role="tab"
          tabindex="0"
          onclick={() => active_tab = 'response'}
        >{has_error ? 'Error' : 'Response'}</button>
      {/if}
    </div>

    <!-- ── Tab bodies ─────────────────────────────────────── -->
    <div class="tab-body">

      {#if active_tab === 'general' && event.entry_kind === 'rpc'}
        <table class="info-table">
          <tbody>
            <!-- Call summary -->
            <tr class="section-header"><td colspan="2">Call</td></tr>
            <tr>
              <td class="label">Direction</td>
              <td>
                {#if event.direction === 'client'}
                  <span class="badge-client">↑ Client → Server</span>
                {:else}
                  <span class="badge-server">↓ Server → Client</span>
                {/if}
              </td>
            </tr>
            <tr>
              <td class="label">Status</td>
              <td class="status-{event.status}">{status_label(event)}</td>
            </tr>
            <tr>
              <td class="label">Timestamp</td>
              <td class="mono">{fmt_time(event.timestamp)}</td>
            </tr>
            <tr>
              <td class="label">Duration</td>
              <td class="mono">{fmt_duration(event)}</td>
            </tr>
            <tr>
              <td class="label">Req size</td>
              <td class="mono">{fmt_bytes(event.request_bytes)}</td>
            </tr>
            <tr>
              <td class="label">Resp size</td>
              <td class="mono">{fmt_bytes(event.response_bytes)}</td>
            </tr>

            <!-- Object identity -->
            <tr class="section-header"><td colspan="2">Object</td></tr>
            <tr>
              <td class="label">Class</td>
              <td class="mono wrap">{event.class_id}</td>
            </tr>
            <tr>
              <td class="label">poa_idx</td>
              <td class="mono">{event.poa_idx}</td>
            </tr>
            <tr>
              <td class="label">object_id</td>
              <td class="mono">{fmt_object_id(event.object_id)}</td>
            </tr>
            <tr>
              <td class="label">interface_idx</td>
              <td class="mono">{event.interface_idx}</td>
            </tr>
            <tr>
              <td class="label">func_idx</td>
              <td class="mono">{event.func_idx}</td>
            </tr>
            {#if event.method_name}
              <tr>
                <td class="label">Method</td>
                <td class="mono">{event.method_name}</td>
              </tr>
            {/if}

            <!-- Transport -->
            <tr class="section-header"><td colspan="2">Transport</td></tr>
            <tr>
              <td class="label">Endpoint</td>
              <td class="mono">{endpoint_url(event)}</td>
            </tr>
            <tr>
              <td class="label">Host</td>
              <td class="mono">{event.endpoint.hostname}</td>
            </tr>
            <tr>
              <td class="label">Port</td>
              <td class="mono">{event.endpoint.port}</td>
            </tr>
            <tr>
              <td class="label">Protocol</td>
              <td>
                <span class="transport-badge transport-{event.endpoint.transport}">
                  {event.endpoint.transport.toUpperCase()}
                </span>
              </td>
            </tr>
          </tbody>
        </table>

      {:else if active_tab === 'general' && event.entry_kind === 'stream'}
        <table class="info-table">
          <tbody>
            <tr class="section-header"><td colspan="2">Stream</td></tr>
            <tr>
              <td class="label">Direction</td>
              <td>
                {#if event.direction === 'client'}
                  <span class="badge-client">↑ Client initiated</span>
                {:else}
                  <span class="badge-server">↓ Server initiated</span>
                {/if}
              </td>
            </tr>
            <tr>
              <td class="label">Status</td>
              <td class="status-{event.status}">{status_label(event)}</td>
            </tr>
            <tr>
              <td class="label">Timestamp</td>
              <td class="mono">{fmt_time(event.timestamp)}</td>
            </tr>
            <tr>
              <td class="label">Duration</td>
              <td class="mono">{fmt_duration(event)}</td>
            </tr>
            <tr>
              <td class="label">stream_id</td>
              <td class="mono wrap">{event.stream_id}</td>
            </tr>
            <tr>
              <td class="label">Kind</td>
              <td class="mono">{event.stream_kind}</td>
            </tr>
            <tr>
              <td class="label">Messages</td>
              <td class="mono">{event.message_count}</td>
            </tr>
            <tr>
              <td class="label">Sent bytes</td>
              <td class="mono">{fmt_bytes(event.sent_bytes)}</td>
            </tr>
            <tr>
              <td class="label">Received bytes</td>
              <td class="mono">{fmt_bytes(event.received_bytes)}</td>
            </tr>

            <tr class="section-header"><td colspan="2">Object</td></tr>
            <tr>
              <td class="label">Class</td>
              <td class="mono wrap">{event.class_id}</td>
            </tr>
            <tr>
              <td class="label">Method</td>
              <td class="mono">{event.method_name ?? `fn#${event.func_idx}`}</td>
            </tr>
            <tr>
              <td class="label">object_id</td>
              <td class="mono">{fmt_object_id(event.object_id)}</td>
            </tr>

            <tr class="section-header"><td colspan="2">Transport</td></tr>
            <tr>
              <td class="label">Endpoint</td>
              <td class="mono">{endpoint_url(event)}</td>
            </tr>
            <tr>
              <td class="label">Init size</td>
              <td class="mono">{fmt_bytes(event.request_bytes)}</td>
            </tr>
          </tbody>
        </table>

      {:else if active_tab === 'request' && event.entry_kind === 'rpc'}
        <div class="payload-header">
          <span class="payload-label">Request arguments</span>
          {#if event.request_args !== undefined}
            <button class="copy-btn" type="button" onclick={() => copy_json(event.request_args)}>
              Copy JSON
            </button>
          {/if}
        </div>
        <div class="payload-body">
          {#if event.request_args !== undefined}
            <JsonTree value={event.request_args} expand_depth={3} />
          {:else}
            <span class="no-data">(no arguments captured)</span>
          {/if}
        </div>

      {:else if active_tab === 'response' && event.entry_kind === 'rpc'}
        {#if has_error}
          <div class="payload-header">
            <span class="payload-label error-label">Exception / Error</span>
          </div>
          <div class="payload-body error-body">
            <span class="error-text">{event.error}</span>
          </div>
        {:else}
          <div class="payload-header">
            <span class="payload-label">Response / return value</span>
            {#if event.response_args !== undefined}
              <button class="copy-btn" type="button" onclick={() => copy_json(event.response_args)}>
                Copy JSON
              </button>
            {/if}
          </div>
          <div class="payload-body">
            {#if event.status === 'pending'}
              <span class="no-data">Waiting for response…</span>
            {:else if event.response_args !== undefined}
              <JsonTree value={event.response_args} expand_depth={3} />
            {:else}
              <span class="no-data">(void)</span>
            {/if}
          </div>
        {/if}

      {:else if active_tab === 'init' && event.entry_kind === 'stream'}
        <div class="payload-header">
          <span class="payload-label">Stream init arguments</span>
          {#if event.request_args !== undefined}
            <button class="copy-btn" type="button" onclick={() => copy_json(event.request_args)}>
              Copy JSON
            </button>
          {/if}
        </div>
        <div class="payload-body">
          {#if event.request_args !== undefined}
            <JsonTree value={event.request_args} expand_depth={3} />
          {:else}
            <span class="no-data">(no init arguments captured)</span>
          {/if}
        </div>

      {:else if active_tab === 'messages' && event.entry_kind === 'stream'}
        <div class="stream-shell">
          <div class="stream-list">
            <div class="stream-list-header">
              <span class="stream-col-index">#</span>
              <span class="stream-col-dir">Dir</span>
              <span class="stream-col-kind">Kind</span>
              <span class="stream-col-meta">Meta</span>
            </div>
            <div class="stream-list-body">
              {#each event.messages as message}
                <button
                  type="button"
                  class="stream-row"
                  class:stream-row-active={selected_message?.index === message.index}
                  onclick={() => selected_message = message}
                >
                  <span class="stream-col-index mono">{message.index}</span>
                  <span class="stream-col-dir mono">{message.direction === 'outgoing' ? '↑' : '↓'}</span>
                  <span class="stream-col-kind">{stream_message_label(message)}</span>
                  <span class="stream-col-meta mono">{stream_message_meta(message)}</span>
                </button>
              {:else}
                <div class="no-selection">No stream messages recorded yet.</div>
              {/each}
            </div>
          </div>

          <div class="stream-message-detail">
            {#if selected_message}
              <div class="payload-header">
                <span class="payload-label">{stream_message_label(selected_message)} message</span>
                {#if selected_message.payload !== undefined}
                  <button class="copy-btn" type="button" onclick={() => copy_json(selected_message?.payload)}>
                    Copy JSON
                  </button>
                {/if}
              </div>
              <div class="payload-body">
                <table class="info-table stream-info-table">
                  <tbody>
                    <tr>
                      <td class="label">Timestamp</td>
                      <td class="mono">{fmt_time(selected_message.timestamp)}</td>
                    </tr>
                    <tr>
                      <td class="label">Direction</td>
                      <td class="mono">{selected_message.direction}</td>
                    </tr>
                    <tr>
                      <td class="label">Kind</td>
                      <td class="mono">{selected_message.kind}</td>
                    </tr>
                    {#if selected_message.sequence !== undefined}
                      <tr>
                        <td class="label">Sequence</td>
                        <td class="mono">{selected_message.sequence}</td>
                      </tr>
                    {/if}
                    {#if selected_message.bytes !== undefined}
                      <tr>
                        <td class="label">Bytes</td>
                        <td class="mono">{fmt_bytes(selected_message.bytes)}</td>
                      </tr>
                    {/if}
                    {#if selected_message.credits !== undefined}
                      <tr>
                        <td class="label">Credits</td>
                        <td class="mono">{selected_message.credits}</td>
                      </tr>
                    {/if}
                    {#if selected_message.error_code !== undefined}
                      <tr>
                        <td class="label">Error code</td>
                        <td class="mono">{selected_message.error_code}</td>
                      </tr>
                    {/if}
                  </tbody>
                </table>

                {#if selected_message.payload !== undefined}
                  <JsonTree value={selected_message.payload} expand_depth={3} />
                {:else if selected_message.payload_summary}
                  <div class="payload-summary mono">{selected_message.payload_summary}</div>
                {:else}
                  <span class="no-data">(no payload captured)</span>
                {/if}
              </div>
            {:else}
              <div class="no-selection">
                <span>Select a stream message to inspect it.</span>
              </div>
            {/if}
          </div>
        </div>
      {/if}

    </div>
  </div>
{/if}

<style>
  .no-selection {
    display: flex;
    align-items: center;
    justify-content: center;
    height: 100%;
    color: #555;
    font-size: 12px;
  }

  .detail {
    display: flex;
    flex-direction: column;
    height: 100%;
    overflow: hidden;
  }

  /* ─ Tabs ─ */
  .tabs {
    display: flex;
    gap: 0;
    background: #292b2f;
    border-bottom: 1px solid #3c4043;
    flex-shrink: 0;
  }
  .tab {
    background: transparent;
    border: 0;
    padding: 4px 12px;
    font-size: 11px;
    color: #9aa0a6;
    cursor: pointer;
    border-bottom: 2px solid transparent;
    user-select: none;
  }
  .tab:hover  { color: #e8eaed; }
  .tab.active { color: #e8eaed; border-bottom-color: #4a9eff; }
  .tab-error  { color: #ef9a9a !important; }
  .tab-error.active { border-bottom-color: #f44336 !important; }

  /* ─ Tab body ─ */
  .tab-body {
    flex: 1;
    overflow-y: auto;
    overflow-x: hidden;
    padding: 6px 8px;
  }

  /* ─ Info table (General tab) ─ */
  .info-table {
    width: 100%;
    border-collapse: collapse;
    font-size: 11px;
  }
  .info-table tr { border-bottom: 1px solid #2d2f33; }
  .info-table td { padding: 2px 4px; vertical-align: top; }

  .section-header td {
    padding: 6px 4px 2px;
    color: #9aa0a6;
    font-weight: 600;
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    border-bottom: 1px solid #3c4043;
  }
  .label {
    color: #9aa0a6;
    width: 110px;
    white-space: nowrap;
    font-size: 11px;
  }
  .mono {
    font-family: Menlo, Monaco, 'Courier New', monospace;
    color: #e8eaed;
  }
  .wrap { word-break: break-all; white-space: normal; }

  /* status colours */
  .status-success { color: #4caf50; }
  .status-error   { color: #f44336; }
  .status-pending { color: #ff9800; }

  /* direction badges */
  .badge-client { color: #7ab8f5; }
  .badge-server { color: #ffb347; }

  /* transport badge */
  .transport-badge {
    display: inline-block;
    padding: 1px 6px;
    border-radius: 3px;
    font-size: 10px;
    font-weight: 700;
    font-family: Menlo, Monaco, 'Courier New', monospace;
  }
  .transport-ws   { background: #1a4775; color: #7ab8f5; }
  .transport-wss  { background: #1a4a20; color: #6dbf67; }
  .transport-http { background: #3a2a00; color: #ffb347; }
  .transport-https{ background: #1a4a20; color: #6dbf67; }

  .stream-shell {
    display: flex;
    gap: 8px;
    height: 100%;
    min-height: 0;
  }
  .stream-list,
  .stream-message-detail {
    min-width: 0;
    border: 1px solid #2d2f33;
    border-radius: 4px;
    overflow: hidden;
    background: rgba(255,255,255,0.01);
  }
  .stream-list {
    width: 42%;
    display: flex;
    flex-direction: column;
  }
  .stream-message-detail {
    flex: 1;
    display: flex;
    flex-direction: column;
  }
  .stream-list-header {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 4px 8px;
    background: #292b2f;
    border-bottom: 1px solid #3c4043;
    font-size: 10px;
    color: #9aa0a6;
    text-transform: uppercase;
  }
  .stream-list-body {
    overflow: auto;
    flex: 1;
  }
  .stream-row {
    width: 100%;
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 4px 8px;
    border: 0;
    border-bottom: 1px solid #2d2f33;
    background: transparent;
    color: inherit;
    text-align: left;
    cursor: pointer;
  }
  .stream-row:hover { background: rgba(255,255,255,0.04); }
  .stream-row-active { background: #1a3a5c; }
  .stream-col-index { width: 24px; flex-shrink: 0; }
  .stream-col-dir { width: 24px; flex-shrink: 0; }
  .stream-col-kind { width: 72px; flex-shrink: 0; }
  .stream-col-meta { flex: 1; overflow: hidden; text-overflow: ellipsis; }
  .stream-info-table { margin-bottom: 8px; }
  .payload-summary {
    padding: 8px;
    border: 1px solid #2d2f33;
    border-radius: 4px;
    color: #9aa0a6;
  }

  /* ─ Payload sections ─ */
  .payload-header {
    display: flex;
    align-items: center;
    gap: 8px;
    margin-bottom: 6px;
    padding-bottom: 4px;
    border-bottom: 1px solid #3c4043;
  }
  .payload-label {
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: #9aa0a6;
    font-weight: 600;
  }
  .error-label { color: #ef9a9a; }

  .payload-body {
    padding: 4px 0;
    overflow-x: auto;
    min-width: 0;
    max-width: 100%;
  }
  .error-body {
    background: rgba(244,67,54,0.06);
    border-radius: 4px;
    padding: 8px;
  }
  .error-text {
    font-family: Menlo, Monaco, 'Courier New', monospace;
    font-size: 11px;
    color: #ef9a9a;
    white-space: pre-wrap;
    word-break: break-all;
  }
  .no-data {
    font-size: 11px;
    color: #555;
    font-style: italic;
  }

  .copy-btn {
    padding: 1px 8px;
    font-size: 10px;
    background: #2a2d30;
    color: #9aa0a6;
    border: 1px solid #3c4043;
    border-radius: 3px;
    cursor: pointer;
  }
  .copy-btn:hover {
    background: #3a3d40;
    color: #e8eaed;
  }
</style>
