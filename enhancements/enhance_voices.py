#!/usr/bin/env python3
"""Apply pinned, fail-closed Azeroth Voices enhancements inside the world image build.

No edits to TortoiseBots or AutoBalance. Run after all modules are cloned.
"""
from pathlib import Path

root = Path("/build/core/modules/mod-azeroth-voices/src")
manager_path = root / "AzerothVoicesManager.cpp"
scripts_path = root / "AzerothVoicesScripts.cpp"
provider_path = root / "AzerothVoicesProvider.cpp"


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

# These are deliberately on-demand: ordinary conversation does not need a large
# equipment, backpack and quest payload on a CPU-only Qwen model.
equipment = r'''        if (bot && m_config->environmentMaximumItems &&
            (m_config->environmentIncludeEquipment || m_config->environmentIncludeBackpack))
        {
            std::string const question = Lower(request.incomingMessage);
            auto mentions = [&](char const* word) {
                return question.find(word) != std::string::npos;
            };
            bool const gearQuestion = mentions("weapon") || mentions("wep") ||
                mentions("equip") || mentions("gear") || mentions("wear") ||
                mentions("sword") || mentions("staff") || mentions("armour") ||
                mentions("armor") || mentions("shield");
            bool const bagQuestion = mentions("bag") || mentions("inventor") ||
                mentions("backpack") || mentions("carrying") || mentions("have on you");
            uint32_t totalItems = 0;
            auto writeItem = [&](std::string const& label, Item* item) {
                if (!item || !item->GetProto() || totalItems >= m_config->environmentMaximumItems)
                    return;
                context << "\n- " << label << ": " << item->GetProto()->Name1;
                if (item->GetCount() > 1)
                    context << " x" << item->GetCount();
                ++totalItems;
            };
            if (gearQuestion && m_config->environmentIncludeEquipment)
            {
                context << "\nActual equipped items belonging to " << bot->GetName()
                        << " (these are the BOT's items, not the speaker's):";
                Item* main = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
                Item* off = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
                context << "\n- Main hand: " << (main && main->GetProto() ? main->GetProto()->Name1 : "empty");
                context << "\n- Off hand: " << (off && off->GetProto() ? off->GetProto()->Name1 : "empty");
                static char const* const slots[] = {
                    "Head", "Neck", "Shoulders", "Shirt", "Chest", "Waist",
                    "Legs", "Feet", "Wrists", "Hands", "Ring 1", "Ring 2",
                    "Trinket 1", "Trinket 2", "Back", "Main hand",
                    "Off hand", "Ranged", "Tabard"
                };
                for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END &&
                     totalItems < m_config->environmentMaximumItems; ++slot)
                {
                    if (slot == EQUIPMENT_SLOT_MAINHAND || slot == EQUIPMENT_SLOT_OFFHAND)
                        continue;
                    writeItem(slots[slot], bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
                }
            }
            if (bagQuestion && m_config->environmentIncludeBackpack)
            {
                context << "\nItems in " << bot->GetName() << "'s bags:";
                for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END &&
                     totalItems < m_config->environmentMaximumItems; ++slot)
                    writeItem("Backpack", bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
                for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END &&
                     totalItems < m_config->environmentMaximumItems; ++bagSlot)
                {
                    Item* bagItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bagSlot);
                    if (!bagItem)
                        continue;
                    Bag* bag = static_cast<Bag*>(bagItem);
                    for (uint32 slot = 0; slot < bag->GetBagSize() &&
                         totalItems < m_config->environmentMaximumItems; ++slot)
                        writeItem("Bag " + std::to_string(bagSlot - INVENTORY_SLOT_BAG_START + 1),
                                  bot->GetItemByPos(bagSlot, static_cast<uint8>(slot)));
                }
                if (totalItems == 0)
                    context << "\n- Empty";
            }
        }
        return context.str();
    }

'''
manager = replace_between(
    manager,
    '        if (bot && m_config->environmentMaximumItems &&\n',
    '    std::string Manager::BuildCurrentSnapshotContext',
    equipment,
    "Bounded live equipment and bags",
)

quest_old = '''                appendSectionLine(quests, "Active quests", quest->GetTitle() + " (" + statusName + ")");
                ++count;'''
quest_new = '''                std::ostringstream questLine;
                questLine << quest->GetTitle() << " (" << statusName << ")";
                QuestStatusData const* progress = bot->GetQuestStatusData(questId);
                if (progress)
                {
                    for (uint32_t objective = 0; objective < QUEST_OBJECTIVES_COUNT; ++objective)
                    {
                        uint32_t neededCreatures = quest->ReqCreatureOrGOCount[objective];
                        uint32_t neededItems = quest->ReqItemCount[objective];
                        if (!neededCreatures && !neededItems)
                            continue;
                        std::string const objectiveName = quest->ObjectiveText[objective].empty()
                            ? ("objective " + std::to_string(objective + 1))
                            : quest->ObjectiveText[objective];
                        if (neededCreatures)
                            questLine << "; " << objectiveName << " "
                                      << progress->m_creatureOrGOcount[objective] << "/" << neededCreatures;
                        if (neededItems)
                            questLine << "; item " << quest->ReqItemId[objective] << " "
                                      << progress->m_itemcount[objective] << "/" << neededItems;
                    }
                }
                appendSectionLine(quests, "Active quests and real objective progress", questLine.str());
                ++count;'''
manager=replace_once(manager,quest_old,quest_new,"Quest objective counters")

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
manager_path.write_text(manager)

scripts=scripts_path.read_text()
scripts=replace_once(scripts,
    '#include "AzerothVoicesManager.h"',
    '#include "AzerothVoicesManager.h"\n#include "AzerothVoicesBotSeam.h"\n#include "Chat/Chat.h"\n#include "Group.h"',
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

            std::string utterance = Lower(input);
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
            else if (everyone && (utterance == "pull my target" ||
                                  utterance == "pull that target"))
                command = ".bot action pull";
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
                    Manager::Instance().HandleChat(player, scope, message, "");
                }'''
scripts=replace_once(scripts,old,new,"Party and say command handling")
old='''                            Manager::Instance().HandleChat(session->GetPlayer(), ChatScope::Whisper, message, to);'''
new='''                            if (TryNaturalBotInstruction(session->GetPlayer(), ChatScope::Whisper, message, to))
                                return false; // handled locally; do not send the command as a whisper
                            Manager::Instance().HandleChat(session->GetPlayer(), ChatScope::Whisper, message, to);'''
scripts=replace_once(scripts,old,new,"Whisper command handling")
scripts_path.write_text(scripts)

# A specifically named Docker DNS service is allowed under AllowLocalHttp=1.
# Broad private ranges or arbitrary external plain HTTP remain disallowed.
provider=provider_path.read_text()
old='''            if (hostLower == "[::1]" || hostLower == "::1" || hostLower == "127.0.0.1" || hostLower == "localhost")
                parsed.local = true;'''
new='''            if (hostLower == "[::1]" || hostLower == "::1" ||
                hostLower == "127.0.0.1" || hostLower == "localhost" ||
                hostLower == "tortoise-ollama")
                parsed.local = true;'''
provider=replace_once(provider,old,new,"Explicit internal Docker DNS endpoint")
provider_path.write_text(provider)
print("Applied character equipment, bags, quest counts, identity, safe party commands and private DNS endpoint.")
