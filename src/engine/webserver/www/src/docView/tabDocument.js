// THE DOCUMENT OF A TAB THE SERVER RAISED — what every tab is, whatever its view, as the thin client's (ibTabDocument):
// made for each tab the frame brings by the tab's Kind (createDocument), each kind with a document of its own,
// registered by the module that defines it (defineDocument), as a control is (defineControl). The document draws its
// view into its element and names it in the calls the view's controls make; the window shows one tab's and hides the
// others'. What is done to the document — undo, redo, save — goes to the server's, taken as its tab's Commands say;
// a document of files is saved as a file the server writes and the page takes down; its view goes on paper here.

import { reconcile, dispose } from '../controls/control.js';
import { _ } from '../localization.js';
import { downloadFile, saveFile } from '../window/files.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */
/** @typedef {import('../controls/control.js').FormContext} FormContext */
/** @typedef {import('../window/window.js').ClientWindow} ClientWindow */

// The kinds of a tab's view drawn here (ibView::ibKind): a form — the kind no Kind is written for — a text document and
// a spreadsheet document.
const form = 1;
const text = 2;
const spreadsheet = 3;

// The commands of the frame (ibFrame::ibCommand): those a tab's document takes — its tab's Commands say which, now —,
// the edits the element under the focus does (editFocused.js), and the window's own: Exit, Close All.
export const Command = { Undo: 1, Redo: 2, Save: 3, Exit: 6, Cut: 7, Copy: 8, Paste: 9, Delete: 10, SelectAll: 11,
	SaveAs: 12, CloseAll: 13 };

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
		this.element.textContent = _('This view is not in the browser yet.');
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
			studioMode: () => session.studioMode(),
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
		return this.tabNode()?.Commands?.includes(command) ?? false;
	}

	/**
	 * Done on the server's document — command {Command}, on the active tab's, which this one is (DoCommand).
	 * @param {number} command (Command)
	 */
	doCommand(command) {
		return this.window.session.call('command', { Command: command });
	}

	/**
	 * Saved as the thin client's tab is (Save): a document of files — its tab names the Formats it is written in —
	 * saved as a file, its place asked each time (the page keeps no place on the person's disk); another by the
	 * server's own Save — a form's object written.
	 * @returns {Promise<unknown>}
	 */
	save() {
		if ((this.tabNode()?.Formats?.NodeChildren?.length ?? 0) === 0)
			return this.doCommand(Command.Save);
		return this.saveAs();
	}

	/**
	 * Saved as a file (SaveAs): named as its tab is, without its «*», with its first format's extension when it has
	 * none of theirs; its place chosen in the browser's save dialog — the server writes the document under the name
	 * chosen (command SaveAs {Name}) and the page takes it down, the tab's File. False — the dialog closed.
	 * @returns {Promise<boolean>}
	 */
	saveAs() {
		const tab = this.tabNode();
		// What no file name holds made «_», as the browser's download makes it — «Report: Sales» is a tab's title.
		const title = String(tab?.Title ?? '').trim().replace(/\*$/, '').trim().replace(/[\\/:*?"<>|]/g, '_');
		/** @type {ProtocolNode[]} */
		const formats = tab?.Formats?.NodeChildren ?? [];
		// «*.txt;*.text» → .txt, .text
		const extensions = formats.flatMap((format) => String(format.Mask ?? '').split(';'))
			.map((mask) => mask.trim().replace(/^\*/, '').toLowerCase())
			.filter((extension) => /^\.[^.*]+$/.test(extension));
		const named = extensions.length === 0
			|| extensions.some((extension) => title.toLowerCase().endsWith(extension));
		const { session } = this.window;
		return saveFile(async (name) => {
			await session.call('command', { Command: Command.SaveAs, Name: name });
			const file = this.tabNode()?.File;
			if (file === undefined)
				throw new Error(`${name} was not saved.`);
			return downloadFile(session, file);
		}, named ? title : `${title}${extensions[0]}`);
	}

	/** Its tab, as the frame has it now. @returns {ProtocolNode | undefined} */
	tabNode() {
		/** @type {ProtocolNode[]} */
		const tabs = this.window.session.frame?.Tabs?.NodeChildren ?? [];
		return tabs.find((one) => one.NodeId === this.tab);
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

// A FORM'S TAB, A TEXT'S OR A SHEET'S — the view the server composes, drawn control by control, as the thin client
// draws them all with one host (ibFormVisualDocument): a form is named by its Key, a text document (its view a text
// box's node) and a spreadsheet document (a list output, a sheet shown by code — a grid box's) by its tab.
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
defineDocument(text, FormVisualDocument);
defineDocument(spreadsheet, FormVisualDocument);
