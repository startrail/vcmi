/*
 * Effect.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#pragma once

#include "../Magic.h"

struct BattleHex;
class CBattleInfoCallback;
class JsonSerializeFormat;
class IBattleState;

namespace vstd
{
	class RNG;
}

namespace spells
{
using EffectTarget = Target;

namespace effects
{
using RNG = ::vstd::RNG;
class Effects;
class Effect;
class Registry;

template<typename F>
class RegisterEffect;

using TargetType = ::spells::AimType;

class BattleStateProxy
{
public:
	const bool describe;

	BattleStateProxy(const PacketSender * server_);
	BattleStateProxy(IBattleState * battleState_);

	template<typename P>
	void apply(P * pack)
	{
		if(server)
			server->sendAndApply(pack);
		else
			pack->applyBattle(battleState);
	}

	void complain(const std::string & problem) const;
private:
	const PacketSender * server;
	IBattleState * battleState;
};

class Effect
{
public:
	bool indirect;
	bool optional;

	std::string name;

	Effect(const int level);
	virtual ~Effect();

	virtual void adjustTargetTypes(std::vector<TargetType> & types) const = 0;

	virtual void adjustAffectedHexes(std::set<BattleHex> & hexes, const Mechanics * m, const Target & spellTarget) const = 0;

	virtual bool applicable(Problem & problem, const Mechanics * m) const;
	virtual bool applicable(Problem & problem, const Mechanics * m, const Target & aimPoint, const EffectTarget & target) const;

	void apply(const PacketSender * server, RNG & rng, const Mechanics * m, const EffectTarget & target) const;
	void apply(IBattleState * battleState, RNG & rng, const Mechanics * m, const EffectTarget & target) const;

	virtual EffectTarget filterTarget(const Mechanics * m, const EffectTarget & target) const = 0;

	virtual EffectTarget transformTarget(const Mechanics * m, const Target & aimPoint, const Target & spellTarget) const = 0;

	void serializeJson(JsonSerializeFormat & handler);

protected:
	int spellLevel;

	virtual void apply(BattleStateProxy * battleState, RNG & rng, const Mechanics * m, const EffectTarget & target) const = 0;

	virtual void serializeJsonEffect(JsonSerializeFormat & handler) = 0;
};


} // namespace effects
} // namespace spells
