#!/usr/bin/env python3
"""Apply pinned, fail-closed Azeroth Voices enhancements inside the world image build.

No edits to TortoiseBots or AutoBalance. Run after all modules are cloned.
"""
import argparse
from pathlib import Path
from shutil import copyfile

parser = argparse.ArgumentParser()
parser.add_argument("--source", type=Path, default=Path("/build/core/modules/mod-azeroth-voices/src"))
parser.add_argument("--assets", type=Path, default=Path("/build/enhancements"))
parser.add_argument("--knowledge-target", type=Path, default=Path("/opt/tortoise/data/humanlike-knowledge.tsv"))
args = parser.parse_args()
root = args.source
manager_path = root / "AzerothVoicesManager.cpp"
scripts_path = root / "AzerothVoicesScripts.cpp"
provider_path = root / "AzerothVoicesProvider.cpp"
copyfile(args.assets / "AzerothVoicesDeterministic.h",
         root / "AzerothVoicesDeterministic.h")
copyfile(args.assets / "AzerothVoicesIntelligenceV4.h",
         root / "AzerothVoicesIntelligenceV4.h")
copyfile(args.assets / "AzerothVoicesIntentV4.h", root / "AzerothVoicesIntentV4.h")
for name in ("AzerothVoicesSocialV4.h", "AzerothVoicesNativeV4.h", "AzerothVoicesKnowledgeV4.h",
             "AzerothVoicesNativeV4.cpp", "AzerothVoicesNativeManagerV4.inc"):
    copyfile(args.assets / name, root / name)
knowledge_target = args.knowledge_target
knowledge_target.parent.mkdir(parents=True, exist_ok=True)
copyfile(args.assets / "humanlike-knowledge.tsv", knowledge_target)


def replace_once(value, before, after, label):
    count = value.count(before)
    if count != 1:
        raise RuntimeError(f"{label}: expected one anchor, found {count}")
    return value.replace(before, after, 1)


def replace_between(value, start, finish, replacement, label):
    if value.count(start) != 1:
        raise RuntimeError(f"{label}: start anchor count {value.count(start)}")
    index = value.index(start)
    end = value.index(finish, index)
    return value[:index] + replacement + value[end:]


manager = manager_path.read_text()
manager = replace_once(manager, '#include "AzerothVoicesManager.h"',
                       '#include "AzerothVoicesManager.h"\n#include "Bag.h"',
                       "Include Bag for full inventory inspection")

# Shared state replaces duplicated bot item/quest/spell readers. Non-bot NPC
# context remains upstream-owned. This executes on the world thread, before queueing.
manager = replace_once(manager, '#include "Bag.h"',
    '#include "Bag.h"\n#include "AzerothVoicesIntelligenceV4.h"', "Shared state dependency")
anchor = '    std::string Manager::BuildEnvironmentContext(ChatRequest const& request) const\n    {\n'
manager = replace_once(manager, anchor, anchor + r'''        if (request.actor.kind == ActorKind::PlayerBot)
        {
            if (!m_config->environmentContextEnabled) return "";
            Player* bot = ObjectAccessor::FindPlayer(ObjectGuid(request.actor.guid));
            Player* human = ObjectAccessor::FindPlayer(ObjectGuid(request.speaker.guid));
            bool const privateChannel = request.scope == ChatScope::Party ||
                request.scope == ChatScope::Raid || request.scope == ChatScope::Whisper;
            return IntelligenceV4::RequestContext(IntelligenceV4::ReadState(
                human, bot, request.incomingMessage, privateChannel));
        }
''', "Shared Ollama state")
anchor = '    std::string Manager::BuildCurrentSnapshotContext(ChatRequest const& request) const\n    {\n'
manager = replace_once(manager, anchor, anchor +
    '        if (request.actor.kind == ActorKind::PlayerBot) return ""; // shared capture above\n',
    "Remove divergent bot snapshots")

identity_old = '''        if (!rolePrompt.empty())
            request.systemPrompt += "\\n" + rolePrompt;
        if (actor.kind == ActorKind::Creature)'''
