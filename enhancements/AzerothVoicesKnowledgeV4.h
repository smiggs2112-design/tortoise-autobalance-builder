#ifndef AZEROTH_VOICES_KNOWLEDGE_V4_H
#define AZEROTH_VOICES_KNOWLEDGE_V4_H
// Included inside IntelligenceV4, after its small live readers. No DB queries.

enum class EntityKind { Quest, Creature, Item, Spell, Object };
struct Entry { uint32_t id=0; std::string name,key; EntityKind kind=EntityKind::Quest; };
struct RecordedPoint
{
    int32_t entry=0; uint32_t map=0; float x=0,y=0;
};
struct LocatedPoint { bool found=false; RecordedPoint point; float distance=0; };
struct QuestStep
{
    uint32_t id=0; int score=0; bool complete=false, available=false;
    uint32_t shared=0, overlap=0, sharedTargets=0;
    std::string title, objective, giver;
    LocatedPoint location;
};
struct QuestAdvice { std::vector<QuestStep> steps; std::string summary, detail; };

struct Index
{
    bool built=false, spawnTruncated=false;
    std::vector<Entry> quests,creatures,items,spells,objects;
    std::map<uint32_t,std::vector<int32_t>> starters,finishers;
    std::map<int32_t,std::vector<uint32_t>> startsAt;
    std::map<int32_t,std::vector<RecordedPoint>> points;
    std::map<std::tuple<uint32_t,int,int>,std::vector<RecordedPoint>> giverCells;
    uint64_t lookups=0,hits=0,misses=0;
    size_t spawnVisits=0,pointCount=0;

