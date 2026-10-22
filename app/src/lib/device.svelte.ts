// Connection to the device and everything that runs on its own: status polling,
// time sync, calibration, and auto-download (spec §8.2–8.3, decisions D5–D6).
//
// Rules this follows:
// - The device serves one request at a time, so every request goes through one
//   queue here. A status poll that waits behind a download isn't a disconnect.
// - Most requests get 409 unless the device is idle. Background work (time sync,
//   calibration, list, downloads) only starts when a poll says "idle", and simply
//   stops on a 409; the next idle poll picks it up again.
// - Auto-download isn't tied to watching a recording finish: whenever the device is
//   idle and its list may have changed, anything not on the tablet is fetched. That
//   also covers recordings made from the device menu or while the tablet was away,
//   and 0-sample recordings that the firmware never saves.
import { SvelteSet } from 'svelte/reactivity';
import { ApiError, DeviceApi, type Calibration, type DeviceRecording, type Status } from './api';
import { errorText } from './format';
import { parseIrec } from './irec';
import { missingLocally, recordingKey, type LocalRecording } from './recordings';
import type { LocalStore } from './storage';

export type Connection = 'connecting' | 'online' | 'unreachable';

export const DEVICE_URL = 'http://192.168.4.1';
export const MOCK_URL = 'http://127.0.0.1:8080';

const POLL_MS = 1000;
const POLL_OFFLINE_MS = 2000;
/** Consecutive failed polls before the device counts as gone */
const FAILURES_UNTIL_UNREACHABLE = 2;
/** Network-error attempts per download before waiting for a manual retry */
const DOWNLOAD_ATTEMPTS = 3;

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

function stored(key: string, fallback: string): string {
	try {
		return localStorage.getItem(key) ?? fallback;
	} catch {
		return fallback;
	}
}

function store(key: string, value: string) {
	try {
		localStorage.setItem(key, value);
	} catch {
		// private mode etc. — not worth failing over
	}
}

export interface Notice {
	kind: 'error' | 'info';
	text: string;
}

export class DeviceController {
	baseUrl = $state(stored('irsensor.baseUrl', DEVICE_URL));
	connection = $state<Connection>('connecting');
	status = $state<Status | null>(null);
	/** SSID of the device last seen, kept across restarts for the "join WiFi" hint */
	lastSsid = $state<string | null>(stored('irsensor.lastSsid', '') || null);
	/** The connected device's recordings; null until listed this session */
	deviceRecordings = $state<DeviceRecording[] | null>(null);
	locals = $state<LocalRecording[]>([]);
	timeSynced = $state(false);
	/** 'unavailable' = the chip has no eFuse calibration (503); the app shows ADC counts */
	calibration = $state<'unknown' | 'ok' | 'unavailable'>('unknown');
	/** Key of the recording being downloaded */
	downloading = $state<string | null>(null);
	/** Keys whose auto-download gave up, with the reason; retried by hand */
	failed = $state<Record<string, string>>({});
	notice = $state<Notice | null>(null);
	/** Set when a recording started from the app has downloaded; the page opens it */
	autoOpen = $state<string | null>(null);

	private api = new DeviceApi(this.baseUrl);
	private queue: Promise<unknown> = Promise.resolve();
	private generation = 0;
	private failures = 0;
	private needsList = true;
	private calibrationChecked = false;
	private calibrationTable: Calibration | null = null;
	private idleWorkRunning = false;
	private attempts = new Map<string, number>();
	/** Keys deleted from the tablet only: don't fetch them straight back */
	private skipAuto = new SvelteSet<string>(JSON.parse(stored('irsensor.skipAutoDownload', '[]')));
	private running = false;
	private openWhenDownloaded: { ssid: string; id: number } | null = null;

	constructor(readonly local: LocalStore) {}

	get ssid(): string | null {
		return this.status?.wifi.ssid ?? null;
	}

	get online() {
		return this.connection === 'online';
	}

	get idle() {
		return this.online && this.status?.state === 'idle';
	}

	async start() {
		if (this.running) return;
		this.running = true;
		await this.reloadLocals();
		void this.pollLoop();
	}

	stop() {
		this.running = false;
	}

	/** `remember: false` for a one-run override (IRSENSOR_DEVICE_URL) */
	setBaseUrl(url: string, remember = true) {
		url = url.trim().replace(/\/+$/, '');
		if (!url || url === this.baseUrl) return;
		this.baseUrl = url;
		if (remember) store('irsensor.baseUrl', url);
		this.api = new DeviceApi(url);
		this.generation++;
		this.queue = Promise.resolve();
		this.status = null;
		this.connection = 'connecting';
		this.resetSession();
		void this.pollNow();
	}

	async reloadLocals() {
		this.locals = await this.local.loadAll();
	}

	// ── Request queue ───────────────────────────────────────────────────────

