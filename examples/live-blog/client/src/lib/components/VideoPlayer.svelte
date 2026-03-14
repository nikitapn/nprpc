<script lang="ts">
	import { streamPostVideo } from '$lib/live-blog-rpc';

	let { postId }: { postId: bigint } = $props();

	let videoEl = $state<HTMLVideoElement | undefined>(undefined);
	let videoStatus = $state<'idle' | 'loading' | 'streaming' | 'done' | 'error'>('idle');
	let videoError = $state('');
	let bytesReceived = $state(0);
	let detectedMimeType = $state('');

	// Scan the moov init segment for an avcC box and return the exact codec string,
	// e.g. "avc1.640028" for H.264 High 4.0.  Returns null if not found.
	function detectAvcCodec(data: Uint8Array): string | null {
		// 'avcC' = 0x61 0x76 0x63 0x43
		for (let i = 0; i < data.length - 10; i++) {
			if (data[i] === 0x61 && data[i + 1] === 0x76 && data[i + 2] === 0x63 && data[i + 3] === 0x43) {
				// avcDecoderConfigurationRecord immediately follows the 4-byte box type:
				//   [i+4] configurationVersion (= 1)
				//   [i+5] AVCProfileIndication
				//   [i+6] profile_compatibility
				//   [i+7] AVCLevelIndication
				const profile = data[i + 5].toString(16).padStart(2, '0');
				const compat = data[i + 6].toString(16).padStart(2, '0');
				const level = data[i + 7].toString(16).padStart(2, '0');
				return `avc1.${profile}${compat}${level}`;
			}
		}
		return null;
	}

	function waitUpdateEnd(sb: SourceBuffer): Promise<void> {
		if (!sb.updating) return Promise.resolve();
		return new Promise((resolve) => sb.addEventListener('updateend', resolve, { once: true }));
	}

	function removeBuffer(sb: SourceBuffer, start: number, end: number): Promise<void> {
		return new Promise<void>((resolve) => {
			sb.addEventListener('updateend', resolve, { once: true });
			sb.remove(start, end);
		});
	}

	function appendChunk(sb: SourceBuffer, data: Uint8Array): Promise<void> {
		return new Promise<void>((resolve, reject) => {
			const onEnd = () => resolve();
			// SourceBuffer fires plain Event (no .message); real detail is in videoEl.error.
			const onErr = () => reject(new Error('SourceBuffer append failed'));
			sb.addEventListener('updateend', onEnd, { once: true });
			sb.addEventListener('error', onErr, { once: true });
			try {
				sb.appendBuffer(data);
			} catch (e) {
				sb.removeEventListener('updateend', onEnd);
				sb.removeEventListener('error', onErr);
				reject(e);
			}
		});
	}

	async function safeAppend(sb: SourceBuffer, data: Uint8Array): Promise<void> {
		await waitUpdateEnd(sb);
		try {
			await appendChunk(sb, data);
		} catch (e) {
			if (e instanceof DOMException && e.name === 'QuotaExceededError' && videoEl) {
				const t = videoEl.currentTime;
				if (sb.buffered.length > 0 && t > 30) {
					await removeBuffer(sb, 0, t - 30);
					await waitUpdateEnd(sb);
					await appendChunk(sb, data);
				} else {
					throw new Error(`QuotaExceededError at ${t.toFixed(1)}s — not enough played-back range to evict`);
				}
			} else if (e instanceof DOMException) {
				throw new Error(`SourceBuffer ${e.name}: ${e.message}`);
			} else {
				throw e;
			}
		}
	}

	// Maximum seconds to buffer ahead of the current playback position.
	// When the buffer exceeds this, we stop pulling new chunks (and stop
	// sending window-update credits) until playback catches up.
	const MAX_BUFFER_AHEAD_S = 30;

	function bufferedAhead(sb: SourceBuffer): number {
		if (!videoEl) return 0;
		const t = videoEl.currentTime;
		for (let i = 0; i < sb.buffered.length; i++) {
			if (sb.buffered.start(i) <= t + 0.1 && sb.buffered.end(i) > t) {
				return sb.buffered.end(i) - t;
			}
		}
		return 0;
	}

	function waitForTimeUpdate(): Promise<void> {
		return new Promise<void>((resolve) =>
			videoEl!.addEventListener('timeupdate', () => resolve(), { once: true })
		);
	}

	async function waitForBufferSpace(sb: SourceBuffer): Promise<void> {
		while (videoEl && bufferedAhead(sb) >= MAX_BUFFER_AHEAD_S) {
			await waitForTimeUpdate();
		}
	}

	function mediaErrorName(code: number): string {
		return (
			['', 'MEDIA_ERR_ABORTED', 'MEDIA_ERR_NETWORK', 'MEDIA_ERR_DECODE', 'MEDIA_ERR_SRC_NOT_SUPPORTED'][code] ??
			`MediaError(${code})`
		);
	}

	async function startVideo() {
		if (!videoEl) return;
		if (!('MediaSource' in window)) {
			videoError = 'Media Source Extensions are not supported in this browser.';
			videoStatus = 'error';
			return;
		}

		videoStatus = 'loading';
		videoError = '';
		bytesReceived = 0;
		detectedMimeType = '';

		try {
			// Open the NPRPC server stream first so we can read the init segment
			// and detect the exact codec before creating the SourceBuffer.
			// (256 KB first chunk always contains the full moov box.)
			const stream = await streamPostVideo(postId);
			const iter = stream[Symbol.asyncIterator]();

			const { value: initChunk, done } = await iter.next();
			if (done || !initChunk) {
				videoError = `Server returned an empty stream — check that /app/media/post-${postId}.fmp4 exists.`;
				videoStatus = 'error';
				return;
			}

			// Derive the exact MIME type from the avcC box in the moov init segment.
			// Using a wrong profile string (e.g. avc1.42E01E for a High-profile file)
			// causes Chrome to fire a SourceBuffer error event on the first append.
			const avcCodec = detectAvcCodec(initChunk);
			const mimeType = avcCodec ? `video/mp4; codecs="${avcCodec},mp4a.40.2"` : 'video/mp4';

			if (!MediaSource.isTypeSupported(mimeType)) {
				videoError = `Codec not supported in this browser: ${mimeType}`;
				videoStatus = 'error';
				return;
			}

			detectedMimeType = mimeType;

			const mediaSource = new MediaSource();
			const objectUrl = URL.createObjectURL(mediaSource);
			videoEl.src = objectUrl;
			void videoEl.play().catch(() => {});

			await new Promise<void>((resolve) =>
				mediaSource.addEventListener('sourceopen', () => resolve(), { once: true })
			);

			const sourceBuffer = mediaSource.addSourceBuffer(mimeType);
			videoStatus = 'streaming';

			await safeAppend(sourceBuffer, initChunk);
			bytesReceived += initChunk.byteLength;

			for (;;) {
				await waitForBufferSpace(sourceBuffer);
				const { value: chunk, done } = await iter.next();
				if (done || !chunk) break;
				await safeAppend(sourceBuffer, chunk);
				bytesReceived += chunk.byteLength;
			}

			await waitUpdateEnd(sourceBuffer);
			mediaSource.endOfStream();
			videoStatus = 'done';
		} catch (e) {
			const mediaErr = videoEl?.error;
			videoError = mediaErr
				? `${mediaErrorName(mediaErr.code)}: ${mediaErr.message || '(no message)'}`
				: e instanceof Error
					? e.message
					: String(e);
			videoStatus = 'error';
		}
	}

	function reset() {
		videoStatus = 'idle';
		videoError = '';
		bytesReceived = 0;
		detectedMimeType = '';
		if (videoEl) {
			const old = videoEl.src;
			videoEl.src = '';
			if (old.startsWith('blob:')) URL.revokeObjectURL(old);
		}
	}
