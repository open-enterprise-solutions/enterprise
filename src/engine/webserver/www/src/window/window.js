// THE WINDOW — the client's window as the server draws it (the frame): its title, its menu, the section panel down its
// side, its tabs, the active tab's view, the messages to the person and what the server asks them. The menu's routine
// is the client's — its own File and Edit ahead of the server's menu, done on the active tab's document; everything done
// to a document goes to the server (docs/public/client-protocol.md, Menu and commands). Each tab is a document of its
// kind (src/docView/), which keeps the element its view was drawn into, so a tab gone back to finds its scroll and its
// table where they were.

import { componentsDrawn } from '../controls/control.js';
import { Command, createDocument } from '../docView/tabDocument.js';
import { _, format, labelText, setLanguage } from '../localization.js';
import { Refusal } from '../protocol/connection.js';
import { Picture } from '../protocol/pictures.js';
import { editFocused, textFields } from './editFocused.js';
import { acceptOf, uploadFile } from './files.js';
import { showFind } from './find.js';
import { Questions } from './questions.js';
import { Sections } from './sections.js';
import { showFunctions } from './functions.js';
import { showAbout } from './about.js';
import { assistantSchema, showAssistant } from './assistantAccess.js';
import { showTheme, styleSchema } from './theme.js';

/** @typedef {import('../protocol/session.js').Session} Session */
/** @typedef {import('../protocol/pictures.js').Pictures} Pictures */
/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */
/** @typedef {import('../docView/tabDocument.js').TabDocument} TabDocument */

// The schemas the window opens itself (ibSchema): All functions.
const allFunctions = 1;
const about = 4;
// Tools → Options... (Debugging): the thin client's pages are the debugging and the assistant access; the browser's, the
// second — refused to a person whose roles do not give it.
const debugging = 6;
// …and the registration journal, which shows nothing of its own: its command Open (ibSchema::ibCommand) opens its tab.
const auditLog = 5;
const schemaOpen = 1;

// How long a call is waited on before the window says it waits: a quick answer shows nothing.
const busyAfter = 800;

// How long a form just opened is waited for, the one on show staying where it is: past this it comes as far as it has got.
const arrivesWithin = 1000;

// How long the first window is waited for, the splash up: nothing on show is held back by it, so it has the time its rows take.
const startsWithin = 15000;

// The keys of the document's commands, the thin client's defaults (SetDefaultHotKeys), with Ctrl or Cmd.
/** @type {Record<string, number>} */
const commandKeys = { s: Command.Save, z: Command.Undo, y: Command.Redo };

// THE TOOLBAR — the thin client's under its menu (CreateGUI): New, Open, Save, Save as | Find | Redo, Undo, each with
// its tooltip; a button is the File or Edit item of the same picture, done and enabled as that item. null: a separator.
const toolbarItems = [[Picture.New, 'New'], [Picture.Open, 'Open'], [Picture.Save, 'Save'], [Picture.SaveAs, 'Save as'],
	null, [Picture.Find, 'Find'], null, [Picture.Redo, 'Redo'], [Picture.Undo, 'Undo']];

// A message's level (ibMessage::ibLevel), as its line is marked.
const information = 1;
/** @type {Record<number, string>} */
const levels = { 1: 'information', 2: 'warning', 3: 'error' };

