import { readFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';
import { firstRisingEdge, midThreshold, traceOffset } from './align';
import { parseIrec, sampleRateHz } from './irec';

describe('firstRisingEdge', () => {
	it('interpolates between the samples around the crossing', () => {
		// crosses 50 a quarter of the way from sample 2 (40) to 3 (80)
		expect(firstRisingEdge(Uint16Array.from([10, 20, 40, 80, 90]), 1000, 50)).toBeCloseTo(0.00225, 12);
	});

	it('ignores a trace that starts high, and finds the next edge', () => {
		expect(firstRisingEdge(Uint16Array.from([90, 90, 10, 10, 90]), 100, 50)).toBeCloseTo(0.035, 12);
		expect(firstRisingEdge(Uint16Array.from([90, 90, 10]), 100, 50)).toBeNull();
	});

	it('finds the first pulse in a mock recording', () => {
		const f = parseIrec(new Uint8Array(readFileSync(new URL('./fixtures/fractional_rate.irec', import.meta.url))));
		const thr = midThreshold(f.samples);
		const t = firstRisingEdge(f.samples, sampleRateHz(f.header), thr)!;
		const i = Math.ceil(t * sampleRateHz(f.header));
		expect(f.samples[i - 1]).toBeLessThanOrEqual(thr);
		expect(f.samples[i]).toBeGreaterThan(thr);
	});
});

describe('traceOffset', () => {
	it('aligns by start or by edge, plus the manual offset', () => {
		expect(traceOffset('start', 1.5, 0)).toBe(0);
		expect(traceOffset('edge', 1.5, 0)).toBe(-1.5);
		expect(traceOffset('edge', 1.5, 250)).toBe(-1.25);
		expect(traceOffset('edge', null, -10)).toBe(-0.01);
	});
});
