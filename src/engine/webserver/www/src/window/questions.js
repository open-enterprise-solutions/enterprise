// WHAT THE SERVER ASKS THE PERSON — the frame's Request, by its Kind (docs/public/client-protocol.md, Questions to the
// person), answered with respond {Id, Response}. The server's code waits where it asked, so a question is never left
// standing: one this page cannot ask yet is answered as cancelled — the Response empty — and the person is told why.
// Five windows outlive their answers — Change form's (formEditor.js), the saved settings' shelf (savedSettings.js), a
// list's and a report's settings (listSettings.js, composerSettings.js), a table's advanced search (searchWindow.js): each
// act is one, and the window is asked again.

import { drawSheet } from '../spreadsheet/sheet.js';
import { _, format, labelText } from '../localization.js';
import { ComposerSettings } from './composerSettings.js';
import { editFocused } from './editFocused.js';
import { acceptOf, downloadFile, saveFile, uploadFile } from './files.js';
import { FormEditor } from './formEditor.js';
import { isValid, scheduleEditor } from './jobSchedule.js';
import { ListSettings } from './listSettings.js';
import { SavedSettings } from './savedSettings.js';
import { SearchWindow } from './searchWindow.js';
import { calculator, calendar } from './simpleChoice.js';
import { typeEditor } from './typeSelector.js';

/** @typedef {import('../protocol/session.js').Session} Session */
/** @typedef {import('../protocol/pictures.js').Pictures} Pictures */
/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

// The kinds of question (ibRequest::ibKind) the application asks. A designer's frame asks three more of its own —
// ApplyChange 17, ObjectKind 18, PredefinedSelector 19, answered by the thin designer's frame over the client's
// (appstudio, ibFrontendMainFrameDesigner::ShowRequest): a designer's page would answer them in its own Questions over
// these, and the application is never asked them.
const Kind = Object.freeze({
	Message: 1, Choice: 2, Edit: 4, File: 5, Generation: 6, ViewMode: 7, SavedSettings: 8, Menu: 9, FormEditor: 10,
	ListSettings: 11, SimpleChoice: 12, ComposerSettings: 13, Error: 14, Print: 15, Schedule: 16, Search: 20,
	TypeSelector: 21,
});

// A message's buttons — wx's flags in its Style, wx's codes in the answer (ibMessage); said as they are asked for, in
// the language read by then, and as the thin's message box says them: wx's stock labels with their mnemonic
// (wxSTOCK_FOR_BUTTON), Cancel without one (ibGetStockLabel on MSW).
const buttons = () => [
	{ flag: 0x02, code: 2, caption: labelText('&Yes') },
	{ flag: 0x08, code: 8, caption: labelText('&No') },
	{ flag: 0x04, code: 4, caption: labelText('&OK') },
	{ flag: 0x10, code: 16, caption: _('Cancel') },
];
// The button a message's Enter presses, when it is not the first (wxNO_DEFAULT, wxCANCEL_DEFAULT).
const noDefault = 0x80;
const cancelDefault = 0x80000000;

export class Questions {
	/**
	 * @param {Session} session
	 * @param {Pictures} pictures
	 */
	constructor(session, pictures) {
		this.session = session;
		this.pictures = pictures;
		/** @type {number | undefined} the request asked now — a frame drawn again does not ask it twice */
		this.asked = undefined;
		/** @type {any} the dialog or the menu it is asked in, until it is answered */
		this.shown = null;
		/** where the person last pressed — a menu is asked there */
		this.pointer = { x: 0, y: 0 };
		/**
		 * @type {Element | null} the field the person last pressed in — a calculator, a calendar or a list drops under it. Taken
		 * at the press: a table's cell editor is gone by the time the question comes, its cell is not.
		 */
		this.pressed = null;
		/** @type {any} the form the person last pressed in — Change form is asked of it */
		this.pressedForm = null;
		/**
		 * @type {{kind: number, window: {dialog: any, ask: (request: ProtocolNode) => void, waiting?: unknown,
		 *   aside?: boolean}} | null} the window open through its acts — Change form's, the shelf's, a list's settings:
		 * each act is an answer, and the server asks again of the same kind; `waiting` is the act answered, null between
		 */
		this.lasting = null;
		document.addEventListener('pointerdown', (event) => {
			this.pointer = { x: event.clientX, y: event.clientY };
			const path = event.composedPath();
			const target = path[0];
			this.pressed = target instanceof Element ? target.closest('wa-input, wa-textarea, .tabulator-cell') : null;
			this.pressedForm = path.find((node) => node instanceof Element && node.localName === 'oes-clientform') ?? this.pressedForm;
		}, true);
	}

