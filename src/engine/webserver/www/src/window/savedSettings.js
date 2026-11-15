// THE SAVED SETTINGS' SHELF — a list's or a report's settings kept under names, to save the ones in force into or to
// restore one of (the server's question SavedSettings, the thin client's ibDialogSavedSettings): the entries, newest
// first, the one restored on open in bold. The window's own act — Save into the entry taken, or Restore it; a double
// click is the same — closes it once done; New... saves under a new name (a name the shelf has already is asked about,
// and replaced); Restore on open marks the entry taken, or — the marked one — unmarks it; Rename..., Delete. Each act is
// an answer: the server does it and asks again with the shelf as it stands and whether it was done (Done) — a refusal
// is said in a box. Close, Escape and × — answered with no Act.

import { _, format, labelText } from '../localization.js';

/** @typedef {import('./questions.js').Questions} Questions */
/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

// What the window is opened for (ibSavedSettings::ibMode), and what it does to the shelf (ibAct).
const Mode = Object.freeze({ Save: 0, Restore: 1 });
const Act = Object.freeze({ Save: 1, Restore: 2, Default: 3, Rename: 4, Remove: 5 });

export class SavedSettings {
	/**
	 * @param {Questions} questions what it is asked through
	 * @param {ProtocolNode} request
	 */
	constructor(questions, request) {
		this.questions = questions;
		this.request = request;
		this.saving = request.Mode === Mode.Save;
		/** @type {string | null} the entry taken, by its Id — kept by it as the shelf changes under it */
		this.taken = null;
		/** @type {{act: number, name: string, closing: boolean} | null} the act answered, until the server asks again */
		this.waiting = null;
		/** the window closed while an act was answered: it goes when the server asks again */
		this.closing = false;
		this.dialog = this.build();
		document.body.append(this.dialog);
		this.ask(request);
		this.dialog.open = true;
	}

	/** The window, its list empty. */
	build() {
		const dialog = /** @type {any} */ (document.createElement('wa-dialog'));
		dialog.className = 'saved-settings';
		dialog.label = this.saving ? _('Save settings') : _('Restore settings');
		// One column, no header: the entries are names.
		this.list = document.createElement('div');
		this.list.className = 'saved-settings-list';
		this.list.tabIndex = 0;
		this.list.setAttribute('role', 'listbox');
		this.list.addEventListener('keydown', (event) => this.key(event));
		const buttons = document.createElement('div');
		buttons.className = 'saved-settings-buttons';
		// The act the window is for, first and named after it; when saving, somewhere new to put it.
		this.main = this.button(this.saving ? _('Save') : _('Restore'), () => this.actOnTaken());
		this.create = this.saving ? this.button(_('New...'), () => this.saveNew()) : null;
		this.mark = this.button(_('Restore on open'), () => this.markDefault());
		this.mark.classList.add('apart');
		this.rename = this.button(_('Rename...'), () => this.renameTaken());
		this.remove = this.button(_('Delete'), () => this.removeTaken());
		buttons.append(...[this.main, this.create, this.mark, this.rename, this.remove].filter((one) => one !== null));
		const body = document.createElement('div');
		body.className = 'saved-settings-body';
		body.append(this.list, buttons);
		const close = this.button(_('Close'), () => this.close());
		close.slot = 'footer';
		dialog.append(body, close);
		dialog.addEventListener('wa-hide', (/** @type {Event} */ event) => {
			if (event.target !== dialog || dialog.answered)
				return;
			if (this.waiting !== null) {
				event.preventDefault();
				this.closing = true;
				return;
			}
			this.close();
		});
		return dialog;
	}

	/**
	 * The shelf asked again — as it stands after the act answered, and whether it was done.
	 * @param {ProtocolNode} request
	 */
	ask(request) {
		this.request = request;
		const acted = this.waiting;
		this.waiting = null;
		if (this.closing || (acted?.closing === true && request.Done === true)) {
			this.close();
			return;
		}
		if (acted !== null && request.Done !== true)
			this.refused(acted.act, acted.name);
		this.draw();
	}

	/** The entries, the one taken kept by its Id, the one restored on open bold. */
	draw() {
		const entries = this.entries();
		if (!entries.some((entry) => entry.Id === this.taken))
			this.taken = null;
		this.list.replaceChildren(...entries.map((entry) => {
			const line = document.createElement('div');
			line.className = 'saved-settings-entry';
			line.setAttribute('role', 'option');
			line.textContent = entry.Caption ?? '';
			// Bold is the whole mark: a word in the name would be read and renamed around.
			line.classList.toggle('default', entry.Id === this.request.Default);
			line.classList.toggle('taken', entry.Id === this.taken);
			line.ariaSelected = String(entry.Id === this.taken);
			line.addEventListener('click', () => this.take(entry.Id));
			line.addEventListener('dblclick', () => {
				this.take(entry.Id);
				this.actOnTaken();
			});
			return line;
		}));
		this.enable();
	}

	/** @returns {ProtocolNode[]} the shelf's entries */
	entries() {
		return this.request.NodeChildren ?? [];
	}

