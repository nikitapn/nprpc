<script lang="ts">
  // Recursive JSON tree renderer.
  // Props:
  //   value      – any JSON-serialisable value
  //   key        – optional property name shown to the left
  //   depth      – current nesting depth (root = 0)
  //   expand_depth – auto-expand nodes up to this depth (default 2)
  import { onMount } from 'svelte';

  export let value       : unknown = undefined;
  export let key         : string  | undefined = undefined;
  export let depth       : number  = 0;
  export let expand_depth: number  = 2;

  let expanded = depth < expand_depth;

  function toggle() { expanded = !expanded; }

  const isObj   = (v: unknown): v is Record<string, unknown> =>
    v !== null && typeof v === 'object' && !Array.isArray(v);
  const isArr   = (v: unknown): v is unknown[] => Array.isArray(v);
  const isStr   = (v: unknown): v is string  => typeof v === 'string';
  const isNum   = (v: unknown): v is number  => typeof v === 'number';
  const isBool  = (v: unknown): v is boolean => typeof v === 'boolean';
  const isNull  = (v: unknown)               => v === null;
  const isUndef = (v: unknown)               => v === undefined;

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

  $: keys  = childKeys(value);
  $: isContainer = isArr(value) || isObj(value);
  $: previewBracket = isArr(value) ? '[' : '{';
  $: closeBracket   = isArr(value) ? ']' : '}';
  $: previewItems   = isContainer && keys.length > 0
    ? (keys.length > 3
        ? keys.slice(0, 3).join(', ') + ', …'
        : keys.join(', '))
    : '';
</script>

<span class="node">
  <!-- key:  -->
  {#if key !== undefined}
    <span class="key">{key}:&#32;</span>
  {/if}

  <!-- container types (object / array) -->
  {#if isContainer}
    <!-- collapse/expand triangle -->
    <!-- svelte-ignore a11y-click-events-have-key-events -->
    <!-- svelte-ignore a11y-no-static-element-interactions -->
    <span class="toggle" on:click={toggle}>
      {expanded ? '▾' : '▸'}
    </span>

    {#if expanded}
      <span class="bracket">{previewBracket}</span>
      <span class="children">
        {#each keys as k}
          <div class="child">
            <svelte:self
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
      <span class="collapsed">
        {previewBracket}
        {#if keys.length > 0}
          <span class="preview">{previewItems}</span>
        {/if}
        {closeBracket}
      </span>
    {/if}

  <!-- primitive types -->
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
  .boolean{ color: #569cd6; }
  .null   { color: #808080; font-style: italic; }
  .other  { color: #e8eaed; }
  .bracket{ color: #888; }
  .comma  { color: #888; }
  .preview{ color: #888; font-style: italic; }

  .toggle {
    cursor: pointer;
    color: #888;
    user-select: none;
    margin-right: 2px;
  }
  .toggle:hover { color: #e8eaed; }

  .collapsed {
    color: #888;
    cursor: pointer;
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
