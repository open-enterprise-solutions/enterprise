// THE LOGIN — as the thin client's start: the login goes first with no name, and only a name refused asks the person —
// a base with no users lets them in at once. Then the names the base offers (users, asked before the login) and the
// password; a refusal is said where it was made, and the person tries again with the password typed anew.

import { Refusal } from '../protocol/connection.js';

/** @typedef {import('../protocol/session.js').Session} Session */

/**
 * The login, asking the person until the session is logged in.
 * @param {HTMLElement} root
 * @param {Session} session
 * @param {string} publication the base's name in the address
 * @returns {Promise<void>}
 */
export async function logIn(root, session, publication) {
	let refused = '';
	try {
		await session.login('', '');
		return;
	}
	catch (error) {
		// A name refused is what the window asks for; anything else is said in it.
		if (!(error instanceof Refusal) || error.code !== 401)
			refused = /** @type {Error} */ (error).message;
	}
	const users = await session.users();
	return new Promise((resolve) => {
		const form = document.createElement('form');
		form.className = 'login';
		const heading = document.createElement('h1');
		heading.textContent = publication;

		const user = /** @type {any} */ (document.createElement(users.length > 0 ? 'wa-select' : 'wa-input'));
		user.label = 'User';
		user.setAttribute('name', 'user');
		for (const name of users) {
			const option = /** @type {any} */ (document.createElement('wa-option'));
			option.value = name;
			option.textContent = name;
			user.append(option);
		}
		if (users.length > 0)
			user.value = users[0];

		const password = /** @type {any} */ (document.createElement('wa-input'));
		password.label = 'Password';
		password.setAttribute('type', 'password');
		password.setAttribute('password-toggle', '');
		password.setAttribute('autocomplete', 'current-password');

		const refusal = document.createElement('p');
		refusal.className = 'refusal';
		refusal.setAttribute('role', 'alert');
		refusal.textContent = refused;

		const enter = document.createElement('wa-button');
		enter.setAttribute('type', 'submit');
		enter.setAttribute('variant', 'brand');
		enter.textContent = 'Log in';

		form.append(heading, user, password, refusal, enter);
		form.addEventListener('submit', async (event) => {
			event.preventDefault();
			refusal.textContent = '';
			enter.setAttribute('loading', '');
			try {
				await session.login(user.value ?? '', password.value ?? '');
				resolve();
			}
			catch (error) {
				refusal.textContent = /** @type {Error} */ (error).message;
				password.value = '';
				password.focus?.();
			}
			finally {
				enter.removeAttribute('loading');
			}
		});
		root.replaceChildren(form);
		(users.length > 0 ? password : user).focus?.();
	});
}
