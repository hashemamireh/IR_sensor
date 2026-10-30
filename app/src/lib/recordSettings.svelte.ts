// The app's record settings (rate, duration), shared by the Record popover and the
// Record button in live view, and remembered between runs.
import { maxDurationS } from './api';

export const PRESETS_S = [5, 10, 30, 60, 120, 300];
/** The duration preset value meaning "use customS" */
export const CUSTOM = 0;

function saved(key: string, fallback: number): number {
	try {
		const raw = localStorage.getItem(key);
		const v = Number(raw);
		return raw !== null && Number.isFinite(v) ? v : fallback;
	} catch {
		return fallback;
	}
}

class RecordSettings {
	#rateHz = $state(saved('irsensor.rateHz', 10000));
	#preset = $state(saved('irsensor.preset', 30));
	#customS = $state(saved('irsensor.customS', 45));

	get rateHz() {
		return this.#rateHz;
	}
	set rateHz(v: number) {
		this.#rateHz = v;
		// 300 s isn't allowed at 20 kHz (spec §4.4): fall back to the cap
		if (this.#preset > this.cap) this.#preset = this.cap;
		this.save();
	}
	get preset() {
		return this.#preset;
	}
	set preset(v: number) {
		this.#preset = v;
		this.save();
	}
	get customS() {
		return this.#customS;
	}
	set customS(v: number) {
		this.#customS = v;
		this.save();
	}

	get cap() {
		return maxDurationS(this.#rateHz);
	}
	get durationS() {
		return this.#preset === CUSTOM ? this.#customS : this.#preset;
	}
	get valid() {
		const d = this.durationS;
		return Number.isInteger(d) && d >= 1 && d <= this.cap;
	}
	get label() {
		return `${this.valid ? `${this.durationS} s ` : ''}at ${this.#rateHz / 1000} kHz`;
	}

	private save() {
		try {
			localStorage.setItem('irsensor.rateHz', String(this.#rateHz));
			localStorage.setItem('irsensor.preset', String(this.#preset));
			localStorage.setItem('irsensor.customS', String(this.#customS));
		} catch {
			// not important
		}
	}
}

export const recordSettings = new RecordSettings();