export class ClientWindow {
	/**
	 * @param {HTMLElement} root
	 * @param {Session} session
	 * @param {Pictures} pictures
	 */
	constructor(root, session, pictures) {
		this.session = session;
		this.pictures = pictures;
		this.questions = new Questions(session, pictures);
		root.replaceChildren();
		root.className = 'client-window';

		this.header = document.createElement('header');
		const logo = document.createElement('img');
		logo.className = 'logo';
		logo.src = 'styles/logo.png';
		logo.alt = 'OES';
		this.title = document.createElement('h1');
		this.menu = document.createElement('nav');
		this.menu.className = 'menu';
		this.header.append(logo, this.title, this.menu);
		this.toolbar = document.createElement('div');
		this.toolbar.className = 'command-bar toolbar';
		this.toolbar.setAttribute('role', 'toolbar');

		this.tabs = document.createElement('div');
		this.tabs.className = 'tabs';
		this.tabs.setAttribute('role', 'tablist');
		// A wheel turned over the tabs scrolls them sideways: a mouse has no other way to the ones past the edge.
		this.tabs.addEventListener('wheel', (event) => {
			if (event.deltaX !== 0 || event.deltaY === 0 || this.tabs.scrollWidth <= this.tabs.clientWidth)
				return;
			event.preventDefault();
			this.tabs.scrollLeft += event.deltaY;
		}, { passive: false });

		this.views = document.createElement('main');
		this.views.className = 'views';

		// The messages — the thin client's output pane: cleared by the person or by the server, its top edge dragged
		// to make it taller or shorter.
		this.messages = document.createElement('section');
		this.messages.className = 'messages';
		this.messages.hidden = true;
		this.lines = document.createElement('div');
		this.lines.className = 'lines';
		this.lines.setAttribute('aria-live', 'polite');
		const clear = document.createElement('button');
		clear.type = 'button';
		clear.className = 'clear';
		clear.textContent = '×';
		clear.title = _('Clear');
		clear.setAttribute('aria-label', _('Clear the messages'));
		clear.addEventListener('click', () => this.clearMessages());
		this.messages.append(grip(this.messages), clear, this.lines);

		this.status = document.createElement('footer');
		this.status.className = 'status';

		// The sections are the application's and stay while the session lasts: read once, as the window comes up.
		this.sections = new Sections(session, pictures, (error) => this.refused(error));
		/** @type {Promise<unknown>} */
		this.sectionsRead = session.frame !== null
			? this.sections.read(session.frame).catch((error) => this.refused(error)) : Promise.resolve();

		// The server waited on, and the connection lost — over the window, taking no click.
		this.busy = document.createElement('div');
		this.busy.className = 'busy';
		this.busy.hidden = true;
		this.busy.append(document.createElement('wa-spinner'));
		this.offline = document.createElement('div');
		this.offline.className = 'offline';
		this.offline.setAttribute('role', 'status');
		this.offline.textContent = _('No connection - reconnecting...');
		this.offline.hidden = true;
		/** @type {number | undefined} */
		this.busyTimer = undefined;
		/** @type {number | undefined} */
		this.offlineTimer = undefined;

		root.append(this.header, this.toolbar, this.sections.element, this.tabs, this.views, this.messages, this.status,
			this.busy, this.offline);

		/** @type {Map<number, TabDocument>} each tab's document, by the tab's id (createDocument) */
		this.documents = new Map();
		/** @type {Map<number, HTMLElement>} each tab's button in the strip, kept while the tab is open */
		this.tabButtons = new Map();
		/** @type {{id: number} | null} the tab whose form is being drawn, the one on show still up */
		this.arrival = null;
		this.drawnMenu = '';
		this.drawnToolbar = '';
		this.drawnTabs = '';

		session.addEventListener('frame', (event) => this.draw(/** @type {CustomEvent} */ (event).detail));
		session.addEventListener('events', (event) => this.happened(/** @type {CustomEvent} */ (event).detail));
		session.addEventListener('busy', (event) => this.waiting(/** @type {CustomEvent} */ (event).detail));
		session.addEventListener('connection', (event) => this.connected(/** @type {CustomEvent} */ (event).detail));
		session.addEventListener('ended', (event) => this.ended(/** @type {CustomEvent} */ (event).detail));
		document.addEventListener('keydown', (event) => this.escaped(event));
		document.addEventListener('keydown', (event) => this.commandKeyed(event));
		// A dialog gone whose form went with it has nothing to give the focus back to.
		document.addEventListener('wa-after-hide', () => {
			if (document.activeElement === document.body)
				this.focusTable(this.currentDocument()?.element);
		});
		/**
		 * @type {Element | null} the control the focus went into last — kept while it is in what no control is (Find, a
		 * menu): Find looks in it (findOnShow), as the thin client's form keeps its active control
		 */
		this.focusedControl = null;
		document.addEventListener('focusin', (event) => {
			const control = event.composedPath().find((node) => node instanceof Element && node.localName.startsWith('oes-')
				&& node.localName !== 'oes-clientform');
			if (control instanceof Element)
				this.focusedControl = control;
		});
	}

	/**
	 * The active tab's document, as the thin client's (GetCurrentDocument) — none while no tab is up, or before its view
	 * came.
	 * @returns {TabDocument | undefined}
	 */
	currentDocument() {
		return this.documents.get(this.session.frame?.ActiveTab ?? 0);
	}

	/**
	 * A call out — said once it has been waited on long enough to notice; answered — unsaid.
	 * @param {boolean} busy
	 */
	waiting(busy) {
		clearTimeout(this.busyTimer);
		if (busy)
			this.busyTimer = setTimeout(() => {
				this.busy.hidden = false;
			}, busyAfter);
		else
			this.busy.hidden = true;
	}

