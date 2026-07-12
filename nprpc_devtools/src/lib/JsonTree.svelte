<svelte:options runes={true} />

<script lang="ts">
  import JsonTree from './JsonTree.svelte';

  // Recursive JSON tree renderer.
  // Props:
  //   value      – any JSON-serialisable value
  //   key        – optional property name shown to the left
  //   depth      – current nesting depth (root = 0)
  //   expand_depth – auto-expand nodes up to this depth (default 2)
  let {
    value = undefined,
    key = undefined,
    depth = 0,
    expand_depth = 2,
  }: {
    value?: unknown;
    key?: string;
    depth?: number;
    expand_depth?: number;
  } = $props();

  /** How many bytes to reveal each time the user expands a byte blob. */
  const BYTES_PAGE = 64;
  /** Plain number[] shorter than this always render as a normal array. */
  const BYTE_ARRAY_HEURISTIC_MIN = 32;

  type BytesInfo = { type: string; length: number; data: number[] };

  let expanded = $state(false);
  let expandedInitialized = false;
  /** Number of bytes currently shown for a byte blob (grows with "show more"). */
  let visibleBytes = $state(0);

  function toggle() {
    expanded = !expanded;
    if (expanded && isBytesValue(value) && visibleBytes === 0) {
      visibleBytes = Math.min(BYTES_PAGE, getBytesInfo(value).length);
    }
    if (!expanded) {
      visibleBytes = 0;
    }
  }

  function showMoreBytes(total: number) {
    visibleBytes = Math.min(total, visibleBytes + BYTES_PAGE);
  }

  $effect(() => {
    if (expandedInitialized) return;
    // Never auto-expand byte blobs — they can be huge.
    expanded = !isBytesValue(value) && depth < expand_depth;
    expandedInitialized = true;
  });

  const isObj    = (v: unknown): v is Record<string, unknown> =>
    v !== null && typeof v === 'object' && !Array.isArray(v);
  const isArr    = (v: unknown): v is unknown[] => Array.isArray(v);
  const isStr    = (v: unknown): v is string  => typeof v === 'string';
  const isNum    = (v: unknown): v is number  => typeof v === 'number';
  const isBool   = (v: unknown): v is boolean => typeof v === 'boolean';
  const isNull   = (v: unknown)               => v === null;
  const isUndef  = (v: unknown)               => v === undefined;
  // Tagged sentinel produced by inject.js sanitize() for BigInt values.
  const isBigInt = (v: unknown): v is { __bigint__: string } =>
    isObj(v) && '__bigint__' in (v as object) && Object.keys(v as object).length === 1;

  // Tagged sentinel produced by inject.js sanitize() for ArrayBuffer / TypedArray.
  function isBytesTag(v: unknown): v is { __bytes__: { type: string; length: number; data: number[] } } {
    if (!isObj(v) || !('__bytes__' in v)) return false;
    const b = (v as { __bytes__: unknown }).__bytes__;
    if (!isObj(b)) return false;
    const { type, length, data } = b as Record<string, unknown>;
    return typeof type === 'string'
      && typeof length === 'number'
      && Array.isArray(data);
  }

  /** Large plain arrays of 0–255 integers (e.g. already-JSON'd binary). */
  function isByteNumberArray(v: unknown): v is number[] {
    if (!isArr(v) || v.length < BYTE_ARRAY_HEURISTIC_MIN) return false;
    for (let i = 0; i < v.length; i++) {
      const x = v[i];
      if (typeof x !== 'number' || !Number.isInteger(x) || x < 0 || x > 255) {
        return false;
      }
    }
    return true;
  }

  function isBytesValue(v: unknown): boolean {
    return isBytesTag(v) || isByteNumberArray(v);
  }

  function getBytesInfo(v: unknown): BytesInfo {
    if (isBytesTag(v)) {
      const b = v.__bytes__;
      return {
        type: b.type,
        length: b.length,
        data: b.data as number[],
      };
    }
    // Plain number[] path (already-JSON'd binary)
    const arr = v as number[];
    return { type: 'bytes', length: arr.length, data: arr };
  }

  function childKeys(v: unknown): string[] {
    if (isArr(v))  return v.map((_, i) => String(i));
    if (isObj(v))  return Object.keys(v);
    return [];
  }
  function childAt(v: unknown, k: string): unknown {
    if (isArr(v)) return (v as unknown[])[Number(k)];
    if (isObj(v)) return (v as Record<string, unknown>)[k];
    return undefined;
  }

  function fmtByte(n: number): string {
    return n.toString(16).padStart(2, '0');
  }

  function fmtByteCount(n: number): string {
    if (n === 1) return '1 byte';
    return `${n} bytes`;
  }

  const keys = $derived(childKeys(value));
  const bytes = $derived(isBytesValue(value) ? getBytesInfo(value) : null);
  const isContainer = $derived(
    (isArr(value) || isObj(value)) && !isBigInt(value) && !isBytesValue(value)
  );
  const previewBracket = $derived(isArr(value) ? '[' : '{');
  const closeBracket = $derived(isArr(value) ? ']' : '}');
  const previewItems = $derived(
    isContainer && keys.length > 0
      ? (keys.length > 3
          ? keys.slice(0, 3).join(', ') + ', …'
          : keys.join(', '))
      : ''
  );
  const shownBytes = $derived(
    bytes ? bytes.data.slice(0, visibleBytes) : []
  );
  const remainingBytes = $derived(
    bytes ? Math.max(0, bytes.length - visibleBytes) : 0
  );
  const nextPageSize = $derived(
    bytes ? Math.min(BYTES_PAGE, remainingBytes) : 0
  );