    static void Add(std::vector<Entry>& entries,uint32_t id,std::string const& name,EntityKind kind)
    {
        if(name.empty() || name.size()>160 || entries.size()>=100000) return;
        entries.push_back({id,name,Normalise(name),kind});
    }
    template<class Bounds> void Relations(int32_t entry,Bounds bounds,bool starts)
    {
        uint32_t count=0;
        for(auto i=bounds.first;i!=bounds.second && count++<64;++i)
        {
            auto& byQuest=starts?starters[i->second]:finishers[i->second];
            if(byQuest.size()<32) byQuest.push_back(entry);
            if(starts && startsAt[entry].size()<64) startsAt[entry].push_back(i->second);
        }
    }
    void Point(int32_t entry,uint32_t map,float x,float y)
    {
        if(!entry || !std::isfinite(x) || !std::isfinite(y) || std::abs(x)>100000 || std::abs(y)>100000) return;
        if(pointCount>=600000) {spawnTruncated=true;return;}
        auto& list=points[entry];
        if(list.size()>=4096) {spawnTruncated=true;return;}
        RecordedPoint p{entry,map,x,y}; list.push_back(p);++pointCount;
        if(startsAt.count(entry))
        {
            auto& cell=giverCells[{map,static_cast<int>(std::floor(x/512)),static_cast<int>(std::floor(y/512))}];
            if(cell.size()<512) cell.push_back(p); else spawnTruncated=true;
        }
    }
    void Build()
    {
        if(built) return;
        built=true;
        if(sObjectMgr.GetQuestTemplates().size()<=100000)
            for(auto const& pair:sObjectMgr.GetQuestTemplates()) if(pair.second)
                Add(quests,pair.first,pair.second->GetTitle(),EntityKind::Quest);
        if(sObjectMgr.GetCreatureInfoMap().size()<=100000)
            for(auto const& pair:sObjectMgr.GetCreatureInfoMap()) if(pair.second)
            {
                Add(creatures,pair.first,pair.second->name,EntityKind::Creature);
                Relations(static_cast<int32_t>(pair.first),sObjectMgr.GetCreatureQuestRelationsMapBounds(pair.first),true);
                Relations(static_cast<int32_t>(pair.first),sObjectMgr.GetCreatureQuestInvolvedRelationsMapBounds(pair.first),false);
            }
        if(sObjectMgr.GetGameObjectInfoMap().size()<=100000)
            for(auto const& pair:sObjectMgr.GetGameObjectInfoMap())
            {
                Add(objects,pair.first,pair.second.name,EntityKind::Object);
                Relations(-static_cast<int32_t>(pair.first),sObjectMgr.GetGOQuestRelationsMapBounds(pair.first),true);
                Relations(-static_cast<int32_t>(pair.first),sObjectMgr.GetGOQuestInvolvedRelationsMapBounds(pair.first),false);
            }
        if(sObjectMgr.GetItemPrototypeMap().size()<=100000)
            for(auto const& pair:sObjectMgr.GetItemPrototypeMap()) Add(items,pair.first,pair.second.Name1,EntityKind::Item);
        // Merge ranks by English spell name. A caller can request an exact ID.
        if(sSpellMgr.GetMaxSpellId()<=300000)
        {
            std::map<std::pair<std::string,uint32_t>,uint32_t> highest;
            for(uint32_t id=1;id<sSpellMgr.GetMaxSpellId();++id)
                if(auto const* info=sSpellMgr.GetSpellEntry(id))
                {
                    if(info->SpellName[0].empty() || info->SpellName[0].size()>160) continue;
                    if(!sSpellMgr.GetSpellRank(id))
                    {Add(spells,id,info->SpellName[0],EntityKind::Spell);continue;}
                    auto& chosen=highest[{Normalise(info->SpellName[0]),info->SpellFamilyName}];
                    if(!chosen || sSpellMgr.GetSpellRank(id)>sSpellMgr.GetSpellRank(chosen) ||
                        (sSpellMgr.GetSpellRank(id)==sSpellMgr.GetSpellRank(chosen) && id>chosen)) chosen=id;
                }
            for(auto const& pair:highest) if(auto const* info=sSpellMgr.GetSpellEntry(pair.second))
                Add(spells,pair.second,info->SpellName[0],EntityKind::Spell);
        }
        struct CreatureCollector
        {
            Index& index;
            bool operator()(CreatureDataPair const& pair)
            {
                if(++index.spawnVisits>300000) {index.spawnTruncated=true;return true;}
                for(auto id:pair.second.creature_id) if(id)
                    index.Point(static_cast<int32_t>(id),pair.second.position.mapId,
                        pair.second.position.x,pair.second.position.y);
                return false;
            }
        } cc{*this};
        sObjectMgr.DoCreatureData(cc);
        struct ObjectCollector
        {
            Index& index; size_t visited=0;
            bool operator()(GameObjectDataPair const& pair)
            {
                if(++visited>300000) {index.spawnTruncated=true;return true;}
                index.Point(-static_cast<int32_t>(pair.second.id),pair.second.position.mapId,
                    pair.second.position.x,pair.second.position.y);
                return false;
            }
        } oc{*this};
        sObjectMgr.DoGOData(oc);
    }
    static uint32_t ExplicitId(std::string const& q,char const* type)
    {
        std::istringstream words(q); std::string token,next;
        while(words>>token)
            if(token==type && words>>next)
            {
                if(next.empty() || next.size()>9 || !std::all_of(next.begin(),next.end(),[](unsigned char c){return std::isdigit(c);})) continue;
                return static_cast<uint32_t>(std::stoul(next));
            }
        return 0;
    }
    static Entry const* Match(std::vector<Entry> const& entries,std::string const& q,bool& ambiguous)
    {
        Entry const* selected=nullptr; size_t length=0; ambiguous=false;
        for(auto const& e:entries)
        {
            if(e.key.size()<3 || !Contains(q,e.key)) continue;
            if(e.key.size()>length) {selected=&e;length=e.key.size();ambiguous=false;}
            else if(e.key.size()==length && selected && e.id!=selected->id) ambiguous=true;
        }
        return selected;
    }
    Entry const* NamedQuest(std::string const& q,bool& ambiguous)
    {
        Build(); auto id=ExplicitId(q,"quest");
        if(id) {for(auto const& e:quests) if(e.id==id) {ambiguous=false;return &e;} return nullptr;}
        auto* found=Match(quests,q,ambiguous);return ambiguous?nullptr:found;
    }
    std::string Label(int32_t entry) const
    {
        if(entry>0) {auto* c=sObjectMgr.GetCreatureTemplate(entry);if(c) return c->name;}
        if(entry<0) {auto* o=sObjectMgr.GetGameObjectInfo(static_cast<uint32_t>(-static_cast<int64_t>(entry)));if(o)return o->name;}
        return "an unidentified source";
    }
    LocatedPoint Nearest(Player* human,int32_t entry) const
    {
        LocatedPoint best;
        if(!human) return best;
        auto i=points.find(entry); if(i==points.end()) return best;
        for(auto const& p:i->second) if(p.map==human->GetMapId())
        {
            float d=std::hypot(p.x-human->GetPositionX(),p.y-human->GetPositionY());
            if(!best.found || d<best.distance) best={true,p,d};
        }
        return best;
    }
    LocatedPoint Nearest(Player* human,std::vector<int32_t> const& entries) const
    {
        LocatedPoint best;
        for(int32_t e:entries) {auto p=Nearest(human,e);if(p.found && (!best.found || p.distance<best.distance))best=p;}
        return best;
    }
    std::string Waypoint(LocatedPoint const& p) const
    {
        if(!p.found) return "No recorded position verified on your map.";
        std::ostringstream out;
        out<<"Recorded "<<Label(p.point.entry)<<" near world X "<<static_cast<int>(p.point.x)
           <<", Y "<<static_cast<int>(p.point.y)<<" (about "<<static_cast<uint32_t>(p.distance)
           <<" yd straight-line; path, instance and live presence unverified).";
        return out.str();
    }
    bool Search(std::string const& input,std::string& answer,Player* human=nullptr)
    {
        if(input.size()>220) return false;
        ++lookups; Build(); auto q=Normalise(input);
        struct Choice {Entry const* entry=nullptr;bool ambiguous=false;};
        Choice best;Entry direct;bool directId=false;
        if(auto id=ExplicitId(q,"spell"))
            if(auto* spell=sSpellMgr.GetSpellEntry(id))
            {direct={id,spell->SpellName[0],Normalise(spell->SpellName[0]),EntityKind::Spell};best.entry=&direct;directId=true;}
        bool explicitType=Any(q,{"quest","item","spell","npc","creature","mob","object","weapon","gear","drop","drops","sell","sells"});
        auto consider=[&](std::vector<Entry> const& list,char const* type,bool permitted)
        {
            if(!permitted || directId) return;
            auto id=ExplicitId(q,type); bool ambiguous=false;
            Entry const* e=nullptr;
            if(id) {for(auto const& item:list)if(item.id==id){e=&item;break;}}
            else e=Match(list,q,ambiguous);
            if(!e) return;
            if(!best.entry || e->key.size()>best.entry->key.size()) best={e,ambiguous};
            else if(e->key.size()==best.entry->key.size() &&
                (ambiguous || e->kind!=best.entry->kind || e->id!=best.entry->id)) best.ambiguous=true;
        };
        bool itemQuery=Any(q,{"item","weapon","gear","drop","drops","buy","sell","sells","where can i get"});
        consider(quests,"quest",!explicitType || Contains(q,"quest"));
        consider(items,"item",!explicitType || itemQuery);
        consider(spells,"spell",!explicitType || Contains(q,"spell"));
        consider(objects,"object",!explicitType || Contains(q,"object"));
        consider(creatures,"npc",!explicitType || Any(q,{"npc","creature","mob","who","drop","drops","sell","sells"}));
        if(best.ambiguous) {answer="That name matches several server records. Please specify quest, item, NPC, object or spell and its ID.";++hits;return true;}
        if(!best.entry) {++misses;return false;}
        auto const& e=*best.entry; std::ostringstream out;
        if(e.kind==EntityKind::Quest)
        {
            auto* quest=sObjectMgr.GetQuestTemplate(e.id);if(!quest)return false;
            out<<e.name<<" (quest "<<e.id<<"), level "<<quest->GetQuestLevel()<<", minimum "<<quest->GetMinLevel()<<". ";
            if(Any(q,{"start","pick up","giver","accept","who gives","where do i get"}))
            {if(starters.count(e.id))out<<"Starts at "<<Label(starters[e.id].front())<<". "<<Waypoint(Nearest(human,starters[e.id]));}
            else if(Any(q,{"hand in","turn in","finish","complete"}))
            {if(finishers.count(e.id))out<<"Turn-in: "<<Label(finishers[e.id].front())<<". "<<Waypoint(Nearest(human,finishers[e.id]));}
            else out<<SocialText(quest->GetObjectives(),300);
        }
        else if(e.kind==EntityKind::Spell)
        {
            auto* spell=sSpellMgr.GetSpellEntry(e.id);if(!spell)return false;
            out<<e.name<<" (spell "<<e.id<<", rank "<<static_cast<uint32_t>(sSpellMgr.GetSpellRank(e.id))
                <<"), spell level "<<spell->spellLevel<<", base resource cost "<<spell->manaCost
                <<", base cooldown "<<spell->RecoveryTime/1000<<" seconds. ";
            uint32_t count=0;for(uint32_t i=0;i<MAX_SPELL_REAGENTS && count<2;++i)
                if(spell->Reagent[i]>0 && spell->ReagentCount[i]) {out<<"Reagent: "<<ItemLabel(spell->Reagent[i])<<" x"<<spell->ReagentCount[i]<<". ";++count;}
            out<<"Template facts; this does not mean the companion knows or can cast it.";
        }
        else if(e.kind==EntityKind::Item)
        {
            auto* item=sObjectMgr.GetItemPrototype(e.id);if(!item)return false;
            out<<e.name<<" (item "<<e.id<<"), item level "<<item->ItemLevel<<", required level "<<item->RequiredLevel<<". ";
            if(itemQuery && Native().sources)
            {
                auto sources=Native().sources(e.id,0);
                if(!sources.drops.empty()) {out<<"Recorded loot sources: ";uint32_t n=0;for(int32_t source:sources.drops){if(n++)out<<", ";out<<Label(source);if(n==3)break;}out<<". ";}
                if(!sources.vendors.empty()) {out<<"Recorded vendors: ";uint32_t n=0;for(auto source:sources.vendors){if(n++)out<<", ";out<<Label(source);if(n==3)break;}out<<". ";}
                if(sources.drops.empty() && sources.vendors.empty())out<<"No source was resolved by the bot's loot/vendor indexes. ";
                out<<"Loot conditions, rates, stock and access are not guaranteed.";
            }
        }
        else if(e.kind==EntityKind::Creature)
        {
            auto* creature=sObjectMgr.GetCreatureTemplate(e.id);if(!creature)return false;
            out<<e.name<<" (NPC "<<e.id<<"), level "<<creature->level_min<<"-"<<creature->level_max<<". ";
            if(creature->npc_flags & UNIT_NPC_FLAG_QUESTGIVER)out<<"Quest giver. ";
            if(creature->npc_flags & UNIT_NPC_FLAG_VENDOR)out<<"Vendor. ";
            if(creature->npc_flags & UNIT_NPC_FLAG_TRAINER)out<<"Trainer. ";
            if(Any(q,{"drop","drops","loot"}) && Native().sources)
            {
                auto sources=Native().sources(0,static_cast<int32_t>(e.id));uint32_t count=0;
                out<<"Recorded loot includes ";for(auto id:sources.loot){if(count++)out<<", ";out<<ItemLabel(id);if(count==4)break;}
                if(!count)out<<"no resolved items";out<<"; conditions and drop rates vary. ";
            }
            if(Any(q,{"where","location","find","spawn"}))out<<Waypoint(Nearest(human,static_cast<int32_t>(e.id)));
        }
        else
        {
            auto* object=sObjectMgr.GetGameObjectInfo(e.id);if(!object)return false;
            out<<e.name<<" (object "<<e.id<<", type "<<object->type<<"). ";
            if(Any(q,{"where","location","find"}))out<<Waypoint(Nearest(human,-static_cast<int32_t>(e.id)));
        }
        answer=Limit(out.str(),1100);++hits;return true;
    }
};
inline Index& Knowledge() {static Index index;return index;}

