#pragma once

namespace THPrac {
/** Main update function for the Links page. Initialization is handled automatically. */
void LauncherLinksUiUpdate();
/**
    Signals the Links page to save/reset its state. Call this before switching pages or closing the
    launcher.
    NOTE: Calling before closing the launcher is currently unnecessary. This may change later on.
*/
void LauncherLinksInformPageClosing();
} // namespace THPrac
