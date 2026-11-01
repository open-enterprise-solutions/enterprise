// A BUTTON — its caption, its picture, or both, as the State's Representation says (1 text, 2 picture, 3 both; 4 a
// TILE: the picture large over the caption and the Note under them, the button filling its cell - one for a finger);
// pressed back as event Press. A button of a group of commands (State's Members) opens them, and one picked is Press
// {Member} — its place among the group's.

import { Control, EventKind, defineControl } from './control.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

// What a control shows (ibState::ibRepresentation).
const textOnly = 1;
const pictureOnly = 2;
const tile = 4;

class Button extends Control {
	/** @param {ProtocolNode} node */
	build(node) {
		this.drawn = '';
		this.update(node);
	}

	/** @param {ProtocolNode} node */
	update(node) {
		const state = this.state(node);
		const drawn = JSON.stringify([state.Caption, state.Picture, state.Representation, state.Note, state.Members]);
		if (drawn === this.drawn)
			return true;
		this.drawn = drawn;

		const asTile = state.Representation === tile;
		this.classList.toggle('tile', asTile);
		const button = document.createElement('wa-button');
		if (!asTile)
			button.setAttribute('size', 's');
		const picture = state.Representation !== textOnly ? this.context.picture(state.Picture) : '';
		if (asTile) {
			// Stacked in the button's own label: the picture, the caption, the note.
			const face = document.createElement('span');
			face.className = 'tile-face';
			if (picture) {
				const image = document.createElement('img');
				image.src = picture;
				image.alt = '';
				face.append(image);
			}
			const caption = document.createElement('span');
			caption.className = 'tile-caption';
			caption.textContent = state.Caption ?? '';
			face.append(caption);
			if (state.Note) {
				const note = document.createElement('span');
				note.className = 'tile-note';
				note.textContent = state.Note;
				face.append(note);
			}
			button.append(face);
		}
		else {
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
		}
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
