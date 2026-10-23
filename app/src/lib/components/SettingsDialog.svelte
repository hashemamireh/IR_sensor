<!-- Device settings (GET/POST /api/settings). Reading works in any state; saving
     only while idle — the device writes them to flash (decision E4: no queueing). -->
<script lang="ts">
	import { SAMPLE_RATES_HZ, type DeviceSettings } from '$lib/api';
	import type { DeviceController } from '$lib/device.svelte';
	import { errorText } from '$lib/format';
	import Dialog from './Dialog.svelte';
	import Segmented from './Segmented.svelte';

	let { dev, onclose }: { dev: DeviceController; onclose: () => void } = $props();

	let saved = $state<DeviceSettings | null>(null);
	let draft = $state<DeviceSettings | null>(null);
	let error = $state<string | null>(null);
	let saving = $state(false);

	async function load() {
		error = null;
		try {
			saved = await dev.loadSettings();
			draft = { ...saved };
		} catch (e) {
			error = `Couldn't read the settings: ${errorText(e)}`;
		}
	}
	load();

	const changes = $derived.by(() => {
		if (!saved || !draft) return {};
		const out: Partial<DeviceSettings> = {};
		for (const k of Object.keys(draft) as (keyof DeviceSettings)[]) {
			if (draft[k] !== saved[k]) (out as Record<string, unknown>)[k] = draft[k];
		}
		return out;
	});
	const dirty = $derived(Object.keys(changes).length > 0);

	async function save() {
		saving = true;
		error = null;
		try {
			saved = await dev.saveSettings(changes);
			draft = { ...saved };
		} catch (e) {
			error = `Not saved: ${errorText(e)}`;
		} finally {
			saving = false;
		}
	}
</script>

<Dialog title="Device settings — {dev.ssid ?? 'device'}" onclose={onclose}>
	{#if draft}
		<span class="label">Screen brightness</span>
		<Segmented label="Screen brightness" bind:value={draft.brightness} options={[
			{ value: 'low', label: 'Low' },
			{ value: 'medium', label: 'Medium' },
			{ value: 'high', label: 'High' },
		]} />

		<span class="label">Screen timeout</span>
		<Segmented label="Screen timeout" bind:value={draft.screen_timeout_s} options={[
			{ value: 30, label: '30 s' },
			{ value: 60, label: '1 min' },
			{ value: 300, label: '5 min' },
			{ value: 0, label: 'Never' },
		]} />

		<span class="label">Quick Start sample rate</span>
		<Segmented label="Quick Start sample rate" bind:value={draft.default_sample_rate_hz}
			options={SAMPLE_RATES_HZ.map((r) => ({ value: r as number, label: `${r / 1000} kHz` }))} />

		<span class="label">Quick Start duration</span>
		<Segmented label="Quick Start duration" bind:value={draft.default_duration_s} options={[
			{ value: 5, label: '5 s' },
			{ value: 10, label: '10 s' },
			{ value: 30, label: '30 s' },
			{ value: 60, label: '1 min' },
		]} />

		<p class="muted small">
			The Quick Start values are what the device's own Record → Quick Start uses after each boot or wake.
			They don't change the app's record settings.
		</p>
		{#if !dev.idle}
			<p class="banner warn small">
				{dev.online ? 'The device is busy. Settings can be saved once it is idle again.' : 'The device is offline.'}
			</p>
		{/if}
	{:else if !error}
		<p class="muted">Reading settings…</p>
	{/if}
	{#if error}
		<p class="banner danger small">{error}</p>
	{/if}

	{#snippet actions()}
		{#if !draft && error}<button onclick={load}>Try again</button>{/if}
		<button onclick={onclose}>{dirty ? 'Cancel' : 'Close'}</button>
		<button class="primary" disabled={!dirty || saving || !dev.idle} onclick={save}>
			{saving ? 'Saving…' : 'Save'}
		</button>
	{/snippet}
</Dialog>

<style>
	.label {
		font-size: 13px;
		color: var(--muted);
		margin-bottom: -8px;
	}
	.small {
		font-size: 13px;
		margin: 0;
	}
</style>
