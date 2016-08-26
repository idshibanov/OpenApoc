#pragma once

#include "game/state/stateobject.h"
#include "library/strings.h"
#include <map>

namespace OpenApoc
{

class Vehicle;
template <typename T> class StateObject;

class Organisation : public StateObject<Organisation>
{
  public:
	enum class Relation
	{
		Allied,
		Friendly,
		Neutral,
		Unfriendly,
		Hostile
	};
	UString name;
	int balance;
	int income;

	Organisation(const UString &name = "", int balance = 0, int income = 0);
	Relation isRelatedTo(const StateRef<Organisation> &other) const;
	bool isPositiveTo(const StateRef<Organisation> &other) const;
	bool isNegativeTo(const StateRef<Organisation> &other) const;
	float getRelationTo(const StateRef<Organisation> &other) const;
	bool isAlien() const;
	std::map<StateRef<Organisation>, float> current_relations;
};

}; // namespace OpenApoc
