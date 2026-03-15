// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT
//
// Content script (isolated world).
// 1. Injects inject.js into the page's MAIN world via a <script> tag.
// 2. Listens for CustomEvents fired by inject.js and forwards them to
//    the DevTools panel via a direct port connection (no background SW needed).

const script = document.createElement('script');
script.src = chrome.runtime.getURL('inject.js');
script.onload = () => script.remove();
(document.head || document.documentElement).appendChild(script);

// The DevTools panel connects directly via chrome.tabs.connect.
let panelPort: chrome.runtime.Port | null = null;
const EARLY_EVENT_CAP = 200;
const earlyEvents: (Record<string, unknown> & { id: number })[] = [];
let idOffset: number = 0;

chrome.runtime.onConnect.addListener((port) => {
  if (port.name !== 'nprpc_devtools')
    return;

  panelPort = port;
  port.onDisconnect.addListener(() => { panelPort = null; });

  port.onMessage.addListener((msg: any) => {
    if (msg.type !== 'nprpc_devtools_setup') return;

    idOffset = msg.nextId;

    for (const detail of earlyEvents) {
      try {
        detail.id += idOffset;
        port.postMessage({ source: 'nprpc_content', ...detail });
      } catch { break; }
    }
    earlyEvents.length = 0;
  });
});

window.addEventListener('__nprpc_debug_event__', (e: Event) => {
  const detail = (e as CustomEvent<Record<string, unknown> & { id: number }>).detail;

  if (panelPort && earlyEvents.length === 0) {
    try {
      detail.id += idOffset;
      panelPort.postMessage({ source: 'nprpc_content', ...detail });
    } catch { /*panelPort = null;*/ }
  } else if (earlyEvents.length < EARLY_EVENT_CAP) {
    earlyEvents.push(detail);
  }
});
