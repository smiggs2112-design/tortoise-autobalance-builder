# Voices Intelligence v4 test candidate

This is the current guide for `voices-intelligence-v4`. The v2/v3 documents
describe earlier candidates. A successful image build establishes compile and
publication status only. Startup, in-game behaviour and QNAP performance must
still pass the acceptance checks below. The builder never connects to QNAP.

## Shared facts and intents

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
long names defer instead of choosing an arbitrary entry. This service cannot
infer drop tables, routes, unaccepted quests, target cast validity or completed
actions from those templates.

### Ownership and privacy

- "What weapon have you got?" describes the responding bot.
- "What quests are you doing?" describes the bot's active log.
- "What are my quests?" and named player quest questions describe the human's log.
- "Where should we go next?" plans from the human's active quests, prioritising
  completed turn-ins, shared incomplete quests, level suitability and quest zone.
- Bot finances, bags, quests and spells require a shared group **and** party,
  raid or whisper delivery. Public `/say` withholds them even for group members.
- Equipment, role, level and health may be answered publicly when addressed.
- No answer or model output is interpreted as a game command.

Spell self-readiness checks the learned spell's highest rank, cooldown, base
resource cost and reagents. Target, range, percentage/modified costs, stance and
other cast restrictions remain unverified. The wording preserves this limitation.
Actual casting and action outcome remain TortoiseBots' responsibility.

### Bounds and scheduling

- Input: 220 bytes for fact queries; 160 bytes for explicit commands.
- Verified local answer: at most 240 bytes; shared request block: at most 1,200.
- Active quests: at most the core's fixed quest-log size; general log summaries
  show two quests and up to two counters per quest. Named quests search the log.
- Nearby party context: at most five members within 50 yards, capped to 260 bytes.
- Loaded-name index: at most 50,000 entries per category, names at most 160 bytes.
  An oversized category is disabled as a whole to avoid hiding ambiguous names.
  The index is built lazily once; restart after template reloads.
- Spawn hint: at most 4,096 records inspected, on demand. A matching record on
  the human's map yields world X/Y, never an assertion that the creature is alive,
  in this instance, or reachable. A budget miss withholds the location.
- Social scan: every 45 seconds; at most 12 human anchor locations; one exchange
  per global scan and a 240-second cooldown per human. At most eight delayed lines.
- Advisory party recovery prompts: at most one per scan, with a five-minute
  cooldown per human. They perform no healing, movement, rebuff or strategy change.
- Ambient exchanges require nearby same-team idle bots and a human witness;
  dungeons, battlegrounds and combat are excluded, including delayed delivery.
- Conversation subjects expire after 210 seconds; follow-up topics after 90.
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
Non-player NPC context stays upstream-owned. Deterministic questions and social
scheduling currently run independently of the Ollama enable/pause setting.
Rollback the candidate if those behaviours are unwanted; no live config is changed
by this build.

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
