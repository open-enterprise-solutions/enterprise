// THE WINDOW — the client's window as the server draws it (the frame): its title, its menu, the section panel down its
// side, its tabs, the active tab's view, the messages to the person and what the server asks them. The menu's routine
// is the client's; everything done to a document goes to the server (docs/public/client-protocol.md, Menu and
// commands). Each tab keeps the element its view was drawn into, so a tab gone back to finds its scroll and its table
// where they were.

import { reconcile, dispose } from '../controls/control.js';
import { Refusal } from '../protocol/connection.js';
import { HomePage } from './homePage.js';
import { Questions } from './questions.js';
import { Sections } from './sections.js';
import { showFunctions } from './functions.js';
import { showAbout } from './about.js';

/** @typedef {import('../protocol/session.js').Session} Session */
/** @typedef {import('../protocol/pictures.js').Pictures} Pictures */
/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */
/** @typedef {import('../controls/control.js').FormContext} FormContext */

// The schemas the window opens itself (ibSchema): All functions.
const allFunctions = 1;
const about = 4;

// What a tab's view shows when it is no form (ibView::ibKind): the start page.
const homePage = 16;

// How long a call is waited on before the window says it waits: a quick answer shows nothing.
const busyAfter = 800;

// How long a form just opened is waited for, the one on show staying where it is: past this it comes as far as it has got.
const arrivesWithin = 1000;

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
		this.questions = new Questions(session);
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
		clear.title = 'Clear';
		clear.setAttribute('aria-label', 'Clear the messages');
		clear.addEventListener('click', () => this.clearMessages());
		this.messages.append(grip(this.messages), clear, this.lines);

		this.status = document.createElement('footer');
		this.status.className = 'status';

		// The sections are the configuration's and stay while the session lasts: read once, as the window comes up.
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
		this.offline.textContent = 'No connection — reconnecting…';
		this.offline.hidden = true;
		/** @type {number | undefined} */
		this.busyTimer = undefined;
		/** @type {number | undefined} */
		this.offlineTimer = undefined;

		root.append(this.header, this.sections.element, this.tabs, this.views, this.messages, this.status, this.busy,
			this.offline);

		/** @type {Map<number, HTMLElement>} each tab's view, drawn */
		this.tabViews = new Map();
		/** @type {Map<number, HTMLElement>} each tab's button in the strip, kept while the tab is open */
		this.tabButtons = new Map();
		/** @type {{id: number} | null} the tab whose form is being drawn, the one on show still up */
		this.arrival = null;
		this.drawnMenu = '';
		this.drawnTabs = '';

		session.addEventListener('frame', (event) => this.draw(/** @type {CustomEvent} */ (event).detail));
		session.addEventListener('events', (event) => this.happened(/** @type {CustomEvent} */ (event).detail));
		session.addEventListener('busy', (event) => this.waiting(/** @type {CustomEvent} */ (event).detail));
		session.addEventListener('connection', (event) => this.connected(/** @type {CustomEvent} */ (event).detail));
		session.addEventListener('ended', (event) => this.ended(/** @type {CustomEvent} */ (event).detail));
		document.addEventListener('keydown', (event) => this.escaped(event));
		// A dialog gone whose form went with it has nothing to give the focus back to.
		document.addEventListener('wa-after-hide', () => {
			if (document.activeElement === document.body)
				this.focusTable(this.tabViews.get(this.session.frame?.ActiveTab ?? 0));
		});
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
		await this.pictures.fetchFor({ Tabs: frame.Tabs, View: frame.View, Menu: frame.Menu }).catch(() => {});
		this.title.textContent = frame.Title || document.title;
		this.status.textContent = frame.Status ?? '';
		this.drawMenu(frame.Menu);
		this.drawTabs(frame);
		this.drawView(frame);
		this.questions.ask(frame.Request);
	}

	/** @param {ProtocolNode | undefined} menu */
	drawMenu(menu) {
		const drawn = JSON.stringify(menu ?? {});
		if (drawn === this.drawnMenu)
			return;
		this.drawnMenu = drawn;
		this.menu.replaceChildren(...(menu?.NodeChildren ?? []).map((top) => {
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
	 * A menu item done: a command of a schema, a schema read and shown, or a command on the active document.
	 * @param {ProtocolNode} item
	 */
	async menuItem(item) {
		try {
			if (item.Schema !== undefined && item.Command !== undefined)
				await this.session.call('schema', { Schema: item.Schema, Command: item.Command });
			else if (item.Schema === allFunctions)
				await showFunctions(this.session, this.pictures, (error) => this.refused(error));
			else if (item.Schema === about)
				await showAbout(this.session);
			else if (item.Schema !== undefined)
				this.say(`${item.Title ?? ''}: not in the browser yet`);
			else if (item.Command !== undefined)
				await this.session.call('command', { Command: item.Command });
		}
		catch (error) {
			this.refused(/** @type {Error} */ (error));
		}
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
		// A tab closed takes its view with it.
		for (const [id, element] of this.tabViews) {
			if (!open.has(id)) {
				dispose(element);
				this.tabViews.delete(id);
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
		button.querySelector('.close')?.setAttribute('aria-label', `Close ${tab.Title ?? ''}`);
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
		const field = /** @type {HTMLElement | null} */ (document.activeElement);
		field?.blur();
		field?.focus({ preventScroll: true });
		this.closeTab(tab.NodeId);
	}

	/**
	 * The active tab's view. A form just opened is drawn out of sight, the one on show staying up until it has its rows:
	 * the person sees the one, then the other, and never an empty table filling.
	 * @param {ProtocolNode} frame
	 */
	drawView(frame) {
		const active = frame.ActiveTab;
		const view = frame.View;
		if (view === undefined || active === undefined || active === 0) {
			this.showView(active);
			return;
		}
		const existing = this.tabViews.get(active) ?? null;
		const isHomePage = frame.Tabs?.NodeChildren?.find((tab) => tab.NodeId === active)?.Kind === homePage;
		if (view.NodeType !== 'ClientForm' && !isHomePage) {
			// A view that is no form — a text, a spreadsheet, the journal — is not drawn here yet.
			const placeholder = existing ?? document.createElement('div');
			placeholder.className = 'view-placeholder';
			placeholder.textContent = 'This view is not in the browser yet.';
			if (existing === null) {
				this.views.append(placeholder);
				this.tabViews.set(active, placeholder);
			}
			this.showView(active);
			return;
		}
		const returned = existing?.hidden === true;
		const form = isHomePage ? this.drawHomePage(existing, view)
			: reconcile(this.views, /** @type {any} */ (existing), view, this.contextOf(view));
		this.tabViews.set(active, form);
		if (existing === null)
			this.arrive(active, /** @type {any} */ (form));
		else if (this.arrival?.id !== active) {
			this.showView(active);
			if (returned)
				this.focusTable(form);
		}
	}

	/**
	 * The start page brought up to its view: each of its forms goes back to the server by its own Key.
	 * @param {HTMLElement | null} existing
	 * @param {ProtocolNode} view
	 * @returns {HomePage}
	 */
	drawHomePage(existing, view) {
		const page = /** @type {HomePage} */ (existing ?? this.views.appendChild(document.createElement('oes-homepage')));
		page.draw(view, (form) => this.contextOf(form), this.pictures);
		return page;
	}

	/**
	 * A form just drawn, out of sight until it has settled.
	 * @param {number} id its tab
	 * @param {import('../controls/control.js').Control} form
	 */
	arrive(id, form) {
		const arrival = { id };
		this.arrival = arrival;
		form.classList.add('arriving');
		Promise.race([form.settled(), new Promise((resolve) => setTimeout(resolve, arrivesWithin))]).then(() => {
			if (this.arrival !== arrival)
				return;
			this.showView(id);
			this.focusTable(form);
		});
	}

	/**
	 * The table takes the focus as its form comes up, as the thin client's, from the section pressed to open it — once it
	 * can be seen, and never out of a question being answered.
	 * @param {HTMLElement | undefined} form
	 */
	focusTable(form) {
		if (document.activeElement?.closest('wa-dialog') === null || document.activeElement === null)
			/** @type {HTMLElement | null | undefined} */ (form?.querySelector('oes-tablebox'))?.focus({ preventScroll: true });
	}

	/**
	 * The tab's view on show and no other — the form that was arriving comes in.
	 * @param {number | undefined} active
	 */
	showView(active) {
		this.arrival = null;
		for (const [id, element] of this.tabViews) {
			element.hidden = id !== active;
			element.classList.remove('arriving');
		}
	}

	/** The window as it is meant to be seen: its sections read, the form on show drawn. */
	settled() {
		const form = this.tabViews.get(this.session.frame?.ActiveTab ?? 0);
		const drawn = /** @type {any} */ (form)?.settled?.() ?? Promise.resolve();
		return Promise.race([Promise.all([this.sectionsRead, drawn]), new Promise((resolve) => setTimeout(resolve, arrivesWithin))]);
	}

	/**
	 * The doors a form's controls go back to the server through, named by its Key.
	 * @param {ProtocolNode} view
	 * @returns {FormContext}
	 */
	contextOf(view) {
		const key = view.Key;
		return {
			key,
			send: (control, kind, args, subscribed) => this.session
				.send({ Control: control, Event: kind, ...(args !== undefined ? { Args: args } : {}), Form: key }, subscribed)
				.catch((error) => this.refused(error)),
			fetch: (control, request) => this.session.call('fetch', { Control: control, Request: request, Form: key }),
			picture: (picture) => this.pictures.url(picture),
			fetchPictures: (node) => this.pictures.fetchFor(node).catch(() => {}),
		};
	}

	/** @param {{Messages?: ProtocolNode, Clear?: boolean, Exit?: boolean}} events */
	happened(events) {
		if (events.Clear)
			this.clearMessages();
		for (const message of events.Messages?.NodeChildren ?? [])
			this.say(message.Text ?? '', message.Level);
		if (events.Exit)
			this.ended('Exit');
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
		this.questions.dialog(this.title.textContent ?? '', error.message || 'The server refused it.',
			[{ caption: 'OK', answer: {}, preferred: true }], null);
	}

	/** @param {string} reason */
	ended(reason) {
		// Said once: the socket's close and a refusal of the session gone may both tell it.
		if (document.querySelector('.ended') !== null)
			return;
		const shade = document.createElement('div');
		shade.className = 'ended';
		const text = document.createElement('p');
		text.textContent = `The session has ended: ${reason}`;
		const again = document.createElement('wa-button');
		again.setAttribute('variant', 'brand');
		again.textContent = 'Log in again';
		again.addEventListener('click', () => location.reload());
		shade.append(text, again);
		document.body.append(shade);
	}
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
