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

  let expanded = $state(false);
  let expandedInitialized = false;

  function toggle() { expanded = !expanded; }

  $effect(() => {
    if (expandedInitialized) return;
    expanded = depth < expand_depth;
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

  const keys = $derived(childKeys(value));
  const isContainer = $derived((isArr(value) || isObj(value)) && !isBigInt(value));
  const previewBracket = $derived(isArr(value) ? '[' : '{');
  const closeBracket = $derived(isArr(value) ? ']' : '}');
  const previewItems = $derived(
    isContainer && keys.length > 0
      ? (keys.length > 3
          ? keys.slice(0, 3).join(', ') + ', …'
          : keys.join(', '))
      : ''
  );
</script>

<span class="node">
  <!-- key:  -->
  {#if key !== undefined}
    <span class="key">{key}:&#32;</span>
  {/if}

  <!-- container types (object / array) -->
  {#if isContainer}
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
  .bigint { color: #b5cea8; font-style: italic; }
  .boolean{ color: #569cd6; }
  .null   { color: #808080; font-style: italic; }
  .other  { color: #e8eaed; }
  .bracket{ color: #888; }
  .comma  { color: #888; }
  .preview{ color: #888; font-style: italic; }

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