	/** @param {ProtocolNode | undefined} request */
	ask(request) {
		const id = request?.Id;
		// A window open through its acts is asked again after each, of the same kind: it takes the new question — and,
		// set aside while a choice form chooses, the same one on each frame, with what was chosen once it is.
		const lasting = this.lasting?.window;
		const open = lasting !== undefined && !lasting.dialog.answered;
		if (open && request !== undefined && request.Kind === this.lasting?.kind
			&& (id !== this.asked || lasting.aside === true)) {
			this.asked = id;
			this.shown = lasting.dialog;
			lasting.ask(request);
			return;
		}
		if (id === this.asked)
			return;
		// The question before is not asked any more — answered, or let go by the server: it leaves the screen, and
		// nobody answers it. A window open through its acts goes with it, unless its act asked this one on the way (a
		// quick choice): that is asked over it.
		const kept = open && request !== undefined && (lasting.waiting ?? null) !== null;
		if (kept && this.shown === lasting.dialog)
			this.shown = null;
		this.withdraw();
		if (open && !kept)
			this.withdraw(lasting.dialog);
		this.asked = id;
		if (request === undefined || id === undefined)
			return;
		// By its kind, as the thin client's frame asks it (ibFrontendMainFrame::ShowRequest).
		switch (request.Kind) {
		case Kind.Message:
			this.message(request);
			break;
		case Kind.Error:
			this.dialog(request.Caption ?? _('Error'), request.Text ?? '', [{ caption: _('Close'), answer: { Act: 1 } }], request);
			break;
		case Kind.Print:
			this.print(request);
			break;
		case Kind.Choice:
			this.choice(request);
			break;
		case Kind.Edit:
			this.edit(request);
			break;
		case Kind.File:
			this.file(request);
			break;
		case Kind.Generation:
			this.generation(request);
			break;
		case Kind.ViewMode:
			this.viewMode(request);
			break;
		case Kind.Menu:
			this.menu(request);
			break;
		case Kind.SimpleChoice:
			this.simpleChoice(request);
			break;
		case Kind.FormEditor:
			this.formEditor(request);
			break;
		case Kind.SavedSettings:
			this.savedSettings(request);
			break;
		case Kind.ListSettings:
		case Kind.ComposerSettings:
			this.settings(request);
			break;
		case Kind.Schedule:
			this.schedule(request);
			break;
		case Kind.Search:
			this.search(request);
			break;
		case Kind.TypeSelector:
			this.typeSelector(request);
			break;
		default:
			this.dialog(_('Not in the browser yet'),
				format(_('This question (%d) cannot be asked here yet; it is answered as cancelled.'), request.Kind),
				[{ caption: labelText('&OK'), answer: {} }], request);
		}
	}

	/**
	 * A message box, as the thin client's (ibMessageBox): Enter presses its default button — the first, or No or Cancel
	 * when the Style says so. Closed by Escape or ×, it answers as the box does: Cancel when it offers Cancel, OK when OK
	 * is all it offers; a Yes/No without Cancel is not closed but answered.
	 * @param {ProtocolNode} request
	 */
	message(request) {
		const style = request.Style ?? 0x04;
		const all = buttons();
		const offered = all.filter((one) => (style & one.flag) !== 0);
		const shown = offered.length > 0 ? offered : [all[2]];
		const code = (/** @type {number} */ wanted) => shown.find((one) => one.code === wanted);
		const preferred = ((style & noDefault) !== 0 && code(8)) || ((style & cancelDefault) !== 0 && code(16)) || shown[0];
		const closed = code(16) ?? (shown.length === 1 ? code(4) : undefined);
		this.dialog(request.Caption ?? '', request.Text ?? '',
			shown.map((one) => ({ caption: one.caption, answer: { Button: one.code }, preferred: one === preferred })),
			request, closed !== undefined ? { Button: closed.code } : null);
	}

