// Minimal DOM helpers.
//
// The islands build their own markup instead of hydrating server HTML: they are
// self-contained widgets, not views of page data, so there is nothing to match
// up and no framework needed.

type Attrs = Record<string, string | boolean | undefined>;

export function el<K extends keyof HTMLElementTagNameMap>(
	tag: K,
	attrs: Attrs = {},
	children: Array<Node | string> = []
): HTMLElementTagNameMap[K] {
	const node = document.createElement(tag);
	for (const [name, value] of Object.entries(attrs)) {
		if (value === undefined || value === false) continue;
		if (value === true) {
			node.setAttribute(name, '');
		} else {
			node.setAttribute(name, value);
		}
	}
	// Strings go in as text nodes, never as HTML — chat messages and error text
	// come from other users and from the server.
	node.append(...children);
	return node;
}

export function clear(node: Element): void {
	while (node.firstChild) node.removeChild(node.firstChild);
}

/// Mount `setup` on every element matching `selector` that carries a numeric
/// `data-post-id`, skipping ones already mounted.
export function mountAll(
	selector: string,
	setup: (root: HTMLElement, postId: bigint) => void
): void {
	for (const root of document.querySelectorAll<HTMLElement>(selector)) {
		if (root.dataset.nprpcMounted === 'true') continue;

		const raw = root.dataset.postId;
		if (!raw) continue;
		let postId: bigint;
		try {
			postId = BigInt(raw);
		} catch {
			continue;
		}

		root.dataset.nprpcMounted = 'true';
		setup(root, postId);
	}
}
