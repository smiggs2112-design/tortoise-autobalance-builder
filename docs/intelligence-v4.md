# Voices Intelligence v4 test candidate

This is the current guide for `voices-intelligence-v4`. The v2/v3 documents
describe earlier candidates. A successful image build establishes compile and
publication status only. Startup, in-game behaviour and QNAP performance must
still pass the acceptance checks below. The builder never connects to QNAP.

## Shared facts and intents

The knowledge/quest/social expansion adds:

- Server-loaded spell and game-object knowledge as well as quests/items/NPCs.
  Unique short names (such as Hogger) and typed IDs are supported; ambiguous
  names ask for an ID. Ranked spells are grouped within their spell family.
- Conditional loot sources and vendors from TortoiseBots' existing shared indexes.
  Voices introduces no loot SQL or private-layout casts. Native indexes may warm
  lazily on the first relevant query; measure this cost in the test environment.
- Real bag contents/stack counts and slot-labelled equipment in the shared capture,
  restoring the detail lost when the earlier bot snapshot was replaced.
- Ranked quest plans: completed turn-ins, shared unfinished targets across nearby
  party members, overlap across the human's active quests, level suitability and
  straight-line clustering. Each character retains individual credit requirements.
- Discovery of recorded nearby givers whose quests pass the core's character
  eligibility and quest-log/source-item capacity checks. This does not guarantee
  a friendly, present or currently usable giver in the player's instance.
- Direct prerequisites and up to five chain links, with cycle detection and current
  eligibility labels. Personal quest follow-ups retain both the quest ID and owner.
- Local social replies seeded from existing Voices personality traits, persistent
  relationship scores and verified event memories. No new memory SQL schema is added.
  Local conversations store canonical topics, not raw player chat.
- Three-turn exchanges with stable preferences, mild disagreement, repeat-topic
  recall and an invitation for the human to join. A human turn stops queued lines;
  each delayed turn rechecks both bots, the witness, combat, faction and instance.
- Thanks/compliments can adjust the existing persistent sentiment score by at most
  one per bot/player pair per minute, respecting Sentiment.Enable and the configured
  conversation delta limit. Ordinary LLM sentiment handling remains upstream-owned.

Examples: "Where can I get item Hunting Trophy?", "What does Hogger drop?",
"What is spell 133?", "What quests are nearby?", "Which quests have shared objectives?",
"What comes after quest 123?", "Do you remember me?", "What do you prefer?",
"What are you two discussing?" and "Can I join?". Names/IDs must exist in the
running server; example item/quest names are not promises of availability.

Persistent event memory continues to obey the existing Voices configuration and
generation chances. The local friendship/topic cache lasts at most six hours and
resets on module reload/restart; stored Voices records survive according to the
existing database settings. Module reload also invalidates the world index and
discards pending local dialogue. This is advisory conversation: no acceptance,
turn-in, movement, loot, trade, cast or group formation is performed.

Full target/action success verification, additional proactive care checks, a new
contextual-emote system, the companion addon and new open-world gameplay remain
outside this expansion. In-game regression and load tests are still required.

`AzerothVoicesIntentV4.h` owns normalisation, phrase boundaries, topic recognition,
follow-up topic handling and private-channel policy. `AzerothVoicesIntelligenceV4.h`
owns the live readers and `ReadState`. Both deterministic delivery and the Voices
request builder call that reader. The request builder formats the returned owned
value with `RequestContext` before handing the request to the existing worker
queue. Player, quest, spell and item pointers never enter the provider thread.

The same recognised request produces the same bounded verified answer in either
path: character facts, configured role, quest counts/planning, spell self-readiness
or server-loaded game knowledge. Unknown questions still reach Ollama with the
available verified facts. Ollama receives facts captured at request time, not a
promise that the state remains current when its answer arrives.

The local TSV mechanics catalogue and short-lived follow-up intent memory are
also shared by both answer paths. The catalogue is not
an exhaustive game database or an LLM instruction source. World knowledge comes
from the running server's loaded quest, item and creature templates. Duplicate
long names defer instead of choosing an arbitrary entry. The native adapter also reuses TortoiseBots' shared loot/vendor indexes, while
quest discovery uses the core's CanTakeQuest/CanAddQuest rules. Recorded sources
are conditional and incomplete where the native indexes cannot resolve references.
The service does not infer drop probabilities, walkable routes, live spawn presence,
target cast validity or completed actions.

### Ownership and privacy

