import { describe, expect, it } from 'vitest';
import type { LiveResponse } from './api';
import { countsToValue, LiveBuffer } from './live';

/** A response of `n` buckets from `first`, each [v, v + 10, v + 5] with v = seq */
const resp = (first: number, n: number, overruns = 0): LiveResponse => ({
	bucket_ms: 20,
	first_seq: first,
	next_seq: first + n,
	overruns,
	buckets: Array.from({ length: n }, (_, k) => [first + k, first + k + 10, first + k + 5]),
});

describe('LiveBuffer', () => {
	it('appends polls in sequence and asks for the next one', () => {
		const b = new LiveBuffer(100);
		b.append(resp(0, 12));
		b.append(resp(12, 13));
		expect(b.length).toBe(25);
		expect(b.nextSeq).toBe(25);
		expect(b.gaps.size).toBe(0);
	});

	it('marks a gap when buckets were missed, and the chart breaks there', () => {
		const b = new LiveBuffer(100);
		b.append(resp(0, 5));
		b.append(resp(9, 3)); // 5–8 missed
		expect([...b.gaps]).toEqual([5]);
		const [xs, lo] = b.chart(10, (c) => c);
		expect(xs).toHaveLength(9); // 8 buckets + 1 break
		expect(lo[5]).toBeNull();
		expect(xs.at(-1)).toBe(0);
		expect(xs[0]).toBeCloseTo(-0.22, 9); // seq 0 is 11 buckets before seq 11
	});

	it('ignores buckets it already has', () => {
		const b = new LiveBuffer(100);
		b.append(resp(0, 10));
		b.append(resp(5, 10));
		expect(b.seq).toEqual(Array.from({ length: 15 }, (_, i) => i));
	});

	it('starts over when the device restarts live mode', () => {
		const b = new LiveBuffer(100);
		b.append(resp(500, 10));
		b.append(resp(0, 3));
		expect(b.seq).toEqual([0, 1, 2]);
	});

	it('keeps only the newest buckets, and the gaps among them', () => {
		const b = new LiveBuffer(10);
		b.append(resp(0, 8));
		b.append(resp(20, 8)); // gap at index 8, then the oldest 6 are dropped
		expect(b.seq).toEqual([6, 7, 20, 21, 22, 23, 24, 25, 26, 27]);
		expect([...b.gaps]).toEqual([2]);
	});

	it('gives the current reading over 100 ms and the peak over a window', () => {
		const b = new LiveBuffer(100);
		b.append(resp(0, 50, 3));
		expect(b.current()).toBe(52); // means 50..54 of seq 45..49
		expect(b.peak(0.2)).toBe(59); // last 10 buckets' max
		expect(b.overruns).toBe(3);
		expect(new LiveBuffer(5).current()).toBeNull();
	});
});

describe('countsToValue', () => {
	it('interpolates the mV table between whole counts and clamps the ends', () => {
		const lut = Float32Array.from({ length: 4096 }, (_, i) => i * 2);
		const f = countsToValue(lut);
		expect(f(10.25)).toBeCloseTo(20.5, 6);
		expect(f(-3)).toBe(0);
		expect(f(5000)).toBe(8190);
		expect(countsToValue(null)(12.5)).toBe(12.5);
	});
});
