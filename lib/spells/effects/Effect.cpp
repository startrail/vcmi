/*
 * Effect.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "Effect.h"

#include "../../serializer/JsonSerializeFormat.h"

namespace spells
{
namespace effects
{

BattleStateProxy::BattleStateProxy(const PacketSender * server_)
	: server(server_),
	battleState(nullptr),
	describe(true)
{
}

BattleStateProxy::BattleStateProxy(IBattleState * battleState_)
	: server(nullptr),
	battleState(battleState_),
	describe(false)
{
}

void BattleStateProxy::complain(const std::string & problem) const
{
	if(server)
		server->complain(problem);
	else
		logGlobal->error(problem);
}


Effect::Effect(const int level)
	: automatic(true),
	optional(false),
	spellLevel(level)
{

}

Effect::~Effect() = default;

bool Effect::applicable(Problem & problem, const Mechanics * m) const
{
	return true;
}

bool Effect::applicable(Problem & problem, const Mechanics * m, const Target & aimPoint, const EffectTarget & target) const
{
	return true;
}

void Effect::apply(const PacketSender * server, RNG & rng, const Mechanics * m, const EffectTarget & target) const
{
	BattleStateProxy proxy(server);
	apply(&proxy, rng, m, target);
}

void Effect::apply(IBattleState * battleState, RNG & rng, const Mechanics * m, const EffectTarget & target) const
{
	BattleStateProxy proxy(battleState);
	apply(&proxy, rng, m, target);
}

void Effect::serializeJson(JsonSerializeFormat & handler)
{
	handler.serializeBool("automatic", automatic, true);
	handler.serializeBool("optional", optional, false);
	serializeJsonEffect(handler);
}

} // namespace effects
} // namespace spells
