// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT
//
// Background service-worker.
// Maintains a tab-id → DevTools panel port map and relays debug events.

const panelPorts = new Map<number, chrome.runtime.Port>();

chrome.runtime.onConnect.addListener((port) => {
  if (port.name !== 'panel') return;

  port.onMessage.addListener((msg: { type: string; tabId: number }) => {
    if (msg.type === 'panel_init') {
      panelPorts.set(msg.tabId, port);
    }
  });

  port.onDisconnect.addListener(() => {
    for (const [tabId, p] of panelPorts) {
      if (p === port) panelPorts.delete(tabId);
    }
  });
});

chrome.runtime.onMessage.addListener((msg, sender) => {
  if (msg?.source !== 'nprpc_content') return;
  const tabId = sender.tab?.id;
  if (tabId === undefined) return;
  const port = panelPorts.get(tabId);
  if (port) {
    try { port.postMessage(msg); } catch { /* panel may have disconnected */ }
  }
});