- "What weapon have you got?" describes the responding bot.
- "What quests are you doing?" describes the bot's active log.
- "What are my quests?" and named player quest questions describe the human's log.
- "Where should we go next?" plans from the human's active quests, prioritising
  completed turn-ins, shared incomplete quests, level suitability and quest zone.
- Bot finances, bags, personal quest progress and learned spells require a shared group **and** party,
  raid or whisper delivery. Public `/say` withholds them even for group members.
- Equipment, role, level and health may be answered publicly when addressed.
- No answer or model output is interpreted as a game command.

Spell self-readiness checks the learned spell's highest rank, cooldown, base
resource cost and reagents. Target, range, percentage/modified costs, stance and
other cast restrictions remain unverified. The wording preserves this limitation.
Actual casting and action outcome remain TortoiseBots' responsibility.

### Bounds and scheduling

- Input: 220 bytes for fact queries; 160 bytes for explicit commands.
- Verified local answer: at most 240 bytes; shared request block: at most 2,400. Item detail respects existing include flags
  and MaximumItems (hard ceiling 50), with a 1,600-byte complete-item budget.
- Active quests: at most the core's fixed quest-log size; general log summaries
  show two quests and up to two counters per quest. Named quests and quest IDs search the log.
  Ranked plans contain at most three steps; discovery examines at most 256 recorded
  givers / 1,024 relations within 2,000 straight-line yards.
- Nearby party context: at most five members within 50 yards, capped to 260 bytes.
- Loaded-name index: at most 100,000 entries per category, names at most 160 bytes. Spells scan at most 300,000 IDs and group ranked
  names within a spell family, leaving ambiguous names unresolved.
  An oversized category is disabled as a whole to avoid hiding ambiguous names.
  The index is built lazily once; restart after template reloads.
- Spawn index: one lazy pass over at most 300,000 creature and 300,000 object
  records, capped to 600,000 stored points and 4,096 per entry. A matching record on
  the human's map yields world X/Y, never an assertion that the creature is alive,
  in this instance, or reachable. A budget miss withholds the location.
- Social scan: every 45 seconds; at most 12 human anchor locations; one exchange
  per global scan and a 240-second cooldown per human. Three lines per exchange,
  with replies at six and twelve seconds; at most eight delayed lines.
- Advisory party recovery prompts: at most one per scan, with a five-minute
  cooldown per human. They perform no healing, movement, rebuff or strategy change.
- Ambient exchanges require nearby same-team idle bots and a human witness;
  dungeons, battlegrounds and combat are excluded, including delayed delivery.
- Conversation subjects expire after 210 seconds; follow-up topics after 90.
  Local social pairs are capped to 512, bot friendships to 256, scenes to 128,
  and expire after six hours (scenes after 210 seconds). Persistent personality,
  sentiment and event memories reuse the existing Voices stores/configuration.
- `[VoicesV4]` counters are logged every ten minutes. They count local resolutions,
  fallback attempts, quest/spell queries, social/advisory activity and index hits;
  they are not world-tick or provider-latency measurements.

## Existing gameplay reused and preserved

TortoiseBots' `BotPlayerAdapter::GetBotRoles`, exposed through
`sScriptMgr.GetBotRoles`, honours forced roles and named combat strategies before
talent/spec detection. No class heuristic is substituted. Existing bot commands
retain owner/group/target permission checks. "Tank, pull my target" resolves a
single configured tank and forwards a named command; it does not silently expand
into the party-wide pull shortcut. A command being accepted or queued does not
establish action success; the existing handler reports its result.

Core and gameplay pins remain:

| Component | Revision |
| --- | --- |
| tortoise-deploy | `ab25e5888cda2664ea197073c181f787c4a59583` |
| tortoise-wow | `d9bbcd598a98a41fbf39cae5d630dd72b790d10f` |
| TortoiseBots | `3298d3e62b603182fe5e23709a333813c9bf8b7b` |
| AutoBalance | `9e492dfe8d8ed61e3aa91b06a609cb236de73a93` |
| Azeroth Voices | `b76ba4c10d44b41cac284699769ea789f735a567` |
| autoscale | `8675af22569674d906479aec0d7883d13d9212bb` |
| leech | `a3667c2c672b885fddc5afd76e48471dafa4eccc` |

Enhancements patch only Azeroth Voices. No combat rotation, dungeon/raid AI,
AutoBalance scaling code, bot role configuration or live database is edited.
The existing build omission of optional AutoBalance SQL is retained.
The companion addon and new open-world gameplay interactions are deferred.

## Build verification

