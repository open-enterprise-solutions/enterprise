// A SETTINGS WINDOW — a list's (ListSettings) or a report's (ComposerSettings): the server's question carries the
// setting in force, its fields described, the references they hold; the window edits a COPY and hands it back on OK,
// whole (Settings); Cancel, Escape and × hand nothing back. A table's advanced search (SearchWindow) is one over a single
// condition, answered with it. A setting the server refuses is asked again with why
// (Error), said before the window opens on it — as the thin client's new window does.
//
// ON THE WAY it asks the server twice, each an answer to its question, which the server asks again (the thin client's
// frame: one road for both windows, IsSettingsRequest): a reference field's own fields (Act Expand, Targets →
// Expanded, References), and a value of the server's choosing (Act Choose: Type, Value, Choice → Chosen: Choice,
// Value, its Presentation). A quick choice is asked over the window (Questions keeps it); a CHOICE FORM opens as a tab
// under the modal window, so the window steps ASIDE for it — hidden, unanswered — and Questions hands it the same
// question on every frame meanwhile: it comes back with the value chosen, or once its own tab is active again (the
// choice form closed unchosen). The thin client keeps its window up and takes the value from a later call on the same
// question (TakeChosen) — the server's question keeps its Id while the value is written into it.

import { _, labelText } from '../localization.js';
import { SettingsFieldTree } from './fieldTree.js';

/** @typedef {import('../window/questions.js').Questions} Questions */
/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */
/** @typedef {import('./fieldTree.js').Field} Field */

// What the window asks on the way (ibListSettings::ibAct).
const Act = Object.freeze({ Expand: 1, Choose: 2 });

export class SettingsWindow {
	/**
	 * @param {Questions} questions what it is asked through
	 * @param {ProtocolNode} request
	 */
	constructor(questions, request) {
		this.questions = questions;
		this.request = request;
		/** @type {any} the setting edited — a copy of the one in force */
		this.settings = structuredClone(request.Settings ?? {});
		/** @type {{act: number, field?: Field} | null} the act answered, until the server asks again */
		this.waiting = null;
		/** @type {Map<number, (value: object, presentation: string) => void>} a value being chosen, by its Choice */
		this.choosing = new Map();
		/** the last Choice asked — each value chosen is told by its own */
		this.choice = 0;
		/** the tab the window is asked over — a choice form's tab sets it aside until this one is active again */
		this.tab = questions.session.frame?.ActiveTab;
		/** set aside, unanswered, while a choice form chooses */
		this.aside = false;
		this.fields = new SettingsFieldTree(questions.pictures, request, (field, targets) => {
			if (this.waiting === null)
				this.act({ act: Act.Expand, field }, { Targets: targets });
		});
		/** @type {any} the window — made by `open` */
		this.dialog = null;
		/** @type {any} */
		this.ok = null;
	}

	/**
	 * What the window holds between its title and its buttons.
	 * @returns {HTMLElement}
	 */
	content() {
		return document.createElement('div');
	}

	/**
	 * The setting as it goes back.
	 * @returns {object}
	 */
	written() {
		return structuredClone(this.settings);
	}

	/**
	 * What OK answers — the setting as it goes back (Settings).
	 * @returns {object}
	 */
	response() {
		return { Settings: this.written() };
	}

	/**
	 * The window built, put up and drawn — once the window's own parts stand; a refusal said over it.
	 * @param {string} caption
	 * @param {string} className
	 * @param {[string, string]} [captions] OK's and Cancel's
	 */
	open(caption, className, captions = [labelText('&OK'), _('Cancel')]) {
		const dialog = /** @type {any} */ (document.createElement('wa-dialog'));
		dialog.className = `settings-window ${className}`;
		dialog.label = caption;
		this.ok = button(captions[0], () => this.questions.answer(dialog, this.request, this.response()), 'brand');
		const cancel = button(captions[1], () => this.close());
		for (const button of [this.ok, cancel])
			button.slot = 'footer';
		dialog.append(this.content(), this.ok, cancel);
		dialog.addEventListener('wa-hide', (/** @type {Event} */ event) => {
			if (event.target !== dialog || dialog.answered || this.aside)
				return;
			if (this.waiting !== null) {
				event.preventDefault();
				return;
			}
			this.close();
		});
		this.dialog = dialog;
		document.body.append(dialog);
		dialog.open = true;
		if (this.request.Error) {
			this.questions.dialog(caption, String(this.request.Error),
				[{ caption: labelText('&OK'), answer: {}, preferred: true }], null);
		}
	}