	/**
	 * The connection dropped — said once it stays down long enough to notice, a blip taken back at once says nothing;
	 * back — unsaid.
	 * @param {boolean} connected
	 */
	connected(connected) {
		clearTimeout(this.offlineTimer);
		if (!connected)
			this.offlineTimer = setTimeout(() => {
				this.offline.hidden = false;
			}, busyAfter);
		else
			this.offline.hidden = true;
	}

	/**
	 * The frame drawn — after the pictures it names have come, so it is drawn once and whole.
	 * @param {ProtocolNode} frame
	 */
	async draw(frame) {
		// The language a translated text is read in — the session's, before anything is drawn (as the thin's frame).
		setLanguage(String(frame.Language ?? ''));
		await this.pictures.fetchFor({ Tabs: frame.Tabs, View: frame.View, Menu: frame.Menu, Own: Object.values(Picture)
			.map((id) => ({ Picture: id })) }).catch(() => {});
		this.title.textContent = frame.Title || document.title;
		this.status.textContent = frame.Status ?? '';
		// The view first: the menu's File and Edit are enabled by the document it is drawn by.
		this.drawView(frame);
		this.drawMenu(frame.Menu);
		this.drawToolbar();
		// A tab comes with its form, in one frame: the strip waits for the one arriving.
		if (this.arrival === null)
			this.drawTabs(frame);
		this.questions.ask(frame.Request);
	}

	/**
	 * The menu: the client's own File and Edit, then the server's, its Window before Help as the thin client's tabbed
	 * frame puts it (AddWindowMenu: before the menu titled as the stock Help, else last) — drawn anew only when what it
	 * shows changed.
	 * @param {ProtocolNode | undefined} menu the server's
	 */
	drawMenu(menu) {
		const server = menu?.NodeChildren ?? [];
		const help = server.findIndex((top) => top.Title === _('Help'));
		const at = help < 0 ? server.length : help;
		const tops = [...this.documentMenu(), ...server.slice(0, at), this.windowMenu(), ...server.slice(at)];
		const drawn = JSON.stringify(tops);
		if (drawn === this.drawnMenu)
			return;
		this.drawnMenu = drawn;
		this.menu.replaceChildren(...tops.map((top) => {
			const dropdown = document.createElement('wa-dropdown');
			const trigger = document.createElement('wa-button');
			trigger.slot = 'trigger';
			trigger.setAttribute('size', 's');
			trigger.setAttribute('appearance', 'plain');
			trigger.setAttribute('with-caret', '');
			trigger.textContent = top.Title ?? '';
			dropdown.append(trigger);
			/** @type {Map<string, ProtocolNode>} */
			const items = new Map();
			const add = (item, depth) => {
				if (item.Separator) {
					dropdown.append(document.createElement('wa-divider'));
					return;
				}
				const entry = document.createElement('wa-dropdown-item');
				entry.textContent = item.Title ?? '';
				const picture = this.pictures.url(item.Picture);
				if (picture) {
					const image = document.createElement('img');
					image.src = picture;
					image.alt = '';
					image.slot = 'icon';
					entry.append(image);
				}
				entry.style.paddingInlineStart = depth > 0 ? `${depth}em` : '';
				entry.toggleAttribute('disabled', item.Enabled === false || (item.NodeChildren?.length ?? 0) > 0);
				const value = String(items.size);
				entry.value = value;
				items.set(value, item);
				dropdown.append(entry);
				for (const sub of item.NodeChildren ?? [])
					add(sub, depth + 1);
			};
			for (const item of top.NodeChildren ?? [])
				add(item, 0);
			dropdown.addEventListener('wa-select', (event) => {
				const item = items.get(/** @type {any} */ (event).detail.item.value);
				if (item !== undefined)
					this.menuItem(item);
			});
			return dropdown;
		}));
	}