	/**
	 * One of a list, as the thin client asks it (ibDialogSelectDataType — each item with its picture, its pictures fetched
	 * first): the one Selected chosen to begin with, else the first; OK — or a double click on an item — answers with it,
	 * Cancel and closing with none. SEVERAL (Multiple, the thin client's ibMultiChoiceDialog): each ticked or not, those
	 * Selected ticked to begin with; OK answers with the ticked ones' Ids in the order offered — none ticked, none.
	 * @param {ProtocolNode} request
	 */
	async choice(request) {
		if (request.Quick === true) {
			this.quickChoice(request);
			return;
		}
		await this.pictures.fetchFor(request).catch(() => {});
		// Let go by the server while its pictures came.
		if (this.asked !== request.Id)
			return;
		const items = request.NodeChildren ?? [];
		const several = request.Multiple === true;
		const list = document.createElement('div');
		list.className = 'choice-list';
		const ticked = several ? items.filter((item) => item.Selected === true) : [];
		const first = items.find((item) => item.Selected === true) ?? items[0];
		/** @type {{Id?: number, Ids?: number[]}} */
		const answer = several ? { Ids: ticked.map((item) => item.Id) } : first !== undefined ? { Id: first.Id } : {};
		const dialog = this.dialog(request.Caption ?? '', '', [
			{ caption: labelText('&OK'), answer, preferred: true },
			{ caption: _('Cancel'), answer: {} },
		], request);
		/** @type {HTMLInputElement[]} */
		const inputs = [];
		for (const item of items) {
			const label = document.createElement('label');
			const input = document.createElement('input');
			input.type = several ? 'checkbox' : 'radio';
			input.name = 'choice';
			input.checked = several ? ticked.includes(item) : item === first;
			input.addEventListener('change', () => {
				if (several)
					answer.Ids = items.filter((_, at) => inputs[at].checked).map((one) => one.Id);
				else
					answer.Id = item.Id;
			});
			inputs.push(input);
			label.append(input);
			const url = this.pictures.url(item.Picture);
			if (url) {
				const image = document.createElement('img');
				image.src = url;
				image.alt = '';
				label.append(image);
			}
			label.append(item.Caption ?? '');
			if (!several)
				label.addEventListener('dblclick', () => this.answer(dialog, request, { Id: item.Id }));
			list.append(label);
		}
		dialog.querySelector('.text')?.replaceWith(list);
	}

	/**
	 * How a table shows its rows, as the thin client's window asks it (wxTableViewModeDialog): the three, the one in force
	 * chosen. OK answers with the one chosen, Cancel and closing with none.
	 * @param {ProtocolNode} request
	 */
	viewMode(request) {
		const list = document.createElement('div');
		list.className = 'view-modes';
		for (const [mode, caption] of /** @type {[number, string][]} */ ([[0, _('Tree')], [1, _('Hierarchy')], [2, _('List')]])) {
			const label = document.createElement('label');
			const radio = document.createElement('input');
			radio.type = 'radio';
			radio.name = 'view-mode';
			radio.value = String(mode);
			radio.checked = mode === request.Mode;
			label.append(radio, caption);
			list.append(label);
		}
		const answer = { Mode: request.Mode };
		list.addEventListener('change', (event) => {
			answer.Mode = Number(/** @type {HTMLInputElement} */ (event.target).value);
		});
		const dialog = this.dialog(_('View mode'), '', [
			{ caption: labelText('&OK'), answer, preferred: true },
			{ caption: _('Cancel'), answer: {} },
		], request);
		dialog.querySelector('.text')?.replaceWith(list);
	}

	/**
	 * A table's advanced search — a settings window over one condition (the thin client's ibDialogSearch): open through
	 * its acts, a value of the server's choosing chosen on the way, answered and the window asked again.
	 * @param {ProtocolNode} request
	 */
	search(request) {
		this.lasting = { kind: Kind.Search, window: new SearchWindow(this, request) };
		this.shown = this.lasting.window.dialog;
	}