	/** Runs `fn` after every request queued before it; one request in flight at a time */
	private exclusive<T>(fn: (api: DeviceApi) => Promise<T>): Promise<T> {
		const api = this.api;
		const run = this.queue.then(() => fn(api));
		this.queue = run.catch(() => undefined);
		return run;
	}

	// ── Polling ─────────────────────────────────────────────────────────────

	private async pollLoop() {
		while (this.running) {
			await this.pollNow();
			await sleep(this.online ? POLL_MS : POLL_OFFLINE_MS);
		}
	}

	/** One status poll; also called right after the user starts or stops a recording */
	async pollNow() {
		const gen = this.generation;
		try {
			const s = await this.exclusive((api) => api.status());
			if (gen !== this.generation) return;
			this.onStatus(s);
		} catch (e) {
			if (gen !== this.generation) return;
			if (e instanceof ApiError) {
				// Something answered, but not a device we understand
				this.onStatusFailed(`Unexpected answer from ${this.baseUrl}: ${e.message}`);
			} else {
				this.onStatusFailed(null);
			}
		}
	}

	private onStatus(s: Status) {
		const prev = this.status;
		this.failures = 0;
		if (this.connection !== 'online' || (prev && prev.wifi.ssid !== s.wifi.ssid)) {
			this.resetSession();
			this.notice = null;
		}
		this.connection = 'online';
		this.status = s;
		if (s.wifi.ssid !== this.lastSsid) {
			this.lastSsid = s.wifi.ssid;
			store('irsensor.lastSsid', s.wifi.ssid);
		}
		// The list may have changed: a recording finished (or was discarded), or
		// someone used the device menu
		if (prev && (prev.state !== s.state || prev.recording_count !== s.recording_count)) this.needsList = true;
		if (s.state === 'idle') void this.idleWork();
	}

	private onStatusFailed(message: string | null) {
		this.failures++;
		if (this.connection === 'connecting' || this.failures >= FAILURES_UNTIL_UNREACHABLE) {
			if (this.connection !== 'unreachable') this.resetSession();
			this.connection = 'unreachable';
			if (message) this.notice = { kind: 'error', text: message };
			else if (this.notice?.kind === 'info') this.notice = null;
		}
	}

	/** Forget per-connection progress; on reconnect, time and list are redone */
	private resetSession() {
		this.timeSynced = false;
		this.needsList = true;
		this.deviceRecordings = null;
		this.calibrationChecked = false;
		this.calibrationTable = null;
		this.calibration = 'unknown';
		this.attempts.clear();
		this.failed = {};
	}

	// ── Background work while idle ──────────────────────────────────────────

	private async idleWork() {
		if (this.idleWorkRunning) return;
		this.idleWorkRunning = true;
		const gen = this.generation;
		const ssid = this.ssid!;
		try {
			if (!this.timeSynced) {
				await this.exclusive((api) => api.syncTime());
				this.timeSynced = true;
			}
			if (!this.calibrationChecked) await this.checkCalibration(ssid);
			if (this.needsList) await this.refreshList();
			while (gen === this.generation && this.idle && this.deviceRecordings) {
				const next = missingLocally(ssid, this.deviceRecordings, this.locals).find((d) => {
					const key = recordingKey(ssid, d.id, d.start_utc_us);
					return !this.skipAuto.has(key) && !(key in this.failed);
				});
				if (!next) break;
				await this.download(ssid, next);
				if (this.needsList) await this.refreshList();
			}
		} catch (e) {
			// 409: the device got busy; the next idle poll resumes. Network errors are
			// the poll loop's business.
			if (e instanceof ApiError && !e.busy) this.notice = { kind: 'error', text: errorText(e) };
		} finally {
			this.idleWorkRunning = false;
		}
	}

	private async refreshList() {
		this.deviceRecordings = await this.exclusive((api) => api.listRecordings());
		this.needsList = false;
	}

	private async checkCalibration(ssid: string) {
		try {
			const cal = await this.exclusive((api) => api.calibration());
			if (cal.mv?.length === 4096) {
				this.calibrationTable = cal;
				this.calibration = 'ok';
				await this.local.saveCalibration(ssid, cal);
			}
		} catch (e) {
			if (!(e instanceof ApiError) || e.busy) throw e;
			// 503 calibration_unavailable (or similar): fall back to a table saved on an
			// earlier connection, else raw counts
			this.calibrationTable = await this.local.loadCalibration(ssid);
			this.calibration = this.calibrationTable ? 'ok' : 'unavailable';
		}
		this.calibrationChecked = true;
	}

