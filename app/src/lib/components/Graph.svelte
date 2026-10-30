<!--
  One or more recordings vs time (overlays, decision E6). The first recording is the
  main one: its name/tags/notes and facts are shown, and Export… exports it.
  Time comes from each header's actual rate (millihertz). Values are mV via each
  sidecar's calibration table only if every trace has one; otherwise all are raw
  counts. Traces align by start or by first rising edge, plus a manual offset each.
  Zoom and pan: wheel / trackpad, drag, two-finger pinch; double-click resets.
-->
<script lang="ts">
	import { untrack } from 'svelte';
	import { SvelteMap, SvelteSet } from 'svelte/reactivity';
	import 'uplot/dist/uPlot.min.css';
	import { firstRisingEdge, midThreshold, traceOffset, type AlignMode } from '$lib/align';
	import { Summary, valueAt, type Series } from '$lib/decimate';
	import type { DeviceController } from '$lib/device.svelte';
	import { duration, errorText, localTime, rate, utcTime } from '$lib/format';
	import { GraphView, type GraphTrace } from '$lib/graphView';
	import { screen } from '$lib/layout.svelte';
	import { durationS, parseIrec, sampleRateHz, type IrecFile } from '$lib/irec';
	import { allTags, displayName, usableCalibration, type LocalRecording } from '$lib/recordings';
	import ExportPanel from './ExportPanel.svelte';
	import Popover from './Popover.svelte';
	import RecordingMeta from './RecordingMeta.svelte';
	import Segmented from './Segmented.svelte';

	let {
		recordings,
		dev,
		onremove,
	}: { recordings: LocalRecording[]; dev: DeviceController; onremove: (r: LocalRecording) => void } = $props();

	// Colour-blind-safe (Okabe–Ito based), readable on the dark background
	const COLORS = ['#56b4e9', '#e69f00', '#2fc49a', '#f0e442', '#cc79a7', '#ff7a45', '#a6a8ff', '#bbbbbb'];

	interface Loaded {
		file: IrecFile;
		rateHz: number;
		durationS: number;
		/** First rising edge (s) at the min–max midpoint, for edge alignment */
		edgeS: number | null;
		/** Built on first use, per unit */
		summaries: { counts?: Summary; mv?: Summary };
	}

	// Parsed files by .irec name; kept while the recording stays on the graph
	let files = $state.raw(new Map<string, Loaded>());
	let errors = $state<Record<string, string>>({});
	let useMv = $state(true);
	let align = $state<AlignMode>('start');
	const hidden = new SvelteSet<string>();
	const manualMs = new SvelteMap<string, number>();
	let host = $state<HTMLDivElement>();
	let zoom = $state(1);
	let cursorT = $state<number | null>(null);
	let view: GraphView | null = null;
	let built = '';
	let exporting = $state<[number, number] | null | false>(false);
	let exportPanel = $state<ExportPanel>();

	const main = $derived(recordings[0]);
	/** Portrait / phone: name, tags, notes and facts fold away to give the chart room */
	const compact = $derived(screen.compact);
	let showDetails = $state(false);
	const overlay = $derived(recordings.length > 1);

	// Load files as recordings are added; drop ones that were removed
	$effect(() => {
		const wanted = recordings.map((r) => r.file);
		untrack(() => {
			const kept = new Map([...files].filter(([f]) => wanted.includes(f)));
			if (kept.size !== files.size) files = kept;
		});
		for (const r of recordings) {
			if (untrack(() => files.has(r.file) || r.file in errors)) continue;
			dev.local
				.readIrec(r)
				.then((bytes) => {
					const file = parseIrec(bytes);
					const rateHz = sampleRateHz(file.header);
					const edgeS = firstRisingEdge(file.samples, rateHz, midThreshold(file.samples));
					files = new Map(files).set(r.file, { file, rateHz, durationS: durationS(file.header), edgeS, summaries: {} });
				})
				.catch((e) => (errors = { ...errors, [r.file]: errorText(e) }));
		}
	});

	const luts = $derived(
		new Map(
			recordings.map((r) => {
				const l = files.get(r.file);
				const cal = l && usableCalibration(r, l.file.header.attenTenthsDb);
				return [r.file, cal ? Float32Array.from(cal.mv) : null] as const;
			}),
		),
	);
	const ready = $derived(recordings.length > 0 && recordings.every((r) => files.has(r.file)));
	const canMv = $derived(ready && recordings.every((r) => luts.get(r.file)));
	const inMv = $derived(useMv && canMv);
	const unit = $derived(inMv ? 'mV' : 'ADC counts');

	function series(r: LocalRecording): Series {
		const l = files.get(r.file)!;
		const lut = inMv ? luts.get(r.file)! : null;
		const k = inMv ? 'mv' : 'counts';
		l.summaries[k] ??= new Summary(l.file.samples, lut);
		return { samples: l.file.samples, rateHz: l.rateHz, lut, summary: l.summaries[k] };
	}

	const offsetOf = (r: LocalRecording) => traceOffset(align, files.get(r.file)?.edgeS ?? null, manualMs.get(r.file) ?? 0);

	function traces(): GraphTrace[] {
		return recordings.map((r, i) => ({
			series: series(r),
			offsetS: offsetOf(r),
			durationS: files.get(r.file)!.durationS,
			visible: !hidden.has(r.file),
			color: COLORS[i % COLORS.length],
		}));
	}

	// Which files are plotted, in which unit. A string, so edits that only give the
	// recordings new object identities (e.g. saving a note) don't rebuild the plot.
	const signature = $derived(ready ? recordings.map((r) => r.file).join('|') + unit : '');

	// (Re)build the plot when the set of traces or the unit changes
	$effect(() => {
		if (!signature || !host) return;
		const el = host;
		const g = untrack(() => new GraphView(el, traces(), unit, (z) => (zoom = z), (t) => (cursorT = t)));
		view = g;
		built = signature;
		return () => {
			g.destroy();
			if (view === g) view = null;
		};
	});

	// Offsets / visibility change the same plot; a new alignment shows everything
	let lastAlign: AlignMode = 'start';
	$effect(() => {
		if (!signature) return;
		const next = traces(); // tracks align, hidden, manualMs
		untrack(() => {
			if (!view || built !== signature) return;
			view.update(next, align !== lastAlign);
			lastAlign = align;
		});
	});

	function nudge(r: LocalRecording, ms: number) {
		manualMs.set(r.file, Math.round(((manualMs.get(r.file) ?? 0) + ms) * 1000) / 1000);
	}

	function toggle(r: LocalRecording) {
		if (hidden.has(r.file)) hidden.delete(r.file);
		else hidden.add(r.file);
	}

	/** The main recording's part of the visible window, in its own time, for export */
	function exportWindow(): [number, number] | null {
		const w = view?.window;
		const l = files.get(main.file);
		if (!w || !l) return null;
		const off = offsetOf(main);
		const a = Math.max(0, w[0] - off), b = Math.min(l.durationS, w[1] - off);
		return b > a ? [a, b] : null;
	}

	function fmtValue(v: number | null) {
		if (v === null) return '—';
		return inMv ? `${v.toFixed(1)} mV` : `${v}`;
	}

	const mainLoaded = $derived(files.get(main?.file ?? ''));
	const h = $derived(mainLoaded?.file.header);
	const overruns = $derived(recordings.filter((r) => files.get(r.file)?.file.header.overrun));
	const corrupt = $derived(recordings.filter((r) => files.get(r.file) && !files.get(r.file)!.file.crcOk));
	const noEdge = $derived(align === 'edge' ? recordings.filter((r) => files.get(r.file) && files.get(r.file)!.edgeS === null) : []);
	const names = (rs: LocalRecording[]) => rs.map((r) => displayName(r)).join(', ');
