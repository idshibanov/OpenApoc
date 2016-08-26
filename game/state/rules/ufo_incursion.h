#pragma once
#include "game/state/stateobject.h"
#include "library/strings.h"
#include "library/vec.h"
#include <map>

class VehicleMission;

namespace OpenApoc
{

class UFOIncursion : public StateObject<UFOIncursion>
{
  public:
	enum class PrimaryMission
	{
		Infiltration,
		Subversion,
		Attack,
		Overspawn
	};
	static const std::map<PrimaryMission, UString> primaryMissionMap;

	PrimaryMission primaryMission;
	std::vector<std::pair<UString, int>> primaryList;
	std::vector<std::pair<UString, int>> escortList;
	std::vector<std::pair<UString, int>> attackList;
	int priority;
};

class UFOMissionLaunch {
public:
	int ticksScheduled;
	UString& type;
	Vec3<float> position;
	std::vector<sp<VehicleMission>> missionList;
	bool operator<(const UFOMissionLaunch& rhs);
};

}; // namespace OpenApoc