inline std::string DescribeQuest(Player* owner,Quest const* quest)
{
    if(!quest || !owner || !HasInLog(owner,quest->GetQuestId()))return "That quest is not in the requested character's active log.";
    auto status=owner->GetQuestStatus(quest->GetQuestId());
    if(status==QUEST_STATUS_COMPLETE)return quest->GetTitle()+": ready to hand in.";
    if(status==QUEST_STATUS_FAILED)return quest->GetTitle()+": failed; check whether it can be retaken.";
    auto* p=owner->GetQuestStatusData(quest->GetQuestId());
    if(!p)return quest->GetTitle()+": progress counters unavailable.";
    std::ostringstream out;out<<quest->GetTitle()<<": ";uint32_t count=0;
    for(uint32_t i=0;i<QUEST_OBJECTIVES_COUNT;++i)if(quest->ReqCreatureOrGOCount[i])
    {if(count++)out<<"; ";out<<Knowledge().Label(quest->ReqCreatureOrGOId[i])<<" "<<p->m_creatureOrGOcount[i]<<"/"<<quest->ReqCreatureOrGOCount[i];}
    for(uint32_t i=0;i<QUEST_ITEM_OBJECTIVES_COUNT;++i)if(quest->ReqItemCount[i])
    {if(count++)out<<"; ";out<<ItemLabel(quest->ReqItemId[i])<<" "<<p->m_itemcount[i]<<"/"<<quest->ReqItemCount[i];}
    if(!count)out<<"No kill/item counters; check the remaining quest requirements.";
    return Limit(out.str(),900);
}

