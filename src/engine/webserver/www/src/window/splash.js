// THE SPLASH — the thin client's (ibProcessSplashScreen), with the stage of the start under it: up while the page loads
// and the client logs in, gone before a login or a refusal is said, which it would stand over, and up again until the
// first window is drawn. It is in the page itself (index.html), so it is on the screen before anything else has come.

// How long the components on the page are waited for — Web Awesome's own cloak waits as long.
const definedWithin = 2000;
// …and how long the fade is given.
const fadeWithin = 150;

/** @type {number | undefined} the splash's fade under way */
let fading = undefined;

/** @returns {HTMLElement} */
function splash() {
	return /** @type {HTMLElement} */ (document.getElementById('splash'));
}

/**
 * The splash up, saying the stage the start has reached.
 * @param {string} stage
 */
export function showSplash(stage) {
	clearTimeout(fading);
	const element = splash();
	element.classList.remove('leaving');
	element.hidden = false;
	/** @type {HTMLElement} */ (element.querySelector('.stage')).textContent = stage;
}

/** The splash gone, once the components drawn under it are ready to be seen: no raw element shows as it fades. */
export async function hideSplash() {
	const element = splash();
	if (element.hidden || element.classList.contains('leaving'))
		return;
	const waited = new Set([...document.querySelectorAll(':not(:defined)')].map((one) => one.localName));
	await Promise.race([
		Promise.all([...waited].map((tag) => customElements.whenDefined(tag))),
		new Promise((resolve) => setTimeout(resolve, definedWithin)),
	]);
	element.classList.add('leaving');
	fading = setTimeout(() => {
		element.hidden = true;
		element.classList.remove('leaving');
	}, fadeWithin);
}
