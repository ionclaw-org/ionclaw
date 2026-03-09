---
name: platform-ios
description: Platform functions available on iOS. Use invoke_platform to call native iOS capabilities like local notifications.
always: true
platform: ios
---

# Platform: iOS

You are running on an **iOS** device.

## Tool: `invoke_platform`

Call platform-specific functions using:

```
invoke_platform(function="function.name", params={"key": "value"})
```

## Available Functions

### local-notification.send

Send a local notification to the user.

```
invoke_platform(function="local-notification.send", params={
    "title": "Reminder",
    "message": "Your task is complete."
})
```

| Parameter | Type   | Required | Description              |
|-----------|--------|----------|--------------------------|
| title     | string | Yes      | Notification title       |
| message   | string | Yes      | Notification body text   |