	/**
	 * THE CLIENT'S OWN FILE AND EDIT, as the thin client's (InitializeDefaultMenu): done on the active tab's document,
	 * found as the item is chosen; enabled as its tab says — a command by its Commands, Close where its tab is not locked,
	 * Print where there is a document at all. Cut … Select All are the element's under the focus (editFocused), Find the
	 * text's on show (findText) — always on, as the thin client's. Quit is the server's Exit (the thin's AllowClose): it
	 * closes every tab but the start page, asking about what is unsaved, and says Exit once each one closed. Worded as
	 * the thin's menu words them (labelText). Not here of its File: Revert (it reads a file on the person's disk again — a
	 * server's tab has none), Print Setup and Print Preview (the browser's print dialog is both), the files opened last
	 * (the page is told no path).
	 * @returns {ProtocolNode[]}
	 */
	documentMenu() {
		const current = this.currentDocument();
		const tab = this.session.frame?.Tabs?.NodeChildren?.find((one) => one.NodeId === current?.tab);
		/**
		 * @param {string} title
		 * @param {number} command (Command)
		 * @param {string} picture (Picture)
		 */
		const commandItem = (title, command, picture) => ({ Title: title, Picture: picture,
			Enabled: current?.isCommandEnabled(command) ?? false, done: () => this.currentDocument()?.doCommand(command) });
		const templates = this.session.frame?.Templates?.NodeChildren ?? [];
		// The thin client's stock labels (wx's, in the one dictionary), as its menu takes them — with the mnemonic.
		return [
			{ Title: labelText('&File'), NodeChildren: [
				{ Title: labelText('&New'), Picture: Picture.New, Enabled: templates.some((one) => one.OnlyOpen !== true),
					done: () => this.newDocument() },
				{ Title: labelText('&Open...'), Picture: Picture.Open, Enabled: templates.length > 0,
					done: () => this.openFile() },
				{ Title: labelText('&Close'), Picture: Picture.CloseForm, Enabled: tab !== undefined && !tab.Locked,
					done: () => this.closeTab(this.session.frame?.ActiveTab ?? 0) },
				{ Title: labelText('&Save'), Picture: Picture.Save, Enabled: current?.isCommandEnabled(Command.Save) ?? false,
					done: () => this.currentDocument()?.save() },
				{ Title: labelText('Save &As...'), Picture: Picture.SaveAs,
					Enabled: current?.isCommandEnabled(Command.SaveAs) ?? false, done: () => this.currentDocument()?.saveAs() },
				{ Separator: true },
				{ Title: labelText('&Print...'), Picture: Picture.Print, Enabled: current !== undefined,
					done: () => this.currentDocument()?.print() },
				{ Separator: true },
				{ Title: labelText('&Quit'), Picture: Picture.Quit,
					done: () => this.session.call('command', { Command: Command.Exit }) },
			] },
			{ Title: labelText('&Edit'), NodeChildren: [
				commandItem(labelText('&Undo'), Command.Undo, Picture.Undo),
				commandItem(labelText('&Redo'), Command.Redo, Picture.Redo),
				{ Separator: true },
				{ Title: labelText('Cu&t'), Picture: Picture.Cut, done: () => editFocused(Command.Cut) },
				{ Title: labelText('&Copy'), Picture: Picture.ClipboardCopy, done: () => editFocused(Command.Copy) },
				{ Title: labelText('&Paste'), Picture: Picture.Paste, done: () => editFocused(Command.Paste) },
				{ Title: labelText('&Delete'), Picture: Picture.Delete, done: () => editFocused(Command.Delete) },
				{ Title: labelText('Select &All'), done: () => editFocused(Command.SelectAll) },
				{ Separator: true },
				{ Title: labelText('&Find...'), Picture: Picture.Find, done: () => this.find() },
			] },
		];
	}

	/**
	 * The toolbar (toolbarItems): each button File's or Edit's item of its picture — drawn anew only when what it shows
	 * changed.
	 */
	drawToolbar() {
		/** @type {Map<string, ProtocolNode>} */
		const items = new Map();
		for (const top of this.documentMenu()) {
			for (const item of top.NodeChildren ?? []) {
				if (item.Picture !== undefined)
					items.set(item.Picture, item);
			}
		}
		const drawn = JSON.stringify(toolbarItems.map((one) => one && [one[0], items.get(one[0])?.Enabled !== false,
			this.pictures.url(one[0]) !== '']));
		if (drawn === this.drawnToolbar)
			return;
		this.drawnToolbar = drawn;
		this.toolbar.replaceChildren(...toolbarItems.map((one) => {
			if (one === null) {
				const separator = document.createElement('span');
				separator.className = 'separator';
				return separator;
			}
			const [picture, tooltip] = one;
			const item = items.get(picture);
			const button = document.createElement('wa-button');
			button.setAttribute('size', 's');
			button.setAttribute('appearance', 'plain');
			button.toggleAttribute('disabled', item?.Enabled === false);
			button.title = _(tooltip);
			button.setAttribute('aria-label', _(tooltip));
			const image = document.createElement('img');
			image.src = this.pictures.url(picture);
			image.alt = '';
			button.append(image);
			button.addEventListener('click', () => {
				if (item !== undefined)
					this.menuItem(item);
			});
			return button;
		}));
	}

