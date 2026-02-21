// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT
//
// Content script (isolated world).
// 1. Injects inject.js into the page's MAIN world via a <script> tag.
// 2. Listens for CustomEvents fired by inject.js and relays them to the
//    background service worker via chrome.runtime.sendMessage.

const script = document.createElement('script');
script.src = chrome.runtime.getURL('inject.js');
script.onload = () => script.remove();
(document.head || document.documentElement).appendChild(script);

window.addEventListener('__nprpc_debug_event__', (e: Event) => {
  const detail = (e as CustomEvent<Record<string, unknown>>).detail;
  chrome.runtime.sendMessage({ source: 'nprpc_content', ...detail });
});
