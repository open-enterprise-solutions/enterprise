// THE DOCUMENT OF A TAB THE SERVER RAISED — what every tab is, whatever its view, as the thin client's (ibTabDocument):
// made for each tab the frame brings by the tab's Kind (createDocument), each kind with a document of its own,
// registered by the module that defines it (defineDocument), as a control is (defineControl). The document draws its
// view into its element and names it in the calls the view's controls make; the window shows one tab's and hides the
// others'. What is done to the document — undo, redo, save — goes to the server's, taken as its tab's Commands say;
// its view goes on paper here.

import { reconcile, dispose } from '../controls/control.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */
/** @typedef {import('../controls/control.js').FormContext} FormContext */
/** @typedef {import('../window/window.js').ClientWindow} ClientWindow */

// The kinds of a tab's view drawn here (ibView::ibKind): a form — the kind no Kind is written for — and a spreadsheet
// document.
const form = 1;
const spreadsheet = 3;

// The commands a tab's document takes (ibFrame::ibCommand) — its tab's Commands say which, now.
export const Command = { Undo: 1, Redo: 2, Save: 3 };

/** @type {Map<number, typeof TabDocument>} a tab's Kind → its document */
const kinds = new Map();

/**
 * A kind of tab, and the document that draws it.
 * @param {number} kind the tab's Kind (ibView::ibKind)
 * @param {typeof TabDocument} document
 */
export function defineDocument(kind, document) {
	kinds.set(kind, document);
}

/**
 * A TAB'S DOCUMENT, BY THE TAB'S KIND — as the thin client's (CreateDocument): the one registered for it; a form's when
 * the tab names none; a view the browser does not draw yet, of a kind none is registered for.
 * @param {ClientWindow} window
 * @param {number | undefined} kind
 * @param {number} tab
 * @returns {TabDocument}
 */
export function createDocument(window, kind, tab) {
	return new (kinds.get(kind ?? form) ?? TabDocument)(window, tab);
}

export class TabDocument {
	/**
	 * @param {ClientWindow} window
	 * @param {number} tab its NodeId
	 */
	constructor(window, tab) {
		this.window = window;
		this.tab = tab;
		/** @type {HTMLElement} where its view is drawn — made by its first drawView, kept while the tab is open: its scroll
		 * and its table with it */
		this.element = /** @type {any} */ (null);
	}

	/**
	 * Its view, brought up to the frame's — a view the browser does not draw yet: said so in its place (a text, the
	 * journal).
	 * @param {ProtocolNode} _view
	 */
	drawView(_view) {
		if (this.element !== null)
			return;
		this.element = document.createElement('div');
		this.element.className = 'view-placeholder';
		this.element.textContent = 'This view is not in the browser yet.';
		this.window.views.append(this.element);
	}

	/**
	 * THE DOORS A VIEW'S CONTROLS GO BACK TO THE SERVER THROUGH — a form named by its Key; a view that is no form (no Key),
	 * by the tab it is drawn in, as the thin client's host names it (SetViewOf).
	 * @param {ProtocolNode} view
	 * @returns {FormContext}
	 */
	contextOf(view) {
		const key = view.Key ?? '';
		const of = key !== '' ? { Form: key } : { Tab: this.tab };
		const { session, pictures } = this.window;
		return {
			key,
			send: (control, kind, args, subscribed) => session
				.send({ Control: control, Event: kind, ...(args !== undefined ? { Args: args } : {}), ...of }, subscribed)
				.catch((error) => this.window.refused(error)),
			fetch: (control, request) => session.call('fetch', { Control: control, Request: request, ...of }),
			picture: (picture) => pictures.url(picture),
			fetchPictures: (node) => pictures.fetchFor(node).catch(() => {}),
		};
	}

	/** Its view drawn as it is meant to be seen — a table's rows come, a sheet's cells. @returns {Promise<unknown>} */
	settled() {
		return Promise.resolve();
	}

	/**
	 * The command taken by the document now — its tab's Commands have it, as the thin client's (IsCommandEnabled).
	 * @param {number} command (Command)
	 */
	isCommandEnabled(command) {
		const tab = this.window.session.frame?.Tabs?.NodeChildren?.find((one) => one.NodeId === this.tab);
		return tab?.Commands?.includes(command) ?? false;
	}

	/**
	 * Done on the server's document — command {Command}, on the active tab's, which this one is (DoCommand).
	 * @param {number} command (Command)
	 */
	doCommand(command) {
		return this.window.session.call('command', { Command: command });
	}

	/** Its view on paper — the page printed, the print styles leaving the view on show alone (the view's printout). */
	print() {
		globalThis.print();
	}

	/** The tab gone from the frame — its view with it. */
	close() {
		dispose(this.element);
	}
}

// A FORM'S TAB, OR A SHEET'S — the view the server composes, drawn control by control, as the thin client draws both with
// one host (ibFormVisualDocument): a form is named by its Key, a spreadsheet document (a list output, a sheet shown by
// code) by its tab.
export class FormVisualDocument extends TabDocument {
	/** @param {ProtocolNode} view */
	drawView(view) {
		this.element = reconcile(this.window.views, /** @type {any} */ (this.element), view, this.contextOf(view));
	}

	settled() {
		return /** @type {import('../controls/control.js').Control} */ (this.element).settled();
	}
}
defineDocument(form, FormVisualDocument);
defineDocument(spreadsheet, FormVisualDocument);
