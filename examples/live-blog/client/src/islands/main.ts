// Island entry point.
//
// Pages are server-rendered; this bundle only wakes up the two widgets that
// need a live NPRPC connection. Both are idempotent, so htmx swaps that bring
// new markup in can simply call mount() again.

import { mountChat } from './chat';
import { mountVideo } from './video';

function mount(): void {
	mountChat();
	mountVideo();
}

if (document.readyState === 'loading') {
	document.addEventListener('DOMContentLoaded', mount, { once: true });
} else {
	mount();
}

// htmx replaces page fragments after load; anything swapped in still needs
// mounting, and mountAll skips elements it has already claimed.
document.body?.addEventListener('htmx:afterSwap', mount);
