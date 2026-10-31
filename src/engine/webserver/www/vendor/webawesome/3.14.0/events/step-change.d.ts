import type WaStep from '../components/step/step.js';
export declare class WaStepChangeEvent extends Event {
    readonly detail: WaStepChangeEventDetail;
    constructor(detail: WaStepChangeEventDetail);
}
interface WaStepChangeEventDetail {
    /** The name of the step that's now active. */
    name: string;
    /** The name of the step that was active before, or `null` if none was. */
    previousName: string | null;
    /** The step that's now active. */
    step: WaStep;
    /** The step that was active before, or `null` if none was. */
    previousStep: WaStep | null;
}
declare global {
    interface GlobalEventHandlersEventMap {
        'wa-step-change': WaStepChangeEvent;
    }
}
export {};