</script>

{#snippet meta()}
	<div class="meta">
		<RecordingMeta recording={main} suggestions={allTags(dev.locals)} onsave={(p) => dev.updateMeta(main, p)} />
	</div>
{/snippet}

<div class="graph">
	<div class="info" class:compact class:phone={screen.phone}>
		{#if compact}
			<button class="toggle" aria-expanded={showDetails} onclick={() => (showDetails = !showDetails)}>
				<strong class="title">{displayName(main)}</strong>
				<span class="muted">{showDetails ? 'Hide details' : 'Details'} <span class="chev" class:up={showDetails}>▾</span></span>
			</button>
		{:else}
			{@render meta()}
		{/if}
		<div class="tools">
			{#if canMv}
				<Segmented label="Units" fill={false} bind:value={useMv} options={[
					{ value: true, label: 'mV' },
					{ value: false, label: 'Counts' },
				]} />
			{/if}
			<button onclick={() => view?.reset()} disabled={zoom <= 1.0001}>{screen.phone ? 'Reset' : 'Reset zoom'}</button>
			<Popover
				open={exporting !== false && !!mainLoaded}
				onclose={() => {
					if (!exportPanel?.busy()) exporting = false;
				}}
				align="right"
				width={540}
				label="Export"
			>
				{#snippet trigger()}
					<button
						disabled={!mainLoaded}
						aria-expanded={exporting !== false}
						onclick={() => (exporting = exporting === false ? exportWindow() : false)}>Export…</button
					>
				{/snippet}
				{#if mainLoaded && exporting !== false}
					<ExportPanel
						bind:this={exportPanel}
						recording={main}
						file={mainLoaded.file}
						lut={luts.get(main.file) ?? null}
						window={exporting}
						dataDir={dev.local.root}
						onclose={() => (exporting = false)}
					/>
				{/if}
			</Popover>
		</div>
	</div>
	{#if compact && showDetails}
		{@render meta()}
	{/if}
	{#if !compact || showDetails}
	<div class="facts muted">
		<span title={utcTime(main.start_utc_us)}>{localTime(main.start_utc_us)}</span> · #{main.id} · {main.device_ssid}
		{#if h}
			· {duration(durationS(h) * 1000)} · {rate(h.sampleRateMhz)} · {h.sampleCount.toLocaleString()} samples ·
			{h.attenTenthsDb / 10} dB{h.compressed ? ' · compressed' : ''}
		{/if}
	</div>
	{/if}

	{#if overlay}
		<div class="align">
			<span class="muted">Align</span>
			<Segmented label="Align" fill={false} bind:value={align} options={[
				{ value: 'start', label: 'By start' },
				{ value: 'edge', label: 'By first edge' },
			]} />
		</div>
	{/if}

	{#if overruns.length}
		<p class="banner warn">
			<strong>Overrun{overlay ? ` in ${names(overruns)}` : ''}:</strong> samples were lost during
			{overruns.length > 1 ? 'these recordings' : 'this recording'}, so times within {overruns.length > 1 ? 'them' : 'it'} can't be trusted.
		</p>
	{/if}
	{#if corrupt.length}
		<p class="banner danger">
			<strong>CRC check failed{overlay ? ` for ${names(corrupt)}` : ''}:</strong> the data shown may be wrong.
		</p>
	{/if}
	{#if ready && !canMv}
		<p class="banner">
			{#if overlay}
				Not every recording here has a matching calibration table, so all are shown in raw ADC counts.
			{:else if main.calibration}
				The calibration table doesn't match this file's ADC attenuation, so raw ADC counts are shown.
			{:else}
				No calibration table for this recording (the chip has no eFuse calibration), so raw ADC counts are shown.
			{/if}
		</p>
	{/if}
	{#if noEdge.length}
		<p class="banner">No rising edge found in {names(noEdge)}; aligned by start instead.</p>
	{/if}
	{#each recordings.filter((r) => errors[r.file]) as r (r.file)}
		<p class="banner danger">Couldn't open {displayName(r)}: {errors[r.file]}</p>
	{/each}

	<div class="plot" bind:this={host}></div>

	<div class="legend">
		<span class="t muted">{cursorT === null ? 'Time —' : `t = ${cursorT.toFixed(4)} s`}</span>
		{#each recordings as r, i (r.file)}
			{@const l = files.get(r.file)}
			{@const off = manualMs.get(r.file) ?? 0}
			<div class="trace" class:off={hidden.has(r.file)}>
				<button class="swatch" style="--c: {COLORS[i % COLORS.length]}" aria-pressed={!hidden.has(r.file)}
					title={hidden.has(r.file) ? 'Show' : 'Hide'} onclick={() => toggle(r)}>
					<span class="dot"></span>
					<span class="name">{displayName(r)}</span>
					{#if l?.file.header.overrun}<span class="badge warn">overrun</span>{/if}
				</button>
				<span class="value">
					{l && cursorT !== null && !hidden.has(r.file) ? fmtValue(valueAt(series(r), cursorT - offsetOf(r))) : ''}
				</span>
				{#if overlay}
					<span class="offset" title="Manual time offset">
						<button aria-label="Shift earlier by 1 ms" onclick={() => nudge(r, -1)}>−</button>
						<input type="number" step="0.1" value={off} aria-label="Offset (ms) for {displayName(r)}"
							onchange={(e) => manualMs.set(r.file, Number((e.currentTarget as HTMLInputElement).value) || 0)} />
						<span class="muted">ms</span>
						<button aria-label="Shift later by 1 ms" onclick={() => nudge(r, 1)}>+</button>
					</span>
					<button class="remove" aria-label="Remove {displayName(r)} from the graph" onclick={() => onremove(r)}>✕</button>
				{/if}
			</div>
		{/each}
	</div>


	<p class="muted help" class:hide={screen.phone}>
		Scroll or pinch to zoom · drag to pan · double-click to reset{zoom > 1.0001 ? ` · ${zoom < 100 ? zoom.toFixed(1) : Math.round(zoom)}×` : ''}
	</p>
</div>

<style>
	.graph {
		display: flex;
		flex-direction: column;
		gap: 8px;
		height: 100%;
		min-height: 0;
	}
	.info {
		display: flex;
		align-items: flex-start;
		gap: 12px 16px;
	}
	.meta {
		flex: 1;
		min-width: 0;
	}
	.info.compact {
		align-items: center;
		flex-wrap: wrap;
	}
	/* Phones: the tools get their own full row, on one line */
	.info.phone .tools {
		flex: 1 1 100%;
		flex-wrap: nowrap;
		justify-content: space-between;
	}
	.info.phone .tools > :global(*) {
		flex: none;
	}
	.info.phone .tools :global(button) {
		padding: 0 12px;
	}
	.toggle {
		flex: 1 1 200px;
		min-width: 0;
		display: flex;
		align-items: baseline;
		gap: 10px;
		padding: 0;
		border: none;
		background: none;
		text-align: left;
	}
	.toggle .title {
		font-size: 16px;
		overflow: hidden;
		text-overflow: ellipsis;
		white-space: nowrap;
		min-width: 0;
	}
	.toggle .muted {
		flex: none;
		font-size: 13px;
	}
	.chev {
		display: inline-block;
	}
	.chev.up {
		transform: rotate(180deg);
	}
	.help.hide {
		display: none;
	}
	.tools {
		display: flex;
		gap: 8px;
		flex-wrap: wrap;
		justify-content: flex-end;
	}
	.facts {
		font-size: 13px;
	}
	.align {
		display: flex;
		align-items: center;
		gap: 10px;
	}
	.plot {
		flex: 1;
		min-height: 200px;
		overflow: hidden;
	}
	.legend {
		display: flex;
		flex-wrap: wrap;
		gap: 6px 14px;
		align-items: center;
		font-size: 13px;
	}
	.t {
		font-variant-numeric: tabular-nums;
		min-width: 120px;
	}
	.trace {
		display: flex;
		align-items: center;
		gap: 6px;
		padding-right: 4px;
		border: 1px solid var(--border);
		border-radius: var(--radius);
		background: var(--panel-2);
	}
	.trace.off {
		opacity: 0.5;
	}
	.trace button {
		min-height: 36px;
		padding: 0 8px;
		border: none;
		background: none;
	}
	.swatch {
		display: flex;
		align-items: center;
		gap: 6px;
	}
	.dot {
		width: 12px;
		height: 12px;
		border-radius: 3px;
		background: var(--c);
	}
	.trace.off .dot {
		background: none;
		border: 2px solid var(--c);
	}
	.name {
		max-width: 160px;
		overflow: hidden;
		text-overflow: ellipsis;
		white-space: nowrap;
	}
	.value {
		font-variant-numeric: tabular-nums;
		min-width: 70px;
	}
	.offset {
		display: flex;
		align-items: center;
		gap: 2px;
	}
	.offset input {
		width: 72px;
		min-height: 32px;
		padding: 0 6px;
		text-align: right;
	}
	.remove {
		color: var(--muted);
	}
	.help {
		margin: 0;
		font-size: 12px;
		text-align: center;
	}
	p.banner {
		margin: 0;
	}
	.plot :global(.u-over) {
		cursor: grab;
	}
	.plot :global(.u-over:active) {
		cursor: grabbing;
	}
</style>