	/**
	 * Done by the element under the focus, as the Edit menu's (editFocused). Answered with nothing: a field changed sends
	 * its Change as it does when typed into.
	 * @param {ProtocolNode} request
	 */
	async edit(request) {
		await editFocused(Number(request.Command));
		this.respond(request, {});
	}

	/**
	 * A FILE, as the thin client's file dialog asks it. With File — that one of the server's put where the person keeps
	 * it: its name read first, the place asked as Save is pressed (the browser's save dialog opens from a press alone),
	 * answered Done. Without — one of the person's chosen, by the masks offered, put in the session's temporary storage
	 * and answered with its id (File). Cancelled — neither.
	 * @param {ProtocolNode} request
	 */
	async file(request) {
		const taken = request.File ?? '';
		if (taken !== '') {
			const first = await this.session.call('download', { File: taken, Part: 0 }).catch(() => null);
			if (this.asked !== request.Id)
				return;
			const name = first?.Name ?? taken;
			this.dialog(request.Caption ?? _('Save'), name, [
				{ caption: _('Save'), preferred: true,
					answer: async () => await saveFile(() => downloadFile(this.session, taken), name) ? { Done: true } : null },
				{ caption: _('Cancel'), answer: {} },
			], request);
			return;
		}
		const offered = request.NodeChildren ?? [];
		const input = document.createElement('input');
		input.type = 'file';
		input.accept = acceptOf(offered.map((item) => item.Mask ?? ''));
		const dialog = this.dialog(request.Caption ?? _('Open'), '', [
			{ caption: labelText('&OK'), preferred: true, answer: async () => {
				const chosen = input.files?.[0];
				return chosen !== undefined ? { File: await uploadFile(this.session, chosen) } : null;
			} },
			{ caption: _('Cancel'), answer: {} },
		], request);
		const body = document.createElement('div');
		body.className = 'file-choice';
		// What may be chosen, as the thin client's filter names it: «Title (Mask)».
		const kinds = document.createElement('div');
		kinds.className = 'file-kinds';
		kinds.textContent = offered.map((item) => `${item.Title ?? ''} (${item.Mask ?? ''})`).join(', ');
		body.append(input, kinds);
		dialog.querySelector('.text')?.replaceWith(body);
	}

	/**
	 * What a new object is based on, as the thin client's window asks it (ibDialogGeneration): the objects this one
	 * generates, each by its picture and caption — one pressed is the answer, as one selected there closes it; OK answers
	 * with the one the keys stand on, none before they stand on one; Cancel and closing — none.
	 * @param {ProtocolNode} request
	 */
	async generation(request) {
		await this.pictures.fetchFor(request).catch(() => {});
		// Let go by the server while its pictures came.
		if (this.asked !== request.Id)
			return;
		/** @type {{Id?: number}} */
		const answer = {};
		// Its own word, not the stock OK (generation.cpp writes `_("Ok")`).
		const dialog = this.dialog(_('Select generation'), '', [
			{ caption: _('Ok'), answer, preferred: true },
			{ caption: _('Cancel'), answer: {} },
		], request);
		const list = document.createElement('div');
		list.className = 'choice-list';
		for (const item of request.NodeChildren ?? []) {
			const button = document.createElement('button');
			button.type = 'button';
			const url = this.pictures.url(item.Picture);
			if (url) {
				const image = document.createElement('img');
				image.src = url;
				image.alt = '';
				button.append(image);
			}
			button.append(item.Caption ?? '');
			button.addEventListener('focus', () => {
				answer.Id = item.Id;
			});
			button.addEventListener('click', () => this.answer(dialog, request, { Id: item.Id }));
			list.append(button);
		}
		dialog.querySelector('.text')?.replaceWith(list);
	}

	/**
	 * Change form, as the thin client's frame asks it — over the form the person is in; with none, answered with nothing.
	 * Its pictures fetched first: the controls' classes' and its own.
	 * @param {ProtocolNode} request
	 */
	async formEditor(request) {
		await this.pictures.fetchFor(request).catch(() => {});
		if (this.asked !== request.Id)
			return;
		if (this.formOf() === null) {
			this.respond(request, {});
			return;
		}
		this.lasting = { kind: Kind.FormEditor, window: new FormEditor(this, request) };
		this.shown = this.lasting.window.dialog;
	}