CI runs catalogue checks, compiles and executes the actual C++ intent/privacy
policy and live-reader tests with a small fake core, applies the patch to the
pinned Voices checkout and checks fail-closed
repeat application, then compiles/links the complete Linux amd64 world image.
It publishes:

```text
ghcr.io/smiggs2112-design/tortoise-autobalance:voices-<full builder commit SHA>
```

Use this commit-specific tag for testing and record the published digest.
CI pulls the published digest and runs `mangosd --version` plus a packaged
catalogue check with networking disabled and a read-only container filesystem.
This verifies binary loading and packaging; it starts no world and tests no DB.
`voices-intelligence-candidate` is a moving convenience tag. The workflow also
attaches profiles, catalogue and documentation as `voices-intelligence-qnap`.

Local checks:

```text
python -m unittest discover -s tests -v
python -m py_compile enhancements/enhance_voices.py
```

The C++ test requires g++; the pinned patch test requires
`VOICES_SOURCE_TEST_ROOT` pointing to a clean checkout of the pinned Voices repo.
CI provides both. A local skipped test is not evidence that it passed.

## Configuration and staged testing

Keep existing complete configuration files and volumes. The fragments under
`profiles/` are suggestions to review and merge; they are not replacements.
Enable `AzerothVoices.Environment.Enable = 1` to send the shared block to Ollama.
The service replaces the older bot environment/item/snapshot block so the model
does not receive divergent duplicated spell/quest facts. Its bounds above apply
independently of the older `Environment.MaximumItems`/`Snapshot.Maximum*` knobs.
Non-player NPC context stays upstream-owned. Deterministic questions and social scheduling now respect the manager enable/pause
state. Pausing discards queued local social lines. No live config is changed by this build.

The existing Ollama bridge example in `deploy/tortoise-ollama-bridge.yml` remains
optional deployment work. The build alone neither creates a network nor changes
your existing endpoint. If testing the bridge, preserve the world's database/realm
networks and verify DNS/HTTP from inside the candidate container after recreation.

Before manually testing, record the currently deployed image **digest**, back up
configuration and databases, and create a separate test environment using copied
data and separate storage. Never run a candidate world against the live character
database while the live world is active. Do not change/remove NAS volumes.

## Remaining runtime acceptance checks

1. Start the published image in the isolated environment; check startup, module
   registration, existing DB compatibility and a clean shutdown/restart.
2. Compare gold, main/offhand, equipped gear, bags, level, health and mana with
   the actual responding character and existing `.bot inv` tools. Test "my" vs
   "your", punctuation and follow-ups; repeat across two humans and two bots.
3. Compare named quest counters against the human's log and the bot's separate
   log. Test completed/failed/absent quests, item objectives, another map and a
   completed turn-in competing with shared incomplete quests. No invented route.
4. Test forced tank/healer/DPS roles and multi-role ambiguity. Test learned spells
   of multiple ranks, cooldown, low power and missing reagents. Verify that a
   readiness answer neither casts a spell nor promises target/range validity.
5. Exercise `/say`, `/party`, `/raid` and `/whisper`: strangers and public channels
   must not expose private logs/resources. A human's ordinary chat remains visible.
   Named command permission denials remain enforced. Check that named tank pull
   does not order unrelated bots and that unsuccessful commands report honestly.
6. Ask an unknown factual question to exercise Ollama. Inspect debug request
   context to confirm the shared intent, actor identity and bounded live facts;
   verify they agree with the local answer. Test disabled environment context,
   unavailable Ollama, queue delay and rapid repeated queries.
7. Observe two-bot social exchanges, joining a discussion, TTL expiry and recovery
   advice. Move, change instance, enter combat or log out before delayed delivery.
   Verify no cross-team dialogue or ambient exchange in dungeons/battlegrounds.
8. Regress dungeon/raid combat, tank pulls/pullback, healer/dispels, buffs, interrupts,
   navigation, wipes/repop and existing command/owner permissions. Check AutoBalance
   health/damage/scaling against the existing pinned candidate.
9. Measure world tick latency, CPU/RAM, response pacing and Ollama queue/latency
   at the intended 800â€“1,200-bot population. Compare against the current deployment;
   the source bounds do not establish acceptable QNAP performance.
10. Verify read-only catalogue reload and candidate rollback. Record observations,
    image digest and config differences before considering promotion.

For rollback, stop the test candidate, select the previously recorded deployed
image digest and restore the matching config. Use backups if testing changed a
copied database. Do not assume an earlier chat's historical tag is your current
live rollback target. Production readiness requires recorded runtime results.