identity_new = '''        if (!rolePrompt.empty())
            request.systemPrompt += "\\n" + rolePrompt;
        if (actor.kind == ActorKind::PlayerBot)
        {
            request.systemPrompt += "\\nIDENTITY: YOU are " + actor.name +
                ", a separate companion character. The player speaking to you is " +
                speaker.name + ". You are NEVER that player. Your equipment, quests, "
                "inventory and abilities belong to you, not the player. Use the "
                "provided live character facts when relevant. Never invent equipment, "
                "abilities, actions performed or quest progress. If the requested "
                "fact was not supplied, say you cannot check it. Speak naturally "
                "in first person. Do not prefix your response with your name, "
                "repeat the player's name unnecessarily, or ask generic follow-up "
                "questions instead of answering directly.";
        }
        if (actor.kind == ActorKind::Creature)'''
manager=replace_once(manager,identity_old,identity_new,"Per-actor identity")

# Azeroth Voices' context allocator trims older chat/RAG before the current
# environment, so current facts remain high priority. Ensure live snapshots
# prefer quests to spells inside a short context budget.
manager=replace_once(manager,
    '        appendBlock(keptHistory);\n        appendBlock(keptSurrounding);\n        appendBlock(keptSnapshotHistory);\n        appendBlock(keptRag);\n        appendBlock(keptCurrent);\n        appendBlock(keptSnapshot);',
    '        appendBlock(keptHistory);\n        appendBlock(keptSurrounding);\n        appendBlock(keptSnapshotHistory);\n        appendBlock(keptRag);\n        appendBlock(keptCurrent);\n        appendBlock(keptSnapshot);',
    "Context ordering invariant")
manager += '\n#include "AzerothVoicesNativeManagerV4.inc"\n'
manager = replace_once(manager,
    '                        ApplyDeliveredSentiment(line.request);',
    '                        ApplyDeliveredSentiment(line.request);\n'
    '                        if (line.request.actor.kind == ActorKind::PlayerBot && !line.request.speaker.isBot)\n'
    '                        {\n'
    '                            Player* bot = ObjectAccessor::FindPlayer(ObjectGuid(line.request.actor.guid));\n'
    '                            Player* human = ObjectAccessor::FindPlayer(ObjectGuid(line.request.speaker.guid));\n'
    '                            IntelligenceV4::RecordInteraction(human,bot,line.request.incomingMessage,\n'
    '                                IntelligenceV4::Recognise(IntelligenceV4::Normalise(line.request.incomingMessage),line.request.incomingMessage),false);\n'
    '                        }', "Observe only delivered model conversations")
manager_path.write_text(manager)
header_path = root / "AzerothVoicesManager.h"
header = header_path.read_text()
header = replace_once(header, '#include "AzerothVoicesAddon.h"',
    '#include "AzerothVoicesAddon.h"\n#include "AzerothVoicesNativeV4.h"', "Shared native interface")
header = replace_once(header, '        static Manager& Instance();',
    '        static Manager& Instance();\n'
    '        IntelligenceV4::ReaderLimits SharedReaderLimits() const;\n'
    '        IntelligenceV4::SocialSeed SharedSocialSeed(Player* bot, Player* human);\n'
    '        void SharedSocialDelivered(Player* bot, Player* human, int delta);', "Expose bounded native reads")
header_path.write_text(header)

scripts=scripts_path.read_text()
scripts=replace_once(scripts,'                Manager::Instance().Start();',
    '                Manager::Instance().Start();\n                IntelligenceV4::InstallNativeBridge();',
    "Install native shared-data adapters")
scripts=replace_once(scripts,
    '                if (reload)\n                    Manager::Instance().Reload();',
    '                if (reload)\n                {\n'
    '                    Manager::Instance().Reload();\n'
    '                    IntelligenceV4::InstallNativeBridge();\n'
    '                    Deterministic::Instance().pending.clear();\n                }',
    "Refresh shared indexes and local social state on module reload")
scripts=replace_once(scripts,
    '#include "AzerothVoicesManager.h"',
    '#include "AzerothVoicesManager.h"\n#include "AzerothVoicesBotSeam.h"\n#include "AzerothVoicesDeterministic.h"\n#include "Chat/Chat.h"\n#include "Group.h"',
    "Safe command dependencies")

