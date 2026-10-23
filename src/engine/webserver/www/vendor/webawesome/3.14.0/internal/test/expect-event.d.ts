/**
 * Verifies that the specified events fire on an element during an action.
 *
 * Usage:
 *   await expectEvent(el, ['input', 'wa-input'], () => clickOnElement(el));
 *   const events = await expectEvent(el, 'wa-change', () => el.click());
 *
 * Options:
 *   - timeout: max ms to wait for events (default 1000)
 *   - count: expected number of times each event fires (default 1)
 */
export declare function expectEvent(el: Element, events: string | string[], action: () => void | Promise<void>, options?: {
    timeout?: number;
    count?: number;
}): Promise<Event[]>;