	/**
	 * Asked again — after an act: a reference's fields (Expanded), the value chosen (Chosen); set aside, the same
	 * question on each frame, until its tab is active again or the value comes.
	 * @param {ProtocolNode} request
	 */
	ask(request) {
		this.request = request;
		const acted = this.waiting;
		this.waiting = null;
		const chosen = request.Chosen;
		const done = chosen !== undefined ? this.choosing.get(chosen.Choice) : undefined;
		const active = this.questions.session.frame?.ActiveTab;
		// A choice form opened — a tab of its own: the window steps aside for it, unanswered.
		if (acted?.act === Act.Choose && done === undefined && active !== this.tab) {
			this.aside = true;
			this.dialog.open = false;
		}
		else if (this.aside && (done !== undefined || active === this.tab)) {
			this.aside = false;
			this.dialog.open = true;
		}
		this.fields.takeReferences(request.References);
		if (acted?.act === Act.Expand && acted.field !== undefined && request.Expanded !== undefined)
			this.fields.opened(acted.field, request);
		if (done !== undefined) {
			this.choosing.delete(chosen.Choice);
			const { Presentation: presentation, ...value } = chosen.Value ?? {};
			done(value, String(presentation ?? ''));
		}
		this.enable();
	}

	/**
	 * A value chosen by the server — false while another act is answered.
	 * @param {string} type
	 * @param {object | undefined} value the value in the cell — the kind's empty one when there is none
	 * @param {(value: object, presentation: string) => void} done
	 */
	choose(type, value, done) {
		if (this.waiting !== null)
			return false;
		const choice = ++this.choice;
		this.choosing.set(choice, done);
		this.act({ act: Act.Choose }, { Type: type, Value: value, Choice: choice });
		return true;
	}

	/**
	 * An act answered: the server does it and asks again.
	 * @param {{act: number, field?: Field}} waiting
	 * @param {object} args
	 */
	act(waiting, args) {
		this.waiting = waiting;
		this.enable();
		this.questions.session.call('respond', { Id: this.request.Id, Response: { Act: waiting.act, ...args } }).catch(() => {
			this.waiting = null;
			this.enable();
		});
	}

	/** The window closed — answered with no setting. */
	close() {
		this.questions.answer(this.dialog, this.request, {});
	}

	/** Nothing pressed while an act is answered. */
	enable() {
		this.ok.disabled = this.waiting !== null;
	}
}

/**
 * A window of its own over the settings window — a part of a line edited in it (the thin client's condition, fields
 * and appearance windows, each an ibDialog over the editor): OK takes what was done in it, Cancel, Escape and × take
 * nothing. The content is the caller's and outlives the window — shown again in the next one.
 * @param {string} caption
 * @param {string} className
 * @param {HTMLElement} content
 * @returns {Promise<boolean>} whether OK was pressed
 */
export function editWindow(caption, className, content) {
	const dialog = /** @type {any} */ (document.createElement('wa-dialog'));
	dialog.className = `settings-window ${className}`;
	dialog.label = caption;
	return new Promise((resolve) => {
		let taken = false;
		const ok = button(labelText('&OK'), () => {
			taken = true;
			dialog.open = false;
		}, 'brand');
		const cancel = button(_('Cancel'), () => { dialog.open = false; });
		for (const one of [ok, cancel])
			one.slot = 'footer';
		dialog.addEventListener('wa-after-hide', (/** @type {Event} */ event) => {
			if (event.target !== dialog)
				return;
			dialog.remove();
			resolve(taken);
		});
		dialog.append(content, ok, cancel);
		document.body.append(dialog);
		dialog.open = true;
	});
}

/**
 * Pages under tabs, the one asked for shown, else the first. Named once the tabs are loaded, not left to the group nor
 * set before (Web Awesome 3.14): the group takes its tab as it first comes into view, by the tabs' `panel` — and a
 * tab not loaded yet (the first window with tabs) has none, so the group came up on no tab at all; set after, its
 * watch on `active` takes it.
 * @param {[string, string, HTMLElement][]} pages each name, caption and content
 * @param {string} [active]
 */
export function tabGroup(pages, active = pages[0]?.[0]) {
	const group = /** @type {any} */ (document.createElement('wa-tab-group'));
	if (active !== undefined) {
		Promise.all(['wa-tab-group', 'wa-tab', 'wa-tab-panel'].map((name) => customElements.whenDefined(name)))
			.then(() => group.updateComplete)
			.then(() => { group.active = active; });
	}
	for (const [name, caption, content] of pages) {
		const tab = document.createElement('wa-tab');
		tab.slot = 'nav';
		tab.setAttribute('panel', name);
		tab.textContent = caption;
		const panel = document.createElement('wa-tab-panel');
		panel.setAttribute('name', name);
		panel.append(content);
		group.append(tab, panel);
	}
	return group;
}

/**
 * A setting's part as it goes back — an empty one left out: absence reads back as none (or as what the storey above
 * says).
 * @param {any} node
 * @param {string[]} names the parts that are runs of lines
 */
export function withoutEmpty(node, names) {
	for (const name of names) {
		if ((node[name]?.NodeChildren?.length ?? 0) === 0)
			delete node[name];
	}
}

/**
 * A button of a settings window's footer.
 * @param {string} caption
 * @param {() => void} press
 * @param {string} [variant]
 */
function button(caption, press, variant = 'neutral') {
	const button = /** @type {any} */ (document.createElement('wa-button'));
	button.setAttribute('variant', variant);
	button.setAttribute('size', 's');
	button.textContent = caption;
	button.addEventListener('click', press);
	return button;
}
