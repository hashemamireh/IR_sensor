// Min/max decimation for the graph. A recording can hold 3 M samples; uPlot gets
// at most ~2 points per pixel column — each column's minimum and maximum, at their
// real sample times, so pulse peaks and edges are drawn where they are. Once the
// view holds few enough samples, they're passed through unchanged.
//
// A Summary (min/max per block of 64 and of 4096 samples) makes each column cost
// about the same however many samples it spans, so redraws stay fast with several
// long traces overlaid.

export interface Series {
	/** Raw ADC counts */
	samples: Uint16Array;
	/** Actual sample rate (Hz) */
	rateHz: number;
	/** Count → display value (e.g. the 4096-entry mV table); identity if absent */
	lut?: Float32Array | null;
	/** Precomputed block min/max for these samples and this lut */
	summary?: Summary | null;
}

const B1 = 64;
const B2 = 4096;

interface Level {
	size: number;
	minV: Float32Array;
	maxV: Float32Array;
	minI: Uint32Array;
	maxI: Uint32Array;
}

/** Block minima/maxima (and where they are) at two block sizes */
export class Summary {
	readonly levels: [Level, Level];

	constructor(samples: Uint16Array, lut?: Float32Array | null) {
		const value = valueFn(samples, lut);
		const l1 = level(B1, Math.floor(samples.length / B1));
		for (let b = 0; b < l1.minV.length; b++) {
			let lo = b * B1, hi = lo;
			let vlo = value(lo), vhi = vlo;
			for (let i = lo + 1; i < (b + 1) * B1; i++) {
				const v = value(i);
				if (v < vlo) { vlo = v; lo = i; }
				if (v > vhi) { vhi = v; hi = i; }
			}
			l1.minV[b] = vlo; l1.minI[b] = lo; l1.maxV[b] = vhi; l1.maxI[b] = hi;
		}
		const per = B2 / B1;
		const l2 = level(B2, Math.floor(samples.length / B2));
		for (let b = 0; b < l2.minV.length; b++) {
			let lo = b * per, hi = lo;
			for (let c = lo + 1; c < (b + 1) * per; c++) {
				if (l1.minV[c] < l1.minV[lo]) lo = c;
				if (l1.maxV[c] > l1.maxV[hi]) hi = c;
			}
			l2.minV[b] = l1.minV[lo]; l2.minI[b] = l1.minI[lo];
			l2.maxV[b] = l1.maxV[hi]; l2.maxI[b] = l1.maxI[hi];
		}
		this.levels = [l1, l2];
	}
}

function level(size: number, n: number): Level {
	return { size, minV: new Float32Array(n), maxV: new Float32Array(n), minI: new Uint32Array(n), maxI: new Uint32Array(n) };
}

function valueFn(samples: Uint16Array, lut?: Float32Array | null) {
	// Float32 like the summary, so comparisons agree exactly
	return lut ? (i: number) => lut[samples[i]] : (i: number) => samples[i];
}

/**
 * Indices of the first minimum and first maximum in [start, end). Uses the
 * summary's whole blocks where it can, raw samples at the ragged ends.
 */
function minMax(s: Series, start: number, end: number): [number, number] {
	const value = valueFn(s.samples, s.lut);
	let lo = start, hi = start;
	let vlo = value(start), vhi = vlo;
	const take = (i: number) => {
		const v = value(i);
		if (v < vlo) { vlo = v; lo = i; }
		if (v > vhi) { vhi = v; hi = i; }
	};
	let i = start + 1;
	const sum = s.summary;
	if (sum) {
		const [l1, l2] = sum.levels;
		while (i < end && i % B1 !== 0) take(i++);
		while (i + B1 <= end && i / B1 < l1.minV.length) {
			const l = i % B2 === 0 && i + B2 <= end && i / B2 < l2.minV.length ? l2 : l1;
			const b = i / l.size;
			if (l.minV[b] < vlo) { vlo = l.minV[b]; lo = l.minI[b]; }
			if (l.maxV[b] > vhi) { vhi = l.maxV[b]; hi = l.maxI[b]; }
			i += l.size;
		}
	}
	while (i < end) take(i++);
	return [lo, hi];
}

/** The samples in [t0, t1] (seconds) plus one either side, reduced to ≤ 2 × `columns` points */
export function decimate(s: Series, t0: number, t1: number, columns: number): [Float64Array, Float64Array] {
	const n = s.samples.length;
	const empty: [Float64Array, Float64Array] = [new Float64Array(0), new Float64Array(0)];
	if (n === 0 || t1 <= t0) return empty;
	const i0 = Math.max(0, Math.floor(t0 * s.rateHz) - 1);
	const i1 = Math.min(n - 1, Math.ceil(t1 * s.rateHz) + 1);
	if (i1 < i0) return empty;
	const count = i1 - i0 + 1;
	const value = valueFn(s.samples, s.lut);
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
		const [lo, hi] = minMax(s, start, end);
		// Emit in time order; one point if the column is flat
		const [a, c] = lo <= hi ? [lo, hi] : [hi, lo];
		xs[m] = a / s.rateHz; ys[m++] = value(a);
		if (c !== a) { xs[m] = c / s.rateHz; ys[m++] = value(c); }
	}
	return [xs.subarray(0, m), ys.subarray(0, m)];
}

/** The display value of the sample nearest time t (s), or null outside the recording */
export function valueAt(s: Series, t: number): number | null {
	const i = Math.round(t * s.rateHz);
	if (i < 0 || i >= s.samples.length) return null;
	return valueFn(s.samples, s.lut)(i);
}
