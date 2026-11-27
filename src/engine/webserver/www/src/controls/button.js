// A BUTTON — its caption, its picture, or both, as the State's Representation says (1 text, 2 picture, 3 both), the
// picture where its PictureLocation puts it (on the left; 2 on the right; 3 a TILE: the picture large over the caption
// and the State's Note under them, the button filling its cell - one for a finger), drawn as its View says (1 a
// HYPERLINK: no face and no frame, the caption underlined); pressed back as event Press. A button of a group of
// commands (State's Members — entries as a bar's group has) opens them as a bar's group is opened, and one picked is
// Command {Id}, as on a bar.

import { CommandBar } from './commandBar.js';
import { Control, EventKind, defineControl } from './control.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

// What a control shows (ibState::ibRepresentation).
const textOnly = 1;
const pictureOnly = 2;

// Where a button's picture stands (ibProperty::ibPictureLocation), and how it is drawn (ibProperty::ibButtonView).
const right = 2;
const tile = 3;
const hyperlink = 1;

class Button extends Control {
	/** @param {ProtocolNode} node */
	build(node) {
		this.drawn = '';
		this.update(node);
	}

	/** @param {ProtocolNode} node */
	update(node) {
		const state = this.state(node);
		const drawn = JSON.stringify([state.Caption, state.Picture, state.Representation, node.PictureLocation, node.View,
			state.Note, state.Members]);
		if (drawn === this.drawn)
			return true;
		this.drawn = drawn;

		const asTile = node.PictureLocation === tile;
		const asHyperlink = node.View === hyperlink;
		this.classList.toggle('tile', asTile);
		this.classList.toggle('hyperlink', asHyperlink);
		const button = document.createElement('wa-button');
		if (!asTile)
			button.setAttribute('size', 's');
		if (asHyperlink)
			button.setAttribute('appearance', 'plain');
		const picture = state.Representation !== textOnly ? this.context.picture(state.Picture) : '';
		const image = picture ? document.createElement('img') : null;
		if (image) {
			image.src = picture;
			image.alt = '';
		}
		// The caption names the button even where the picture alone is shown: said to a reader then, not drawn.
		const captioned = state.Representation !== pictureOnly || !picture;
		if (!captioned)
			button.setAttribute('aria-label', state.Caption ?? '');
		if (asTile) {
			// Stacked in the button's own label: the picture, the caption, the note.
			const face = document.createElement('span');
			face.className = 'tile-face';
			if (image)
				face.append(image);
			if (captioned) {
				const caption = document.createElement('span');
				caption.className = 'tile-caption';
				caption.textContent = state.Caption ?? '';
				face.append(caption);
			}
			if (state.Note) {
				const note = document.createElement('span');
				note.className = 'tile-note';
				note.textContent = state.Note;
				face.append(note);
			}
			button.append(face);
		}
		else {
			if (image) {
				image.slot = node.PictureLocation === right ? 'end' : 'start';
				button.append(image);
			}
			if (captioned)
				button.append(state.Caption ?? '');
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
		dropdown.append(button, ...CommandBar.groupItems(members));
		dropdown.addEventListener('wa-select', (event) => {
			const value = /** @type {any} */ (event).detail.item.value;
			if (value)
				this.send(EventKind.Command, { Id: Number(value) });
		});
		this.replaceChildren(dropdown);
		return true;
	}
}

defineControl('Button', Button);