inline std::map<std::pair<char,int32_t>,uint32_t> UnfinishedTargets(Player* player,Quest const* quest)
{
    std::map<std::pair<char,int32_t>,uint32_t> targets;
    if(!player || !quest || player->GetQuestStatus(quest->GetQuestId())!=QUEST_STATUS_INCOMPLETE)return targets;
    auto* progress=player->GetQuestStatusData(quest->GetQuestId());if(!progress)return targets;
    for(uint32_t i=0;i<QUEST_OBJECTIVES_COUNT;++i)
        if(quest->ReqCreatureOrGOCount[i]>progress->m_creatureOrGOcount[i])
            targets[{'c',quest->ReqCreatureOrGOId[i]}]=quest->ReqCreatureOrGOCount[i]-progress->m_creatureOrGOcount[i];
    for(uint32_t i=0;i<QUEST_ITEM_OBJECTIVES_COUNT;++i)
        if(quest->ReqItemCount[i]>progress->m_itemcount[i])
            targets[{'i',static_cast<int32_t>(quest->ReqItemId[i])}]=quest->ReqItemCount[i]-progress->m_itemcount[i];
    return targets;
}
inline std::string QuestChain(Player* human,Quest const* quest)
{
    if(!quest)return "";
    std::ostringstream out;
    int32_t prev=quest->GetPrevQuestId();
    if(prev)
        if(auto* p=sObjectMgr.GetQuestTemplate(static_cast<uint32_t>(std::abs(static_cast<int64_t>(prev)))))
            out<<"Prerequisite: "<<p->GetTitle()<<(prev<0?" (must be active). ":". ");
    std::set<uint32_t> visited{quest->GetQuestId()};auto* current=quest;
    for(uint32_t count=0;count<5;++count)
    {
        uint32_t next=current->GetNextQuestInChain();
        if(!next && current->GetNextQuestId()>0)next=static_cast<uint32_t>(current->GetNextQuestId());
        if(!next || !visited.insert(next).second)break;
        current=sObjectMgr.GetQuestTemplate(next);if(!current)break;
        out<<"Then "<<current->GetTitle();
        if(human)out<<(human->CanTakeQuest(current,false)?" (quest rules currently pass)":" (not currently eligible)");
        out<<". ";
    }
    auto result=out.str();return result.empty()?"No direct quest-chain link is recorded.":Limit(result,700);
}
inline QuestAdvice PlanQuests(Player* human,Player* companion,bool availableOnly=false)
{
    QuestAdvice plan;
    if(!human || !companion || !human->GetGroup() || human->GetGroup()!=companion->GetGroup())return plan;
    auto& world=Knowledge();world.Build();
    std::vector<std::pair<Quest const*,std::map<std::pair<char,int32_t>,uint32_t>>> active;
    for(uint8_t slot=0;slot<MAX_QUEST_LOG_SIZE;++slot)
        if(auto* q=sObjectMgr.GetQuestTemplate(GetPlayerQuestSlotQuestId(human,slot)))
            if(human->GetQuestStatus(q->GetQuestId())==QUEST_STATUS_COMPLETE || human->GetQuestStatus(q->GetQuestId())==QUEST_STATUS_INCOMPLETE)
                active.push_back({q,UnfinishedTargets(human,q)});
    std::vector<Player*> party;uint32_t visited=0;
    for(GroupReference* ref=human->GetGroup()->GetFirstMember();ref && visited++<40;ref=ref->next())
    {auto* member=ref->getSource();if(member && member!=human && member->IsInWorld() && human->IsWithinDistInMap(member,100.0f) && party.size()<10)party.push_back(member);}
    if(party.size()<10 && human->IsWithinDistInMap(companion,100.0f) &&
        std::find(party.begin(),party.end(),companion)==party.end())party.push_back(companion);
    std::vector<std::set<std::pair<char,int32_t>>> partyTargets;
    for(auto* member:party)
    {
        std::set<std::pair<char,int32_t>> targets;
        for(uint8_t slot=0;slot<MAX_QUEST_LOG_SIZE;++slot)
            if(auto* q=sObjectMgr.GetQuestTemplate(GetPlayerQuestSlotQuestId(member,slot)))
                for(auto const& target:UnfinishedTargets(member,q))targets.insert(target.first);
        partyTargets.push_back(std::move(targets));
    }
    uint32_t lootBudget=8;
    if(!availableOnly)for(auto const& entry:active)
    {
        auto* q=entry.first;QuestStep step;step.id=q->GetQuestId();step.title=q->GetTitle();
        step.complete=human->GetQuestStatus(step.id)==QUEST_STATUS_COMPLETE;
        step.score=step.complete?10000:0;
        int delta=static_cast<int>(q->GetQuestLevel())-static_cast<int>(human->GetLevel());
        step.score+=std::max(-100,40-std::abs(delta)*6);
        if(delta>5)step.score-=100;
        if(q->GetZoneOrSort()>0 && static_cast<uint32_t>(q->GetZoneOrSort())==human->GetZoneId())step.score+=30;
        for(auto* member:party)if(HasInLog(member,step.id) && member->GetQuestStatus(step.id)==QUEST_STATUS_INCOMPLETE)++step.shared;
        step.score+=std::min(100,static_cast<int>(step.shared)*25);
        if(step.complete)
        {
            auto sources=world.finishers.find(step.id);
            if(sources!=world.finishers.end())
            {step.location=world.Nearest(human,sources->second);if(!sources->second.empty())step.giver=world.Label(step.location.found?step.location.point.entry:sources->second.front());}
            step.objective="ready to hand in";
        }
        else for(auto const& target:entry.second)
        {
            uint32_t overlap=0;
            for(auto const& other:active)if(other.first!=q && other.second.count(target.first))++overlap;
            step.overlap=std::max(step.overlap,overlap);
            uint32_t sharedTargets=0;for(auto const& targets:partyTargets)if(targets.count(target.first))++sharedTargets;
            step.sharedTargets=std::max(step.sharedTargets,sharedTargets);
            LocatedPoint location;
            std::string label;
            if(target.first.first=='c')
            {location=world.Nearest(human,target.first.second);label=world.Label(target.first.second);}
            else
            {
                label=ItemLabel(target.first.second);
                if(Native().sources && lootBudget)
                {--lootBudget;auto sources=Native().sources(target.first.second,0);location=world.Nearest(human,sources.drops);}
            }
            if(step.objective.empty() || (location.found && (!step.location.found || location.distance<step.location.distance)))
            {step.objective=std::to_string(target.second)+" more "+label;step.location=location;}
        }
        step.score+=std::min(100,static_cast<int>(step.overlap)*35);
        step.score+=std::min(80,static_cast<int>(step.sharedTargets)*20);
        if(step.location.found)step.score+=std::max(-30,70-static_cast<int>(std::min(step.location.distance,10000.0f)/50));
        plan.steps.push_back(std::move(step));
    }
    // Spatial discovery uses recorded giver locations, then the core's exact
    // character eligibility checks (race/class/reputation/prerequisites/etc).
    std::set<uint32_t> seen;
    int cx=static_cast<int>(std::floor(human->GetPositionX()/512)),cy=static_cast<int>(std::floor(human->GetPositionY()/512));
    uint32_t giverVisits=0,relationVisits=0;
    for(int dx=-4;dx<=4 && giverVisits<256 && relationVisits<1024;++dx)
    for(int dy=-4;dy<=4 && giverVisits<256 && relationVisits<1024;++dy)
    {
        auto cell=world.giverCells.find({human->GetMapId(),cx+dx,cy+dy});if(cell==world.giverCells.end())continue;
        for(auto const& p:cell->second)
        {
            if(++giverVisits>256 || relationVisits>=1024)break;
            float distance=std::hypot(p.x-human->GetPositionX(),p.y-human->GetPositionY());if(distance>2000)continue;
            for(auto id:world.startsAt[p.entry])
            {
                if(++relationVisits>1024)break;
                if(!seen.insert(id).second || HasInLog(human,id))continue;
                auto* q=sObjectMgr.GetQuestTemplate(id);
                if(!q || !human->CanTakeQuest(q,false) || !human->CanAddQuest(q,false))continue;
                if(q->GetQuestLevel()>human->GetLevel()+5)continue;
                QuestStep step;step.id=id;step.title=q->GetTitle();step.available=true;
                step.location={true,p,distance};step.giver=world.Label(p.entry);
                step.score=20-static_cast<int>(distance/50);step.objective="eligible by quest-log rules; giver conditions still apply";
                plan.steps.push_back(std::move(step));
            }
        }
    }
    std::stable_sort(plan.steps.begin(),plan.steps.end(),[](auto const& a,auto const& b){return a.score!=b.score?a.score>b.score:a.id<b.id;});
    if(plan.steps.size()>3)plan.steps.resize(3);
    if(plan.steps.size()==3 && !plan.steps[1].complete && !plan.steps[2].complete &&
        plan.steps[0].location.found && plan.steps[1].location.found && plan.steps[2].location.found)
    {
        auto const& first=plan.steps[0].location.point;
        auto distance=[&](QuestStep const& s){return std::hypot(s.location.point.x-first.x,s.location.point.y-first.y);};
        if(distance(plan.steps[2])+100<distance(plan.steps[1]))std::swap(plan.steps[1],plan.steps[2]);
    }
    if(plan.steps.empty())
    {plan.summary=availableOnly?"No eligible nearby quest was found within the recorded search budget.":"I couldn't resolve an active objective or an eligible nearby quest.";plan.detail=plan.summary;return plan;}
    std::ostringstream details;
    for(size_t n=0;n<plan.steps.size();++n)
    {
        auto const& step=plan.steps[n];details<<n+1<<". "<<(step.complete?"Turn in ":step.available?"Consider accepting ":"Work on ")<<step.title<<": "<<step.objective<<". ";
        if(step.shared)details<<step.shared<<" companion(s) have it incomplete. ";
        if(step.sharedTargets)details<<"Matching unfinished targets for "<<step.sharedTargets<<" nearby party member(s); each keeps their own credit requirements. ";
        if(step.overlap)details<<"An unfinished target overlaps "<<step.overlap<<" other active quest(s). ";
        if(!step.giver.empty())details<<"Giver: "<<step.giver<<". ";
        if(step.location.found)details<<world.Waypoint(step.location)<<" ";
        if(step.complete)details<<QuestChain(human,sObjectMgr.GetQuestTemplate(step.id))<<" ";
    }
    auto const& best=plan.steps.front();
    plan.summary=(best.complete?"Hand in ":best.available?"Consider ":"I'd work on ")+best.title+". "+best.objective+".";
    if(best.overlap)plan.summary+=" Its target overlaps "+std::to_string(best.overlap)+" other active quest(s).";
    if(best.available && !best.giver.empty())plan.summary+=" Recorded giver: "+best.giver+".";
    plan.summary=Limit(plan.summary,240);plan.detail=Limit(details.str(),1400);return plan;
}
inline bool QuestPlan(Player* human,Player* companion,std::string& answer,std::string const& specificName)
{
    if(!human || !companion || !human->GetGroup() || human->GetGroup()!=companion->GetGroup())return false;
    if(!specificName.empty())return QuestByName(human,Normalise(specificName),answer);
    answer=PlanQuests(human,companion).summary;return true;
}

