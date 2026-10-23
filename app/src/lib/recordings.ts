// Local recording model (spec §10.4) and how device and tablet copies are matched.
// Pure functions only — file I/O lives in storage.ts.
import type { Calibration, DeviceRecording } from './api';

export const SIDECAR_SCHEMA = 1;

/**
 * The `.json` sidecar saved next to each downloaded `.irec`. The sidecars are the
 * local index: the app scans them on startup, there is no separate database.
 */
export interface Sidecar {
	schema: typeof SIDECAR_SCHEMA;
	/** The `.irec` file next to this sidecar */
	file: string;
	device_ssid: string;
	/** Device recording ID — unique per device only, and restarts if NVS is erased */
	id: number;
	downloaded_utc_us: number;
	// The device's list entry, as sent (GET /api/recordings)
	start_utc_us: number;
	duration_ms: number;
	sample_rate_mhz: number;
	sample_count: number;
	size_bytes: number;
	compressed: boolean;
	overrun: boolean;
	attenuation_db: number;
	/** From the IREC header, and whether it matched the samples on download */
	crc32: number;
	crc_ok: boolean;
	/** The device's raw → mV table at download time; null if the chip had none */
	calibration: Calibration | null;
	// Reserved for later milestones
	name: string | null;
	notes: string;
	tags: string[];
}

export interface LocalRecording extends Sidecar {
	/** Sidecar file name, e.g. IRSensor-AB12_00042.json */
	sidecar: string;
}

/** Identifies one recording across device and tablet: ID alone isn't unique (see Sidecar.id) */
export const recordingKey = (ssid: string, id: number, startUtcUs: number) => `${ssid}|${id}|${startUtcUs}`;
export const localKey = (r: Sidecar) => recordingKey(r.device_ssid, r.id, r.start_utc_us);

/** An SSID made safe for a file name */
export const safeName = (s: string) => s.replace(/[^A-Za-z0-9_-]/g, '_');

/** `<SSID>_<id5>`, then `-2`, `-3`… for a different recording that wants the same name */
export function baseName(ssid: string, id: number, attempt = 1): string {
	const base = `${safeName(ssid)}_${String(id).padStart(5, '0')}`;
	return attempt === 1 ? base : `${base}-${attempt}`;
}

/** The user's name for a recording, else its device ID */
export const displayName = (r: { name?: string | null; id: number }) => r.name?.trim() || `#${r.id}`;

/** Every tag used on the tablet, most used first (for suggestions) */
export function allTags(locals: Pick<Sidecar, 'tags'>[]): string[] {
	const counts = new Map<string, number>();
	for (const l of locals) for (const t of l.tags ?? []) counts.set(t, (counts.get(t) ?? 0) + 1);
	return [...counts].sort((a, b) => b[1] - a[1] || a[0].localeCompare(b[0])).map(([t]) => t);
}

/** Search the list: every word must appear in the ID, name, tags or notes */
export function matchesSearch(row: RecordingRow, query: string): boolean {
	const words = query.toLowerCase().split(/\s+/).filter(Boolean);
	if (!words.length) return true;
	const l = row.local;
	const hay = [`#${row.id}`, l?.name ?? '', ...(l?.tags ?? []), l?.notes ?? ''].join('\n').toLowerCase();
	return words.every((w) => hay.includes(w));
}

/** One row of the recordings list: on the device, on the tablet, or both */
export interface RecordingRow {
	key: string;
	ssid: string;
	id: number;
	start_utc_us: number;
	duration_ms: number;
	sample_rate_mhz: number;
	sample_count: number;
	overrun: boolean;
	device?: DeviceRecording;
	local?: LocalRecording;
}

/**
 * Merges the connected device's list with every tablet copy, newest first.
 * `deviceSsid`/`device` are null when no device list is known (offline).
 */
export function mergeRecordings(
	deviceSsid: string | null,
	device: DeviceRecording[] | null,
	locals: LocalRecording[],
): RecordingRow[] {
	const rows = new Map<string, RecordingRow>();
	for (const l of locals) {
		rows.set(localKey(l), {
			key: localKey(l),
			ssid: l.device_ssid,
			id: l.id,
			start_utc_us: l.start_utc_us,
			duration_ms: l.duration_ms,
			sample_rate_mhz: l.sample_rate_mhz,
			sample_count: l.sample_count,
			overrun: l.overrun,
			local: l,
		});
	}
	if (deviceSsid && device) {
		for (const d of device) {
			const key = recordingKey(deviceSsid, d.id, d.start_utc_us);
			const row = rows.get(key);
			if (row) row.device = d;
			else rows.set(key, { key, ssid: deviceSsid, ...pick(d), device: d });
		}
	}
	return [...rows.values()].sort((a, b) => b.start_utc_us - a.start_utc_us || b.id - a.id);
}

function pick(d: DeviceRecording) {
	const { id, start_utc_us, duration_ms, sample_rate_mhz, sample_count, overrun } = d;
	return { id, start_utc_us, duration_ms, sample_rate_mhz, sample_count, overrun };
}

/** Device recordings with no tablet copy, oldest first (the order to download them in) */
export function missingLocally(ssid: string, device: DeviceRecording[], locals: LocalRecording[]): DeviceRecording[] {
	const have = new Set(locals.map(localKey));
	return device.filter((d) => !have.has(recordingKey(ssid, d.id, d.start_utc_us))).sort((a, b) => a.id - b.id);
}

/**
 * The sidecar's mV table, if it applies to this file: the table is for one
 * attenuation, and the IREC header says which one the samples used.
 */
export function usableCalibration(sidecar: Pick<Sidecar, 'calibration'>, attenTenthsDb: number): Calibration | null {
	const cal = sidecar.calibration;
	if (!cal || cal.mv.length !== 4096) return null;
	return Math.round(cal.attenuation_db * 10) === attenTenthsDb ? cal : null;
}
