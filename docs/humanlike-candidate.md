# Tortoise WoW Humanlike Bots candidate

This is a **candidate**, not an automatic deployment. Existing QNAP world-server volumes and the currently running \`voices-d6b941...\` image are not changed.

## Scope and existing behaviour

The build retains the six pinned modules and revisions from \`.github/workflows/build-voices.yml\`. Enhancements are applied only to \`mod-azeroth-voices\` after it is cloned and before CMake compiles. TortoiseBots and AutoBalance sources are not modified.

### Implemented behaviours

- **Accurate character identity:** each bot is expressly identified as a separate character from the human speaker. The model is told not to repeat names, invent possessions, or claim it completed an action.
- **Equipment on demand:** when asked about weapon, gear or equipment, the module reports genuine equipped item names with slots, main hand and off hand (including empty slots). These are facts for the *responding bot*, not the requesting player.
- **Inventory on demand:** when asked about bags or carried items, the module can inspect the base backpack and the contents of equipped bags. The existing \`AzerothVoices.Environment.MaximumItems\` cap applies (maximum supported by upstream: 50).
- **Quest progress:** live snapshot quest titles now include actual kill and item counts rather than just incomplete/complete. Item objective IDs are reported when no reliable name is available.
- **Fast deterministic requests:** explicit named-bot or party-wide phrases in Party/Say/Whisper are mapped to *existing TortoiseBots commands*, using the human player's existing session and security permissions. No unrestricted LLM tool execution.
- **Stable private-network endpoint:** the server accepts specifically \`http://tortoise-ollama:11434\` with the existing \`AzerothVoices.AllowLocalHttp=1\` setting. All other non-loopback plain HTTP remains denied.

### Natural-language examples

- \`Gwenne, buff me\` -> \`.bot command Gwenne buff\`
- \`Can you all buff me?\` -> \`.bot action rebuff\`
- \`Wenderias, follow me\` -> \`.bot follow Wenderias\`
- \`Everyone stay here\` -> \`.bot action stay\`
- \`Everyone regroup\` -> \`.bot action come\`
- \`Everyone stop attacking\` -> \`.bot action stop\`
- \`Everyone attack my target\` -> \`.bot action attack\`
- \`Everyone pull my target\` -> \`.bot action pull\`
- \`Everyone interrupt my target\` -> \`.bot action interrupt\`

Named bots must be in the player's party; natural-language whispers are only intercepted when the recipient is a verified party bot. Ordinary chat is not treated as a command. Command checks and actual bot action results remain owned by TortoiseBots. In raid/dungeon fights, no custom AI rotates or pathfinding logic is replaced.

**Not implemented:** arbitrary free-form LLM-generated game commands; speech to uncontrolled or non-party bots; arbitrary trading, gear changes or teleports from natural language. These would need a separate authority/intent design and extensive testing.

## Azeroth Voices configuration

Ensure the **existing** \`mod-azeroth-voices.conf\` file includes these settings (do not replace your whole config with just this excerpt):

\`\`\`ini
AiPlayerbot.LLMApiEndpoint = http://tortoise-ollama:11434/v1/chat/completions
AiPlayerbot.LLMApiKey = ollama-local
AzerothVoices.AllowLocalHttp = 1
AzerothVoices.ProviderMode = ChatCompletions
AzerothVoices.Model = tortoise-friends-fast

AzerothVoices.Environment.Enable = 1
AzerothVoices.Environment.IncludeEquipment = 1
AzerothVoices.Environment.IncludeBackpack = 1
AzerothVoices.Environment.MaximumItems = 50
AzerothVoices.Environment.MaximumCreatures = 0
AzerothVoices.Snapshot.Enable = 1
AzerothVoices.Snapshot.IncludeQuests = 1
AzerothVoices.Snapshot.MaximumCharacters = 1200
AzerothVoices.History.MaximumCharacters = 700
AiPlayerbot.LLMContextLength = 3300

AzerothVoices.WorkerThreads = 1
AzerothVoices.MaxResponders = 3
\`\`\`

The 3,300 **character** cap governs contextual text, not Ollama's model context window. Even with on-demand context, large inventory/quest requests may approach the 2,048-token Qwen window; adjust after real-world benchmarking.

### Persistent Ollama networking

The old \`network_mode: container:tortoise-wow-mangosd\` causes the repeated outage when the world container is **recreated**. Use a separate external bridge:

\`\`\`bash
docker network create twow-ai
\`\`\`

The **world-server Compose file** must contain this extra network, alongside whatever networks it already uses for the database/realm server:

\`\`\`yaml
services:
  mangosd:
    networks:
      # KEEP all existing network connections here too.
      - your-existing-world-network
      - twow-ai

networks:
  twow-ai:
    external: true
\`\`\`

Replace \`your-existing-world-network\` with the current network from your QNAP Compose. **Do not delete its existing networks**, or it can lose MySQL/realmd connectivity. Service name may differ. See \`deploy/tortoise-ollama-bridge.yml\` for the independent Ollama app. Its container has the explicit DNS name \`tortoise-ollama\` and publishes *no host port*. Start the network and both apps, then test \`curl http://tortoise-ollama:11434/api/tags\` **from the world container**.

This networking setup is a QNAP deployment change and **is not applied merely by rebuilding the image**. Keep the current localhost endpoint until you have connected both apps to the same bridge.

## Verification before promotion

1. GitHub Actions image build and source patch complete without any warnings.
2. Existing world server starts with unchanged TortoiseBots/AutoBalance, existing character DB and quest data.
3. Ask Gwenne and Wenderias about their own equipped weapons and bag contents, verify with \`.bot inv <name>\`.
4. Ask about each bot's active quests and compare with quest log counters.
5. Confirm \`Gwenne, buff me\` invokes *only* Gwenne; \`Can you all buff me?\` invokes party rebuff.
6. Confirm no unintended action from ordinary questions such as \`Could everyone help with this quest?\`.
7. Test party follow, dungeon pullback/interrupt and raid routines, and ensure no interruption of rotations or AI routines.
8. Recreate \`mangosd\` and prove Ollama still connects on the shared bridge.
9. Compare four-thread Ollama latency and concurrent bot queueing against the current image.

Rollback: reselect the existing QNAP \`voices-d6b941ebf4cdd0d0e8bba7a2929e0bd090f306e0\` image and restore the previous endpoint if not yet using the shared bridge. Do not remove database/model volumes.
