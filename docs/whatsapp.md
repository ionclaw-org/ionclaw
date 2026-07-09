# WhatsApp Channels

IonClaw receives and replies to WhatsApp messages through two interchangeable providers, both driven by inbound **webhooks**:

- **Z-API** (`whatsapp_zapi`) — a third-party WhatsApp gateway.
- **WhatsApp Cloud API** (`whatsapp_meta`) — Meta's official API.

Both share the logical channel `whatsapp`. When both are enabled, **Meta takes precedence** for outbound delivery. Inbound message ids are de-duplicated, so a provider redelivery (after downtime) is processed at most once.

The server must be reachable on a public HTTPS URL for the provider to call its webhook. That base URL is the `server.public_url` in your config.

Both channels are configurable from the **web UI** (Settings → Channels → WhatsApp Z-API / Cloud API), which shows the exact webhook URL to paste into the provider console and masks the stored secrets. The `config.yml` form below is equivalent.

## Webhook URLs

Configure these in the provider console (they are unauthenticated at the app level — each is verified by the provider's signature/token):

| Provider | Webhook URL |
|----------|-------------|
| Z-API | `{public_url}/webhook/whatsapp-zapi` |
| WhatsApp Cloud API (Meta) | `{public_url}/webhook/whatsapp-meta` |

## Z-API setup

1. In the Z-API panel, create an instance and generate the account **Client-Token** (Security → Client Token → activate).
2. Point the instance's "on message received" webhook at `{public_url}/webhook/whatsapp-zapi`.
3. Enable and configure the `whatsapp_zapi` channel:

```yaml
channels:
  whatsapp_zapi:
    enabled: true
    raw:
      instance_id: "YOUR_INSTANCE_ID"
      instance_token: "YOUR_INSTANCE_TOKEN"
      client_token: "YOUR_ACCOUNT_CLIENT_TOKEN"
```

Outbound calls go to `https://api.z-api.io/instances/{instance_id}/token/{instance_token}/...` with the `Client-Token` header. Z-API has no continuous typing indicator; a short `delayTyping` is attached to each send.

## WhatsApp Cloud API (Meta) setup

1. In the Meta App dashboard, add the WhatsApp product, note the **Phone Number ID** and generate a **permanent** access token (System User token).
2. Under Webhooks, set the callback URL to `{public_url}/webhook/whatsapp-meta` and a **Verify Token** of your choosing, then subscribe to the `messages` field. Meta calls the URL with a `GET` handshake (`hub.challenge`); IonClaw echoes it back when the verify token matches.
3. Copy the App **App Secret** (used to validate the `X-Hub-Signature-256` HMAC on every inbound POST).
4. Enable and configure the `whatsapp_meta` channel:

```yaml
channels:
  whatsapp_meta:
    enabled: true
    raw:
      access_token: "YOUR_PERMANENT_ACCESS_TOKEN"
      phone_number_id: "YOUR_PHONE_NUMBER_ID"
      verify_token: "YOUR_CHOSEN_VERIFY_TOKEN"
      app_secret: "YOUR_APP_SECRET"
      graph_version: "v23.0"   # optional, defaults to v23.0
```

Every inbound POST is rejected unless its `X-Hub-Signature-256` matches `HMAC-SHA256(rawBody, app_secret)`. Media messages carry a media id, fetched from the Graph API in two authenticated steps.

## Behavior

- Inbound text (and the caption of a media message) is delivered to the agent as a normal user turn on the `whatsapp` channel, keyed by the sender's phone number.
- The agent's reply is delivered back through the active provider, split into chunks under the WhatsApp message length limit.
- Group messages and the bot's own echoes (Z-API `fromMe`) are ignored.
- Inbound media (image/audio/video/document) is downloaded into `workspace/public/media` and attached to the turn; a media-only message uses its caption as the text.
- If the channel's `allowed_users` list is set, only those phone numbers may talk to the agent; everyone else is ignored. Leave it empty to accept any sender.

## Outbound media and message splitting

The agent can attach files and split its reply using inline markers, which the WhatsApp runner turns into real attachments (both providers fetch the media by its public URL, so `server.public_url` must be set and reachable):

| Marker | Effect |
|--------|--------|
| `[[image:public/media/chart.png]]` | send the file as an image; surrounding text becomes the caption |
| `[[audio:public/media/reply.ogg]]` / `[[voice:...]]` | send as an audio message (no caption) |
| `[[video:public/media/clip.mp4]]` | send as a video with caption |
| `[[document:public/reports/r.pdf]]` / `[[file:...]]` | send as a document with caption and file name |
| `[[media:PATH]]` | pick the type from the file extension |
| `[[break]]` | split the reply into separate WhatsApp messages |

A marker path under `public/` resolves to `{public_url}/public/<path>`; an absolute `http(s)` URL is sent as-is. Plain replies with no markers are delivered as one or more text messages, split under the WhatsApp length limit. The agent is told about these markers automatically when it is responding on the `whatsapp` channel.

The Telegram channel understands the exact same markers (delivered through `sendPhoto`/`sendAudio`/`sendVideo`/`sendDocument`), so the guidance here applies there too.

```yaml
channels:
  whatsapp_zapi:
    enabled: true
    allowed_users: ["5511999999999"]   # optional owner allowlist
    raw: { instance_id: "...", instance_token: "...", client_token: "..." }
```
