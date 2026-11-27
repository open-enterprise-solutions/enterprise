// A COMMAND BAR — the State's CommandBar of a form, a table, a spreadsheet (protocol.h, ibCommandBar): its entries, a
// separator, a group whose entries are its children — a command, or a group of its own, to any depth. An entry pressed
// goes back as its owner's event Command {Id} — its own id, wherever it stands. Drawn as the owner's first child, ahead
// of its controls, and carrying no control id: the folding of the controls leaves it where it is.
//
// ONE LINE, AND WHAT DOES NOT FIT GOES UNDER », as on the thin client's bar (wxAuiToolBar's overflow button): the
// entries past the bar's end are hidden and offered in the menu of its last button, in their order.
//
// …AND A SEARCH FIELD AT ITS END when the State's CommandBar says Search (its text): what it holds goes back as the
// owner's event Search {Text} at every change of it. The entries end where the field begins.

import { EventKind } from './control.js';
import { _ } from '../localization.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */
/** @typedef {import('./control.js').Control} Control */

// What an entry is (ibCommandBar::ibKind).
const group = 2;

export class CommandBar {
	/** @param {Control} owner the control whose bar it is — the event goes as its */
	constructor(owner) {
		this.owner = owner;
		this.element = document.createElement('div');
		this.element.className = 'command-bar';
		this.drawn = '';
		/** @type {ProtocolNode[]} */
		this.entries = [];
		this.overflow = this.overflowMenu();
		/** @type {HTMLElement | undefined} the search field, made the first time the bar has one */
		this.search = undefined;
		new ResizeObserver(() => this.fit()).observe(this.element);
	}

	/** @param {ProtocolNode | undefined} bar the State's CommandBar */
	update(bar) {
		const entries = bar?.NodeChildren ?? [];
		const searched = typeof bar?.Search === 'string';
		if (searched)
			this.searchField(/** @type {string} */ (bar?.Search));
		// Drawn anew only when what it shows changed: a frame that moved nothing on the bar leaves it, and a menu
		// opened from it, alone. The field's text is set, not drawn.
		const drawn = JSON.stringify(entries) + (searched ? '|search' : '');
		if (drawn === this.drawn)
			return;
		this.drawn = drawn;
		this.entries = entries;
		this.element.replaceChildren(...entries.map((entry) => this.entry(entry)), this.overflow,
			...(searched && this.search ? [this.search] : []));
		this.element.hidden = entries.length === 0 && !searched;
		this.fit();
	}

	/**
	 * The search field, made once — the same element put back at every drawing, so what is being typed stays — and the
	 * text the server holds put into it, unless the person is typing there: what they typed is newer than the answer.
	 * @param {string} text
	 */
	searchField(text) {
		if (!this.search) {
			const input = document.createElement('wa-input');
			input.className = 'search';
			input.setAttribute('size', 's');
			input.setAttribute('type', 'search');
			input.setAttribute('with-clear', '');
			input.setAttribute('placeholder', _('Search'));
			input.setAttribute('aria-label', _('Search'));
			// Every change of what it holds, the × too; the same text twice is nothing new to the server.
			const send = () => this.owner.send(EventKind.Search, { Text: /** @type {any} */ (input).value ?? '' });
			input.addEventListener('input', send);
			input.addEventListener('wa-clear', send);
			this.search = input;
		}
		const field = /** @type {any} */ (this.search);
		if (document.activeElement !== this.search && field.value !== text)
			field.value = text;
	}

	/** Hides the entries past the bar's end — the search field's beginning, when it has one — and offers them under ». */
	fit() {
		const drawn = /** @type {HTMLElement[]} */ ([...this.element.children].slice(0, this.entries.length));
		for (const element of drawn)
			element.hidden = false;
		this.overflow.hidden = false;
		const style = getComputedStyle(this.element);
		// The search field keeps at least its smallest width at the end (form.css): the entries end before it.
		const search = this.search !== undefined && this.search.parentElement === this.element ? this.search : undefined;
		const searchRoom = search ? parseFloat(getComputedStyle(search).minWidth) + (parseFloat(style.columnGap) || 0) : 0;
		const end = this.element.getBoundingClientRect().right - parseFloat(style.paddingRight) - parseFloat(style.borderRightWidth)
			- searchRoom;
		// A dropdown has no box of its own (display: contents): its button is what stands on the bar.
		const boxOf = (/** @type {Element} */ element) =>
			(element.localName === 'wa-dropdown' ? element.querySelector(':scope > [slot="trigger"]') ?? element : element).getBoundingClientRect();
		const right = (/** @type {Element} */ element) => boxOf(element).right;
		if (drawn.every((element) => right(element) <= end)) {
			this.overflow.hidden = true;
			return;
		}
		const room = end - boxOf(this.overflow).width - (parseFloat(style.columnGap) || 0);
		let cut = drawn.findIndex((element) => right(element) > room);
		// A separator does not end the bar.
		while (cut > 0 && this.entries[cut - 1].Separator)
			cut--;
		for (const element of drawn.slice(cut))
			element.hidden = true;
		const [trigger] = this.overflow.children;
		this.overflow.replaceChildren(trigger, ...this.overflowItems(this.entries.slice(cut)));
	}

