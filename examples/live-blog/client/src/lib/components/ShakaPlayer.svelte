<script lang="ts">
	import { onMount, onDestroy } from 'svelte';
	import { getVideoDashManifest, getVideoDashSegmentRange } from '$lib/live-blog-rpc';

	let { postId }: { postId: bigint } = $props();

	let videoEl = $state<HTMLVideoElement | undefined>(undefined);
	let status = $state<'idle' | 'loading' | 'playing' | 'done' | 'error'>('idle');
	let errorMsg = $state('');
	let ready = $state(false);

	// Accumulated bytes for the status bar
	let bytesLoaded = $state(0);

	let playerInstance: any = null;
	// Track scheme registration so we only unregister what we own.
	let schemeRegistered = false;

	function parseByteRange(header: string): [bigint, bigint] {
		// Range: bytes=start-end
		const m = header.match(/bytes=(\d+)-(\d+)/);
		if (!m) throw new Error(`Unsupported Range syntax: ${header}`);
		const start = BigInt(m[1]);
		const end = BigInt(m[2]);
		return [start, end - start + 1n]; // [offset, length]
	}

	async function mergeStream(stream: AsyncIterable<Uint8Array>): Promise<Uint8Array> {
		const chunks: Uint8Array[] = [];
		let total = 0;
		for await (const chunk of stream) {
			chunks.push(chunk);
			total += chunk.byteLength;
		}
		const out = new Uint8Array(total);
		let pos = 0;
		for (const c of chunks) { out.set(c, pos); pos += c.byteLength; }
		return out;
	}

	onMount(async () => {
		// Dynamic import keeps Shaka out of the SSR bundle.
		const shaka = await import('shaka-player');
		shaka.polyfill.installAll();

		if (!(shaka.Player as any).isBrowserSupported()) {
			errorMsg = 'This browser is not supported by Shaka Player.';
			status = 'error';
			return;
		}

		ready = true;
	});

	async function startPlayback() {
		if (!videoEl) return;
		const shaka = await import('shaka-player');

		// Register the nprpc:// scheme once per session.
		// Shaka silently replaces an existing handler, so re-registering is safe.
		shaka.net.NetworkingEngine.registerScheme(
			'nprpc',
			(uri: string, request: any, type: number, _prog: any, _hdrs: any, _cfg: any) => {
				const MANIFEST = 0; // shaka.net.NetworkingEngine.RequestType.MANIFEST
				return (shaka.util as any).AbortableOperation.notAbortable(
					(async (): Promise<any> => {
						const url = new URL(uri);
						const pid = BigInt(url.hostname);

						let bytes: Uint8Array;

						if (type === MANIFEST) {
							const manifest = await getVideoDashManifest(pid);
							bytes = new TextEncoder().encode(manifest);
						} else {
							// Byte-range fetch for init segment or media segment.
							const rangeHdr =
								request.headers['Range'] ??
								request.headers['range'] ??
								null;
							if (!rangeHdr) {
								throw new Error(`nprpc scheme: Range header missing for non-manifest request: ${uri}`);
							}
							const [offset, length] = parseByteRange(rangeHdr);
							const stream = await getVideoDashSegmentRange(pid, offset, length);
							bytes = await mergeStream(stream as any);
							bytesLoaded += bytes.byteLength;
						}

						return {
							uri,
							headers: {},
							data: bytes.buffer,
							timeMs: 0,
							fromCache: false
						};
					})()
				);
			}
		);
		schemeRegistered = true;

		playerInstance = new shaka.Player();
		await playerInstance.attach(videoEl);

		playerInstance.addEventListener('error', (e: any) => {
			errorMsg = `Shaka [${e.detail?.code ?? '?'}]: ${e.detail?.message ?? 'Unknown error'}`;
			status = 'error';
		});

		status = 'loading';
		bytesLoaded = 0;

		try {
			await playerInstance.load(`nprpc://${postId}/manifest.mpd`);
			void videoEl.play().catch(() => {});
			status = 'playing';
		} catch (e: any) {
			errorMsg = e?.message ?? String(e);
			status = 'error';
		}
	}

	async function reset() {
		status = 'idle';
		errorMsg = '';
		bytesLoaded = 0;
		if (playerInstance) {
			await playerInstance.detach();
		}
		if (videoEl) {
			videoEl.src = '';
		}
	}

	onDestroy(async () => {
		if (playerInstance) {
			await playerInstance.destroy();
			playerInstance = null;
		}
		if (schemeRegistered) {
			try {
				const shaka = await import('shaka-player');
				shaka.net.NetworkingEngine.unregisterScheme('nprpc');
			} catch {}
			schemeRegistered = false;
		}
	});