	/**
	 * THE WINDOW MENU — the thin client's tabbed frame's own (ibAuiMDIParentFrame: Close, Close All | Next, Previous):
	 * Close is File's; Close All the server's, in one call (CloseAll — the thin's OnCloseAll: the locked tabs stay, and
	 * one the person keeps keeps the rest); Next and Previous the tab beside the active one, round the strip.
	 * @returns {ProtocolNode}
	 */
	windowMenu() {
		const tabs = this.session.frame?.Tabs?.NodeChildren ?? [];
		const tab = tabs.find((one) => one.NodeId === this.session.frame?.ActiveTab);
		return { Title: labelText('&Window'), NodeChildren: [
			{ Title: labelText('Cl&ose'), Enabled: tab !== undefined && !tab.Locked,
				done: () => this.closeTab(this.session.frame?.ActiveTab ?? 0) },
			{ Title: labelText('Close All'), Enabled: tabs.some((one) => !one.Locked),
				done: () => this.session.call('command', { Command: Command.CloseAll }) },
			{ Separator: true },
			{ Title: labelText('&Next'), Enabled: tabs.length > 1, done: () => this.advanceTab(1) },
			{ Title: labelText('&Previous'), Enabled: tabs.length > 1, done: () => this.advanceTab(-1) },
		] };
	}

	/**
	 * The tab beside the active one made active — the next or the previous, round the strip (the thin's ActivateNext /
	 * ActivatePrevious: its notebook's AdvanceSelection).
	 * @param {number} step 1 the next, -1 the previous
	 */
	advanceTab(step) {
		const frame = this.session.frame;
		const tabs = frame?.Tabs?.NodeChildren ?? [];
		if (tabs.length < 2)
			return Promise.resolve();
		const at = tabs.findIndex((one) => one.NodeId === frame?.ActiveTab);
		return this.session.call('activate', { Tab: tabs[(at + step + tabs.length) % tabs.length].NodeId });
	}

	/**
	 * A menu item done: the client's own (documentMenu), a command of a schema, a schema read and shown, or a command on
	 * the active document.
	 * @param {ProtocolNode} item
	 */
	async menuItem(item) {
		try {
			if (item.done !== undefined)
				await item.done();
			else if (item.Schema !== undefined && item.Command !== undefined)
				await this.session.call('schema', { Schema: item.Schema, Command: item.Command });
			else if (item.Schema === allFunctions)
				await showFunctions(this.session, this.pictures, (error) => this.refused(error));
			else if (item.Schema === about)
				await showAbout(this.session);
			else if (item.Schema === styleSchema)
				await showTheme(this.session);
			else if (item.Schema === assistantSchema || item.Schema === debugging)
				await showAssistant(this.session);
			else if (item.Schema === auditLog)
				await this.session.call('schema', { Schema: auditLog, Command: schemaOpen });
			else if (item.Schema !== undefined)
				this.say(format(_('%s: not in the browser yet'), item.Title ?? ''));
			else if (item.Command !== undefined)
				await this.session.call('command', { Command: item.Command });
		}
		catch (error) {
			this.refused(/** @type {Error} */ (error));
		}
	}

	/**
	 * File → New, as the thin client's doc manager makes one over the server's templates: of the one template that makes
	 * documents at once, else of the one chosen (the desktop's «Select a document template»), made by the server
	 * (open {Extension}).
	 */
	newDocument() {
		const templates = (this.session.frame?.Templates?.NodeChildren ?? []).filter((one) => one.OnlyOpen !== true);
		/** @param {ProtocolNode} template */
		const make = (template) => this.session.call('open', { Extension: template.Extension }).catch((error) => this.refused(error));
		if (templates.length === 1) {
			make(templates[0]);
			return;
		}
		const dialog = this.questions.dialog(_('New'), '', [{ caption: _('Cancel'), answer: {} }], null);
		const list = document.createElement('div');
		list.className = 'choice-list';
		for (const template of templates) {
			const button = document.createElement('button');
			button.type = 'button';
			const url = this.pictures.url(template.Picture);
			if (url) {
				const image = document.createElement('img');
				image.src = url;
				image.alt = '';
				button.append(image);
			}
			button.append(template.Title ?? '');
			button.addEventListener('click', () => {
				this.questions.answer(dialog, null, {});
				make(template);
			});
			list.append(button);
		}
		dialog.querySelector('.text')?.replaceWith(list);
	}

