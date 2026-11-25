// Every tab's document the page draws, each registered for its tab's Kind by the module that defines it — the
// workspace's (Mode.Workspace), as appworkspace registers its own; a studio's page would import the studio's set
// beside them, as appstudio does.

import './tabDocument.js';
import './assistant.js';
import './auditLog.js';
import '../window/homePage.js';
