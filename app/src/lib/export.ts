// CSV / JSON export of one recording (spec §10.3.5). Always the real samples —
// never the graph's decimated view — optionally block-averaged to a lower rate.
// Output is produced in chunks so a 3 M-sample export never sits in memory whole.

export type ExportFormat = 'csv' | 'json';

export interface ExportSource {
	/** Raw ADC counts */
	samples: Uint16Array;
	/** Actual sample rate (Hz) */
	rateHz: number;
	/** Usable raw → mV table (attenuation already checked), or null */
	lut: Float32Array | null;
	/** Recording metadata for the JSON header */
	meta: Record<string, unknown>;
}

export interface ExportOptions {
	format: ExportFormat;
	/** First sample index (inclusive) */
	i0: number;
	/** Last sample index (exclusive) */
	i1: number;
	/** Average each run of `factor` samples into one row; 1 = full resolution */
	factor: number;
}

export interface Chunk {
	text: string;
	/** 0–1 */
	progress: number;
}

const ROWS_PER_CHUNK = 20_000;

export const rowCount = (o: Pick<ExportOptions, 'i0' | 'i1' | 'factor'>) =>
	Math.max(0, Math.ceil((o.i1 - o.i0) / o.factor));

/**
 * One output row: block k of the range, averaged. Its time is the centre of the
 * block, in ms since the recording's first sample. A last, shorter block is
 * averaged over the samples it has.
 */
function row(src: ExportSource, o: ExportOptions, k: number) {
	const start = o.i0 + k * o.factor;
	const end = Math.min(start + o.factor, o.i1);
	const n = end - start;
	const tMs = ((start + (n - 1) / 2) / src.rateHz) * 1000;
	// A count past the table (a corrupt file) has no mV: NaN, written as empty / null
	const toMv = (v: number) => src.lut![v] ?? NaN;
	if (n === 1) {
		const v = src.samples[start];
		return { tMs, raw: v, mv: src.lut ? toMv(v) : null };
	}
	let raw = 0, mv = 0;
	for (let i = start; i < end; i++) {
		raw += src.samples[i];
		if (src.lut) mv += toMv(src.samples[i]);
	}
	return { tMs, raw: raw / n, mv: src.lut ? mv / n : null };
}

// Whole counts and table values print as integers; averages get 2 decimals
const num = (v: number, averaged: boolean, missing = '') =>
	!Number.isFinite(v) ? missing : averaged ? v.toFixed(2) : String(v);

function csvRow(src: ExportSource, o: ExportOptions, k: number): string {
	const r = row(src, o, k);
	const avg = o.factor > 1;
	return `${r.tMs.toFixed(3)},${num(r.raw, avg)},${r.mv === null ? '' : num(r.mv, avg)}\n`;
}

export function* exportChunks(src: ExportSource, o: ExportOptions): Generator<Chunk> {
	const rows = rowCount(o);
	const avg = o.factor > 1;
	if (o.format === 'csv') {
		yield { text: 'timestamp_ms,raw_adc,voltage_mv\n', progress: 0 };
		for (let k = 0; k < rows; k += ROWS_PER_CHUNK) {
			let text = '';
			const end = Math.min(rows, k + ROWS_PER_CHUNK);
			for (let j = k; j < end; j++) text += csvRow(src, o, j);
			yield { text, progress: end / rows };
		}
		return;
	}

	const json = JSON.stringify(jsonHead(src, o));
	yield { text: json.slice(0, -1) + ',\n"raw_adc":[', progress: 0 };
	const passes = src.lut ? 2 : 1;
	for (const [pass, key] of (['raw', 'mv'] as const).slice(0, passes).entries()) {
		if (key === 'mv') yield { text: '],\n"voltage_mv":[', progress: 0.5 };
		for (let k = 0; k < rows; k += ROWS_PER_CHUNK) {
			const end = Math.min(rows, k + ROWS_PER_CHUNK);
			const parts: string[] = [];
			for (let j = k; j < end; j++) {
				const r = row(src, o, j);
				parts.push(num(key === 'raw' ? r.raw : r.mv!, avg, 'null'));
			}
			yield { text: (k ? ',' : '') + parts.join(','), progress: (pass + end / rows) / passes };
		}
	}
	yield { text: src.lut ? ']}\n' : '],\n"voltage_mv":null}\n', progress: 1 };
}

/**
 * The JSON export's fields before the sample arrays. Row k's time is
 * first_timestamp_ms + k × timestamp_step_ms (except a shorter last block).
 */
function jsonHead(src: ExportSource, o: ExportOptions) {
	const rows = rowCount(o);
	const outRate = src.rateHz / o.factor;
	return {
		format: 'irsensor-export',
		version: 1,
		recording: src.meta,
		first_sample_index: o.i0,
		source_sample_count: o.i1 - o.i0,
		source_sample_rate_hz: src.rateHz,
		downsample_factor: o.factor,
		sample_rate_hz: outRate,
		row_count: rows,
		first_timestamp_ms: rows ? row(src, o, 0).tMs : 0,
		timestamp_step_ms: 1000 / outRate,
		calibration_mv: src.lut ? Array.from(src.lut) : null,
	};
}

/** Estimated output size in bytes, from a sample of rows (within a few %) */
export function estimateBytes(src: ExportSource, o: ExportOptions): number {
	const rows = rowCount(o);
	if (!rows) return 0;
	const probes = Math.min(rows, 400);
	let bytes = 0;
	for (let p = 0; p < probes; p++) {
		const k = Math.floor((p * rows) / probes);
		if (o.format === 'csv') bytes += csvRow(src, o, k).length;
		else {
			const r = row(src, o, k);
			bytes += num(r.raw, o.factor > 1).length + 1 + (r.mv === null ? 0 : num(r.mv, o.factor > 1).length + 1);
		}
	}
	const perRow = bytes / probes;
	const fixed = o.format === 'csv' ? 32 : JSON.stringify(jsonHead(src, o)).length + 40;
	return Math.round(fixed + perRow * rows);
}

/** Lower rates offered for export: common rates below the source, as whole-number factors */
export function downsampleChoices(rateHz: number): { factor: number; label: string }[] {
	const out: { factor: number; label: string }[] = [];
	for (const target of [10_000, 5000, 2000, 1000, 500, 200, 100]) {
		if (target >= rateHz * 0.99) continue;
		const factor = Math.round(rateHz / target);
		if (factor > 1 && !out.some((c) => c.factor === factor)) {
			out.push({ factor, label: target >= 1000 ? `${target / 1000} kHz` : `${target} Hz` });
		}
	}
	return out;
}
