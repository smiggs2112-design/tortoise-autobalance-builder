# Deterministic Voices v3 candidate (QNAP / Turtle WoW)

This candidate builds on \`voices-humanlike-v2\`. It does **not** modify TortoiseBots rotations, dungeon navigation, AutoBalance, character databases, models or existing NAS volumes. It does **not** update your live container automatically.

## Included in the compiled build

| Feature | Mechanism | Notes |
| --- | --- | --- |
| Common questions | Local C++ intent matcher | Answers without an Ollama request; unknown questions continue to Voices |
| Gold, equipment, health, mana, level | Read current character object | Replies describe the responding bot, never the human speaker |
| Bags | Read base inventory and equipped bag contents | By default reports total occupied slots; full individual item details remain in Voices and \`.bot inv\` |
| Quest progress | Read quest log / counters | Up to two current quests per answer, with objective counts |
| Spells / resurrection | Read learned spells | Reports known spells; **does not claim they are immediately castable** |
| Game mechanics & commands | Editable 4-column TSV | Initial 59 curated entries; 3,000 entry ceiling, no LLM usage |
| Question variants | Word and phrase aliases | Multiple phrasings for gold and common questions; ambiguous/unknown text falls back to LLM |
| Conversation reference | Per-player+bot last topic with 90-second TTL | Limited follow-ups such as "Is that a lot?" |
| Character individuality | Facts remain accurate; some response-template variation | Preserve the original Voices personalities for non-deterministic dialogue |
| Bot-to-bot local chatter | World-thread scheduler | Two bots, two lines, only near real players, no dungeons/combat |
| Natural-language commands | Existing TortoiseBots command handler | Whitelisted party/named instructions, never an arbitrary generated command |
| Role addressing | Conservative single-class-match tank/healer heuristic | Ambiguous roles are not executed; no alteration of TortoiseBots role configuration |
| Nearby non-party bots | Read-only local answers to addressed questions | No control over unowned bots; private inventory/quest/money info gated to group |
| Relationships / existing ambient chat | Existing Voices configuration options | Opt-in settings under \`profiles/\`, not silently applied to QNAP |

This is *not* an autonomous free-form tool-use agent. Dynamic spell casts, changing other bots' gear, entering dungeons on command, independent guild conversations or world-wide LLM chatter are intentionally outside this candidate.

## Simple test phrases

In your party:
- "How much gold you got?" and "Got lots of gold?" -> actual money
- "Are you rich?" -> actual money with limited response variation
- "Gwenne, what weapon have you got?" -> actual main/off-hand items
- "What quests are you doing?" -> active quest titles
- "How many do you still need?" -> quest objective counters (first two active quests)
- "Do you know any resurrection spells?" -> real known spell(s), no false claim of readiness
- "What does spirit do?" -> offline catalogue
- "Where do I learn mining?" -> offline catalogue
- "What next?" -> a currently active quest suggestion
- "Tank, pull my target" -> existing TortoiseBots pull handler **only if role resolution is unambiguous**
- "Healer, buff me" -> an existing targeted buff command, if exactly one eligible character is found
- "Can you all rebuff?" -> existing group command

For /say, address a bot by name or target it. Unaddressed questions near random bots do not automatically trigger every bot.

## Social pacing and CPU safety

- Social scan every **45 seconds**, capped to **12 real-player anchor locations**.
- Per-player social cooldown **240 seconds**; just one two-bot exchange per global scan.
- Max **8 delayed replies** in memory. Follow-up delivered after four seconds.
- Only two nearby alive, idle bot characters within **25 yards** of a real player.
- No deterministic ambient talk in dungeons, battlegrounds, or combat.
- No bot-to-bot LLM request for these lines, even if a large bot population is online.
- Conversation snippets refer to a shared quest only if both bots have that quest incomplete.
- No custom targeting, rotations, party grouping, or combat actions for ambient dialogue.

The \`AzerothVoices.BotReplyChance.*\` and \`AiPlayerbot.LLMBotToBotChatChance\` LLM gates remain controlled by your existing live config. To prevent overlapping LLM and deterministic bot chatter, review the suggested settings in \`profiles/voices-v3.conf.fragment\`.

## Extend the knowledge base without rebuilding

The bundled catalogue is installed at:

\`\`\`text
/opt/tortoise/data/humanlike-knowledge.tsv
\`\`\`

Each TSV record has **four columns**, separated by literal TAB characters:

\`\`\`text
unique-id<TAB>phrase one|phrase two<TAB>concise verified answer<TAB>source
\`\`\`

Mount a host copy of \`humanlike-knowledge.tsv\` over that exact file path **read-only** to edit it in QNAP Container Station. On a mounted-file update the module checks for changes every **60 seconds** and reloads the in-memory catalogue. No LLM call, model restart, or server rebuild is required for data changes.

The QNAP deployment should mount only the knowledge file, *not* replace the entire \`/opt/tortoise/data\` directory. Only use source-checked and version-compatible information; incorrect facts in a deterministic catalogue will be repeated confidently. Validation rejects malformed lines over 1,200 characters and caps the file at 3,000 records.

An example Compose addition (adapt to the existing \`mangosd\` service and NAS path):

\`\`\`yaml
services:
  mangosd:
    volumes:
      - /share/Containers/container-station-data/tortoise/knowledge/humanlike-knowledge.tsv:/opt/tortoise/data/humanlike-knowledge.tsv:ro
\`\`\`

**First copy the TSV to that exact NAS path** and give the container account read access. Do not apply this example unchanged if your actual QNAP volume structure differs.

## Live configuration (not baked into the image)

The builder cannot safely overwrite your existing \`aiplayerbot.conf\` or \`mod-azeroth-voices.conf\`, which are mounted from QNAP. Treat the files under \`profiles/\` as **overlays**, not standalone complete configuration files. Add/replace only matching keys.

Recommended first-run:
- Enable \`AzerothVoices.Sentiment.Enable=1\` only after reviewing LLM output; keep score changes bounded
- Reduce \`AzerothVoices.MaxResponders\` to 2, with speaker rotation and low follow-up chance
- Set bot-to-bot *LLM* reply chances to 0; preserve deterministic local chatter
- Keep unsolicited LLM random chatter off, but important dungeon/quest event commentary on
- Enable TortoiseBots greetings and ready-check rebuff, leaving bot-only random grouping off until performance is confirmed

The original local Ollama shared-network issue still requires the independent \`twow-ai\` Docker bridge from the earlier candidate. The binary alone does not change Compose networking.

## Acceptance tests before using the image

1. Verify GitHub Actions compilation succeeds and publishes an image digest and immutable \`voices-<commit>\` tag.
2. Launch candidate in a copy of your existing Compose environment, preserving a rollback image tag and backing up character databases before any change.
3. Ask five different gold phrasings; compare results to the character's actual money.
4. Compare equipment/bags/spells/quest progress against \`.bot inv\`, \`.bot command NAME quests\` and \`.bot command NAME spells\`.
5. Ask a question not covered by TSV; confirm it routes to Ollama rather than returning a guessed answer.
6. Test \`/say\`, \`/party\`, \`/whisper\` and a targeted passing bot. Ensure real players' messages are not lost.
7. Confirm no unsolicited chat when no human is nearby. Observe the 45-second/global and 240-second/local cooldowns.
8. In dungeons and raids, verify normal combat/AI, pullback, buffs, heals, dispels, line-of-sight navigation, wipes, commands and interaction permissions.
9. Monitor world tick times and Ollama requests with 800–1,200 bots; adjust population only if tick time is healthy.
10. Test TSV hot reload and revert to old image if problems appear.

## Known limitations and future work

- Only an initial 59-item curated offline catalogue, not an exhaustive Turtle WoW knowledge base.
- Phrase aliases work without an LLM, but unanticipated wording is not semantically understood unless the Ollama fallback interprets it.
- Quest progress responses list up to two quests, not guaranteed to find a named quest buried deeper in the log.
- Role addressing is based on an unambiguous conservative class heuristic, not TortoiseBots' configured role overrides.
- Command execution uses TortoiseBots' existing permission checks and status messages; the bridge does **not** independently verify post-cast success.
- Current spell knowledge checks learned abilities; cooldowns, mana and reagents are not yet individually validated for proposed casts.
- Deterministic dialogue uses bounded templates; it does not invent or retain detailed private relationship histories. The existing Sentiment and Memory features provide that separately.

Existing rollback tag:

\`\`\`text
ghcr.io/smiggs2112-design/tortoise-autobalance:voices-d6b941ebf4cdd0d0e8bba7a2929e0bd090f306e0
\`\`\`