	/**
	 * File → Open, as the thin client's doc manager opens one: a file of the person's, chosen by the templates' masks in
	 * the browser's own file dialog, put in the session's temporary storage and opened there by the template its name
	 * says (open {File}).
	 */
	openFile() {
		const input = document.createElement('input');
		input.type = 'file';
		input.accept = acceptOf((this.session.frame?.Templates?.NodeChildren ?? []).map((one) => one.Mask ?? ''));
		input.addEventListener('change', async () => {
			const chosen = input.files?.[0];
			if (chosen === undefined)
				return;
			try {
				await this.session.call('open', { File: await uploadFile(this.session, chosen) });
			}
			catch (error) {
				this.refused(/** @type {Error} */ (error));
			}
		});
		input.click();
	}

	/** Edit → Find..., over what is on show — as the thin client's doc manager finds in its document's view. */
	find() {
		showFind(() => this.findOnShow(), this.questions);
	}

	/**
	 * What Find looks in on show — the thin client's form hands Find to its active control: the control of the tab the
	 * person was in last, a table or a sheet looking by itself, a text box's text; else the tab's first text box (a text
	 * document's) — or none.
	 * @returns {HTMLTextAreaElement | import('./find.js').Finder | null}
	 */
	findOnShow() {
		const element = this.currentDocument()?.element;
		const control = /** @type {any} */ (this.focusedControl);
		if (control?.isConnected && element?.contains(control)) {
			if (typeof control.find === 'function')
				return control;
			if (control.localName === 'oes-textarea')
				return control.querySelector('textarea');
		}
		return element?.querySelector('oes-textarea textarea') ?? null;
	}

	/**
	 * The strip of tabs, brought up to the frame — only when what it shows changed, and over the buttons it has: drawn
	 * anew on every answer, a tab under the pointer lost its hover and its icon was decoded again each time.
	 * @param {ProtocolNode} frame
	 */
	drawTabs(frame) {
		const tabs = frame.Tabs?.NodeChildren ?? [];
		const drawn = JSON.stringify([tabs, frame.ActiveTab]);
		if (drawn === this.drawnTabs)
			return;
		this.drawnTabs = drawn;
		const open = new Set(tabs.map((tab) => tab.NodeId));
		for (const [id, button] of this.tabButtons) {
			if (!open.has(id)) {
				button.remove();
				this.tabButtons.delete(id);
			}
		}
		tabs.forEach((tab, place) => {
			const button = this.tabButton(tab, frame.ActiveTab);
			if (this.tabs.children[place] !== button)
				this.tabs.insertBefore(button, this.tabs.children[place] ?? null);
		});
		this.tabs.querySelector('[aria-selected="true"]')?.scrollIntoView({ block: 'nearest', inline: 'nearest' });
		// A tab closed takes its document with it.
		for (const [id, closed] of this.documents) {
			if (!open.has(id)) {
				closed.close();
				this.documents.delete(id);
			}
		}
	}

	/**
	 * A tab's button in the strip, made once.
	 * @param {ProtocolNode} tab
	 * @param {number | undefined} active
	 */
	tabButton(tab, active) {
		let button = this.tabButtons.get(tab.NodeId);
		if (button === undefined) {
			button = document.createElement('div');
			button.className = 'tab';
			button.setAttribute('role', 'tab');
			button.tabIndex = 0;
			button.append(document.createElement('span'));
			button.addEventListener('click', () => {
				if (tab.NodeId !== this.session.frame?.ActiveTab)
					this.session.call('activate', { Tab: tab.NodeId }).catch((error) => this.refused(error));
			});
			if (!tab.Locked) {
				const close = document.createElement('button');
				close.type = 'button';
				close.className = 'close';
				close.textContent = '×';
				close.addEventListener('click', (event) => {
					event.stopPropagation();
					this.closeTab(tab.NodeId);
				});
				button.append(close);
			}
			this.tabButtons.set(tab.NodeId, button);
		}
		button.setAttribute('aria-selected', String(tab.NodeId === active));
		button.title = tab.Title ?? '';
		/** @type {HTMLElement} */ (button.querySelector('span')).textContent = tab.Title ?? '';
		button.querySelector('.close')?.setAttribute('aria-label', `${_('Close')} ${tab.Title ?? ''}`);
		const icon = this.pictures.url(tab.Icon);
		let image = button.querySelector('img');
		if (icon && image === null) {
			image = document.createElement('img');
			image.alt = '';
			button.prepend(image);
		}
		if (image !== null && image.getAttribute('src') !== icon)
			image.src = icon;
		return button;
	}