# No generated LLM text is executed as a command. This is an explicit allowlist
# of intent phrases translated to TortoiseBots' existing permission-checked
# command interface. Never issue a GM or economy/gear-modifying command here.
command_code=r'''
        // Translate *explicit* party / named-bot requests without waiting for
        // Ollama. Never infer a command from ambient banter or a substring.
        bool TryNaturalBotInstruction(Player* player, ChatScope scope,
                                      std::string const& input,
                                      std::string const& whisperTarget = "")
        {
            if (!player || !player->GetSession() || Script_IsAIControlled(player) ||
                input.empty() || input.size() > 160)
                return false;
            if (scope != ChatScope::Say && scope != ChatScope::Party &&
                scope != ChatScope::Raid && scope != ChatScope::Whisper)
                return false;

            std::string utterance = IntelligenceV4::Normalise(input);
            // Strip polite punctuation without changing the intent.
            for (char& c : utterance)
                if (c == '?' || c == '!' || c == ',' || c == '.' || c == ':')
                    c = ' ';
            auto trim = [](std::string& s) {
                size_t begin = s.find_first_not_of(' ');
                if (begin == std::string::npos) { s.clear(); return; }
                size_t end = s.find_last_not_of(' ');
                s = s.substr(begin, end - begin + 1);
            };
            trim(utterance);
            auto consume = [&](std::string const& prefix) {
                if (utterance == prefix)
                {
                    utterance.clear();
                    return true;
                }
                if (utterance.size() > prefix.size() &&
                    utterance.compare(0, prefix.size(), prefix) == 0 &&
                    utterance[prefix.size()] == ' ')
                {
                    utterance.erase(0, prefix.size() + 1);
                    trim(utterance);
                    return true;
                }
                return false;
            };
            bool everyone = false;
            std::string botName;
            bool addressingTank = false;
            bool addressingHealer = false;
            if (scope == ChatScope::Whisper)
            {
                // Only translate whispers to verified party bots. Other
                // whispers pass through as normal human conversation.
                bool foundBot = false;
                if (player->GetGroup())
                {
                    for (GroupReference* ref = player->GetGroup()->GetFirstMember(); ref; ref = ref->next())
                    {
                        Player* member = ref->getSource();
                        if (member && Script_IsAIControlled(member) &&
                            Lower(member->GetName()) == Lower(whisperTarget))
                        {
                            botName = member->GetName();
                            foundBot = true;
                            break;
                        }
                    }
                }
                if (!foundBot)
                    return false;
            }
            else
            {
                for (char const* groupPrefix : {"can you all", "could you all", "would you all",
                                                "can everyone", "could everyone", "hey everyone",
                                                "all of you", "everyone", "everybody", "all",
                                                "party", "guys", "team", "hey mates"})
                {
                    if (consume(groupPrefix)) { everyone = true; break; }
                }
                if (!everyone && player->GetGroup())
                {
                    addressingTank = consume("tank") || consume("hey tank");
                    addressingHealer = !addressingTank &&
                        (consume("healer") || consume("hey healer"));
                    if (addressingTank || addressingHealer)
                    {
                        Player* single = nullptr;
                        for (GroupReference* ref = player->GetGroup()->GetFirstMember(); ref; ref = ref->next())
                        {
                            Player* member = ref->getSource();
                            if (!member || member == player || !Script_IsAIControlled(member))
                                continue;
                            // Reuse the active TortoiseBots adapter, which
                            // honours forced roles, actual combat strategies
                            // and spec detection. Class alone is misleading.
                            uint8_t roles = sScriptMgr.GetBotRoles(member);
                            bool candidate = addressingTank ?
                                (roles & 0x01) != 0 : (roles & 0x02) != 0;
                            if (!candidate)
                                continue;
                            if (single)
                                return false; // ambiguous role: do not guess
                            single = member;
                        }
                        if (!single)
                            return false;
                        botName = single->GetName();
                    }
                    else
                    {
                        for (GroupReference* ref = player->GetGroup()->GetFirstMember(); ref; ref = ref->next())
                        {
                            Player* member = ref->getSource();
                            if (member && member != player && Script_IsAIControlled(member))
                            {
                                std::string candidate = Lower(member->GetName());
                                if (consume(candidate) || consume("hey " + candidate))
                                {
                                    botName = member->GetName();
                                    break;
                                }
                            }
                        }
                    }
                }
                // Requiring a direct address prevents unintended gameplay actions.
                if (!everyone && botName.empty())
                    return false;
            }
            for (char const* polite : {"please", "could you", "can you", "would you", "will you"})
                consume(polite);
            for (char const* polite : {"please", "just"})
                consume(polite);
            trim(utterance);

            std::string command;
            if (utterance == "buff me" || utterance == "buff us" ||
                utterance == "buff everyone" || utterance == "buff the party" ||
                utterance == "buff up" || utterance == "rebuff" ||
                utterance == "rebuff us" || utterance == "buff")
                command = everyone ? ".bot action rebuff" : ".bot command " + botName + " buff";
            else if (utterance == "follow me" || utterance == "follow" ||
                     utterance == "come with me")
                command = everyone ? ".bot action follow" : ".bot follow " + botName;
            else if (utterance == "stay here" || utterance == "stay" ||
                     utterance == "wait here" || utterance == "hold position")
                command = everyone ? ".bot action stay" : ".bot stay " + botName;
            else if (everyone && (utterance == "come here" || utterance == "regroup" ||
                                  utterance == "come to me"))
                command = ".bot action come";
            else if (everyone && (utterance == "stop attacking" || utterance == "stop fighting"))
                command = ".bot action stop";
            else if (everyone && (utterance == "attack my target" ||
                                  utterance == "attack that target"))
                command = ".bot action attack";
            else if ((everyone || addressingTank) &&
                     (utterance == "pull my target" || utterance == "pull that target"))
                command = everyone ? ".bot action pull" : ".bot command " + botName + " pull my target";
            else if (everyone && (utterance == "interrupt that" ||
                                  utterance == "interrupt my target"))
                command = ".bot action interrupt";
            if (command.empty())
                return false;

            // All actions use the original human session. TortoiseBots enforces
            // its normal owner/group/GM and target permission gates.
            ChatHandler handler(player->GetSession());
            return handler.ParseCommands(command.c_str());
        }

'''
anchor='''        class AzerothVoicesWorldScript final : public WorldScript'''
scripts=replace_once(scripts,anchor,command_code+anchor,"Insert safe natural-language bridge")
scripts=replace_once(scripts,
    "                Manager::Instance().Update(diff);",
    "                Manager::Instance().Update(diff);\n                Deterministic::Instance().Tick(diff);",
    "Schedule offline bot social conversations")
