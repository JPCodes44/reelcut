# MCP Coverage Implementation Plan

> Execute inline using superpowers:executing-plans. Use a read-only independent QML audit and final code review.

**Goal:** Full audited functional MCP coverage of the live app plus the existing file tools.
**Architecture:** Owner-only local IPC server in GUI, typed automation registry, generated MCP tools and client. Explicit QML UI façade for view controls; audited equivalent mapping for native gestures.
**Tech stack:** Qt6 C++17 Network/Quick, JSON-RPC MCP, QtTest.
**Spec:** docs/superpowers/specs/2026-11-18-mcp-coverage-design.md

## Global constraints
- Preserve the launcher checkout's current user edits.
- Never expose token/secret values in snapshots/events/results.
- Never auto-publish or alter external accounts during tests; use fake service endpoints.
- File tools remain independently available without the GUI.

## Review focus
- Public methods added later cannot silently become automation gaps.
- No arbitrary object invocation or QML evaluation.
- Connection/job completion cannot be confused with request acceptance.
- Native file-picker gestures have usable file/path equivalents.
- Multiple windows and malformed/oversized IPC messages cannot target the wrong instance.

## Task 1: Inventory and live registry
- [x] Audit public APIs and QML-only functionality; define schemas and equivalent mappings.
- [x] Add failing tests for descriptors, unsupported operations and secret-free state.
- [x] Implement strict argument conversion, allowed properties/actions and coverage reporting.

## Task 2: IPC and MCP tools
- [x] Add socket integration tests, missing/ambiguous instance and size-limit tests.
- [x] Implement local GUI service/client and generate live MCP tools without changing file tools.
- [x] Wire main app and build configuration, verify real requests mutate actual Backend state.

## Task 3: UI façade and full validation
- [x] Add UI tests for page/playback/filter/draft/selection operations.
- [x] Implement restricted QML façade from audit; document functional equivalents.
- [x] Run focused/full suite, independent review, apply to actual launcher source and build.
- [x] Publish coverage inventory with measured counts and any explicitly human authorization steps.

## Ledger
- Prepared /tmp/reelcut-mcp-coverage from actual launcher checkout, plus before-change snapshot.
- Existing known baseline failure: previewItemsDragAndResize; prior full run 112 passed, 1 failed.

- Final measured inventory: 98 direct + 8 mapped methods, 78 properties, 26 UI actions, unsupported=[]; 100% functional API coverage.
- Focused integration passed; full suite 119 passed, 1 existing preview drag failure.
- Both independent reviewers rechecked their fixes with no remaining findings.
- Installed 25 reviewed files in the launcher checkout after checking for concurrent changes; existing edits preserved.
- Actual application build passed. Built stdio MCP: 160 unique tools. Built GUI/MCP end-to-end smoke passed with isolated settings.
- Claude MCP config uses reelcut-current, which resolves to the rebuilt launcher checkout.
