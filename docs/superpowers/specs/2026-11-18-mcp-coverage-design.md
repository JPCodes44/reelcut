# Complete functional MCP coverage

User request: ensure MCP coverage reaches every functional part of Reelcut.

Current state: 28 file-based project tools; no remote operations for live editor state, layers, posting, calendar, account settings, Stats, Downloads, credit monitoring or playback/navigation.

Approach: retain existing saved-project tools and add a local, same-user IPC bridge to the actual running GUI. Generate typed live tools from the public invokable APIs of Backend, Accounts, Stats, Downloads and HiggsfieldCredits; expose safe property snapshots and setters. UI-only operations get an explicit, restricted façade. File dialogs and drag gestures map to deterministic file/path equivalents rather than requiring mouse gestures. Theme/language remain readable and follow the OS; no unrelated system customization is added.

Coverage means functional parity, not a claim about C++ line coverage or physical OS gestures. The coverage inventory must classify every public application invokable as exposed or mapped to an equivalent operation, and fail tests for unknown/unsupported operations. Audit QML-only actions separately. Document exceptions such as browser OAuth consent, which remains a human step of the same native sign-in flow.

No arbitrary QObject traversal, code evaluation, shell execution or credential getter is exposed. Models contain no access/refresh tokens or client secrets. Credential setters may accept inputs but must not echo or log them. Live tools carry accurate mutation/read-only annotations. Publishing remains an explicit named action, never a side effect of a read tool. Async requests return accepted/current state and have bounded events/progress polling.

IPC uses owner-only local sockets, bounded message sizes and timeouts, unique window instance IDs, explicit instance selection when multiple windows run, and stale-socket handling that never takes over a live server. Existing file tools keep working with no window. Live calls clearly report when a GUI is required.

Verify protocol schemas, coverage accounting, real cross-socket read/edit/async calls, instance selection, invalid/unregistered action rejection, secret-free snapshots, navigation/playback, and existing project/posting/stats regressions. Preserve all existing uncommitted work and rebuild the launcher checkout after applying reviewed changes.
