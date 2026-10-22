// Tablet-side storage (spec §10.4):
//   <root>/recordings/<SSID>_<id5>.irec   the file exactly as downloaded
//   <root>/recordings/<SSID>_<id5>.json   its sidecar (recordings.ts)
//   <root>/devices/<SSID>.json            last calibration table seen per device
// <root> is ~/IRSensor, or $IRSENSOR_DATA_DIR. In a plain browser everything is in
// memory instead, which is enough for UI work against the mock.
import { invoke } from '@tauri-apps/api/core';
import * as fs from '@tauri-apps/plugin-fs';
import type { Calibration, DeviceRecording } from './api';
import { inTauri } from './http';
import { baseName, localKey, recordingKey, safeName, SIDECAR_SCHEMA, type LocalRecording, type Sidecar } from './recordings';

interface Backend {
	readonly root: string;
	list(dir: string): Promise<string[]>;
	readText(path: string): Promise<string | null>;
	writeText(path: string, text: string): Promise<void>;
	readBytes(path: string): Promise<Uint8Array>;
	writeBytes(path: string, data: Uint8Array): Promise<void>;
	remove(path: string): Promise<void>;
}

class TauriBackend implements Backend {
	constructor(readonly root: string) {}
	private p(rel: string) {
		return `${this.root}/${rel}`;
	}
	private async ensureDir(rel: string) {
		const dir = this.p(rel.split('/').slice(0, -1).join('/'));
		if (!(await fs.exists(dir))) await fs.mkdir(dir, { recursive: true });
	}
	async list(dir: string) {
		if (!(await fs.exists(this.p(dir)))) return [];
		return (await fs.readDir(this.p(dir))).filter((e) => e.isFile).map((e) => e.name);
	}
	async readText(rel: string) {
		return (await fs.exists(this.p(rel))) ? fs.readTextFile(this.p(rel)) : null;
	}
	async writeText(rel: string, text: string) {
		await this.ensureDir(rel);
		// Write then rename, so a crash never leaves a half-written sidecar
		await fs.writeTextFile(this.p(rel) + '.tmp', text);
		await fs.rename(this.p(rel) + '.tmp', this.p(rel));
	}
	readBytes(rel: string) {
		return fs.readFile(this.p(rel));
	}
	async writeBytes(rel: string, data: Uint8Array) {
		await this.ensureDir(rel);
		await fs.writeFile(this.p(rel) + '.tmp', data);
		await fs.rename(this.p(rel) + '.tmp', this.p(rel));
	}
	async remove(rel: string) {
		if (await fs.exists(this.p(rel))) await fs.remove(this.p(rel));
	}
}

class MemoryBackend implements Backend {
	readonly root = '(in memory — browser mode)';
	private files = new Map<string, string | Uint8Array>();
	async list(dir: string) {
		return [...this.files.keys()].filter((k) => k.startsWith(dir + '/')).map((k) => k.slice(dir.length + 1));
	}
	async readText(p: string) {
		const v = this.files.get(p);
		return typeof v === 'string' ? v : null;
	}
	async writeText(p: string, t: string) {
		this.files.set(p, t);
	}
	async readBytes(p: string) {
		const v = this.files.get(p);
		if (!(v instanceof Uint8Array)) throw new Error(`${p} not found`);
		return v;
	}
	async writeBytes(p: string, d: Uint8Array) {
		this.files.set(p, d);
	}
	async remove(p: string) {
		this.files.delete(p);
	}
}

export interface StartupConfig {
	data_dir: string;
	/** IRSENSOR_DEVICE_URL, if set: overrides the saved device URL */
	device_url: string | null;
}

/** Environment settings read by the Rust side at startup (Tauri only) */
export const startupConfig = () => invoke<StartupConfig>('startup_config');

/** JSON with the 4096-entry table on one line instead of 4096 */
function sidecarJson(s: Sidecar): string {
	const marker = '"__calibration__"';
	return JSON.stringify({ ...s, calibration: s.calibration ? '__calibration__' : null }, null, 2).replace(
		marker,
		JSON.stringify(s.calibration),
	) + '\n';
}

export class LocalStore {
	private constructor(private readonly be: Backend) {}

	static async open(): Promise<LocalStore> {
		return new LocalStore(inTauri ? new TauriBackend((await startupConfig()).data_dir) : new MemoryBackend());
	}

	get root() {
		return this.be.root;
	}

	/** Every complete recording (one whose sidecar exists), unreadable sidecars skipped */
	async loadAll(): Promise<LocalRecording[]> {
		const names = (await this.be.list('recordings')).filter((n) => n.endsWith('.json'));
		const out: LocalRecording[] = [];
		for (const name of names) {
			try {
				const s = JSON.parse((await this.be.readText(`recordings/${name}`)) ?? 'null') as Sidecar;
				if (s?.schema === SIDECAR_SCHEMA) out.push({ ...s, sidecar: name });
			} catch {
				console.warn(`Skipping unreadable sidecar ${name}`);
			}
		}
		return out;
	}

	/** Saves a downloaded file and its sidecar, never overwriting a different recording */
	async save(
		ssid: string,
		entry: DeviceRecording,
		bytes: Uint8Array,
		crc: { crc32: number; ok: boolean },
		calibration: Calibration | null,
	): Promise<LocalRecording> {
		const key = recordingKey(ssid, entry.id, entry.start_utc_us);
		let base = '';
		for (let attempt = 1; ; attempt++) {
			base = baseName(ssid, entry.id, attempt);
			const existing = await this.be.readText(`recordings/${base}.json`);
			if (existing === null) break;
			try {
				if (localKey(JSON.parse(existing)) === key) break; // same recording: replace it
			} catch {
				break; // unreadable: reuse the name
			}
		}
		const sidecar: Sidecar = {
			schema: SIDECAR_SCHEMA,
			file: `${base}.irec`,
			device_ssid: ssid,
			id: entry.id,
			downloaded_utc_us: Date.now() * 1000,
			start_utc_us: entry.start_utc_us,
			duration_ms: entry.duration_ms,
			sample_rate_mhz: entry.sample_rate_mhz,
			sample_count: entry.sample_count,
			size_bytes: bytes.length,
			compressed: entry.compressed,
			overrun: entry.overrun,
			attenuation_db: entry.attenuation_db,
			crc32: crc.crc32,
			crc_ok: crc.ok,
			calibration,
			name: null,
			notes: '',
			tags: [],
		};
		// .irec first: a sidecar only exists once its recording is complete
		await this.be.writeBytes(`recordings/${sidecar.file}`, bytes);
		await this.be.writeText(`recordings/${base}.json`, sidecarJson(sidecar));
		return { ...sidecar, sidecar: `${base}.json` };
	}

	readIrec(r: LocalRecording): Promise<Uint8Array> {
		return this.be.readBytes(`recordings/${r.file}`);
	}

	async delete(r: LocalRecording): Promise<void> {
		await this.be.remove(`recordings/${r.sidecar}`);
		await this.be.remove(`recordings/${r.file}`);
	}

	async loadCalibration(ssid: string): Promise<Calibration | null> {
		try {
			return JSON.parse((await this.be.readText(`devices/${safeName(ssid)}.json`)) ?? 'null')?.calibration ?? null;
		} catch {
			return null;
		}
	}

	async saveCalibration(ssid: string, calibration: Calibration): Promise<void> {
		await this.be.writeText(
			`devices/${safeName(ssid)}.json`,
			JSON.stringify({ ssid, fetched_utc_us: Date.now() * 1000, calibration }) + '\n',
		);
	}
}
