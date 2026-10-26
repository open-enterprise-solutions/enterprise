// THE WINDOW — the client's window as the server draws it (the frame): its title, its menu, the section panel down its
// side, its tabs, the active tab's view, the messages to the person and what the server asks them. The menu's routine
// is the client's; everything done to a document goes to the server (docs/public/client-protocol.md, Menu and
// commands). Each tab keeps the element its view was drawn into, so a tab gone back to finds its scroll and its table
// where they were.

import { reconcile, dispose } from '../controls/control.js';
import { Refusal } from '../protocol/connection.js';
import { Questions } from './questions.js';
import { Sections } from './sections.js';
import { showFunctions } from './functions.js';

/** @typedef {import('../protocol/session.js').Session} Session */
/** @typedef {import('../protocol/pictures.js').Pictures} Pictures */
/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */
/** @typedef {import('../controls/control.js').FormContext} FormContext */

// The schemas the window opens itself (ibSchema): All functions.
const allFunctions = 1;

// How long a call is waited on before the window says it waits: a quick answer shows nothing.
const busyAfter = 800;

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
		this.title = document.createElement('h1');
		this.menu = document.createElement('nav');
		this.menu.className = 'menu';
		this.header.append(this.title, this.menu);

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
		if (session.frame !== null)
			this.sections.read(session.frame).catch((error) => this.refused(error));

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
		this.drawnMenu = '';

		session.addEventListener('frame', (event) => this.draw(/** @type {CustomEvent} */ (event).detail));
		session.addEventListener('events', (event) => this.happened(/** @type {CustomEvent} */ (event).detail));
		session.addEventListener('busy', (event) => this.waiting(/** @type {CustomEvent} */ (event).detail));
		session.addEventListener('connection', (event) => this.connected(/** @type {CustomEvent} */ (event).detail));
		session.addEventListener('ended', (event) => this.ended(/** @type {CustomEvent} */ (event).detail));
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
			else if (item.Schema !== undefined)
				this.say(`${item.Title ?? ''}: not in the browser yet`);
			else if (item.Command !== undefined)
				await this.session.call('command', { Command: item.Command });
		}
		catch (error) {
			this.refused(/** @type {Error} */ (error));
		}
	}

	/** @param {ProtocolNode} frame */
	drawTabs(frame) {
		const tabs = frame.Tabs?.NodeChildren ?? [];
		this.tabs.replaceChildren(...tabs.map((tab) => {
			const button = document.createElement('div');
			button.className = 'tab';
			button.setAttribute('role', 'tab');
			button.setAttribute('aria-selected', String(tab.NodeId === frame.ActiveTab));
			button.tabIndex = 0;
			const icon = this.pictures.url(tab.Icon);
			if (icon) {
				const image = document.createElement('img');
				image.src = icon;
				image.alt = '';
				button.append(image);
			}
			const title = document.createElement('span');
			title.textContent = tab.Title ?? '';
			button.title = tab.Title ?? '';
			button.append(title);
			button.addEventListener('click', () => {
				if (tab.NodeId !== this.session.frame?.ActiveTab)
					this.session.call('activate', { Tab: tab.NodeId }).catch((error) => this.refused(error));
			});
			if (!tab.Locked) {
				const close = document.createElement('button');
				close.type = 'button';
				close.className = 'close';
				close.textContent = '×';
				close.setAttribute('aria-label', `Close ${tab.Title ?? ''}`);
				close.addEventListener('click', (event) => {
					event.stopPropagation();
					this.session.call('close', { Tab: tab.NodeId }).catch((error) => this.refused(error));
				});
				button.append(close);
			}
			return button;
		}));
		this.tabs.querySelector('[aria-selected="true"]')?.scrollIntoView({ block: 'nearest', inline: 'nearest' });
		// A tab closed takes its view with it.
		const open = new Set(tabs.map((tab) => tab.NodeId));
		for (const [id, element] of this.tabViews) {
			if (!open.has(id)) {
				dispose(element);
				this.tabViews.delete(id);
			}
		}
	}

	/** @param {ProtocolNode} frame */
	drawView(frame) {
		const active = frame.ActiveTab;
		const view = frame.View;
		for (const [id, element] of this.tabViews)
			element.hidden = id !== active;
		if (view === undefined || active === undefined || active === 0)
			return;
		const existing = this.tabViews.get(active) ?? null;
		if (view.NodeType !== 'ClientForm') {
			// A view that is no form — a text, a spreadsheet, the journal, the start page — is not drawn here yet.
			const placeholder = existing ?? document.createElement('div');
			placeholder.className = 'view-placeholder';
			placeholder.textContent = 'This view is not in the browser yet.';
			placeholder.hidden = false;
			if (existing === null) {
				this.views.append(placeholder);
				this.tabViews.set(active, placeholder);
			}
			return;
		}
		const form = reconcile(this.views, /** @type {any} */ (existing), view, this.contextOf(view));
		form.hidden = false;
		this.tabViews.set(active, form);
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
