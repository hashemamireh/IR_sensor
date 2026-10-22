<!--
  One recording vs time. The time axis comes from the actual rate in the header
  (millihertz); values are mV via the sidecar's calibration table, else raw counts.
  Zoom and pan: wheel / trackpad, drag, two-finger pinch; double-click resets.
  uPlot only ever gets the decimated visible window (decimate.ts).
-->
<script lang="ts">
	import 'uplot/dist/uPlot.min.css';
	import { GraphView } from '$lib/graphView';
	import { duration, errorText, localTime, rate, utcTime } from '$lib/format';
	import { durationS, parseIrec, sampleRateHz, type IrecFile } from '$lib/irec';
	import { usableCalibration, type LocalRecording } from '$lib/recordings';
	import type { LocalStore } from '$lib/storage';

	let { recording, store }: { recording: LocalRecording; store: LocalStore } = $props();

	let file = $state.raw<IrecFile | null>(null);
	let error = $state<string | null>(null);
	let useMv = $state(true);
	let host = $state<HTMLDivElement>();
	let zoom = $state(1);
	let view: GraphView | null = null;

	const lut = $derived.by(() => {
		if (!file) return null;
		const cal = usableCalibration(recording, file.header.attenTenthsDb);
		return cal ? Float32Array.from(cal.mv) : null;
	});
	const inMv = $derived(useMv && !!lut);

	// Load whenever the selected recording changes
	$effect(() => {
		const r = recording;
		let stale = false;
		file = null;
		error = null;
		store
			.readIrec(r)
			.then((bytes) => {
				if (!stale) file = parseIrec(bytes);
			})
			.catch((e) => {
				if (!stale) error = errorText(e);
			});
		return () => {
			stale = true;
		};
	});

	// (Re)build the plot when the data or the unit changes
	$effect(() => {
		if (!file || !host) return;
		const g = new GraphView(
			host,
			{ samples: file.samples, rateHz: sampleRateHz(file.header), lut: inMv ? lut : null },
			durationS(file.header),
			inMv ? 'mV' : 'ADC counts',
			(z) => (zoom = z),
		);
		view = g;
		return () => g.destroy();
	});
	const h = $derived(file?.header);
</script>

<div class="graph">
	<div class="info">
		<div class="title">
			<strong>#{recording.id}</strong>
			<span title={utcTime(recording.start_utc_us)}>{localTime(recording.start_utc_us)}</span>
			<span class="muted">{recording.device_ssid}</span>
		</div>
		{#if h}
			<div class="facts muted">
				{duration(durationS(h) * 1000)} · {rate(h.sampleRateMhz)} · {h.sampleCount.toLocaleString()} samples ·
				{h.attenTenthsDb / 10} dB{h.compressed ? ' · compressed' : ''}
			</div>
		{/if}
		<span class="spacer"></span>
		{#if lut}
			<div class="units" role="radiogroup" aria-label="Units">
				<button class:on={useMv} onclick={() => (useMv = true)}>mV</button>
				<button class:on={!useMv} onclick={() => (useMv = false)}>Counts</button>
			</div>
		{/if}
		<button onclick={() => view?.reset()} disabled={zoom <= 1.0001}>Reset zoom</button>
	</div>

	{#if h?.overrun}
		<p class="banner warn">
			<strong>Overrun:</strong> samples were lost during this recording, so times within it can't be trusted.
		</p>
	{/if}
	{#if file && !file.crcOk}
		<p class="banner danger"><strong>CRC check failed:</strong> this file is corrupt; the data shown may be wrong.</p>
	{/if}
	{#if file && !lut}
		<p class="banner">
			{recording.calibration
				? "The calibration table doesn't match this file's ADC attenuation, so raw ADC counts are shown."
				: 'No calibration table for this recording (the chip has no eFuse calibration), so raw ADC counts are shown.'}
		</p>
	{/if}
	{#if error}
		<p class="banner danger">Couldn't open this recording: {error}</p>
	{/if}

	<div class="plot" bind:this={host}></div>
	<p class="muted help">Scroll or pinch to zoom · drag to pan · double-click to reset{zoom > 1.0001 ? ` · ${zoom < 100 ? zoom.toFixed(1) : Math.round(zoom)}×` : ''}</p>
</div>

<style>
	.graph {
		display: flex;
		flex-direction: column;
		gap: 10px;
		height: 100%;
		min-height: 0;
	}
	.info {
		display: flex;
		align-items: center;
		gap: 12px 16px;
		flex-wrap: wrap;
	}
	.title {
		display: flex;
		gap: 10px;
		font-size: 16px;
	}
	.facts {
		font-size: 13px;
	}
	.spacer {
		flex: 1;
	}
	.units {
		display: flex;
	}
	.units button {
		border-radius: 0;
	}
	.units button:first-child {
		border-radius: var(--radius) 0 0 var(--radius);
	}
	.units button:last-child {
		border-radius: 0 var(--radius) var(--radius) 0;
		border-left: none;
	}
	.units button.on {
		background: color-mix(in srgb, var(--accent) 22%, var(--panel-2));
		border-color: var(--accent);
	}
	.plot {
		flex: 1;
		min-height: 200px;
		overflow: hidden;
	}
	.help {
		margin: 0;
		font-size: 12px;
		text-align: center;
	}
	p.banner {
		margin: 0;
	}
	/* uPlot in the dark theme */
	.plot :global(.u-legend) {
		color: var(--text);
		font-size: 13px;
	}
	.plot :global(.u-over) {
		cursor: grab;
	}
	.plot :global(.u-over:active) {
		cursor: grabbing;
	}
</style>
