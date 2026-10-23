// uPlot wrapper for one or more recordings on a shared time axis: owns the visible
// window, feeds uPlot each trace's decimated samples in it, and handles wheel, drag
// and pinch itself (uPlot's own drag-to-select zoom is off — it doesn't do touch).
// Traces may have different rates; uPlot.join aligns them, with gaps spanned.
import uPlot from 'uplot';
import { decimate, type Series } from './decimate';

export interface GraphTrace {
	series: Series;
	/** Where the recording's t = 0 sits on the shared axis (s) */
	offsetS: number;
	durationS: number;
	visible: boolean;
	color: string;
}

type Data = [Float64Array, Float64Array];

export class GraphView {
	private plot: uPlot;
	private t0 = 0;
	private t1 = 1;
	private lo = 0;
	private hi = 1;
	private frame = 0;
	private ro: ResizeObserver;
	private pointers = new Map<number, number>();
	private gesture: { t0: number; t1: number; xs: Map<number, number> } | null = null;
	private minSpan = 1e-4;

	constructor(
		private host: HTMLDivElement,
		private traces: GraphTrace[],
		unit: string,
		private onZoom: (z: number) => void,
		/** Time (s, shared axis) under the pointer, or null when it leaves */
		private onCursor: (t: number | null) => void,
	) {
		const css = getComputedStyle(host);
		const c = (v: string) => css.getPropertyValue(v).trim();
		const axis = { stroke: c('--muted'), grid: { stroke: c('--border'), width: 1 }, ticks: { stroke: c('--border'), width: 1 } };
		this.plot = new uPlot(
			{
				...this.size(),
				scales: {
					x: { time: false, auto: false },
					y: {
						auto: true,
						range: (_u, min, max) => {
							const pad = Math.max(unit === 'mV' ? 2 : 3, (max - min) * 0.06);
							return [min - pad, max + pad];
						},
					},
				},
				axes: [
					{ ...axis, label: 'Time (s)', labelSize: 20 },
					{ ...axis, label: unit, labelSize: 20, size: 64 },
				],
				series: [
					{},
					...traces.map((t) => ({ stroke: t.color, width: 1, show: t.visible, spanGaps: true, points: { show: false } })),
				],
				// The page draws its own legend with live values (Graph.svelte)
				legend: { show: false },
				cursor: { drag: { x: false, y: false, setScale: false }, points: { show: false } },
				hooks: {
					setCursor: [
						(u) => {
							const left = u.cursor.left;
							this.onCursor(left == null || left < 0 ? null : u.posToVal(left, 'x'));
						},
					],
				},
			},
			[[], ...traces.map(() => [])] as unknown as uPlot.AlignedData,
			host,
		);
		this.applyTraces(true);
		this.attachGestures();
		this.ro = new ResizeObserver(() => {
			this.plot.setSize(this.size());
			this.render();
		});
		this.ro.observe(host);
	}

	destroy() {
		cancelAnimationFrame(this.frame);
		this.ro.disconnect();
		this.plot.destroy();
	}

	/** New offsets or visibility for the same traces; `reset` shows everything */
	update(traces: GraphTrace[], reset = false) {
		this.traces = traces;
		this.applyTraces(reset);
	}

	private applyTraces(reset: boolean) {
		const shown = this.traces.filter((t) => t.visible);
		const span = shown.length ? shown : this.traces;
		this.lo = Math.min(...span.map((t) => t.offsetS));
		this.hi = Math.max(...span.map((t) => t.offsetS + t.durationS));
		if (!(this.hi > this.lo)) this.hi = this.lo + 1;
		// Never zoom in past ~10 samples of the fastest trace across the plot
		const fastest = Math.max(...this.traces.map((t) => t.series.rateHz));
		this.minSpan = Math.min(this.hi - this.lo, Math.max(10 / fastest, 1e-4));
		this.traces.forEach((t, i) => {
			if (this.plot.series[i + 1].show !== t.visible) this.plot.setSeries(i + 1, { show: t.visible });
		});
		if (reset) this.setView(this.lo, this.hi);
		else this.setView(this.t0, this.t1);
	}

	private size() {
		return { width: Math.max(200, this.host.clientWidth), height: Math.max(150, this.host.clientHeight) };
	}

	/** The visible window on the shared axis (s), or null when everything is shown */
	get window(): [number, number] | null {
		return this.t1 - this.t0 < (this.hi - this.lo) * 0.9999 ? [this.t0, this.t1] : null;
	}

	reset() {
		this.setView(this.lo, this.hi);
	}