	/**
	 * The shelf of a list's or a report's saved settings, to save into or restore from (the thin client's
	 * ibDialogSavedSettings) — open through its acts, each answered and the shelf asked again.
	 * @param {ProtocolNode} request
	 */
	savedSettings(request) {
		this.lasting = { kind: Kind.SavedSettings, window: new SavedSettings(this, request) };
		this.shown = this.lasting.window.dialog;
	}

	/**
	 * A list's settings — its filter, sort, grouping (the thin client's ibDialogListSettings) — or a report's — its
	 * structure and each node's (ibDialogComposerSettings): open through their acts, a reference's fields asked, a value
	 * chosen, each answered and the window asked again. The fields' pictures first.
	 * @param {ProtocolNode} request
	 */
	async settings(request) {
		await this.pictures.fetchFor(request).catch(() => {});
		if (this.asked !== request.Id)
			return;
		const opened = request.Kind === Kind.ListSettings ? new ListSettings(this, request) : new ComposerSettings(this, request);
		this.lasting = { kind: request.Kind, window: opened };
		this.shown = opened.dialog;
	}

	/**
	 * A schedule, as the thin client's window asks it (ibDialogJobSchedule): its four tabs over the schedule asked, its
	 * sentence under them. OK answers with it edited — one that can never run is refused in a box, the window left open;
	 * Cancel and closing — with none.
	 * @param {ProtocolNode} request
	 */
	schedule(request) {
		const editor = scheduleEditor(request.Schedule ?? {});
		const dialog = this.dialog(_('Schedule'), '', [
			{ caption: labelText('&OK'), preferred: true, answer: async () => {
				const schedule = editor.edited();
				if (isValid(schedule))
					return { Schedule: schedule };
				this.dialog(_('Schedule'), _('This schedule can never run: check the interval and the time window.'),
					[{ caption: labelText('&OK'), answer: {}, preferred: true }], null);
				return null;
			} },
			{ caption: _('Cancel'), answer: {} },
		], request);
		dialog.classList.add('job-schedule-dialog');
		dialog.querySelector('.text')?.replaceWith(editor.element);
	}

	/**
	 * A type description, as the thin client's type picker asks it (ibShowTypeSelector) over a field that holds one: the
	 * types offered named and drawn as the server names them (Classes), their pictures first. OK answers with the
	 * description edited, Cancel and closing with none.
	 * @param {ProtocolNode} request
	 */
	async typeSelector(request) {
		await this.pictures.fetchFor(request).catch(() => {});
		if (this.asked !== request.Id)
			return;
		const editor = typeEditor(request.Value ?? {}, request.Classes?.NodeChildren ?? [], (picture) => this.pictures.url(picture));
		const dialog = this.dialog(_('Select data type'), '', [
			{ caption: labelText('&OK'), preferred: true, answer: async () => ({ Value: editor.edited() }) },
			{ caption: _('Cancel'), answer: {} },
		], request);
		dialog.classList.add('type-selector-dialog');
		dialog.querySelector('.text')?.replaceWith(editor.element);
	}

	/**
	 * The view of the form the person is in (the thin client's frame takes the active tab's form, or the start page's
	 * cell the person is in): the one last pressed in, else the active tab's, else the start page's first; none — null.
	 * @returns {ProtocolNode | null}
	 */
	formOf() {
		const view = this.session.frame?.View;
		/** @type {ProtocolNode[]} */
		const forms = view === undefined ? []
			: view.NodeType === 'ClientForm' ? [view]
				: (view.Columns?.NodeChildren ?? []).flatMap((column) => column.NodeChildren ?? [])
					.map((cell) => cell.View).filter((one) => one !== undefined);
		const key = this.pressedForm?.context?.key;
		return forms.find((form) => form.Key === key) ?? forms[0] ?? null;
	}

