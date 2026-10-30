<!--
  Live view (spec §3.6, §10.3 item 3): a scrolling strip chart of the device's 20 ms
  min/max buckets (band) and their mean (line), newest at the right, plus a big
  current reading (mean of the last 100 ms, like the OLED) and the peak over the
  window. mV through this connection's calibration table, else raw counts.
-->
<script lang="ts">
	import uPlot from 'uplot';
	import 'uplot/dist/uPlot.min.css';
	import type { DeviceController } from '$lib/device.svelte';
	import { screen } from '$lib/layout.svelte';
	import { countsToValue } from '$lib/live';
	import { recordSettings as rs } from '$lib/recordSettings.svelte';
	import Segmented from './Segmented.svelte';

	let { dev, onclose }: { dev: DeviceController; onclose: () => void } = $props();

	let windowS = $state(10);
	let host = $state<HTMLDivElement>();
	let busy = $state(false);

	const inMv = $derived(!!dev.calibrationLut);
	const unit = $derived(inMv ? 'mV' : 'counts');
	const value = $derived(countsToValue(dev.calibrationLut));

	// Readings, recomputed on every poll (liveVersion)
	const reading = $derived.by(() => {
		dev.liveVersion;
		const b = dev.liveBuffer;
		const cur = b.current();
		const pk = b.peak(windowS);
		return { now: cur === null ? null : value(cur), peak: pk === null ? null : value(pk), overruns: b.overruns };
	});

	const fmt = (v: number | null) =>
		v === null ? '—' : inMv ? v.toLocaleString(undefined, { maximumFractionDigits: 0 }) : Math.round(v).toLocaleString();

	// The chart: rebuilt when the unit or the element changes, fed on every poll
	let plot: uPlot | null = null;
	$effect(() => {
		if (!host) return;
		const el = host;
		const label = unit;
		const css = getComputedStyle(el);
		const c = (v: string) => css.getPropertyValue(v).trim();
		const accent = c('--accent');
		const axis = { stroke: c('--muted'), grid: { stroke: c('--border'), width: 1 }, ticks: { stroke: c('--border'), width: 1 } };
		const p = new uPlot(
			{
				width: Math.max(200, el.clientWidth),
				height: Math.max(150, el.clientHeight),
				scales: {
					x: { time: false, auto: false, range: () => [-windowS, 0] },
					y: {
						auto: true,
						range: (_u, min, max) => {
							const pad = Math.max(label === 'mV' ? 20 : 25, (max - min) * 0.1);
							return [Math.max(0, min - pad), max + pad];
						},
					},
				},
				axes: [
					{ ...axis, label: 'Seconds ago', labelSize: 20, values: (_u, ticks) => ticks.map((t) => `${-t}`) },
					{ ...axis, label, labelSize: 20, size: 64 },
				],
				series: [
					{},
					{ label: 'min', stroke: `${accent}55`, width: 1, points: { show: false } },
					{ label: 'max', stroke: `${accent}55`, width: 1, points: { show: false } },
					{ label: 'mean', stroke: accent, width: 2, points: { show: false } },
				],
				bands: [{ series: [2, 1], fill: `${accent}33` }],
				legend: { show: false },
				cursor: { show: false },
			},
			[[], [], [], []],
			el,
		);
		plot = p;
		const ro = new ResizeObserver(() => p.setSize({ width: Math.max(200, el.clientWidth), height: Math.max(150, el.clientHeight) }));
		ro.observe(el);
		return () => {
			ro.disconnect();
			p.destroy();
			if (plot === p) plot = null;
		};
	});

	let frame = 0;
	$effect(() => {
		dev.liveVersion;
		windowS;
		value;
		if (!frame) {
			frame = requestAnimationFrame(() => {
				frame = 0;
				plot?.setData(dev.liveBuffer.chart(windowS, value) as unknown as uPlot.AlignedData);
			});
		}
	});

	const ended = $derived(!dev.live && (dev.liveEnded === 'ended' || dev.liveEnded === 'record-refused'));
	const lost = $derived(!dev.online);
	const waiting = $derived(dev.online && !dev.live && dev.liveEnded === null);

	async function run(fn: () => Promise<unknown>) {
		busy = true;
		await fn();
		busy = false;
	}
</script>

