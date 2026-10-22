// uPlot wrapper for one recording: owns the visible time window, feeds uPlot the
// decimated samples in it, and handles wheel, drag and pinch itself (uPlot's own
// drag-to-select zoom is off — it doesn't do touch).
import uPlot from 'uplot';
import { decimate, type Series } from './decimate';

export class GraphView {
	private plot: uPlot;
	private t0 = 0;
	private t1: number;
	private frame = 0;
	private ro: ResizeObserver;
	private pointers = new Map<number, number>();
	private gesture: { t0: number; t1: number; xs: Map<number, number> } | null = null;
	private readonly minSpan: number;

	constructor(
		private host: HTMLDivElement,
		private series: Series,
		private full: number,
		unit: string,
		private onZoom: (z: number) => void,
	) {
		this.t1 = full || 1;
		// Never zoom in past ~10 samples across the plot
		this.minSpan = Math.min(this.t1, Math.max(10 / series.rateHz, 1e-4));
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
					{ label: 'Time', value: (_u, v) => (v == null ? '—' : `${v.toFixed(4)} s`) },
					{
						label: unit,
						stroke: c('--accent'),
						width: 1,
						points: { show: false },
						value: (_u, v) => (v == null ? '—' : unit === 'mV' ? `${v.toFixed(1)} mV` : `${v}`),
					},
				],
				cursor: { drag: { x: false, y: false, setScale: false }, points: { show: false } },
			},
			[[], []],
			host,
		);
		this.attachGestures();
		this.ro = new ResizeObserver(() => {
			this.plot.setSize(this.size());
			this.render();
		});
		this.ro.observe(host);
		this.render();
	}

	destroy() {
		cancelAnimationFrame(this.frame);
		this.ro.disconnect();
		this.plot.destroy();
	}

	private size() {
		// The legend sits below the plot
		return { width: Math.max(200, this.host.clientWidth), height: Math.max(150, this.host.clientHeight - 44) };
	}

	reset() {
		this.setView(0, this.full);
	}

	private setView(t0: number, t1: number) {
		let span = Math.min(Math.max(t1 - t0, this.minSpan), this.full || 1);
		t0 = Math.min(Math.max(0, t0), (this.full || 1) - span);
		this.t0 = t0;
		this.t1 = t0 + span;
		this.onZoom((this.full || 1) / span);
		this.schedule();
	}

	private schedule() {
		if (!this.frame) this.frame = requestAnimationFrame(() => this.render());
	}

	private render() {
		this.frame = 0;
		const columns = this.plot.over.clientWidth || this.plot.width;
		const [xs, ys] = decimate(this.series, this.t0, this.t1, columns);
		this.plot.batch(() => {
			this.plot.setData([xs as unknown as number[], ys as unknown as number[]], false);
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
