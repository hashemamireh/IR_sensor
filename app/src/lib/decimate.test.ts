import { describe, expect, it } from 'vitest';
import { decimate } from './decimate';

const ramp = (n: number) => Uint16Array.from({ length: n }, (_, i) => i % 4096);

describe('decimate', () => {
	it('passes samples through when zoomed in', () => {
		const samples = ramp(1000);
		const [xs, ys] = decimate({ samples, rateHz: 1000 }, 0.1, 0.2, 500);
		expect(xs[0]).toBeCloseTo(0.099, 12); // one sample before the view
		expect(ys[0]).toBe(99);
		expect(xs.length).toBe(103);
		for (let k = 1; k < xs.length; k++) expect(xs[k] - xs[k - 1]).toBeCloseTo(0.001, 12);
	});

	it('keeps a one-sample spike at its real time', () => {
		const samples = new Uint16Array(3_000_000).fill(30);
		samples[1_234_567] = 2400;
		samples[2_000_001] = 0;
		const [xs, ys] = decimate({ samples, rateHz: 10_000 }, 0, 300, 800);
		expect(xs.length).toBeLessThanOrEqual(1600);
		const peak = ys.indexOf(2400);
		expect(peak).toBeGreaterThanOrEqual(0);
		expect(xs[peak]).toBeCloseTo(123.4567, 9);
		const dip = ys.indexOf(0);
		expect(xs[dip]).toBeCloseTo(200.0001, 9);
	});

	it('emits points in time order', () => {
		const samples = Uint16Array.from({ length: 100_000 }, (_, i) => (i * 7919) % 4096);
		const [xs] = decimate({ samples, rateHz: 1000 }, 0, 100, 300);
		for (let k = 1; k < xs.length; k++) expect(xs[k]).toBeGreaterThan(xs[k - 1]);
	});

	it('applies the lookup table', () => {
		const lut = Float32Array.from({ length: 4096 }, (_, i) => i * 0.5);
		const [, ys] = decimate({ samples: ramp(10), rateHz: 10, lut }, 0, 1, 100);
		expect(Array.from(ys)).toEqual([0, 0.5, 1, 1.5, 2, 2.5, 3, 3.5, 4, 4.5]);
	});

	it('handles empty and inverted ranges', () => {
		expect(decimate({ samples: new Uint16Array(0), rateHz: 1000 }, 0, 1, 10)[0].length).toBe(0);
		expect(decimate({ samples: ramp(10), rateHz: 1000 }, 1, 0, 10)[0].length).toBe(0);
	});
});