</script>

<div class="space-y-4">
	<p class="eyebrow">Server stream — MSE video</p>
	<p class="mt-3 text-sm leading-6 text-stone-600">
		Calls <code>OpenPostVideo(post_id)</code> — a <code>server_stream&lt;binary&gt;</code> —
		and feeds each <code>Uint8Array</code> chunk into a
		<a
			class="underline decoration-amber-600/60 underline-offset-2"
			href="https://developer.mozilla.org/en-US/docs/Web/API/Media_Source_Extensions_API"
			target="_blank"
			rel="noopener noreferrer"
		>MediaSource SourceBuffer</a>.
		The server must provide a <strong>fragmented MP4</strong> (see instructions below).
	</p>

	<div class="relative aspect-video overflow-hidden rounded-[22px] border border-amber-900/10 bg-stone-900">
		<!-- svelte-ignore a11y-media-has-caption -->
		<video
			bind:this={videoEl}
			class="h-full w-full"
			controls
			playsinline
		></video>

		{#if videoStatus === 'idle'}
			<div class="absolute inset-0 flex flex-col items-center justify-center gap-4 bg-stone-900/80 backdrop-blur-sm">
				<button
					type="button"
					class="rounded-full bg-stone-50 px-6 py-2.5 text-sm font-medium text-stone-900 shadow transition hover:bg-white"
					onclick={startVideo}
				>
					▶ Stream via NPRPC
				</button>
				<p class="text-xs text-stone-400">Streams fragmented MP4 chunks over <code>server_stream&lt;binary&gt;</code></p>
			</div>
		{:else if videoStatus === 'loading'}
			<div class="absolute inset-0 flex items-center justify-center bg-stone-900/60">
				<p class="text-sm text-stone-300">Opening RPC stream…</p>
			</div>
		{:else if videoStatus === 'error'}
			<div class="absolute inset-0 flex flex-col items-center justify-center gap-3 bg-stone-900/90 p-6">
				<p class="text-sm font-medium text-rose-400">Stream error</p>
				<p class="max-w-xs text-center text-xs text-stone-400">{videoError}</p>
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

	{#if videoStatus === 'streaming' || videoStatus === 'done'}
		<p class="text-xs text-stone-500">
			{#if videoStatus === 'streaming'}
				Streaming… {(bytesReceived / 1024 / 1024).toFixed(1)} MB received
				{#if detectedMimeType} · <code>{detectedMimeType}</code>{/if}
			{:else}
				Done — {(bytesReceived / 1024 / 1024).toFixed(1)} MB · <code>{detectedMimeType}</code>
			{/if}
		</p>
	{/if}

	<details class="rounded-2xl border border-amber-900/10 bg-amber-50/40 p-4 text-sm">
		<summary class="cursor-pointer font-medium text-stone-800">How to prepare the video file</summary>
		<div class="mt-4 space-y-3 text-stone-700 leading-6">
			<p>
				The server reads a <strong>fragmented MP4</strong> from
				<code>/app/media/post-{postId}.fmp4</code>.
				A regular (non-fragmented) MP4 will not work with MSE because the browser
				cannot parse it incrementally.
			</p>
			<p>
				Generate a test clip or convert an existing file with <code>ffmpeg</code>.
				The critical flags are <code>frag_keyframe</code> (fragment at every keyframe) and
				<code>empty_moov</code> (include a moov box before any media data so the decoder
				can start immediately):
			</p>
			<pre class="overflow-x-auto rounded-xl bg-stone-950 p-4 text-xs leading-6 text-stone-300"
># Synthetic 30-second 1280×720 test clip
ffmpeg \
  -f lavfi -i testsrc=duration=30:size=1280x720:rate=25 \
  -f lavfi -i sine=frequency=440:duration=30 \
  -c:v libx264 -preset fast -c:a aac \
  -movflags frag_keyframe+empty_moov+default_base_moof \
  -f mp4 post-{postId}.fmp4

# Or re-encode an existing file
ffmpeg -i input.mp4 \
  -c:v libx264 -c:a aac \
  -movflags frag_keyframe+empty_moov+default_base_moof \
  -f mp4 post-{postId}.fmp4</pre>
			<p>
				Copy the result to <code>/app/media/</code> inside the running container and
				the server will pick it up automatically.
			</p>
		</div>
	</details>
</div>