struct InventoryFacts { std::string summary, detail; };
inline InventoryFacts Inventory(Player* bot,bool bags,std::string const& input)
{
    InventoryFacts facts;auto limits=Limits();uint32_t cap=std::min<uint32_t>(50,limits.maximumItems),count=0,total=0;
    std::string detail;auto q=Normalise(input);
    auto append=[&](std::string const& slot,Item* item)
    {
        if(!item || !item->GetProto())return;
        ++total;
        if(count>=cap)return;
        auto const& name=item->GetProto()->Name1;
        // A named-item question filters only real possessions, never generic templates.
        bool named=Any(q,{"do you have","have any","carrying any"});
        if(named && !Contains(q,Normalise(name)))return;
        std::string line=slot+": "+SocialText(name,64)+" x"+std::to_string(item->GetCount());
        if(detail.size()+line.size()+2>1600)return;
        if(count++)detail+="; ";
        detail+=line;
    };
    if(bags && limits.backpack)
    {
        for(uint8_t slot=INVENTORY_SLOT_ITEM_START;slot<INVENTORY_SLOT_ITEM_END;++slot)append("backpack",bot->GetItemByPos(INVENTORY_SLOT_BAG_0,slot));
        for(uint8_t bag=INVENTORY_SLOT_BAG_START;bag<INVENTORY_SLOT_BAG_END;++bag)
            if(auto* item=bot->GetItemByPos(INVENTORY_SLOT_BAG_0,bag))
            {
                auto* inventory=static_cast<Bag*>(item);
                for(uint32_t slot=0;slot<std::min<uint32_t>(inventory->GetBagSize(),64);++slot)
                    append("bag "+std::to_string(bag-INVENTORY_SLOT_BAG_START+1),bot->GetItemByPos(bag,static_cast<uint8_t>(slot)));
            }
    }
    else if(!bags && limits.equipment)
    {
        static char const* const names[]={"head","neck","shoulders","shirt","chest","waist","legs","feet","wrists","hands","ring 1","ring 2","trinket 1","trinket 2","back","main hand","off hand","ranged","tabard"};
        for(uint8_t slot=EQUIPMENT_SLOT_START;slot<EQUIPMENT_SLOT_END;++slot)append(names[slot],bot->GetItemByPos(INVENTORY_SLOT_BAG_0,slot));
    }
    facts.detail=detail;
    facts.summary=facts.detail.empty()?"No matching carried/equipped item was included; inventory details may be disabled.":Limit("My actual "+std::string(bags?"bag contents: ":"equipment: ")+facts.detail,240);
    if(count<total && !facts.detail.empty())facts.detail+="; list bounded ("+std::to_string(count)+" shown of "+std::to_string(total)+" occupied slots)";
    return facts;
}
#endif
