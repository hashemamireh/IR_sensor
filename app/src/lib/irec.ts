// IREC recording reader (spec §4.2, §4.5). Mirrors firmware/tools/check_irec.py.
import { unzlibSync } from 'fflate';

export const HEADER_SIZE = 32;
export const FORMAT_VERSION = 1;
export const FLAG_COMPRESSED = 0x01;
export const FLAG_OVERRUN = 0x02;

export interface IrecHeader {
	version: number;
	/** Actual (measured) sample rate in millihertz — use this for the timebase, not the nominal rate */
	sampleRateMhz: number;
	sampleCount: number;
	/** UTC µs since epoch; a number is exact until the year 2255 */
	startUtcUs: number;
	crc32: number;
	flags: number;
	compressed: boolean;
	/** Samples were lost during capture; the timeline is unreliable */
	overrun: boolean;
	/** ADC attenuation in tenths of a dB (120 = 12 dB) */
	attenTenthsDb: number;
}

export interface IrecFile {
	header: IrecHeader;
	/** Raw 12-bit ADC counts */
	samples: Uint16Array;
	/** CRC-32 of the uncompressed samples as computed here */
	crc32: number;
	crcOk: boolean;
}

export class IrecError extends Error {}

export function parseHeader(bytes: Uint8Array): IrecHeader {
	if (bytes.length < HEADER_SIZE) throw new IrecError('File is shorter than the 32-byte header');
	const v = new DataView(bytes.buffer, bytes.byteOffset, HEADER_SIZE);
	const magic = String.fromCharCode(bytes[0], bytes[1], bytes[2], bytes[3]);
	if (magic !== 'IREC') throw new IrecError('Not an IREC file (bad magic bytes)');
	const version = v.getUint8(4);
	if (version !== FORMAT_VERSION) throw new IrecError(`Unsupported IREC version ${version}`);
	const flags = v.getUint8(0x19);
	return {
		version,
		sampleRateMhz: v.getUint32(0x05, true),
		sampleCount: v.getUint32(0x09, true),
		startUtcUs: Number(v.getBigUint64(0x0d, true)),
		crc32: v.getUint32(0x15, true),
		flags,
		compressed: (flags & FLAG_COMPRESSED) !== 0,
		overrun: (flags & FLAG_OVERRUN) !== 0,
		attenTenthsDb: v.getUint8(0x1a),
	};
}

/** Parses a whole file: decompresses a zlib payload if flagged, then checks length and CRC. */
export function parseIrec(bytes: Uint8Array): IrecFile {
	const header = parseHeader(bytes);
	let payload = bytes.subarray(HEADER_SIZE);
	if (header.compressed) {
		try {
			payload = unzlibSync(payload);
		} catch (e) {
			throw new IrecError(`Corrupt zlib payload: ${(e as Error).message}`);
		}
	}
	if (payload.length !== header.sampleCount * 2) {
		throw new IrecError(
			`Payload is ${payload.length} bytes for ${header.sampleCount} samples (expected ${header.sampleCount * 2})`,
		);
	}
	const crc = crc32(payload);
	// Copy into an aligned buffer; the payload view may start at an odd offset
	const samples = new Uint16Array(header.sampleCount);
	const dv = new DataView(payload.buffer, payload.byteOffset, payload.length);
	for (let i = 0; i < samples.length; i++) samples[i] = dv.getUint16(i * 2, true);
	return { header, samples, crc32: crc, crcOk: crc === header.crc32 };
}

export const sampleRateHz = (h: Pick<IrecHeader, 'sampleRateMhz'>) => h.sampleRateMhz / 1000;

export function durationS(h: Pick<IrecHeader, 'sampleRateMhz' | 'sampleCount'>): number {
	return h.sampleRateMhz ? (h.sampleCount * 1000) / h.sampleRateMhz : 0;
}

// ── CRC-32 (IEEE, same as zlib's crc32()) ───────────────────────────────────

const CRC_TABLE = (() => {
	const t = new Uint32Array(256);
	for (let n = 0; n < 256; n++) {
		let c = n;
		for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
		t[n] = c >>> 0;
	}
	return t;
})();

export function crc32(data: Uint8Array, crc = 0): number {
	let c = ~crc >>> 0;
	for (let i = 0; i < data.length; i++) c = CRC_TABLE[(c ^ data[i]) & 0xff] ^ (c >>> 8);
	return ~c >>> 0;
}
