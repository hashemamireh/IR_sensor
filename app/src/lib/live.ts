// Live mode data (spec §3.6): the device sends 20 ms buckets of [min, max, mean] raw
// counts with sequence numbers; this keeps the last N of them for the strip chart,
// marks where buckets were missed so the chart doesn't draw across the gap, and
// works out the big reading and the peak.
import type { LiveResponse } from './api';

export class LiveBuffer {
	readonly seq: number[] = [];
	readonly min: number[] = [];
	readonly max: number[] = [];
	readonly mean: number[] = [];
	/** Indices i where buckets were missed between i-1 and i */
	readonly gaps = new Set<number>();
	bucketMs = 20;
	/** What to ask for next (`since`); null before the first poll */
	nextSeq: number | null = null;
	overruns = 0;

	constructor(readonly capacity: number) {}

	get length() {
		return this.seq.length;
	}

	clear() {
		for (const a of [this.seq, this.min, this.max, this.mean]) a.length = 0;
		this.gaps.clear();
		this.nextSeq = null;
		this.overruns = 0;
	}

	append(r: LiveResponse) {
		// Sequence numbers went backwards: the device restarted live mode. Start over.
		if (this.nextSeq !== null && r.next_seq < this.nextSeq) this.clear();
		this.bucketMs = r.bucket_ms;
		this.overruns = r.overruns;
		r.buckets.forEach(([lo, hi, avg], k) => {
			const s = r.first_seq + k;
			const last = this.seq.at(-1);
			if (last !== undefined && s <= last) return; // already have it
			if (last !== undefined && s > last + 1) this.gaps.add(this.seq.length);
			this.seq.push(s);
			this.min.push(lo);
			this.max.push(hi);
			this.mean.push(avg);
		});
		this.nextSeq = r.next_seq;
		this.trim();
	}

	private trim() {
		const drop = this.seq.length - this.capacity;
		if (drop <= 0) return;
		for (const a of [this.seq, this.min, this.max, this.mean]) a.splice(0, drop);
		const moved = [...this.gaps].map((g) => g - drop).filter((g) => g > 0);
		this.gaps.clear();
		for (const g of moved) this.gaps.add(g);
	}

	/** Mean over the last `ms` (the OLED's big reading uses 100 ms), or null if empty */
	current(ms = 100): number | null {
		const n = Math.min(this.length, Math.max(1, Math.round(ms / this.bucketMs)));
		if (!n) return null;
		let sum = 0;
		for (let i = this.length - n; i < this.length; i++) sum += this.mean[i];
		return sum / n;
	}

	/** Highest max over the last `seconds`, or null if empty */
	peak(seconds: number): number | null {
		const n = Math.min(this.length, Math.round((seconds * 1000) / this.bucketMs));
		if (!n) return null;
		let hi = -Infinity;
		for (let i = this.length - n; i < this.length; i++) hi = Math.max(hi, this.max[i]);
		return hi;
	}

	/**
	 * Strip-chart data for the last `seconds`: x in seconds before the newest bucket
	 * (0 = now), and min / max / mean converted by `value`. A null row is inserted at
	 * every gap so the lines and the band break there.
	 */
	chart(seconds: number, value: (counts: number) => number): [number[], (number | null)[], (number | null)[], (number | null)[]] {
		const xs: number[] = [], lo: (number | null)[] = [], hi: (number | null)[] = [], avg: (number | null)[] = [];
		if (!this.length) return [xs, lo, hi, avg];
		const newest = this.seq[this.length - 1];
		const from = Math.max(0, this.length - Math.round((seconds * 1000) / this.bucketMs));
		const t = (s: number) => ((s - newest) * this.bucketMs) / 1000;
		for (let i = from; i < this.length; i++) {
			if (i > from && this.gaps.has(i)) {
				xs.push((t(this.seq[i - 1]) + t(this.seq[i])) / 2);
				lo.push(null), hi.push(null), avg.push(null);
			}
			xs.push(t(this.seq[i]));
			lo.push(value(this.min[i]));
			hi.push(value(this.max[i]));
			avg.push(value(this.mean[i]));
		}
		return [xs, lo, hi, avg];
	}
}

/**
 * Raw counts → display value through a 4096-entry table, interpolating between
 * entries (bucket means aren't whole counts). Identity without a table.
 */
export function countsToValue(lut: Float32Array | null): (counts: number) => number {
	if (!lut) return (c) => c;
	const top = lut.length - 1;
	return (c) => {
		const x = Math.min(top, Math.max(0, c));
		const i = Math.floor(x);
		return i >= top ? lut[top] : lut[i] + (lut[i + 1] - lut[i]) * (x - i);
	};
}