	private setView(t0: number, t1: number) {
		const full = this.hi - this.lo;
		const span = Math.min(Math.max(t1 - t0, this.minSpan), full);
		t0 = Math.min(Math.max(this.lo, t0), this.hi - span);
		this.t0 = t0;
		this.t1 = t0 + span;
		this.onZoom(full / span);
		this.schedule();
	}

	private schedule() {
		if (!this.frame) this.frame = requestAnimationFrame(() => this.render());
	}

	private render() {
		this.frame = 0;
		const columns = this.plot.over.clientWidth || this.plot.width;
		const tables: Data[] = this.traces.map((t) => {
			if (!t.visible) return [new Float64Array(0), new Float64Array(0)];
			const [xs, ys] = decimate(t.series, this.t0 - t.offsetS, this.t1 - t.offsetS, columns);
			if (t.offsetS) for (let i = 0; i < xs.length; i++) xs[i] += t.offsetS;
			return [xs, ys];
		});
		const data =
			tables.length === 1
				? tables[0]
				: uPlot.join(tables.map((t) => [Array.from(t[0]), Array.from(t[1])]) as uPlot.AlignedData[]);
		this.plot.batch(() => {
			this.plot.setData(data as unknown as uPlot.AlignedData, false);
			this.plot.setScale('x', { min: this.t0, max: this.t1 });
		});
	}

	private attachGestures() {
		const over = this.plot.over;
		over.style.touchAction = 'none';
		const width = () => over.clientWidth || 1;
		const localX = (clientX: number) => clientX - over.getBoundingClientRect().left;

		over.addEventListener(
			'wheel',
			(e) => {
				e.preventDefault();
				const span = this.t1 - this.t0;
				const scale = e.deltaMode === 1 ? 33 : 1; // lines → px
				if (!e.ctrlKey && Math.abs(e.deltaX) > Math.abs(e.deltaY)) {
					const dt = ((e.deltaX * scale) / width()) * span;
					this.setView(this.t0 + dt, this.t1 + dt);
				} else {
					// ctrlKey: a macOS trackpad pinch arrives as a ctrl+wheel with small deltas
					const factor = Math.exp(e.deltaY * scale * (e.ctrlKey ? 0.01 : 0.0015));
					const f = localX(e.clientX) / width();
					const anchor = this.t0 + f * span;
					const next = span * factor;
					this.setView(anchor - f * next, anchor + (1 - f) * next);
				}
			},
			{ passive: false },
		);

		// Drag to pan; two fingers keep the times under them pinned (pinch zoom + pan)
		const snapshot = () => {
			this.gesture = this.pointers.size ? { t0: this.t0, t1: this.t1, xs: new Map(this.pointers) } : null;
		};
		over.addEventListener('pointerdown', (e) => {
			if (e.pointerType === 'mouse' && e.button !== 0) return;
			try {
				over.setPointerCapture(e.pointerId); // keep getting moves outside the plot
			} catch {
				// pointer already gone
			}
			this.pointers.set(e.pointerId, localX(e.clientX));
			snapshot();
		});
		over.addEventListener('pointermove', (e) => {
			if (!this.pointers.has(e.pointerId) || !this.gesture) return;
			this.pointers.set(e.pointerId, localX(e.clientX));
			const g = this.gesture;
			const w = width();
			const span = g.t1 - g.t0;
			const ids = [...g.xs.keys()].filter((id) => this.pointers.has(id));
			if (ids.length >= 2) {
				const [a, b] = ids;
				const xa = g.xs.get(a)!, xb = g.xs.get(b)!;
				const na = this.pointers.get(a)!, nb = this.pointers.get(b)!;
				if (Math.abs(nb - na) < 10) return;
				const ta = g.t0 + (xa / w) * span;
				const tb = g.t0 + (xb / w) * span;
				const next = ((tb - ta) * w) / (nb - na);
				if (!(next > 0)) return;
				const t0 = ta - (na / w) * next;
				this.setView(t0, t0 + next);
			} else if (ids.length === 1) {
				const dt = ((this.pointers.get(ids[0])! - g.xs.get(ids[0])!) / w) * span;
				this.setView(g.t0 - dt, g.t1 - dt);
			}
		});
		const end = (e: PointerEvent) => {
			if (!this.pointers.delete(e.pointerId)) return;
			snapshot(); // e.g. pinch → one finger: continue as a pan from here
		};
		over.addEventListener('pointerup', end);
		over.addEventListener('pointercancel', end);
		over.addEventListener('dblclick', () => this.reset());
	}
}
