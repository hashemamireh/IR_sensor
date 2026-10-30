<script lang="ts">
	import { SAMPLE_RATES_HZ } from '$lib/api';
	import type { DeviceController } from '$lib/device.svelte';
	import { CUSTOM, PRESETS_S, recordSettings as rs } from '$lib/recordSettings.svelte';
	import Segmented from './Segmented.svelte';

	let { dev }: { dev: DeviceController } = $props();

	const rec = $derived(dev.online ? dev.status?.recording : undefined);
	const devState = $derived(dev.online ? dev.status?.state : undefined);
	let starting = $state(false);

	async function start() {
		starting = true;
		await dev.startRecording(rs.durationS, rs.rateHz);
		starting = false;
	}
</script>

<section>
	{#if devState === 'recording' && rec}
		<div class="live">
			<div class="row">
				<span class="recdot"></span>
				<strong>Recording #{rec.id}</strong>
				<span class="muted num">{rec.elapsed_s} / {rec.total_s} s</span>
			</div>
			<div class="bar"><div style="width: {Math.min(100, (100 * rec.elapsed_s) / rec.total_s)}%"></div></div>
			<button class="danger stop" onclick={() => dev.stopRecording()}>Stop recording</button>
		</div>
	{:else if devState === 'saving'}
		<div class="live">
			<strong>Saving on the device…</strong>
			<div class="bar busy"><div></div></div>
		</div>
	{:else if devState === 'compressing' && dev.status?.compress}
		{@const c = dev.status.compress}
		<div class="live">
			<strong>Compressing on the device… {c.done} / {c.total}</strong>
			<div class="bar"><div style="width: {c.total ? (100 * c.done) / c.total : 0}%"></div></div>
		</div>
	{:else}
		<span class="label">Sample rate</span>
		<Segmented
			label="Sample rate"
			bind:value={rs.rateHz}
			options={SAMPLE_RATES_HZ.map((r) => ({ value: r as number, label: `${r / 1000} kHz` }))}
		/>

		<span class="label">Duration</span>
		<Segmented
			label="Duration"
			bind:value={rs.preset}
			options={[
				...PRESETS_S.filter((s) => s <= rs.cap).map((s) => ({ value: s, label: s < 60 ? `${s} s` : `${s / 60} min` })),
				{ value: CUSTOM, label: 'Custom' },
			]}
		/>
		{#if rs.preset === CUSTOM}
			<div class="custom">
				<input type="number" min="1" max={rs.cap} step="1" bind:value={rs.customS} inputmode="numeric" />
				<span class="muted">seconds (1–{rs.cap})</span>
			</div>
		{/if}

		<button class="record" disabled={!dev.ready || !rs.valid || starting} onclick={start}>
			<span class="recdot"></span> Record {rs.label}
		</button>
		{#if devState === 'live'}
			<p class="muted hint">Recording ends live view on the device.</p>
		{:else if dev.online && !dev.ready}
			<p class="muted hint">The device is busy.</p>
		{/if}
	{/if}
</section>

<style>
	section {
		display: flex;
		flex-direction: column;
		gap: 8px;
	}
	.label {
		font-size: 13px;
		color: var(--muted);
		margin-top: 4px;
	}
	.custom {
		display: flex;
		align-items: center;
		gap: 10px;
	}
	.custom input {
		width: 110px;
	}
	.record {
		margin-top: 10px;
		min-height: 56px;
		font-size: 17px;
		font-weight: 600;
		background: color-mix(in srgb, var(--rec) 18%, var(--panel-2));
		border-color: color-mix(in srgb, var(--rec) 60%, transparent);
		display: flex;
		align-items: center;
		justify-content: center;
		gap: 10px;
	}
	.recdot {
		width: 12px;
		height: 12px;
		border-radius: 50%;
		background: var(--rec);
		flex: none;
	}
	.live {
		display: flex;
		flex-direction: column;
		gap: 12px;
	}
	.live .recdot {
		animation: pulse 1.2s ease-in-out infinite;
	}
	.row {
		display: flex;
		align-items: center;
		gap: 10px;
	}
	.num {
		margin-left: auto;
		font-variant-numeric: tabular-nums;
	}
	.bar {
		height: 12px;
		border-radius: 6px;
		background: var(--bg);
		overflow: hidden;
	}
	.bar > div {
		height: 100%;
		background: var(--rec);
		transition: width 0.9s linear;
	}
	.bar.busy > div {
		width: 30%;
		background: var(--accent);
		animation: slide 1.2s ease-in-out infinite alternate;
	}
	.stop {
		min-height: 52px;
	}
	.hint {
		margin: 0;
		font-size: 13px;
	}
	@keyframes pulse {
		50% {
			opacity: 0.25;
		}
	}
	@keyframes slide {
		from {
			margin-left: 0;
		}
		to {
			margin-left: 70%;
		}
	}
</style>
