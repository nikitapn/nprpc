<script lang="ts">
  import type { RpcEvent } from '../types';
  import JsonTree from './JsonTree.svelte';

  export let event: RpcEvent | null = null;

  type Tab = 'general' | 'request' | 'response';
  let active_tab: Tab = 'general';

  $: if (event) active_tab = 'general';

  // ── Formatting ──────────────────────────────────────────────────

  function fmt_time(ts: number): string {
    const d = new Date(ts);
    const h  = String(d.getHours()).padStart(2, '0');
    const m  = String(d.getMinutes()).padStart(2, '0');
    const s  = String(d.getSeconds()).padStart(2, '0');
    const ms = String(d.getMilliseconds()).padStart(3, '0');
    return `${h}:${m}:${s}.${ms}`;
  }

  function fmt_duration(ev: RpcEvent): string {
    if (ev.status === 'pending') return 'pending …';
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

  function endpoint_url(ev: RpcEvent): string {
    return `${ev.endpoint.transport}://${ev.endpoint.hostname}:${ev.endpoint.port}`;
  }

  function status_label(ev: RpcEvent): string {
    if (ev.status === 'success') return 'Success ✓';
    if (ev.status === 'error')   return 'Error ✕';
    return 'Pending …';
  }

  function copy_json(value: unknown) {
    navigator.clipboard?.writeText(JSON.stringify(value, null, 2));
  }

  // Has error tab?
  $: has_error = event?.status === 'error' && !!event.error;
</script>

{#if !event}
  <div class="no-selection">
    <span>Select a call to inspect it.</span>
  </div>
{:else}
  <div class="detail">
    <!-- ── Tabs ─────────────────────────────────────────────── -->
    <div class="tabs" role="tablist">
      <!-- svelte-ignore a11y-click-events-have-key-events -->
      <span
        class="tab"
        class:active={active_tab === 'general'}
        role="tab"
        tabindex="0"
        on:click={() => active_tab = 'general'}
      >General</span>
      <!-- svelte-ignore a11y-click-events-have-key-events -->
      <span
        class="tab"
        class:active={active_tab === 'request'}
        role="tab"
        tabindex="0"
        on:click={() => active_tab = 'request'}
      >Request</span>
      <!-- svelte-ignore a11y-click-events-have-key-events -->
      <span
        class="tab"
        class:active={active_tab === 'response'}
        class:tab-error={has_error}
        role="tab"
        tabindex="0"
        on:click={() => active_tab = 'response'}
      >{has_error ? 'Error' : 'Response'}</span>
    </div>

    <!-- ── Tab bodies ─────────────────────────────────────── -->
    <div class="tab-body">

      {#if active_tab === 'general'}
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

      {:else if active_tab === 'request'}
        <div class="payload-header">
          <span class="payload-label">Request arguments</span>
          {#if event.request_args !== undefined}
            <button class="copy-btn" on:click={() => copy_json(event!.request_args)}>
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

      {:else if active_tab === 'response'}
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
              <button class="copy-btn" on:click={() => copy_json(event!.response_args)}>
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