<div class="live">
	<div class="head">
		<div class="title">
			<strong>Live view</strong>
			{#if dev.live}
				<span class="badge ok"><span class="dot"></span> live</span>
			{:else if ended || lost}
				<span class="badge">ended</span>
			{:else}
				<span class="badge">starting…</span>
			{/if}
			{#if reading.overruns > 0}
				<span class="badge warn" title="Samples the device dropped during this live session, e.g. while serving a download">
					{reading.overruns.toLocaleString()} samples dropped
				</span>
			{/if}
		</div>
		<span class="spacer"></span>
		<Segmented label="Window" fill={false} bind:value={windowS} options={[
			{ value: 10, label: '10 s' },
			{ value: 30, label: '30 s' },
		]} />
		<button class="record" disabled={!dev.ready || !rs.valid || busy} onclick={() => run(() => dev.startRecording(rs.durationS, rs.rateHz))}>
			<span class="recdot"></span> Record {rs.label}
		</button>
		{#if dev.live}
			<button disabled={busy} onclick={() => run(() => dev.stopLive())}>{screen.phone ? 'Stop' : 'Stop live view'}</button>
		{:else}
			<button onclick={onclose}>Close</button>
		{/if}
	</div>

	<div class="readings">
		<div class="big">
			<span class="label muted">{dev.live ? 'Now' : 'Last'}</span>
			<span class="row"><span class="value">{fmt(reading.now)}</span><span class="unit muted">{unit}</span></span>
		</div>
		<div class="big peak">
			<span class="label muted">Peak, last {windowS} s</span>
			<span class="row"><span class="value">{fmt(reading.peak)}</span><span class="unit muted">{unit}</span></span>
		</div>
		{#if !inMv}
			<p class="muted small">No calibration table from this device, so readings are raw ADC counts.</p>
		{/if}
	</div>

	<div class="chart-wrap">
		<div class="chart" bind:this={host}></div>
		{#if ended || lost}
			<div class="overlay">
				<div class="card">
					<strong>{lost ? 'Lost connection to the device' : 'Live view ended'}</strong>
					<p class="muted">
						{#if lost}
							The device stopped answering, so live view stopped. Once it is reachable again you can restart it.
						{:else if dev.liveEnded === 'record-refused'}
							The recording couldn't start (see the message above). The device ends live view before it checks a
							recording, so live view ended too.
						{:else}
							The device ended live view: after the live timeout without activity, from its own button, or from
							another app. The last readings are still shown.
						{/if}
					</p>
					<div class="actions">
						<button onclick={onclose}>Close</button>
						<button class="primary" disabled={!dev.idle || busy} onclick={() => run(() => dev.startLive())}>Restart live view</button>
					</div>
				</div>
			</div>
		{:else if waiting}
			<div class="overlay"><p class="muted">Waiting for the device to start live mode…</p></div>
		{/if}
	</div>
</div>

<style>
	.live {
		display: flex;
		flex-direction: column;
		gap: 12px;
		height: 100%;
		min-height: 0;
	}
	.head {
		display: flex;
		align-items: center;
		gap: 10px;
		flex-wrap: wrap;
	}
	.title {
		display: flex;
		align-items: center;
		gap: 10px;
		font-size: 16px;
	}
	.spacer {
		flex: 1;
	}
	.dot {
		display: inline-block;
		width: 8px;
		height: 8px;
		border-radius: 50%;
		background: var(--ok);
		animation: pulse 1.2s ease-in-out infinite;
	}
	.record {
		display: inline-flex;
		align-items: center;
		gap: 10px;
		font-weight: 600;
		background: color-mix(in srgb, var(--rec) 18%, var(--panel-2));
		border-color: color-mix(in srgb, var(--rec) 60%, transparent);
	}
	.recdot {
		width: 11px;
		height: 11px;
		border-radius: 50%;
		background: var(--rec);
	}
	.readings {
		display: flex;
		align-items: flex-end;
		gap: 40px;
		flex-wrap: wrap;
	}
	/* Label above, then number + unit side by side. Nothing here may shrink or wrap,
	   so a wider number pushes the unit along instead of running into it. */
	.big {
		display: flex;
		flex-direction: column;
		flex: none;
		white-space: nowrap;
	}
	.big .label {
		font-size: 13px;
	}
	.row {
		display: flex;
		align-items: baseline;
		gap: 8px;
	}
	.big .value {
		font-size: 56px;
		font-weight: 600;
		line-height: 1.05;
		font-variant-numeric: tabular-nums;
		/* Room for "2,573" so the unit doesn't jump about as the reading changes */
		min-width: 5ch;
		text-align: right;
	}
	.big.peak .value {
		font-size: 32px;
		color: var(--muted);
	}
	.unit {
		flex: none;
		font-size: 16px;
	}
	.small {
		font-size: 13px;
		margin: 0;
	}
	.chart-wrap {
		position: relative;
		flex: 1;
		min-height: 200px;
	}
	.chart {
		position: absolute;
		inset: 0;
		overflow: hidden;
	}
	.overlay {
		position: absolute;
		inset: 0;
		display: flex;
		align-items: center;
		justify-content: center;
		background: color-mix(in srgb, var(--panel) 55%, transparent);
	}
	.card {
		max-width: 420px;
		padding: 18px 20px;
		display: flex;
		flex-direction: column;
		gap: 8px;
		background: var(--panel);
		border: 1px solid var(--border);
		border-radius: 14px;
		box-shadow: 0 14px 36px rgb(0 0 0 / 0.5);
	}
	.card p {
		margin: 0;
	}
	.actions {
		display: flex;
		justify-content: flex-end;
		gap: 10px;
		margin-top: 6px;
	}
	@keyframes pulse {
		50% {
			opacity: 0.3;
		}
	}
	/* Phones (layout.svelte.ts): smaller numbers so both readings fit one row */
	@media (max-width: 599.98px) {
		.readings {
			gap: 24px;
		}
		.big .value {
			font-size: 42px;
		}
		.big.peak .value {
			font-size: 26px;
		}
	}
</style>
