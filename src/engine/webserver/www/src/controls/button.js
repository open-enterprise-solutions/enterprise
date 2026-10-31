// A BUTTON — its caption, its picture, or both, as the State's Representation says (1 text, 2 picture, 3 both);
// pressed back as event Press. A button of a group of commands (State's Members) opens them, and one picked is Press
// {Member} — its place among the group's.

import { Control, EventKind, defineControl } from './control.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

// What a control shows (ibState::ibRepresentation).
const textOnly = 1;
const pictureOnly = 2;

class Button extends Control {
	/** @param {ProtocolNode} node */
	build(node) {
		this.drawn = '';
		this.update(node);
	}

	/** @param {ProtocolNode} node */
	update(node) {
		const state = this.state(node);
		const drawn = JSON.stringify([state.Caption, state.Picture, state.Representation, state.Members]);
		if (drawn === this.drawn)
			return true;
		this.drawn = drawn;

		const button = document.createElement('wa-button');
		button.setAttribute('size', 's');
		const picture = state.Representation !== textOnly ? this.context.picture(state.Picture) : '';
		if (picture) {
			const image = document.createElement('img');
			image.src = picture;
			image.alt = '';
			image.slot = 'start';
			button.append(image);
		}
		if (state.Representation !== pictureOnly || !picture)
			button.append(state.Caption ?? '');
		else
			button.setAttribute('aria-label', state.Caption ?? '');
		this.lookTarget = button;

		const members = state.Members?.NodeChildren ?? [];
		if (members.length === 0) {
			button.addEventListener('click', () => this.send(EventKind.Press));
			this.replaceChildren(button);
			return true;
		}
		const dropdown = document.createElement('wa-dropdown');
		button.slot = 'trigger';
		button.setAttribute('with-caret', '');
		dropdown.append(button);
		for (const member of members) {
			const item = document.createElement('wa-dropdown-item');
			item.textContent = member.Caption ?? '';
			item.value = String(member.Member);
			dropdown.append(item);
		}
		dropdown.addEventListener('wa-select', (event) => {
			this.send(EventKind.Press, { Member: Number(/** @type {any} */ (event).detail.item.value) });
		});
		this.replaceChildren(dropdown);
		return true;
	}
}

defineControl('Button', Button);
