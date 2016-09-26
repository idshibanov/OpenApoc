#pragma once
#include "framework/includes.h"
#include "game/state/stateobject.h"
#include "library/sp.h"

#define TICKS_TO_STAY_OPEN TICKS_PER_SECOND * 4

namespace OpenApoc
{
class BattleMapPart;

class BattleDoor : public std::enable_shared_from_this<BattleDoor>
{
  public:
	// wether this door is still operational
	bool operational = false;
	bool right = false;
	// "Open" flag for doors
	bool open = false;
	void setDoorState(bool open);
	int animationFrameCount = 0;
	int openTicksRemaining = 0;
	// Amount of ticks until changing to open/closed state
	int animationTicksRemaining = 0;
	int getAnimationFrame();
	
	void update(GameState &state, unsigned int ticks);
	
	void collapse();

	void playDoorSound();

	~BattleDoor() = default;

	// Following members are not serialized, but rather are set in initBattle method

	std::list<wp<BattleMapPart>> mapParts;
	Vec3<float> position;
	wp<Battle> battle;
};
}