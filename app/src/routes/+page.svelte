<script lang="ts">
	import { onMount } from 'svelte';
	import Graph from '$lib/components/Graph.svelte';
	import RecordingsList from '$lib/components/RecordingsList.svelte';
	import RecordPanel from '$lib/components/RecordPanel.svelte';
	import StatusBar from '$lib/components/StatusBar.svelte';
	import { DeviceController } from '$lib/device.svelte';
	import { errorText } from '$lib/format';
	import { localKey } from '$lib/recordings';
	import { inTauri } from '$lib/http';
	import { LocalStore, startupConfig } from '$lib/storage';

	let dev = $state<DeviceController | null>(null);
	let startError = $state<string | null>(null);
	let selected = $state<string | null>(null);

	onMount(() => {
		let ctl: DeviceController | null = null;
		LocalStore.open()
			.then(async (store) => {
				ctl = new DeviceController(store);
				const url = inTauri ? (await startupConfig()).device_url : null;
				if (url) ctl.setBaseUrl(url, false);
				dev = ctl;
				return ctl.start();
			})
			.catch((e) => (startError = errorText(e)));
		return () => ctl?.stop();
	});

	let compare = $state<string[]>([]);
	const MAX_TRACES = 8;

	// The graph's recordings: the selected one first, then the compared ones
	const shown = $derived.by(() => {
		const byKey = new Map((dev?.locals ?? []).map((l) => [localKey(l), l]));
		return [selected, ...compare].flatMap((k) => (k && byKey.has(k) ? [byKey.get(k)!] : []));
	});

	function removeTrace(key: string) {
		if (key === selected) {
			selected = compare[0] ?? null;
			compare = compare.slice(1);
		} else {
			compare = compare.filter((k) => k !== key);
		}
	}

	// A recording started from the app opens once it has downloaded
	$effect(() => {
		if (dev?.autoOpen) {
			selected = dev.autoOpen;
			dev.autoOpen = null;
		}
	});
</script>

{#if startError}
	<p class="banner danger fatal">The app couldn't start: {startError}</p>
{:else if dev}
	<div class="app">
		<StatusBar {dev} />

		{#if dev.connection === 'unreachable'}
			<div class="banner warn offline">
				<strong>Can't reach the device at {dev.baseUrl}.</strong>
				Join the WiFi network <strong>{dev.lastSsid ?? 'IRSensor-XXXX'}</strong> in your system's WiFi settings (the
				password is on the device: WiFi → Show SSID &amp; IP). If the device is asleep, hold its button for 2 s to wake it.
				Recordings already on this tablet are still available.
			</div>
		{/if}
		{#if dev.notice}
			<div class="banner {dev.notice.kind === 'error' ? 'danger' : ''} notice">
				<span>{dev.notice.text}</span>
				<button onclick={() => (dev!.notice = null)}>Dismiss</button>
			</div>
		{/if}

		<main>
			<aside>
				<RecordPanel {dev} />
				<RecordingsList {dev} bind:selected bind:compare maxTraces={MAX_TRACES} />
			</aside>
			<section class="viewer">
				{#if shown.length}
					<Graph recordings={shown} {dev} onremove={(r) => removeTrace(localKey(r))} />
				{:else}
					<div class="empty muted">
						<p>Select a recording on this tablet to see its graph, or use “+ Compare” to overlay several.</p>
						<p>New recordings are downloaded automatically when they finish.</p>
					</div>
				{/if}
			</section>
		</main>
	</div>
{/if}

<style>
	.app {
		display: flex;
		flex-direction: column;
		height: 100vh;
	}
	.offline,
	.notice {
		margin: 10px 16px 0;
	}
	.notice {
		display: flex;
		align-items: center;
		justify-content: space-between;
		gap: 12px;
	}
	.notice button {
		min-height: 34px;
	}
	main {
		flex: 1;
		min-height: 0;
		display: grid;
		grid-template-columns: minmax(330px, 400px) 1fr;
		gap: 16px;
		padding: 16px;
	}
	aside {
		display: flex;
		flex-direction: column;
		gap: 20px;
		min-height: 0;
	}
	.viewer {
		min-width: 0;
		min-height: 0;
		background: var(--panel);
		border: 1px solid var(--border);
		border-radius: 14px;
		padding: 14px;
	}
	.empty {
		height: 100%;
		display: flex;
		flex-direction: column;
		align-items: center;
		justify-content: center;
		text-align: center;
	}
	.empty p {
		margin: 4px;
	}
	.fatal {
		margin: 24px;
	}
</style>