	/**
	 * A tab closed by the server: what is left to save it asks about itself, as a question.
	 * @param {number} id
	 */
	closeTab(id) {
		this.session.call('close', { Tab: id }).catch((error) => this.refused(error));
	}

	/**
	 * Escape closes what stands on top, one thing a press: an editor, a menu or a dialog takes it itself, and with none of
	 * them up the active tab goes. A key held closes one, and a press while the server is still answering closes none. The
	 * field in focus lets go first, so that the text typed in it reaches the server before the close does and is asked about.
	 * @param {KeyboardEvent} event
	 */
	escaped(event) {
		if (event.key !== 'Escape' || event.repeat || event.defaultPrevented || event.ctrlKey || event.altKey || event.shiftKey
			|| event.metaKey)
			return;
		const frame = this.session.frame;
		if (frame === null || frame.Request !== undefined || this.session.outstanding > 0 || this.arrival !== null
			|| document.querySelector('wa-dialog[open], wa-dropdown[open], :popover-open') !== null)
			return;
		const tab = frame.Tabs?.NodeChildren?.find((one) => one.NodeId === frame.ActiveTab);
		if (tab === undefined || tab.Locked)
			return;
		commitField();
		this.closeTab(tab.NodeId);
	}

	/**
	 * A command's key (commandKeys) done on the active tab's document when it takes the command now — the field in
	 * focus letting go first, as for Escape. Undo and Redo pressed in a text field are the field's own, and a key is
	 * the open dialog's while one is up. Ctrl+S never saves the page.
	 * @param {KeyboardEvent} event
	 */
	commandKeyed(event) {
		// A keydown with no key — a field filled in by the browser itself — is none of them.
		const command = commandKeys[event.key?.toLowerCase() ?? ''];
		if (command === undefined || !(event.ctrlKey || event.metaKey) || event.altKey || event.shiftKey
			|| event.defaultPrevented)
			return;
		if (command !== Command.Save && /** @type {Element} */ (event.composedPath()[0]).matches?.(textFields))
			return;
		if (command === Command.Save)
			event.preventDefault();
		const current = this.currentDocument();
		if (event.repeat || current === undefined || this.session.frame?.Request !== undefined
			|| document.querySelector('wa-dialog[open]') !== null || !current.isCommandEnabled(command))
			return;
		event.preventDefault();
		commitField();
		(command === Command.Save ? current.save() : current.doCommand(command)).catch((error) => this.refused(error));
	}

	/**
	 * The active tab's view, drawn by the tab's document. A tab just opened is drawn out of sight, the one on show staying
	 * up until it has its rows: the person sees the one, then the other, and never an empty table filling.
	 * @param {ProtocolNode} frame
	 */
	drawView(frame) {
		const active = frame.ActiveTab;
		const view = frame.View;
		if (view === undefined || active === undefined || active === 0) {
			this.showView(active);
			return;
		}
		const shown = this.documents.get(active);
		const returned = shown?.element.hidden === true;
		const opened = shown
			?? createDocument(this, frame.Tabs?.NodeChildren?.find((tab) => tab.NodeId === active)?.Kind, active);
		opened.drawView(view);
		if (shown === undefined) {
			this.documents.set(active, opened);
			this.arrive(active, opened);
		}
		else if (this.arrival?.id !== active) {
			this.showView(active);
			if (returned)
				this.focusTable(opened.element);
		}
	}

	/**
	 * A tab's view just drawn, out of sight until it has settled.
	 * @param {number} id its tab
	 * @param {TabDocument} opened its document
	 */
	arrive(id, opened) {
		const arrival = { id };
		this.arrival = arrival;
		opened.element.classList.add('arriving');
		Promise.race([opened.settled(), new Promise((resolve) => setTimeout(resolve, arrivesWithin))]).then(() => {
			if (this.arrival !== arrival)
				return;
			this.showView(id);
			if (this.session.frame !== null)
				this.drawTabs(this.session.frame);
			this.focusTable(opened.element);
		});
	}

	/**
	 * The table takes the focus as its form comes up, as the thin client's, from the section pressed to open it — once it
	 * can be seen, and never out of a question being answered.
	 * @param {HTMLElement | undefined} form
	 */
	focusTable(form) {
		if (document.activeElement?.closest('wa-dialog') === null || document.activeElement === null)
			/** @type {HTMLElement | null | undefined} */ (form?.querySelector('oes-table'))?.focus({ preventScroll: true });
	}

