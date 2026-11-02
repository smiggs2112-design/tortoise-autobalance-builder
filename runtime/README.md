# Voices candidate: runtime LLM configuration

The voices-build branch adds pinned Azeroth Voices to the existing AutoBalance build. The original workflow and v1 image tag remain available.

Candidate image after a successful build:
ghcr.io/smiggs2112-design/tortoise-autobalance:voices-candidate

Keep the existing QNAP bot, AutoBalance, autoscale and leech configurations. Do not replace them with upstream defaults.

## Change LLMs without rebuilding

Mount runtime/mod-azeroth-voices.conf into:
 /opt/tortoise/config/modules/mod-azeroth-voices.conf

Edit these values in the NAS copy:
- AiPlayerbot.LLMApiEndpoint: full endpoint URL, including /v1/chat/completions for an OpenAI-compatible server.
- AzerothVoices.Model: the model name or alias exposed by that server.
- AiPlayerbot.LLMApiKey: its credential, or a harmless local placeholder when the local server ignores it.

This uses an HTTP API; the model weights are not in the WoW image. Ollama, LM Studio and other servers must expose the compatible API. Not every arbitrary model/provider protocol is interchangeable.

After editing, use the authorised in-game .reload config command, then check .av status and .av test. The module's config-load hook reloads its settings. If needed, restart only mangosd. A container restart is not an image rebuild. .av restart alone does not reread the core config files.

## Endpoint networking

Pinned stock Voices permits plain HTTP only to localhost, 127.0.0.1 or ::1, with AzerothVoices.AllowLocalHttp enabled. A Docker service name or a NAS/LAN address is not loopback. For a separate container, the prepared Compose uses network_mode: service:mangosd and binds Ollama to 127.0.0.1:11434. For another machine, use a trusted HTTPS endpoint/proxy. A direct plain-HTTP LAN endpoint requires a separate, explicitly scoped source change; it is not enabled in this build. Do not disable certificate verification.

Keep AiPlayerbot.LLMEnabled = 0 in the existing bot config so Voices is the dialogue owner.

## Persistence and scope

Personalities, bounded history and verified gameplay memories are stored in the existing characters database once the module migrations succeed. Save a database backup before the first test. Configuration enables direct whisper/say/party conversations and starts with automatic chatter off. It contains no gameplay-action bridge and no custom ambient presets.

Stock in-game controls: .av status, .av pause, .av resume. Pause affects direct dialogue too; queued replies can still finish. The requested ambient-only runtime presets need a subsequent source change and are not claimed implemented.

The new module ships character SQL migrations. The build verifies their installation paths; test the first startup on copied data before using the live server. Confirm personality/history/memory storage is using the database, not RAM fallback.

A successful image build establishes compilation and packaging, not in-game compatibility or acceptable NAS load.