	/** @param {string | null} id */
	take(id) {
		this.taken = id;
		const entries = this.entries();
		[...this.list.children].forEach((line, at) => {
			line.classList.toggle('taken', entries[at]?.Id === id);
			line.ariaSelected = String(entries[at]?.Id === id);
		});
		this.list.children[entries.findIndex((entry) => entry.Id === id)]?.scrollIntoView({ block: 'nearest' });
		this.enable();
	}

	/** The arrows go between the entries; Enter is the window's act. @param {KeyboardEvent} event */
	key(event) {
		const entries = this.entries();
		const at = entries.findIndex((entry) => entry.Id === this.taken);
		if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
			event.preventDefault();
			const next = entries[Math.max(0, Math.min(entries.length - 1, at + (event.key === 'ArrowDown' ? 1 : -1)))];
			if (next !== undefined)
				this.take(next.Id);
		}
		else if (event.key === 'Enter' && at >= 0) {
			event.preventDefault();
			this.actOnTaken();
		}
	}

	/** The entry taken, if any. */
	takenEntry() {
		return this.entries().find((entry) => entry.Id === this.taken);
	}

	/** Saved into the entry taken — it was picked from what is there, nothing left to confirm — or restored. */
	actOnTaken() {
		const entry = this.takenEntry();
		if (entry === undefined || this.waiting !== null)
			return;
		if (this.saving)
			this.act(Act.Save, { Id: entry.Id, Name: entry.Caption }, entry.Caption, true);
		else
			this.act(Act.Restore, { Id: entry.Id }, entry.Caption, true);
	}

	/** Saved under a new name; one the shelf has already, in any case, is asked about and replaced. */
	async saveNew() {
		const name = await this.questions.textFromUser(_('Save settings'), _('Settings name'));
		if (!name)
			return;
		const same = this.entries().find((entry) => String(entry.Caption ?? '').toLowerCase() === name.toLowerCase());
		if (same !== undefined) {
			if (await this.questions.yesNo(_('Save settings'), format(_('Settings named "%s" already exist. Replace them?'), name)))
				this.act(Act.Save, { Id: same.Id, Name: name }, name, true);
			return;
		}
		this.act(Act.Save, { Name: name }, name, true);
	}

	/** The entry taken marked to be restored on open — or, marked already, unmarked: the same press undoes itself. */
	markDefault() {
		const entry = this.takenEntry();
		if (entry !== undefined)
			this.act(Act.Default, entry.Id === this.request.Default ? {} : { Id: entry.Id }, entry.Caption);
	}

	/** Renamed — only the name: renaming is not restoring. */
	async renameTaken() {
		const entry = this.takenEntry();
		if (entry === undefined)
			return;
		const name = await this.questions.textFromUser(_('Rename settings'), _('New name'), entry.Caption ?? '');
		if (name && name !== entry.Caption)
			this.act(Act.Rename, { Id: entry.Id, Name: name }, name);
	}

	/** Deleted, once the person says so. */
	async removeTaken() {
		const entry = this.takenEntry();
		if (entry !== undefined
			&& await this.questions.yesNo(_('Delete settings'), format(_('Delete the settings named "%s"?'), entry.Caption ?? '')))
			this.act(Act.Remove, { Id: entry.Id }, entry.Caption);
	}

	/**
	 * An act answered: the server does it and asks again.
	 * @param {number} act
	 * @param {{Id?: string, Name?: string}} args
	 * @param {string} name what a refusal names
	 * @param {boolean} [closing] the window's own act — done, it closes
	 */
	act(act, args, name, closing = false) {
		this.waiting = { act, name, closing };
		this.enable();
		this.questions.session.call('respond', { Id: this.request.Id, Response: { Act: act, ...args } }).catch(() => {
			this.waiting = null;
			this.enable();
		});
	}

	/** The window closed — answered with no Act. */
	close() {
		this.questions.answer(this.dialog, this.request, {});
	}

	/**
	 * An act the server did not do, said.
	 * @param {number} act
	 * @param {string} name
	 */
	refused(act, name) {
		const [caption, text] = /** @type {Record<number, [string, string]>} */ ({
			[Act.Save]: [_('Save settings'), _('The settings could not be saved.')],
			[Act.Restore]: [_('Saved settings'), format(_('The settings named "%s" could not be restored.'), name)],
			[Act.Default]: [_('Saved settings'), _('The default settings could not be changed.')],
			[Act.Rename]: [_('Rename settings'), _('The settings could not be renamed.')],
			[Act.Remove]: [_('Delete settings'), _('The settings could not be deleted.')],
		})[act];
		this.questions.dialog(caption, text, [{ caption: labelText('&OK'), answer: {}, preferred: true }], null);
	}

	/** Greyed, not hidden, while no entry is taken — New... needs none; nothing while an act is answered. */
	enable() {
		const none = this.taken === null || this.waiting !== null;
		for (const button of [this.main, this.mark, this.rename, this.remove])
			button.disabled = none;
		if (this.create !== null)
			this.create.disabled = this.waiting !== null;
	}

	/**
	 * @param {string} caption
	 * @param {() => void} press
	 */
	button(caption, press) {
		const button = /** @type {any} */ (document.createElement('wa-button'));
		button.setAttribute('variant', 'neutral');
		button.setAttribute('size', 's');
		button.textContent = caption;
		button.addEventListener('click', press);
		return button;
	}
}
