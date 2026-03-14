<script lang="ts">
	import { page } from '$app/state';
	import ShellHero from '$lib/components/ShellHero.svelte';
	import VideoPlayer from '$lib/components/VideoPlayer.svelte';
	import ShakaPlayer from '$lib/components/ShakaPlayer.svelte';

	const postId = $derived(BigInt(page.params.id ?? '0'));

	let activeTab = $state<'raw' | 'dash'>('dash');
</script>

<div class="space-y-6">
	<ShellHero
		badge="Video player"
		title={`Post #${postId} — streaming demo`}
		description="Two players side by side: the raw fMP4 streamer and the Shaka DASH player — both backed by the same NPRPC server_stream<binary> with credit-based backpressure."
	/>

	<!-- Tab switcher -->
	<div class="glass-card flex gap-2 p-3">
		<button
			type="button"
			class="rounded-full px-5 py-2 text-sm font-medium transition {activeTab === 'dash'
				? 'bg-stone-950 text-stone-50'
				: 'text-stone-600 hover:bg-white/80'}"
			onclick={() => (activeTab = 'dash')}
		>
			Shaka DASH
		</button>
		<button
			type="button"
			class="rounded-full px-5 py-2 text-sm font-medium transition {activeTab === 'raw'
				? 'bg-stone-950 text-stone-50'
				: 'text-stone-600 hover:bg-white/80'}"
			onclick={() => (activeTab = 'raw')}
		>
			Raw fMP4 stream
		</button>
	</div>

	<div class="glass-card p-7">
		{#if activeTab === 'dash'}
			<ShakaPlayer {postId} />
		{:else}
			<VideoPlayer {postId} />
		{/if}
	</div>
</div>