	/**
	 * Downloads one recording, checks it, and saves it with its sidecar. Network
	 * errors are retried with back-off; a CRC mismatch is retried once and then
	 * saved anyway, flagged, so nothing is lost.
	 */
	private async download(ssid: string, entry: DeviceRecording, manual = false) {
		const key = recordingKey(ssid, entry.id, entry.start_utc_us);
		const gen = this.generation;
		this.downloading = key;
		try {
			for (let crcRetry = 0; ; crcRetry++) {
				let bytes: Uint8Array;
				try {
					bytes = await this.exclusive((api) => api.downloadRecording(entry.id));
				} catch (e) {
					if (e instanceof ApiError) {
						if (e.status === 404) {
							this.needsList = true; // gone from the device meanwhile
							return;
						}
						throw e;
					}
					const n = (this.attempts.get(key) ?? 0) + 1;
					this.attempts.set(key, n);
					if (n >= DOWNLOAD_ATTEMPTS || manual) {
						this.failed = { ...this.failed, [key]: `Download failed: ${errorText(e)}` };
						return;
					}
					await sleep(1000 * 2 ** (n - 1));
					if (gen !== this.generation || !this.online) return;
					continue;
				}
				const file = parseIrec(bytes);
				if (!file.crcOk && crcRetry === 0) continue;
				const saved = await this.local.save(
					ssid,
					entry,
					bytes,
					{ crc32: file.header.crc32, ok: file.crcOk },
					this.calibrationTable,
				);
				this.locals = [...this.locals.filter((l) => l.sidecar !== saved.sidecar), saved];
				this.attempts.delete(key);
				if (this.openWhenDownloaded?.ssid === ssid && this.openWhenDownloaded.id === entry.id) {
					this.openWhenDownloaded = null;
					this.autoOpen = key;
				}
				if (!file.crcOk) {
					this.notice = { kind: 'error', text: `Recording #${entry.id} failed its CRC check twice; saved but marked corrupt` };
				}
				return;
			}
		} catch (e) {
			if (e instanceof ApiError && e.busy) throw e;
			this.failed = { ...this.failed, [key]: errorText(e) };
		} finally {
			this.downloading = null;
		}
	}

	// ── User actions ────────────────────────────────────────────────────────

	private async act<T>(what: string, fn: (api: DeviceApi) => Promise<T>): Promise<T | undefined> {
		try {
			const result = await this.exclusive(fn);
			this.notice = null;
			return result;
		} catch (e) {
			this.notice = { kind: 'error', text: `${what}: ${errorText(e)}` };
			return undefined;
		} finally {
			void this.pollNow();
		}
	}

	async startRecording(durationS: number, sampleRateHz: number) {
		const ssid = this.ssid;
		const r = await this.act("Couldn't start recording", (api) => api.startRecording(durationS, sampleRateHz));
		if (r && ssid) this.openWhenDownloaded = { ssid, id: r.id };
		return r;
	}

	stopRecording() {
		return this.act("Couldn't stop recording", (api) => api.stopRecording());
	}

	/** Manual download, e.g. after auto-download gave up or for a skipped recording */
	async downloadNow(entry: DeviceRecording) {
		const ssid = this.ssid;
		if (!ssid) return;
		const key = recordingKey(ssid, entry.id, entry.start_utc_us);
		const { [key]: _, ...rest } = this.failed;
		this.failed = rest;
		this.attempts.delete(key);
		this.unskip(key);
		try {
			await this.download(ssid, entry, true);
		} catch (e) {
			this.notice = { kind: 'error', text: `Download: ${errorText(e)}` };
		}
	}

	/** Deletes the chosen copies. Returns false if the device refused. */
	async deleteRecording(key: string, opts: { device?: DeviceRecording; local?: LocalRecording }): Promise<boolean> {
		if (opts.device) {
			try {
				await this.exclusive((api) => api.deleteRecording(opts.device!.id));
			} catch (e) {
				if (!(e instanceof ApiError && e.status === 404)) {
					this.notice = { kind: 'error', text: `Couldn't delete from device: ${errorText(e)}` };
					return false;
				}
			}
			this.deviceRecordings = this.deviceRecordings?.filter((d) => d.id !== opts.device!.id) ?? null;
			this.needsList = true;
			this.unskip(key);
		}
		if (opts.local) {
			await this.local.delete(opts.local);
			this.locals = this.locals.filter((l) => l.sidecar !== opts.local!.sidecar);
			// The device copy (if any, now or on a later connection) mustn't be
			// downloaded straight back
			if (!opts.device) {
				this.skipAuto.add(key);
				store('irsensor.skipAutoDownload', JSON.stringify([...this.skipAuto]));
			}
		}
		this.notice = null;
		void this.pollNow();
		return true;
	}

	async deleteAllOnDevice(): Promise<number | undefined> {
		const r = await this.act("Couldn't delete all recordings", (api) => api.deleteAllRecordings());
		if (r) {
			this.deviceRecordings = [];
			this.needsList = true;
			this.notice = { kind: 'info', text: `Deleted ${r.deleted} recording${r.deleted === 1 ? '' : 's'} from the device` };
		}
		return r?.deleted;
	}

	isSkipped(key: string) {
		return this.skipAuto.has(key);
	}

	private unskip(key: string) {
		if (this.skipAuto.delete(key)) store('irsensor.skipAutoDownload', JSON.stringify([...this.skipAuto]));
	}
}
