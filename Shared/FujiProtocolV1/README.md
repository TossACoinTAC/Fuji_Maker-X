# Fuji Device/Phone Protocol v1

This directory is the normative device/phone contract for the Fuji P0 vertical
slice. The Swift implementation and the embedded host tests both consume the
fixtures below.

## Envelope

Every message is UTF-8 JSON with these fields:

| Field | Type | Rule |
| --- | --- | --- |
| `version` | integer | Must be `1` |
| `message_id` | UUID string | Unique wire-message identifier |
| `request_id` | UUID string | Required for transaction messages |
| `direction` | string | `device_to_phone` or `phone_to_device` |
| `type` | string | One of the six v1 message types |
| `ttl_ms` | integer | `1...120000`; starts at local receipt time |
| `sent_at_utc` | RFC 3339 string | Optional and for logging only |
| `payload` | object | Typed according to `type` |

`request_id` identifies one action. A navigation action uses a new request ID
and carries the food-search ID in `parent_request_id`. A result must echo the
request ID of the action it completes. `message_id` is used for transport-level
deduplication.

Receivers ignore unknown object fields for protocol version 1, but reject
unknown message types, directions, actions, statuses, error codes, missing
required fields, and values of the wrong JSON type. The encoded message limit
is 8192 bytes.

Absolute time is never used to reject a message on the device. A receiver
starts the message's `ttl_ms` deadline from its monotonic receipt time. Pending
transactions are cleared on restart.

## Message matrix

| Type | Direction | Payload |
| --- | --- | --- |
| `capability_report` | both | Versions, firmware/build, limits, capabilities |
| `action_request` | device to phone | `food_search` or confirmed `start_navigation` |
| `action_result` | phone to device | Status, candidates/navigation state, error |
| `cancel` | both | Target request ID and optional reason |
| `state_snapshot` | device to phone | Complete current state; never an event replay |
| `protocol_error` | both | Stable error code and bounded message |

The only v1 actions are `food_search` and `start_navigation`. Food results
contain at most three candidates. A navigation request is invalid unless
`confirmation` is exactly `confirmed`.

## Deduplication and idempotency

- Keep at most 128 message IDs for 10 minutes using TTL/LRU eviction.
- Keep at most 32 completed or in-progress action results for 10 minutes.
- An exact message duplicate is rejected.
- A repeated action request with a new message ID but the same request ID
  returns the cached status/result and must not repeat an external side effect.

## Files

- `schema/fuji-protocol-v1.schema.json`: normative JSON Schema.
- `fixtures/manifest.json`: fixture names and expected validation outcomes.
- `fixtures/valid`: accepted golden messages.
- `fixtures/invalid`: rejected golden messages.
- `GATT.md`: frozen UUIDs and binary fragmentation format.