	/**
	 * A popup menu where the person pressed, kept inside the window; a submenu opens in place. Dismissed — a press
	 * outside it, Escape — it is answered with no item.
	 * @param {ProtocolNode} request
	 */
	menu(request) {
		const { popup, pick } = this.popup(request, 'popup-menu');
		popup.append(...menuItems(request.NodeChildren ?? [], (id) => pick({ Id: id })));
		popup.showPopover();
		const size = popup.getBoundingClientRect();
		popup.style.left = `${Math.max(0, Math.min(this.pointer.x, innerWidth - size.width))}px`;
		popup.style.top = `${Math.max(0, Math.min(this.pointer.y, innerHeight - size.height))}px`;
	}

	/**
	 * A number's calculator or a date's calendar, dropped under the field pressed, its right edge on the field's (the
	 * thin client's). The value chosen goes back with the type it came with. Which of the two, the Value's payload
	 * says: a number travels as one, a date as its text.
	 * @param {ProtocolNode} request
	 */
	simpleChoice(request) {
		const value = request.Value ?? {};
		const { popup, pick } = this.popup(request, 'simple-choice');
		/** @param {string | number} chosen */
		const take = (chosen) => pick({ Value: { t: value.t, v: chosen } });
		popup.append(typeof value.v === 'number'
			// Sent as a number again: exact while it has no more than 15 digits, as a field's figures do.
			? calculator(numberText(value.v), (chosen) => take(Number(chosen)))
			: calendar(String(value.v ?? ''), take));
		this.dropUnder(popup);
	}

	/**
	 * A choice asked Quick: a reference's items, or those that match what was typed, dropped under the field pressed and
	 * as wide as it (the thin client's quick choice). A click or Enter picks one, answered by its Id; the arrows go
	 * between them. Dismissed — a press outside it, Escape — it is answered with no Id.
	 * @param {ProtocolNode} request
	 */
	quickChoice(request) {
		const items = request.NodeChildren ?? [];
		const { popup, pick } = this.popup(request, 'popup-menu quick-choice');
		const buttons = menuItems(items, (id) => pick({ Id: id }));
		popup.append(...buttons);
		popup.addEventListener('keydown', (/** @type {KeyboardEvent} */ event) => {
			const step = event.key === 'ArrowDown' ? 1 : event.key === 'ArrowUp' ? -1 : 0;
			if (step === 0)
				return;
			event.preventDefault();
			const at = buttons.indexOf(/** @type {HTMLElement} */ (document.activeElement));
			buttons[Math.max(0, Math.min(buttons.length - 1, at + step))]?.focus();
		});
		if (this.pressed?.isConnected)
			popup.style.minWidth = `${this.pressed.getBoundingClientRect().width}px`;
		this.dropUnder(popup);
		const selected = items.findIndex((item) => item.Selected === true);
		buttons[selected]?.classList.add('selected');
		(buttons[selected] ?? buttons[0])?.focus();
	}

	/**
	 * A popup shown under the field pressed, its right edge on the field's (the thin client's); above it when there is
	 * no room below, and where the person pressed when the field is gone.
	 * @param {any} popup
	 */
	dropUnder(popup) {
		popup.showPopover();
		const under = this.pressed?.isConnected
			? this.pressed.getBoundingClientRect() : new DOMRect(this.pointer.x, this.pointer.y, 0, 0);
		const size = popup.getBoundingClientRect();
		popup.style.left = `${Math.max(0, Math.min(under.right - size.width, innerWidth - size.width))}px`;
		popup.style.top = `${under.bottom + size.height <= innerHeight ? under.bottom : Math.max(0, under.top - size.height)}px`;
	}

	/**
	 * A popup over the window, as the thin client's transient ones: a press outside it or Escape dismisses it, and it
	 * is answered then with nothing; `pick` answers it.
	 * @param {ProtocolNode} request
	 * @param {string} className
	 */
	popup(request, className) {
		const popup = /** @type {any} */ (document.createElement('div'));
		popup.className = className;
		popup.popover = 'auto';
		/** @param {object} response */
		const pick = (response) => {
			popup.answered = true;
			popup.hidePopover();
			this.respond(request, response);
		};
		popup.addEventListener('toggle', (event) => {
			if (/** @type {ToggleEvent} */ (event).newState !== 'closed')
				return;
			if (!popup.answered)
				this.respond(request, {});
			popup.remove();
		});
		document.body.append(popup);
		this.shown = popup;
		return { popup, pick };
	}

