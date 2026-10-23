// Time alignment of overlaid recordings (decision E6): by start, or by each
// trace's first rising edge, plus a manual per-trace offset on top.

export type AlignMode = 'start' | 'edge';

/**
 * Time (s) of the first rising crossing of `threshold`, interpolated between the
 * two samples around it. A trace already above the threshold at its first sample
 * doesn't count as an edge there (same rule as firmware/tools/check_irec.py).
 */
export function firstRisingEdge(samples: Uint16Array, rateHz: number, threshold: number): number | null {
	for (let i = 1; i < samples.length; i++) {
		const a = samples[i - 1], b = samples[i];
		if (a <= threshold && b > threshold) return (i - 1 + (threshold - a) / (b - a)) / rateHz;
	}
	return null;
}

/** Halfway between the trace's minimum and maximum, in raw counts */
export function midThreshold(samples: Uint16Array): number {
	let lo = Infinity, hi = -Infinity;
	for (let i = 0; i < samples.length; i++) {
		const v = samples[i];
		if (v < lo) lo = v;
		if (v > hi) hi = v;
	}
	return (lo + hi) / 2;
}

/**
 * Where each trace's t = 0 goes on the shared axis (s): `start` puts every
 * recording's first sample at 0; `edge` puts every first edge at 0 (traces with
 * no edge start at 0). The manual offset (ms) is added on top.
 */
export function traceOffset(mode: AlignMode, edgeS: number | null, manualMs: number): number {
	const base = mode === 'edge' && edgeS !== null ? -edgeS : 0;
	return base + manualMs / 1000;
}