old='''                ChatScope scope;
                if (ToScope(type, scope))
                    Manager::Instance().HandleChat(player, scope, message, "");'''
new='''                ChatScope scope;
                if (ToScope(type, scope))
                {
                    if (TryNaturalBotInstruction(player, scope, message))
                    {
                        message.clear();
                        return;
                    }
                    if (Deterministic::Instance().TryQuestion(player, scope, message))
                        return; // keep human chat visible and skip Ollama
                    Manager::Instance().HandleChat(player, scope, message, "");
                }'''
scripts=replace_once(scripts,old,new,"Party and say command handling")
old='''                            Manager::Instance().HandleChat(session->GetPlayer(), ChatScope::Whisper, message, to);'''
new='''                            if (TryNaturalBotInstruction(session->GetPlayer(), ChatScope::Whisper, message, to))
                                return false; // handled locally; do not send the command as a whisper
                            if (Deterministic::Instance().TryQuestion(session->GetPlayer(),
                                                                          ChatScope::Whisper, message, to))
                                return false; // answer locally, skip Ollama
                            Manager::Instance().HandleChat(session->GetPlayer(), ChatScope::Whisper, message, to);'''
scripts=replace_once(scripts,old,new,"Whisper command handling")
scripts_path.write_text(scripts)

# A specifically named Docker DNS service is allowed under AllowLocalHttp=1.
# Broad private ranges or arbitrary external plain HTTP remain disallowed.
provider=provider_path.read_text()
old='''                parsed.local = hostLower == "localhost" || hostLower == "127.0.0.1" || hostLower == "::1";'''
new='''                parsed.local = hostLower == "localhost" || hostLower == "127.0.0.1" ||
                               hostLower == "::1" || hostLower == "tortoise-ollama";'''
provider=replace_once(provider,old,new,"Explicit internal Docker DNS endpoint")
provider_path.write_text(provider)
print("Applied expanded world knowledge, quest advice and shared social memory; gameplay engines unchanged.")