	/**
	 * A script's sheet printed: drawn for the printer alone, the browser's print dialog asked, the sheet taken away
	 * after. The browser does not say whether the person printed or cancelled, so the answer is Done.
	 * @param {ProtocolNode} request
	 */
	print(request) {
		const sheet = document.createElement('div');
		sheet.className = 'print-sheet';
		sheet.append(drawSheet(request.Sheet ?? {}, { gridLines: false, onCell: () => {} }));
		document.body.append(sheet);
		document.body.classList.add('printing');
		let printed = false;
		const done = () => {
			if (printed)
				return;
			printed = true;
			sheet.remove();
			document.body.classList.remove('printing');
			this.respond(request, { Done: true });
		};
		addEventListener('afterprint', done, { once: true });
		// After the frame is drawn: the dialog holds the page until it closes, where the browser has one that does.
		setTimeout(() => {
			window.print();
			done();
		}, 0);
	}

	/**
	 * A dialog with its buttons, each answering the request its own way; the preferred one is drawn as the default and
	 * takes Enter pressed anywhere in the dialog but on another button. Closed by Escape or ×, it is answered with what
	 * it answers closed — as cancelled unless said otherwise; with none, it is not closed and has no ×. With no request
	 * it is the window's own box (ibMessageBox), and closing it answers nobody. An action whose answer is made as it is
	 * pressed (a file put up, a place chosen) shows it is working meanwhile, and leaves the dialog open when it makes
	 * none (null).
	 * @param {string} caption
	 * @param {string} text
	 * @param {{caption: string, variant?: string, answer: object | (() => Promise<object | null>), preferred?: boolean}[]} actions
	 * @param {ProtocolNode | null} request
	 * @param {object | null} [closed]
	 */
	dialog(caption, text, actions, request, closed = {}) {
		const dialog = /** @type {any} */ (document.createElement('wa-dialog'));
		dialog.label = caption || document.title;
		dialog.classList.toggle('answered-only', closed === null);
		const body = document.createElement('div');
		body.className = 'text';
		body.textContent = text;
		dialog.append(body);
		/**
		 * @param {typeof actions[number]} action
		 * @param {any} button
		 */
		const press = async (action, button) => {
			if (typeof action.answer !== 'function') {
				this.answer(dialog, request, action.answer);
				return;
			}
			if (button.loading)
				return;
			button.loading = true;
			try {
				const made = await action.answer();
				if (made !== null)
					this.answer(dialog, request, made);
			}
			finally {
				button.loading = false;
			}
		};
		/** @type {Map<typeof actions[number], any>} */
		const pressed = new Map();
		for (const action of actions) {
			const button = document.createElement('wa-button');
			button.slot = 'footer';
			button.setAttribute('variant', action.variant ?? (action.preferred ? 'brand' : 'neutral'));
			button.textContent = action.caption;
			button.addEventListener('click', () => press(action, button));
			pressed.set(action, button);
			dialog.append(button);
		}
		// Not by autofocus: the button Web Awesome focuses loses the focus to the <dialog> as it comes up (3.14).
		const preferred = actions.find((action) => action.preferred === true);
		dialog.addEventListener('keydown', (/** @type {KeyboardEvent} */ event) => {
			if (preferred === undefined || event.key !== 'Enter'
				|| event.composedPath().some((node) => node instanceof Element && node.localName === 'wa-button'))
				return;
			event.preventDefault();
			press(preferred, pressed.get(preferred));
		});
		dialog.addEventListener('wa-hide', (/** @type {Event} */ event) => {
			if (event.target !== dialog || dialog.answered)
				return;
			if (closed === null)
				event.preventDefault();
			else
				this.answer(dialog, request, closed);
		});
		document.body.append(dialog);
		if (request !== null)
			this.shown = dialog;
		dialog.open = true;
		return dialog;
	}