	/**
	 * The tab's view on show and no other — the form that was arriving comes in.
	 * @param {number | undefined} active
	 */
	showView(active) {
		this.arrival = null;
		for (const [id, shown] of this.documents) {
			shown.element.hidden = id !== active;
			shown.element.classList.remove('arriving');
		}
	}

	/** The first window as it is meant to be seen: its sections read, its menu and the view on show drawn. */
	settled() {
		const drawn = this.currentDocument()?.settled() ?? Promise.resolve();
		return Promise.race([Promise.all([this.sectionsRead, componentsDrawn(this.menu), drawn]),
			new Promise((resolve) => setTimeout(resolve, startsWithin))]);
	}

	/** @param {{Messages?: ProtocolNode, Clear?: boolean, Exit?: boolean, Close?: boolean}} events */
	happened(events) {
		if (events.Clear)
			this.clearMessages();
		for (const message of events.Messages?.NodeChildren ?? [])
			this.say(message.Text ?? '', message.Level);
		// The server let the client go (Quit, a script's Exit, a start refused): the session goes with the page's window,
		// as the thin client's window closes and logs out.
		if (events.Exit) {
			this.ended(_('Exit'));
			this.session.logout();
		}
		// Closed as the person closes it (a script's EndJob(False)): its Quit, each tab asked what it keeps.
		else if (events.Close)
			this.session.call('command', { Command: Command.Exit });
	}

	/**
	 * A message to the person, in the panel under the view, its level the mark beside it (ibMessage::ibLevel).
	 * @param {string} text
	 * @param {number} [level]
	 */
	say(text, level = information) {
		if (!text)
			return;
		const line = document.createElement('div');
		line.className = `message ${levels[level] ?? levels[information]}`;
		line.textContent = text;
		this.lines.append(line);
		this.messages.hidden = false;
		line.scrollIntoView({ block: 'nearest' });
	}

	/** The messages gone, and the pane with them until the next one. */
	clearMessages() {
		this.lines.replaceChildren();
		this.messages.hidden = true;
	}

	/**
	 * A call refused, said as the thin client says it (ibFrontendMainFrame::Refused): a box the person closes. The
	 * session gone — no such client (401), the connection not taken back (0) — is said by its end, once, and by
	 * nothing else.
	 * @param {Error} error
	 */
	refused(error) {
		if (error instanceof Refusal && (error.code === 401 || error.code === 0))
			return;
		this.questions.dialog(this.title.textContent ?? '', error.message || _('The server refused it.'),
			[{ caption: labelText('&OK'), answer: {}, preferred: true }], null);
	}

	/** @param {string} reason */
	ended(reason) {
		// Said once: the socket's close and a refusal of the session gone may both tell it.
		if (document.querySelector('.ended') !== null)
			return;
		const shade = document.createElement('div');
		shade.className = 'ended';
		const text = document.createElement('p');
		text.textContent = format(_('The session has ended: %s'), reason);
		const again = document.createElement('wa-button');
		again.setAttribute('variant', 'brand');
		again.textContent = _('Log in again');
		again.addEventListener('click', () => location.reload());
		shade.append(text, again);
		document.body.append(shade);
	}
}

/**
 * The field in focus lets go and takes the focus back: the text typed in it reaches the server before what is done next
 * is, and is asked about.
 */
function commitField() {
	const field = /** @type {HTMLElement | null} */ (document.activeElement);
	field?.blur();
	field?.focus({ preventScroll: true });
}

/**
 * The edge a pane's height is dragged by: up makes it taller. Kept between a line or two and most of the window.
 * @param {HTMLElement} pane
 */
function grip(pane) {
	const edge = document.createElement('div');
	edge.className = 'grip';
	edge.setAttribute('role', 'separator');
	edge.setAttribute('aria-orientation', 'horizontal');
	edge.addEventListener('pointerdown', (event) => {
		event.preventDefault();
		edge.setPointerCapture(event.pointerId);
		const from = event.clientY;
		const height = pane.getBoundingClientRect().height;
		/** @param {PointerEvent} moved */
		const drag = (moved) => {
			const wanted = height + from - moved.clientY;
			pane.style.height = `${Math.max(48, Math.min(wanted, innerHeight * 0.7))}px`;
			pane.style.maxHeight = 'none';
		};
		edge.addEventListener('pointermove', drag);
		edge.addEventListener('lostpointercapture', () => edge.removeEventListener('pointermove', drag), { once: true });
	});
	return edge;
}
