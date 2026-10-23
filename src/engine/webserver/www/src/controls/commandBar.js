// A COMMAND BAR — the State's CommandBar of a form, a table, a spreadsheet (protocol.h, ibCommandBar): its entries, a
// separator, a group whose commands are its children. An entry pressed goes back as its owner's event Command {Id};
// one command of a group, {Id, Member} — its place among the group's. Drawn as the owner's first child, ahead of its
// controls, and carrying no control id: the folding of the controls leaves it where it is.

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
		this.element.replaceChildren(...entries.map((entry) => this.entry(entry)));
		this.element.hidden = entries.length === 0;
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