	/**
	 * A text the person types, asked by the window itself (the thin client's ibGetTextFromUser): OK — the text; Cancel,
	 * Escape, × — null.
	 * @param {string} caption
	 * @param {string} label
	 * @param {string} [text] what the field begins with
	 * @returns {Promise<string | null>}
	 */
	textFromUser(caption, label, text = '') {
		return new Promise((resolve) => {
			const input = /** @type {any} */ (document.createElement('wa-input'));
			input.label = label;
			input.value = text;
			const dialog = this.dialog(caption, '', [
				{ caption: labelText('&OK'), preferred: true, answer: async () => {
					resolve(String(input.value ?? ''));
					return {};
				} },
				{ caption: _('Cancel'), answer: {} },
			], null);
			dialog.querySelector('.text')?.replaceWith(input);
			// The field focused when the <dialog> takes the focus as it comes up — once the field is drawn, which it is not
			// yet then (nor loaded, the first time one is met): a dialog opened as it is put in the page is shown by its
			// render watch, with no wa-after-show and no autofocus, and the browser focuses the <dialog> (Web Awesome 3.14).
			const toField = (/** @type {FocusEvent} */ event) => {
				if (event.composedPath()[0] instanceof HTMLDialogElement) {
					dialog.removeEventListener('focusin', toField);
					customElements.whenDefined('wa-input').then(() => input.updateComplete).then(() => input.focus());
				}
			};
			dialog.addEventListener('focusin', toField);
			dialog.addEventListener('wa-after-hide', () => resolve(null), { once: true });
		});
	}

	/**
	 * Yes or No, asked by the window itself (the thin client's message box with wxYES_NO): Escape and × — No.
	 * @param {string} caption
	 * @param {string} text
	 * @returns {Promise<boolean>}
	 */
	yesNo(caption, text) {
		return new Promise((resolve) => {
			const dialog = this.dialog(caption, text, [
				{ caption: _('Yes'), preferred: true, answer: async () => {
					resolve(true);
					return {};
				} },
				{ caption: _('No'), answer: {} },
			], null);
			dialog.addEventListener('wa-after-hide', () => resolve(false), { once: true });
		});
	}

	/**
	 * @param {any} dialog
	 * @param {ProtocolNode | null} request
	 * @param {object} response
	 */
	answer(dialog, request, response) {
		if (dialog.answered)
			return;
		dialog.answered = true;
		dialog.open = false;
		dialog.addEventListener('wa-after-hide', () => dialog.remove(), { once: true });
		if (request !== null)
			this.respond(request, response);
	}

	/**
	 * The answer sent: the server's code goes on from where it asked. The question stays asked until a frame comes
	 * without it — one drawn before the answer has it still, and must not ask it again.
	 * @param {ProtocolNode} request
	 * @param {object} response
	 */
	respond(request, response) {
		this.shown = null;
		this.session.call('respond', { Id: request.Id, Response: response }).catch(() => {});
	}

	/**
	 * The question on show — or the window given — taken off the screen, answered by nobody.
	 * @param {any} [shown]
	 */
	withdraw(shown = this.shown) {
		if (shown === this.shown)
			this.shown = null;
		if (shown === null || shown.answered)
			return;
		shown.answered = true;
		if (shown.popover) {
			shown.hidePopover();
			return;
		}
		shown.open = false;
		shown.addEventListener('wa-after-hide', () => shown.remove(), { once: true });
	}
}

/**
 * A number as the calculator reads it — its digits, never the exponent a very small or large one is printed with.
 * @param {number} number
 */
function numberText(number) {
	const spelled = String(number);
	return /e/i.test(spelled) ? number.toFixed(20).replace(/\.?0+$/, '') : spelled;
}

/**
 * A menu's items: one with items of its own is a submenu, opened in place.
 * @param {ProtocolNode[]} items
 * @param {(id: number) => void} pick
 * @returns {HTMLElement[]}
 */
function menuItems(items, pick) {
	return items.map((item) => {
		const caption = item.Caption ?? '';
		if ((item.NodeChildren?.length ?? 0) > 0) {
			const submenu = document.createElement('details');
			const summary = document.createElement('summary');
			summary.textContent = caption;
			submenu.append(summary, ...menuItems(item.NodeChildren ?? [], pick));
			return submenu;
		}
		const button = document.createElement('button');
		button.type = 'button';
		button.textContent = caption;
		button.disabled = item.Enabled === false;
		if (item.Checked === true)
			button.classList.add('checked');
		button.addEventListener('click', () => pick(item.Id));
		return button;
	});
}