</script>

<div class="space-y-4">
	<p class="eyebrow">Shaka Player — DASH over NPRPC</p>
	<p class="mt-3 text-sm leading-6 text-stone-600">
		Registers a custom <code>nprpc://</code> scheme plugin with
		<a
			class="underline decoration-amber-600/60 underline-offset-2"
			href="https://shaka-player-demo.appspot.com/docs/api/index.html"
			target="_blank"
			rel="noopener noreferrer"
		>Shaka Player</a>.
		<code>GetVideoDashManifest</code> returns the MPD; every byte-range request
		calls <code>GetVideoDashSegmentRange</code> — a <code>server_stream&lt;binary&gt;</code>
		with backpressure.
	</p>

	<div class="relative aspect-video overflow-hidden rounded-[22px] border border-amber-900/10 bg-stone-900">
		<!-- svelte-ignore a11y-media-has-caption -->
		<video bind:this={videoEl} class="h-full w-full" controls playsinline></video>

		{#if status === 'idle'}
			<div class="absolute inset-0 flex flex-col items-center justify-center gap-4 bg-stone-900/80 backdrop-blur-sm">
				{#if !ready}
					<p class="text-sm text-stone-400">Checking browser support…</p>
				{:else}
					<button
						type="button"
						class="rounded-full bg-stone-50 px-6 py-2.5 text-sm font-medium text-stone-900 shadow transition hover:bg-white"
						onclick={startPlayback}
					>
						▶ Stream via Shaka + NPRPC
					</button>
					<p class="text-xs text-stone-400">DASH MPD + byte-range segments over <code>server_stream&lt;binary&gt;</code></p>
				{/if}
			</div>
		{:else if status === 'loading'}
			<div class="absolute inset-0 flex items-center justify-center bg-stone-900/60">
				<p class="text-sm text-stone-300">Fetching manifest and buffering…</p>
			</div>
		{:else if status === 'error'}
			<div class="absolute inset-0 flex flex-col items-center justify-center gap-3 bg-stone-900/90 p-6">
				<p class="text-sm font-medium text-rose-400">Playback error</p>
				<p class="max-w-xs text-center text-xs text-stone-400">{errorMsg}</p>
				<button
					type="button"
					class="mt-2 rounded-full border border-stone-600 px-4 py-1.5 text-xs text-stone-300 transition hover:bg-stone-800"
					onclick={reset}
				>
					Retry
				</button>
			</div>
		{/if}
	</div>

	{#if status === 'playing' || status === 'done'}
		<p class="text-xs text-stone-500">
			{(bytesLoaded / 1024 / 1024).toFixed(1)} MB received via nprpc:// segments
		</p>
	{/if}

	<details class="rounded-2xl border border-amber-900/10 bg-amber-50/40 p-4 text-sm">
		<summary class="cursor-pointer font-medium text-stone-800">How to prepare the DASH manifest</summary>
		<div class="mt-4 space-y-3 text-stone-700 leading-6">
			<p>
				The server expects a single-file DASH package: <code>/app/media/post-{postId}.fmp4</code>
				(media) and <code>/app/media/post-{postId}.mpd</code> (manifest).
				Generate both with <a class="underline" href="https://gpac.io/mp4box/" target="_blank" rel="noopener noreferrer">MP4Box</a>
				from GPAC using the <strong>onDemand</strong> profile — all segments live in the single <code>.fmp4</code>
				and the MPD encodes byte ranges via the <code>sidx</code> box:
			</p>
			<pre class="overflow-x-auto rounded-xl bg-stone-950 p-4 text-xs leading-6 text-stone-300"># 1) Create a fragmented MP4 (skip if you already have one)
ffmpeg -i input.mp4 \
  -c:v libx264 -c:a aac \
  -movflags frag_keyframe+empty_moov+default_base_moof \
  -f mp4 post-{postId}.fmp4

# 2) Package as single-file DASH (4-second segments, preserving the fmp4)
MP4Box -dash 4000 -frag 4000 -rap -profile onDemand \
  post-{postId}.fmp4 -out post-{postId}.mpd

# 3) Copy both files to /app/media/ inside the container</pre>
			<p>
				The <code>-profile onDemand</code> flag tells MP4Box to keep everything in
				one file and write a <code>&lt;SegmentBase indexRange="…"&gt;</code> MPD
				instead of separate <code>.m4s</code> files — perfect for byte-range streaming.
			</p>
		</div>
	</details>
</div>
