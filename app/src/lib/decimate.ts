// Min/max decimation for the graph. A recording can hold 3 M samples; uPlot gets
// at most ~2 points per pixel column — each column's minimum and maximum, at their
// real sample times, so pulse peaks and edges are drawn where they are. Once the
// view holds few enough samples, they're passed through unchanged.

export interface Series {
	/** Raw ADC counts */
	samples: Uint16Array;
	/** Actual sample rate (Hz) */
	rateHz: number;
	/** Count → display value (e.g. the 4096-entry mV table); identity if absent */
	lut?: Float32Array | null;
}

/** The samples in [t0, t1] (seconds) plus one either side, reduced to ≤ 2 × `columns` points */
export function decimate(s: Series, t0: number, t1: number, columns: number): [Float64Array, Float64Array] {
	const n = s.samples.length;
	if (n === 0 || t1 <= t0) return [new Float64Array(0), new Float64Array(0)];
	const i0 = Math.max(0, Math.floor(t0 * s.rateHz) - 1);
	const i1 = Math.min(n - 1, Math.ceil(t1 * s.rateHz) + 1);
	const count = i1 - i0 + 1;
	const value = s.lut ? (i: number) => s.lut![s.samples[i]] : (i: number) => s.samples[i];
	columns = Math.max(1, Math.floor(columns));

	if (count <= columns * 2) {
		const xs = new Float64Array(count);
		const ys = new Float64Array(count);
		for (let k = 0; k < count; k++) {
			xs[k] = (i0 + k) / s.rateHz;
			ys[k] = value(i0 + k);
		}
		return [xs, ys];
	}

	const xs = new Float64Array(columns * 2);
	const ys = new Float64Array(columns * 2);
	let m = 0;
	for (let b = 0; b < columns; b++) {
		const start = i0 + Math.floor((b * count) / columns);
		const end = i0 + Math.floor(((b + 1) * count) / columns); // exclusive
		let lo = start, hi = start;
		let vlo = value(start), vhi = vlo;
		for (let i = start + 1; i < end; i++) {
			const v = value(i);
			if (v < vlo) { vlo = v; lo = i; }
			if (v > vhi) { vhi = v; hi = i; }
		}
		// Emit in time order; one point if the column is flat
		const [a, c] = lo <= hi ? [lo, hi] : [hi, lo];
		xs[m] = a / s.rateHz; ys[m++] = value(a);
		if (c !== a) { xs[m] = c / s.rateHz; ys[m++] = value(c); }
	}
	return [xs.subarray(0, m), ys.subarray(0, m)];
}
