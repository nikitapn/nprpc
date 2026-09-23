// Video island — MediaService.OpenPostVideo, a server_stream<binary> fed into
// a MediaSource SourceBuffer.
//
// Mounts on `[data-nprpc-video]`, which post.mustache emits with the post id.
// The streaming logic is a direct port of the former Svelte component; only the
// rendering changed.

import { formatError, streamPostVideo } from '../lib/live-blog-rpc';
import { clear, el, mountAll } from './dom';

// Maximum seconds to buffer ahead of the current playback position. Past this
// we stop pulling chunks — and so stop sending window-update credits — until
// playback catches up.
const MAX_BUFFER_AHEAD_S = 30;

/// Scan the moov init segment for an avcC box and return the exact codec
/// string, e.g. "avc1.640028" for H.264 High 4.0. Null if not found.
///
/// Guessing here is not harmless: a wrong profile string makes Chrome fire a
/// SourceBuffer error on the first append.
function detectAvcCodec(data: Uint8Array): string | null {
	for (let i = 0; i < data.length - 10; i++) {
		// 'avcC'
		if (data[i] === 0x61 && data[i + 1] === 0x76 && data[i + 2] === 0x63 && data[i + 3] === 0x43) {
			// avcDecoderConfigurationRecord follows the 4-byte box type:
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
	return new Promise((resolve) => sb.addEventListener('updateend', () => resolve(), { once: true }));
}

function removeBuffer(sb: SourceBuffer, start: number, end: number): Promise<void> {
	return new Promise<void>((resolve) => {
		sb.addEventListener('updateend', () => resolve(), { once: true });
		sb.remove(start, end);
	});
}

function appendChunk(sb: SourceBuffer, data: Uint8Array): Promise<void> {
	return new Promise<void>((resolve, reject) => {
		const onEnd = () => resolve();
		// SourceBuffer fires a plain Event with no detail; the real error is on
		// the video element.
		const onErr = () => reject(new Error('SourceBuffer append failed'));
		sb.addEventListener('updateend', onEnd, { once: true });
		sb.addEventListener('error', onErr, { once: true });
		try {
			sb.appendBuffer(data as BufferSource);
		} catch (e) {
			sb.removeEventListener('updateend', onEnd);
			sb.removeEventListener('error', onErr);
			reject(e);
		}
	});
}

function mediaErrorName(code: number): string {
	return (
		['', 'MEDIA_ERR_ABORTED', 'MEDIA_ERR_NETWORK', 'MEDIA_ERR_DECODE', 'MEDIA_ERR_SRC_NOT_SUPPORTED'][
			code
		] ?? `MediaError(${code})`
	);
}

function setupVideo(root: HTMLElement, postId: bigint): void {
	clear(root);

	const video = el('video', { class: 'h-full w-full', controls: true, playsinline: true });

	const startButton = el('button', {
		type: 'button',
		class:
			'rounded-full bg-stone-50 px-6 py-2.5 text-sm font-medium text-stone-900 shadow transition hover:bg-white'
	}, ['▶ Stream via NPRPC']);

	const idleOverlay = el('div', {
		class:
			'absolute inset-0 flex flex-col items-center justify-center gap-4 bg-stone-900/80 backdrop-blur-sm'
	}, [
		startButton,
		el('p', { class: 'text-xs text-stone-400' }, ['Streams fragmented MP4 over server_stream<binary>'])
	]);

	const loadingOverlay = el('div', {
		class: 'absolute inset-0 flex items-center justify-center bg-stone-900/60',
		hidden: true
	}, [el('p', { class: 'text-sm text-stone-300' }, ['Opening RPC stream…'])]);

	const errorText = el('p', { class: 'max-w-xs text-center text-xs text-stone-400' });
	const retryButton = el('button', {
		type: 'button',
		class:
			'mt-2 rounded-full border border-stone-600 px-4 py-1.5 text-xs text-stone-300 transition hover:bg-stone-800'
	}, ['Retry']);

	const errorOverlay = el('div', {
		class: 'absolute inset-0 flex flex-col items-center justify-center gap-3 bg-stone-900/90 p-6',
		hidden: true
	}, [el('p', { class: 'text-sm font-medium text-rose-400' }, ['Stream error']), errorText, retryButton]);

	const progress = el('p', { class: 'text-xs text-stone-500', hidden: true });

	root.append(
		el('div', { class: 'space-y-4' }, [
			el('p', { class: 'eyebrow' }, ['Server stream — MSE video']),
			el('p', { class: 'mt-3 text-sm leading-6 text-stone-600' }, [
				'Calls ',
				el('code', {}, ['OpenPostVideo(post_id)']),
				' and feeds each chunk into a MediaSource SourceBuffer. The server must provide a fragmented MP4 at ',
				el('code', {}, [`/app/media/post-${postId}.fmp4`]),
				'.'
			]),
			el('div', {
				class:
					'relative aspect-video overflow-hidden rounded-[22px] border border-amber-900/10 bg-stone-900'
			}, [video, idleOverlay, loadingOverlay, errorOverlay]),
			progress
		])
	);

	let bytesReceived = 0;
	let running = false;

	function show(state: 'idle' | 'loading' | 'streaming' | 'done' | 'error'): void {
		idleOverlay.hidden = state !== 'idle';
		loadingOverlay.hidden = state !== 'loading';
		errorOverlay.hidden = state !== 'error';
		progress.hidden = state !== 'streaming' && state !== 'done';
	}

	function reportProgress(mimeType: string, done: boolean): void {
		const mb = (bytesReceived / 1024 / 1024).toFixed(1);
		progress.textContent = done
			? `Done — ${mb} MB · ${mimeType}`
			: `Streaming… ${mb} MB received${mimeType ? ` · ${mimeType}` : ''}`;
	}

	function bufferedAhead(sb: SourceBuffer): number {
		const t = video.currentTime;
		for (let i = 0; i < sb.buffered.length; i++) {
			if (sb.buffered.start(i) <= t + 0.1 && sb.buffered.end(i) > t) {
				return sb.buffered.end(i) - t;
			}
		}
		return 0;
	}

	function waitForTimeUpdate(): Promise<void> {
		return new Promise<void>((resolve) =>
			video.addEventListener('timeupdate', () => resolve(), { once: true })
		);
	}

	async function waitForBufferSpace(sb: SourceBuffer): Promise<void> {
		while (bufferedAhead(sb) >= MAX_BUFFER_AHEAD_S) {
			await waitForTimeUpdate();
		}
	}

	async function safeAppend(sb: SourceBuffer, data: Uint8Array): Promise<void> {
		await waitUpdateEnd(sb);
		try {
			await appendChunk(sb, data);
		} catch (e) {
			if (e instanceof DOMException && e.name === 'QuotaExceededError') {
				const t = video.currentTime;
				if (sb.buffered.length > 0 && t > 30) {
					await removeBuffer(sb, 0, t - 30);
					await waitUpdateEnd(sb);
					await appendChunk(sb, data);
				} else {
					throw new Error(
						`QuotaExceededError at ${t.toFixed(1)}s — not enough played-back range to evict`
					);
				}
			} else if (e instanceof DOMException) {
				throw new Error(`SourceBuffer ${e.name}: ${e.message}`);
			} else {
				throw e;
			}
		}
	}

	function fail(message: string): void {
		errorText.textContent = message;
		show('error');
		running = false;
	}

	async function start(): Promise<void> {
		if (running) return;
		running = true;

		if (!('MediaSource' in window)) {
			fail('Media Source Extensions are not supported in this browser.');
			return;
		}

		show('loading');
		bytesReceived = 0;

		try {
			// Open the stream before creating the SourceBuffer: the first chunk
			// carries the full moov box, which is what names the exact codec.
			const stream = await streamPostVideo(postId);
			const iter = stream[Symbol.asyncIterator]();

			const first = await iter.next();
			if (first.done || !first.value) {
				fail(`Server returned an empty stream — check that /app/media/post-${postId}.fmp4 exists.`);
				return;
			}

			const initChunk = first.value;
			const avcCodec = detectAvcCodec(initChunk);
			const mimeType = avcCodec ? `video/mp4; codecs="${avcCodec},mp4a.40.2"` : 'video/mp4';

			if (!MediaSource.isTypeSupported(mimeType)) {
				fail(`Codec not supported in this browser: ${mimeType}`);
				return;
			}

			const mediaSource = new MediaSource();
			const objectUrl = URL.createObjectURL(mediaSource);
			video.src = objectUrl;
			void video.play().catch(() => {});

			await new Promise<void>((resolve) =>
				mediaSource.addEventListener('sourceopen', () => resolve(), { once: true })
			);

			const sourceBuffer = mediaSource.addSourceBuffer(mimeType);
			show('streaming');

			await safeAppend(sourceBuffer, initChunk);
			bytesReceived += initChunk.byteLength;
			reportProgress(mimeType, false);

			for (;;) {
				await waitForBufferSpace(sourceBuffer);
				const next = await iter.next();
				if (next.done || !next.value) break;
				await safeAppend(sourceBuffer, next.value);
				bytesReceived += next.value.byteLength;
				reportProgress(mimeType, false);
			}

			await waitUpdateEnd(sourceBuffer);
			mediaSource.endOfStream();
			reportProgress(mimeType, true);
			show('done');
			running = false;
		} catch (e) {
			// video.error carries the real cause when the failure came from the
			// decoder rather than the stream.
			const mediaErr = video.error;
			fail(
				mediaErr
					? `${mediaErrorName(mediaErr.code)}: ${mediaErr.message || '(no message)'}`
					: formatError(e)
			);
		}
	}

	function reset(): void {
		running = false;
		bytesReceived = 0;
		const old = video.src;
		video.removeAttribute('src');
		video.load();
		if (old.startsWith('blob:')) URL.revokeObjectURL(old);
		show('idle');
	}

	startButton.addEventListener('click', () => void start());
	retryButton.addEventListener('click', () => reset());

	show('idle');
}

export function mountVideo(): void {
	mountAll('[data-nprpc-video]', setupVideo);
}
