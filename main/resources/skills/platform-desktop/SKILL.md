---
name: platform-desktop
description: Platform functions available on macOS, Windows, and Linux. Use invoke_platform to call native platform capabilities.
always: true
platform: [linux, macos, windows]
---

# Platform: macOS, Windows, Linux

You are running on **macOS, Windows, or Linux**.

## Tool: `invoke_platform`

Call platform-specific functions using:

```
invoke_platform(function="function.name", params={"key": "value"})
```

## Available Functions

No platform-specific functions are currently registered on this platform. If you attempt to invoke a function that is not available, you will receive an error indicating it is not implemented on this platform.

Future functions may include:
- System notifications
- Clipboard operations
- Native dialog boxes