	/** @param {ProtocolNode} entry */
	entry(entry) {
		if (entry.Separator) {
			const separator = document.createElement('span');
			separator.className = 'separator';
			return separator;
		}
		if (entry.Kind === group)
			return this.group(entry);
		const button = this.button(entry);
		button.addEventListener('click', () => this.owner.send(EventKind.Command, { Id: entry.Id }));
		return button;
	}

	/** @param {ProtocolNode} entry */
	group(entry) {
		const dropdown = document.createElement('wa-dropdown');
		const trigger = this.button(entry);
		trigger.slot = 'trigger';
		trigger.setAttribute('with-caret', '');
		dropdown.append(trigger, ...CommandBar.groupItems(entry.NodeChildren ?? []));
		dropdown.addEventListener('wa-select', (event) => {
			const value = /** @type {any} */ (event).detail.item.value;
			if (value)
				this.owner.send(EventKind.Command, { Id: Number(value) });
		});
		return dropdown;
	}

	/**
	 * A group's entries as a dropdown's items — a bar's group, a button's (its State's Members): a command by its own id,
	 * a group of them an item whose submenu holds its own, to any depth.
	 * @param {ProtocolNode[]} entries
	 * @returns {HTMLElement[]}
	 */
	static groupItems(entries) {
		return entries.filter((entry) => !entry.Separator).map((entry) => {
			const item = document.createElement('wa-dropdown-item');
			item.textContent = entry.Caption || entry.Tooltip || '';
			item.toggleAttribute('disabled', entry.Enabled === false);
			if (entry.Kind === group) {
				for (const member of CommandBar.groupItems(entry.NodeChildren ?? [])) {
					member.slot = 'submenu';
					item.append(member);
				}
			}
			else
				/** @type {any} */ (item).value = String(entry.Id);
			return item;
		});
	}

	/** The » at the bar's end, its menu filled by fit(). */
	overflowMenu() {
		const dropdown = document.createElement('wa-dropdown');
		dropdown.className = 'overflow';
		dropdown.setAttribute('placement', 'bottom-end');
		const trigger = document.createElement('wa-button');
		trigger.slot = 'trigger';
		trigger.setAttribute('size', 's');
		trigger.setAttribute('appearance', 'plain');
		trigger.setAttribute('aria-label', _('More'));
		trigger.title = _('More');
		trigger.textContent = '»';
		dropdown.append(trigger);
		dropdown.addEventListener('wa-select', (event) => {
			this.owner.send(EventKind.Command, JSON.parse(/** @type {any} */ (event).detail.item.value));
		});
		return dropdown;
	}

	/**
	 * The menu's lines for the entries past the bar's end: a command, a group as its caption with its entries under
	 * it, a step further in at each depth — as the window's menu draws one — and a separator between them.
	 * @param {ProtocolNode[]} entries
	 */
	overflowItems(entries) {
		/** @type {HTMLElement[]} */
		const items = [];
		/** @param {ProtocolNode} entry @param {number} depth */
		const line = (entry, depth) => {
			const command = entry.Kind === group ? undefined : { Id: entry.Id };
			const item = this.overflowItem(entry, command);
			if (command === undefined)
				item.toggleAttribute('disabled', true);
			if (depth > 0)
				item.style.paddingInlineStart = `${depth}em`;
			items.push(item);
			if (entry.Kind === group)
				for (const member of entry.NodeChildren ?? [])
					if (!member.Separator)
						line(member, depth + 1);
		};
		for (const entry of entries) {
			if (entry.Separator) {
				if (items.length > 0 && items[items.length - 1].localName !== 'wa-divider')
					items.push(document.createElement('wa-divider'));
				continue;
			}
			line(entry, 0);
		}
		if (items.length > 0 && items[items.length - 1].localName === 'wa-divider')
			items.pop();
		return items;
	}

	/**
	 * @param {ProtocolNode} entry
	 * @param {{Id: number} | undefined} command what goes back when it is chosen
	 */
	overflowItem(entry, command) {
		const item = document.createElement('wa-dropdown-item');
		const picture = this.owner.context.picture(entry.Picture);
		if (picture) {
			const image = document.createElement('img');
			image.src = picture;
			image.alt = '';
			image.slot = 'icon';
			item.append(image);
		}
		item.append(entry.Caption || entry.Tooltip || '');
		item.toggleAttribute('disabled', entry.Enabled === false);
		if (command !== undefined)
			item.value = JSON.stringify(command);
		return item;
	}

	/** @param {ProtocolNode} entry */
	button(entry) {
		const button = document.createElement('wa-button');
		button.setAttribute('size', 's');
		button.setAttribute('appearance', 'plain');
		button.toggleAttribute('disabled', entry.Enabled === false);
		// The bar's default command stands out (form.css) — a form's «… and close», or the item the author marked.
		button.classList.toggle('default', entry.Default === true);
		const picture = this.owner.context.picture(entry.Picture);
		if (picture) {
			const image = document.createElement('img');
			image.src = picture;
			image.alt = '';
			image.slot = entry.Caption ? 'start' : '';
			button.append(image);
		}
		if (entry.Caption)
			button.append(entry.Caption);
		const tooltip = entry.Tooltip || entry.Caption || '';
		if (tooltip)
			button.title = tooltip;
		// A bar of pictures alone still says what each does to a reader of the screen.
		if (!entry.Caption)
			button.setAttribute('aria-label', tooltip);
		return button;
	}
}
