<!-- Device settings (GET/POST /api/settings), shown in a Popover from the status bar.
     Reading works in any state; saving only while idle — the device writes them to
     flash (decision E4: no queueing). -->
<script lang="ts">
	import { SAMPLE_RATES_HZ, type DeviceSettings } from '$lib/api';
	import type { DeviceController } from '$lib/device.svelte';
	import { errorText } from '$lib/format';
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

<h3>Device Settings <span class="muted">{dev.ssid ?? ''}</span></h3>
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

		<!-- Only firmware with the idle-sleep feature reports this key -->
		{#if saved && 'sleep_timeout_s' in saved}
			<span class="label">Sleep after</span>
			<Segmented label="Sleep after" bind:value={draft.sleep_timeout_s} options={[
				{ value: 300, label: '5 min' },
				{ value: 900, label: '15 min' },
				{ value: 1800, label: '30 min' },
				{ value: 0, label: 'Never' },
			]} />
			<p class="muted small">
				Idle time before the device goes into deep sleep. Recording, downloading or any change from the app counts as
				activity; the app just showing the device's status doesn't.
			</p>
		{/if}

		{#if saved && 'live_timeout_s' in saved}
			<span class="label">Live timeout</span>
			<Segmented label="Live timeout" bind:value={draft.live_timeout_s} options={[
				{ value: 60, label: '1 min' },
				{ value: 300, label: '5 min' },
				{ value: 600, label: '10 min' },
				{ value: 1800, label: '30 min' },
			]} />
			<p class="muted small">Live view ends after this long without activity. Just watching the live graph doesn't count.</p>
		{/if}

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
		{#if !dev.ready}
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

<div class="actions">
	{#if !draft && error}<button onclick={load}>Try again</button>{/if}
	<button onclick={onclose}>{dirty ? 'Cancel' : 'Close'}</button>
	<button class="primary" disabled={!dirty || saving || !dev.ready} onclick={save}>
		{saving ? 'Saving…' : 'Save'}
	</button>
</div>

<style>
	h3 {
		margin: 0;
		font-size: 16px;
	}
	h3 .muted {
		font-weight: normal;
		font-size: 13px;
		margin-left: 6px;
	}
	.actions {
		display: flex;
		justify-content: flex-end;
		gap: 10px;
	}
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