</script>

<span class="node">
  <!-- key:  -->
  {#if key !== undefined}
    <span class="key">{key}:&#32;</span>
  {/if}

  <!-- byte blobs (ArrayBuffer / TypedArray / large byte arrays) -->
  {#if bytes}
    <button type="button" class="toggle" onclick={toggle}>
      {expanded ? '▾' : '▸'}
    </button>

    {#if expanded}
      <span class="bytes-label">{bytes.type}({fmtByteCount(bytes.length)})</span>
      <span class="bracket">[</span>
      <span class="children">
        {#each shownBytes as byte, i}
          <div class="child">
            <span class="key">{i}:&#32;</span>
            <span class="byte">{fmtByte(byte)}</span>
            {#if i < shownBytes.length - 1 || remainingBytes > 0}
              <span class="comma">,</span>
            {/if}
          </div>
        {/each}
        {#if remainingBytes > 0}
          <div class="child">
            <button
              type="button"
              class="show-more"
              onclick={() => showMoreBytes(bytes.length)}
            >
              ▸ show {nextPageSize} more ({fmtByteCount(remainingBytes)} remaining)
            </button>
          </div>
        {/if}
      </span>
      <span class="bracket">]</span>
    {:else}
      <button type="button" class="collapsed" onclick={toggle}>
        <span class="bytes-label">{bytes.type}({fmtByteCount(bytes.length)})</span>
      </button>
    {/if}

  <!-- container types (object / array) -->
  {:else if isContainer}
    <!-- collapse/expand triangle -->
    <button type="button" class="toggle" onclick={toggle}>
      {expanded ? '▾' : '▸'}
    </button>

    {#if expanded}
      <span class="bracket">{previewBracket}</span>
      <span class="children">
        {#each keys as k}
          <div class="child">
            <JsonTree
              key={isArr(value) ? undefined : k}
              value={childAt(value, k)}
              depth={depth + 1}
              {expand_depth}
            />
            {#if Number(k) < keys.length - 1 || k !== keys[keys.length - 1]}
              <span class="comma">,</span>
            {/if}
          </div>
        {/each}
      </span>
      <span class="bracket">{closeBracket}</span>
    {:else}
      <button type="button" class="collapsed" onclick={toggle}>
        {previewBracket}
        {#if keys.length > 0}
          <span class="preview">{previewItems}</span>
        {/if}
        {closeBracket}
      </button>
    {/if}

  <!-- primitive types -->
  {:else if isBigInt(value)}
    <span class="bigint">{(value as { __bigint__: string }).__bigint__}n</span>
  {:else if isStr(value)}
    <span class="string">"{value}"</span>
  {:else if isNum(value)}
    <span class="number">{value}</span>
  {:else if isBool(value)}
    <span class="boolean">{value}</span>
  {:else if isNull(value)}
    <span class="null">null</span>
  {:else if isUndef(value)}
    <span class="null">undefined</span>
  {:else}
    <span class="other">{String(value)}</span>
  {/if}
</span>

<style>
  .node {
    font-family: Menlo, Monaco, 'Courier New', monospace;
    font-size: 11px;
    line-height: 1.6;
    white-space: pre;
    display: inline;
  }
  .key    { color: #9cdcfe; }
  .string { color: #ce9178; }
  .number { color: #b5cea8; }
  .byte   { color: #b5cea8; }
  .bigint { color: #b5cea8; font-style: italic; }
  .boolean{ color: #569cd6; }
  .null   { color: #808080; font-style: italic; }
  .other  { color: #e8eaed; }
  .bracket{ color: #888; }
  .comma  { color: #888; }
  .preview{ color: #888; font-style: italic; }
  .bytes-label { color: #4ec9b0; }

  .toggle {
    background: transparent;
    border: 0;
    cursor: pointer;
    color: #888;
    user-select: none;
    margin-right: 2px;
    padding: 0;
  }
  .toggle:hover { color: #e8eaed; }

  .collapsed {
    background: transparent;
    border: 0;
    color: #888;
    cursor: pointer;
    padding: 0;
  }
  .collapsed:hover { color: #e8eaed; }

  .show-more {
    background: transparent;
    border: 0;
    color: #888;
    cursor: pointer;
    font: inherit;
    padding: 0;
    font-style: italic;
  }
  .show-more:hover { color: #e8eaed; }

  .children {
    display: block;
    padding-left: 16px;
    text-align: left;
  }
  .child {
    display: block;
    text-align: left;
  }
</style>
