// WHAT THE SERVER ASKS THE PERSON — the frame's Request, by its Kind (docs/public/client-protocol.md, Questions to the
// person), answered with respond {Id, Response}. The server's code waits where it asked, so a question is never left
// standing: one this page cannot ask yet is answered as cancelled — the Response empty — and the person is told why.

import { drawSheet } from '../gridbox/sheet.js';
import { calculator, calendar } from './simpleChoice.js';

/** @typedef {import('../protocol/session.js').Session} Session */
/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

// The kinds asked here (ibRequest::ibKind).
const message = 1;
const choice = 2;
const menu = 9;
const simpleChoice = 12;
const error = 14;
const print = 15;

// A message's buttons — wx's flags in its Style, wx's codes in the answer (ibMessage).
const buttons = [
	{ flag: 0x02, code: 2, caption: 'Yes' },
	{ flag: 0x08, code: 8, caption: 'No' },
	{ flag: 0x04, code: 4, caption: 'OK' },
	{ flag: 0x10, code: 16, caption: 'Cancel' },
];
// The button a message's Enter presses, when it is not the first (wxNO_DEFAULT, wxCANCEL_DEFAULT).
const noDefault = 0x80;
const cancelDefault = 0x80000000;

export class Questions {
	/** @param {Session} session */
	constructor(session) {
		this.session = session;
		/** @type {number | undefined} the request asked now — a frame drawn again does not ask it twice */
		this.asked = undefined;
		/** @type {any} the dialog or the menu it is asked in, until it is answered */
		this.shown = null;
		/** where the person last pressed — a menu is asked there */
		this.pointer = { x: 0, y: 0 };
		/**
		 * @type {Element | null} the field the person last pressed in — a calculator or a calendar drops under it. Taken
		 * at the press: a table's cell editor is gone by the time the question comes, its cell is not.
		 */
		this.pressed = null;
		document.addEventListener('pointerdown', (event) => {
			this.pointer = { x: event.clientX, y: event.clientY };
			const target = event.composedPath()[0];
			this.pressed = target instanceof Element ? target.closest('wa-input, wa-textarea, .tabulator-cell') : null;
		}, true);
	}

	/** @param {ProtocolNode | undefined} request */
	ask(request) {
		const id = request?.Id;
		if (id === this.asked)
			return;
		// The question before is not asked any more — answered, or let go by the server: it leaves the screen, and
		// nobody answers it.
		this.withdraw();
		this.asked = id;
		if (request === undefined || id === undefined)
			return;
		if (request.Kind === message)
			this.message(request);
		else if (request.Kind === choice)
			this.choice(request);
		else if (request.Kind === menu)
			this.menu(request);
		else if (request.Kind === simpleChoice)
			this.simpleChoice(request);
		else if (request.Kind === error)
			this.dialog(request.Caption ?? 'Error', request.Text ?? '', [{ caption: 'Close', answer: { Act: 1 } }], request);
		else if (request.Kind === print)
			this.print(request);
		else
			this.dialog('Not in the browser yet', `This question (${request.Kind}) cannot be asked here yet; it is answered as cancelled.`,
				[{ caption: 'OK', answer: {} }], request);
	}

	/**
	 * A message box, as the thin client's (wxMessageBox): Enter presses its default button — the first, or No or Cancel
	 * when the Style says so. Closed by Escape or ×, it answers as the box does: Cancel when it offers Cancel, OK when OK
	 * is all it offers; a Yes/No without Cancel is not closed but answered.
	 * @param {ProtocolNode} request
	 */
	message(request) {
		const style = request.Style ?? 0x04;
		const offered = buttons.filter((one) => (style & one.flag) !== 0);
		const shown = offered.length > 0 ? offered : [buttons[2]];
		const code = (/** @type {number} */ wanted) => shown.find((one) => one.code === wanted);
		const preferred = ((style & noDefault) !== 0 && code(8)) || ((style & cancelDefault) !== 0 && code(16)) || shown[0];
		const closed = code(16) ?? (shown.length === 1 ? code(4) : undefined);
		this.dialog(request.Caption ?? '', request.Text ?? '',
			shown.map((one) => ({ caption: one.caption, answer: { Button: one.code }, preferred: one === preferred })),
			request, closed !== undefined ? { Button: closed.code } : null);
	}

	/** @param {ProtocolNode} request */
	choice(request) {
		const items = request.NodeChildren ?? [];
		const list = document.createElement('div');
		list.className = 'choice-list';
		const dialog = this.dialog(request.Caption ?? '', '', [{ caption: 'Cancel', answer: {} }], request);
		for (const item of items) {
			const button = document.createElement('wa-button');
			button.setAttribute('appearance', 'plain');
			button.textContent = item.Caption ?? '';
			button.addEventListener('click', () => this.answer(dialog, request, { Id: item.Id }));
			list.append(button);
		}
		dialog.querySelector('.text')?.replaceWith(list);
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
	 * it is the window's own box (wxMessageBox), and closing it answers nobody.
	 * @param {string} caption
	 * @param {string} text
	 * @param {{caption: string, variant?: string, answer: object, preferred?: boolean}[]} actions
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
		for (const action of actions) {
			const button = document.createElement('wa-button');
			button.slot = 'footer';
			button.setAttribute('variant', action.variant ?? (action.preferred ? 'brand' : 'neutral'));
			button.textContent = action.caption;
			button.addEventListener('click', () => this.answer(dialog, request, action.answer));
			dialog.append(button);
		}
		// Not by autofocus: the button Web Awesome focuses loses the focus to the <dialog> as it comes up (3.14).
		const preferred = actions.find((action) => action.preferred === true);
		dialog.addEventListener('keydown', (/** @type {KeyboardEvent} */ event) => {
			if (preferred === undefined || event.key !== 'Enter'
				|| event.composedPath().some((node) => node instanceof Element && node.localName === 'wa-button'))
				return;
			event.preventDefault();
			this.answer(dialog, request, preferred.answer);
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

	/** The question on show taken off the screen, answered by nobody. */
	withdraw() {
		const shown = this.shown;
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
