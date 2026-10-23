// A COMMAND BAR — the State's CommandBar of a form, a table, a spreadsheet (protocol.h, ibCommandBar): its entries, a
// separator, a group whose commands are its children. An entry pressed goes back as its owner's event Command {Id};
// one command of a group, {Id, Member} — its place among the group's. Drawn as the owner's first child, ahead of its
// controls, and carrying no control id: the folding of the controls leaves it where it is.
//
// ONE LINE, AND WHAT DOES NOT FIT GOES UNDER », as on the thin client's bar (wxAuiToolBar's overflow button): the
// entries past the bar's end are hidden and offered in the menu of its last button, in their order.

import { EventKind } from './control.js';

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
		new ResizeObserver(() => this.fit()).observe(this.element);
	}

	/** @param {ProtocolNode | undefined} bar the State's CommandBar */
	update(bar) {
		const entries = bar?.NodeChildren ?? [];
		// Drawn anew only when what it shows changed: a frame that moved nothing on the bar leaves it, and a menu
		// opened from it, alone.
		const drawn = JSON.stringify(entries);
		if (drawn === this.drawn)
			return;
		this.drawn = drawn;
		this.entries = entries;
		this.element.replaceChildren(...entries.map((entry) => this.entry(entry)), this.overflow);
		this.element.hidden = entries.length === 0;
		this.fit();
	}

	/** Hides the entries past the bar's end and offers them under ». */
	fit() {
		const drawn = /** @type {HTMLElement[]} */ ([...this.element.children].slice(0, this.entries.length));
		for (const element of drawn)
			element.hidden = false;
		this.overflow.hidden = false;
		const style = getComputedStyle(this.element);
		const end = this.element.getBoundingClientRect().right - parseFloat(style.paddingRight) - parseFloat(style.borderRightWidth);
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
		dropdown.append(trigger);
		(entry.NodeChildren ?? []).forEach((member, place) => {
			const item = document.createElement('wa-dropdown-item');
			item.textContent = member.Caption || member.Tooltip || '';
			item.toggleAttribute('disabled', member.Enabled === false);
			item.value = String(member.Position ?? place);
			dropdown.append(item);
		});
		dropdown.addEventListener('wa-select', (event) => {
			const member = Number(/** @type {any} */ (event).detail.item.value);
			this.owner.send(EventKind.Command, { Id: entry.Id, Member: member });
		});
		return dropdown;
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
		trigger.setAttribute('aria-label', 'More');
		trigger.title = 'More';
		trigger.textContent = '»';
		dropdown.append(trigger);
		dropdown.addEventListener('wa-select', (event) => {
			this.owner.send(EventKind.Command, JSON.parse(/** @type {any} */ (event).detail.item.value));
		});
		return dropdown;
	}

	/**
	 * The menu's lines for the entries past the bar's end: a command, a group as its caption with its commands under
	 * it — as the window's menu draws one — and a separator between them.
	 * @param {ProtocolNode[]} entries
	 */
	overflowItems(entries) {
		/** @type {HTMLElement[]} */
		const items = [];
		for (const entry of entries) {
			if (entry.Separator) {
				if (items.length > 0 && items[items.length - 1].localName !== 'wa-divider')
					items.push(document.createElement('wa-divider'));
				continue;
			}
			if (entry.Kind !== group) {
				items.push(this.overflowItem(entry, { Id: entry.Id }));
				continue;
			}
			const caption = this.overflowItem(entry, undefined);
			caption.toggleAttribute('disabled', true);
			items.push(caption);
			(entry.NodeChildren ?? []).forEach((member, place) => {
				const item = this.overflowItem(member, { Id: entry.Id, Member: member.Position ?? place });
				item.style.paddingInlineStart = '1em';
				items.push(item);
			});
		}
		if (items.length > 0 && items[items.length - 1].localName === 'wa-divider')
			items.pop();
		return items;
	}

	/**
	 * @param {ProtocolNode} entry
	 * @param {{Id: number, Member?: number} | undefined} command what goes back when it is chosen
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
