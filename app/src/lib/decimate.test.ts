import { describe, expect, it } from 'vitest';
import { decimate, Summary, valueAt } from './decimate';

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

describe('decimate with a Summary', () => {
	// Deterministic pseudo-random data with plateaus (ties) and spikes
	function data(n: number, seed: number) {
		let x = seed;
		const rnd = () => ((x = (x * 1103515245 + 12345) & 0x7fffffff) / 0x7fffffff);
		return Uint16Array.from({ length: n }, (_, i) => (i % 5000 < 300 ? 2000 : Math.floor(rnd() * 60)) + (rnd() < 0.0005 ? 1500 : 0));
	}

	it('gives exactly the same points as the brute-force scan', () => {
		const samples = data(1_000_003, 7);
		const lut = Float32Array.from({ length: 4096 }, (_, i) => i * 0.8 + Math.sin(i / 300) * 4);
		for (const l of [null, lut]) {
			const plain = { samples, rateHz: 10_000, lut: l };
			const fast = { ...plain, summary: new Summary(samples, l) };
			for (const [t0, t1, cols] of [[0, 100.0003, 900], [3.2771, 61.3, 1234], [99.9, 100.0003, 50], [0.0001, 0.5, 777]]) {
				const a = decimate(plain, t0, t1, cols);
				const b = decimate(fast, t0, t1, cols);
				expect(Array.from(b[0])).toEqual(Array.from(a[0]));
				expect(Array.from(b[1])).toEqual(Array.from(a[1]));
			}
		}
	});

	it('looks up the nearest sample for the hover readout', () => {
		const samples = Uint16Array.from([5, 6, 7, 8]);
		expect(valueAt({ samples, rateHz: 10 }, 0.26)).toBe(8);
		expect(valueAt({ samples, rateHz: 10 }, 0.04)).toBe(5);
		expect(valueAt({ samples, rateHz: 10 }, -0.1)).toBeNull();
		expect(valueAt({ samples, rateHz: 10 }, 0.5)).toBeNull();
	});
});
