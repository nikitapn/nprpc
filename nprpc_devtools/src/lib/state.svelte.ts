// Shared panel UI state. Lives for the lifetime of the DevTools panel only
// (no localStorage / chrome.storage).

export type RpcTab = 'general' | 'request' | 'response';
export type StreamTab = 'general' | 'init' | 'messages';
export type DetailTab = RpcTab | StreamTab;

export const detailTabs = $state({
  rpc: 'general' as RpcTab,
  stream: 'general' as StreamTab,
});
