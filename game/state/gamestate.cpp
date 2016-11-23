#include "game/state/gamestate.h"
#include "framework/trace.h"
#include "game/state/base/base.h"
#include "game/state/base/facility.h"
#include "game/state/city/building.h"
#include "game/state/city/city.h"
#include "game/state/city/doodad.h"
#include "game/state/city/scenery.h"
#include "game/state/city/vehicle.h"
#include "game/state/city/vehiclemission.h"
#include "game/state/organisation.h"
#include "game/state/tileview/tileobject_vehicle.h"
#include <random>

namespace OpenApoc
{

GameState::GameState()
    : player(this), showTileOrigin(false), showVehiclePath(false), showSelectableBounds(false),
      gameTime(0), lastVehicle(0)
{
}

GameState::~GameState()
{
	for (auto &v : this->vehicles)
	{
		auto vehicle = v.second;
		if (vehicle->tileObject)
		{
			vehicle->tileObject->removeFromMap();
		}
		vehicle->tileObject = nullptr;
		// Detatch some back-pointers otherwise we get circular sp<> depedencies and leak
		// FIXME: This is not a 'good' way of doing this, maybe add a destroyVehicle() function? Or
		// make StateRefWeak<> or something?
		//
		vehicle->city = "";
		vehicle->homeBuilding = "";
		vehicle->currentlyLandedBuilding = "";
		vehicle->missions.clear();
		vehicle->equipment.clear();
		vehicle->mover = nullptr;
	}
	for (auto &b : this->player_bases)
	{
		for (auto &f : b.second->facilities)
		{
			if (f->lab)
				f->lab->current_project = "";
			f->lab = "";
		}
	}
	for (auto &org : this->organisations)
	{
		org.second->current_relations.clear();
	}
}

// Just a handy shortcut since it's shown on every single screen
UString GameState::getPlayerBalance() const
{
	return Strings::FromInteger(this->getPlayer()->balance);
}

StateRef<Organisation> GameState::getOrganisation(const UString &orgID)
{
	return StateRef<Organisation>(this, orgID);
}

const StateRef<Organisation> &GameState::getPlayer() const { return this->player; }
StateRef<Organisation> GameState::getPlayer() { return this->player; }

void GameState::initState()
{
	// FIXME: reseed rng when game starts

	for (auto &c : this->cities)
	{
		auto &city = c.second;
		city->initMap();
		for (auto &v : this->vehicles)
		{
			auto vehicle = v.second;
			if (vehicle->city == city && !vehicle->currentlyLandedBuilding)
			{
				city->map->addObjectToMap(vehicle);
			}
		}

		if (city->portals.empty())
		{
			city->generatePortals(*this);
		}
	}
	for (auto &v : this->vehicles)
	{
		if (!v.second->currentlyLandedBuilding)
		{
			v.second->setupMover();
		}
	}
	for (auto &c : this->cities)
	{
		auto &city = c.second;
		for (auto &s : city->scenery)
		{
			for (auto &b : city->buildings)
			{
				auto &building = b.second;
				Vec2<int> pos2d{s->initialPosition.x, s->initialPosition.y};
				if (building->bounds.within(pos2d))
				{
					s->building = {this, building};
				}
			}
		}
	}
}

void GameState::startGame()
{
	for (auto &pair : this->cities)
	{
		auto &city = pair.second;
		// Start the game with all buildings whole
		for (auto &tilePair : city->initial_tiles)
		{
			auto s = mksp<Scenery>();

			s->type = tilePair.second;
			s->initialPosition = tilePair.first;
			s->currentPosition = s->initialPosition;

			city->scenery.insert(s);
		}
	}

	auto buildingIt = this->cities["CITYMAP_HUMAN"]->buildings.begin();

	// Create some random vehicles
	for (int i = 0; i < 5; i++)
	{
		for (auto vehicleType : this->vehicle_types)
		{
			auto &type = vehicleType.second;
			if (type->type != VehicleType::Type::Flying)
				continue;
			if (type->manufacturer == this->getPlayer())
				continue;

			auto v = mksp<Vehicle>();
			v->type = {this, vehicleType.first};
			v->name = UString::format("%s %d", type->name.c_str(), ++type->numCreated);
			v->city = {this, "CITYMAP_HUMAN"};
			v->currentlyLandedBuilding = {this, buildingIt->first};
			v->owner = type->manufacturer;
			v->health = type->health;

			buildingIt++;
			if (buildingIt == this->cities["CITYMAP_HUMAN"]->buildings.end())
				buildingIt = this->cities["CITYMAP_HUMAN"]->buildings.begin();

			// Vehicle::equipDefaultEquipment uses the state reference from itself, so make sure the
			// vehicle table has the entry before calling it
			UString vID = UString::format("%s%d", Vehicle::getPrefix().c_str(), lastVehicle++);
			this->vehicles[vID] = v;

			v->currentlyLandedBuilding->landed_vehicles.insert({this, vID});

			v->equipDefaultEquipment(*this);
		}
	}

	// Create the intial starting base
	// Randomly shuffle buildings until we find one with a base layout
	sp<City> humanCity = this->cities["CITYMAP_HUMAN"];
	this->current_city = {this, humanCity};

	std::vector<sp<Building>> buildingsWithBases;
	for (auto &b : humanCity->buildings)
	{
		if (b.second->base_layout)
			buildingsWithBases.push_back(b.second);
	}

	if (buildingsWithBases.empty())
	{
		LogError("City map has no buildings with valid base layouts");
	}

	std::uniform_int_distribution<int> bldDist(0, buildingsWithBases.size() - 1);

	auto bld = buildingsWithBases[bldDist(this->rng)];

	auto base = mksp<Base>(*this, StateRef<Building>{this, bld});
	base->startingBase(*this);
	base->name = "Base " + Strings::FromInteger(this->player_bases.size() + 1);
	this->player_bases[Base::getPrefix() + Strings::FromInteger(this->player_bases.size() + 1)] =
	    base;
	bld->owner = this->getPlayer();
	this->current_base = {this, base};

	// Give the player one of each equipable vehicle
	for (auto &it : this->vehicle_types)
	{
		auto &type = it.second;
		if (!type->equipment_screen)
			continue;
		auto v = mksp<Vehicle>();
		v->type = {this, type};
		v->name = UString::format("%s %d", type->name.c_str(), ++type->numCreated);
		v->city = {this, "CITYMAP_HUMAN"};
		v->currentlyLandedBuilding = {this, bld};
		v->homeBuilding = {this, bld};
		v->owner = this->getPlayer();
		v->health = type->health;
		UString vID = UString::format("%s%d", Vehicle::getPrefix().c_str(), lastVehicle++);
		this->vehicles[vID] = v;
		v->currentlyLandedBuilding->landed_vehicles.insert({this, vID});
		v->equipDefaultEquipment(*this);
	}
	// Give that base some inventory
	for (auto &pair : this->vehicle_equipment)
	{
		auto &equipmentID = pair.first;
		base->inventory[equipmentID] = 10;
	}

	for (auto &agentTypePair : this->initial_agents)
	{
		auto type = agentTypePair.first;
		auto count = agentTypePair.second;
		while (count > 0)
		{
			auto agent = this->agent_generator.createAgent(*this, type);
			agent->home_base = {this, base};
			agent->owner = this->getPlayer();
			count--;
		}
	}
	growUFOs();

	gameTime = GameTime::midday();
}

bool GameState::canTurbo() const
{
	if (!this->current_city->projectiles.empty())
	{
		return false;
	}
	for (auto &v : this->vehicles)
	{
		if (v.second->city == this->current_city && v.second->tileObject != nullptr &&
		    v.second->type->aggressiveness > 0 && !v.second->isCrashed() &&
		    v.second->owner->isRelatedTo(this->getPlayer()) == Organisation::Relation::Hostile)
		{
			return false;
		}
	}
	return true;
}

void GameState::update(unsigned int ticks)
{
	Trace::start("GameState::update::cities");
	for (auto &c : this->cities)
	{
		c.second->update(*this, ticks);
	}
	Trace::end("GameState::update::cities");
	Trace::start("GameState::update::vehicles");
	for (auto &v : this->vehicles)
	{
		v.second->update(*this, ticks);
	}
	Trace::end("GameState::update::vehicles");
	Trace::start("GameState::update::labs");
	for (auto &lab : this->research.labs)
	{
		Lab::update(ticks, {this, lab.second}, shared_from_this());
	}
	Trace::end("GameState::update::labs");

	gameTime.addTicks(ticks);

	while (!this->ufo_queue.empty() && this->ufo_queue.top().ticksScheduled < gameTime.getTicks())
	{
		auto launch = this->ufo_queue.top();
		StateRef<City> humanCity = { this, "CITYMAP_HUMAN" };
		for (auto v : this->vehicles)
		{
			auto vehicle = v.second;
			auto typeID = vehicle->type->getId(*this, vehicle->type);
			if (typeID == launch.type && vehicle->city != humanCity)
			{
				vehicle->removeFromMap();
				vehicle->city = humanCity;
				vehicle->launch(*humanCity->map, *this, launch.position);

				vehicle->missions.clear();
				for (auto m : launch.missionList)
				{
					vehicle->missions.emplace_back(m);
				}
				vehicle->missions.front()->start(*this, *vehicle);
				break;
			}
		}
		this->ufo_queue.pop();
	}

	if (gameTime.dayPassed())
	{
		this->updateEndOfDay();
	}
	if (gameTime.weekPassed())
	{
		this->updateEndOfWeek();
	}
	gameTime.clearFlags();
}

void GameState::updateEndOfDay()
{
	for (auto &b : this->player_bases)
	{
		for (auto &f : b.second->facilities)
		{
			if (f->buildTime > 0)
			{
				f->buildTime--;
				// FIXME: Notify the player
			}
		}
	}

	Trace::start("GameState::updateEndOfDay::cities");
	for (auto &c : this->cities)
	{
		c.second->dailyLoop(*this);
	}
	Trace::end("GameState::updateEndOfDay::cities");

	Trace::start("GameState::updateEndOfDay::ufoIncursion");
	std::map<UString, int> alienVehCount;
	StateRef<City> alienCity = { this, "CITYMAP_ALIEN" };
	for (auto v : this->vehicles)
	{
		auto vehicle = v.second;
		if (vehicle->city == alienCity && vehicle->owner->isAlien())
		{
			auto typeID = vehicle->type->getId(*this, vehicle->type);
			auto cnt = alienVehCount.find(typeID);
			if (cnt != alienVehCount.end())
			{
				cnt->second++;
			}
			else
			{
				alienVehCount.emplace(std::pair<UString, int>(typeID, 1));
			}
		}
	}

	std::vector<sp<UFOIncursion>> incursions;
	for (auto i : this->ufo_incursions)
	{
		incursions.push_back(i.second);
	}
	std::sort(incursions.begin(), incursions.end(), [](sp<UFOIncursion> a, sp<UFOIncursion> b) -> bool
	{
		return a->priority > b->priority;
	});


	auto checkList = [&alienVehCount](std::pair<UString, int> p) -> bool
	{
		auto count = alienVehCount.find(p.first);
		if (count != alienVehCount.end() && p.second < count->second)
		{
			return true;
		}
		return false;
	};

	for (auto inc : incursions)
	{
		bool valid = true;
		for (auto ufo : inc->primaryList)
		{
			if (!checkList(ufo))
			{
				valid = false;
				break;
			}
		}
		for (auto ufo : inc->escortList)
		{
			if (!checkList(ufo))
			{
				valid = false;
				break;
			}
		}
		for (auto ufo : inc->attackList)
		{
			if (!checkList(ufo))
			{
				valid = false;
				break;
			}
		}

		if (valid)
		{
			StateRef<City> humanCity = { this, "CITYMAP_HUMAN" };
			std::uniform_int_distribution<int> portal_rng(0, humanCity->portals.size() - 1);
			std::uniform_int_distribution<int> bld_rng(0, humanCity->buildings.size() - 1);
			int scheduled = this->gameTime.getTicks();

			for (auto ufo : inc->primaryList)
			{
				scheduled += TICKS_PER_SECOND / 2;

				auto portal = humanCity->portals.begin();
				std::advance(portal, portal_rng(this->rng));

				auto bld_iter = humanCity->buildings.begin();
				std::advance(bld_iter, bld_rng(this->rng));
				StateRef<Building> bld = { this, (*bld_iter).second };

				UFOMissionLaunch launch;
				launch.type = ufo.first;
				launch.position = (*portal)->getPosition();
				launch.ticksScheduled = scheduled;
				launch.missionList.emplace_back(VehicleMission::infiltrateBuilding(bld));
				this->ufo_queue.push(launch);
			}

			// Incursion launched succesfully, stop execution
			break;
		}
	}
	Trace::end("GameState::updateEndOfDay::ufoIncursion");
}

void GameState::updateEndOfWeek()
{
	growUFOs();
}

void GameState::growUFOs()
{
	int week = this->gameTime.getWeek();
	auto growth =
		this->ufo_growth_lists.find(UString::format("%s%d", UFOGrowth::getPrefix().c_str(), week));
	if (growth == this->ufo_growth_lists.end())
	{
		growth = this->ufo_growth_lists.find(
			UString::format("%s%s", UFOGrowth::getPrefix().c_str(), "DEFAULT"));
	}

	if (growth != this->ufo_growth_lists.end())
	{
		StateRef<City> city = { this, "CITYMAP_ALIEN" };
		StateRef<Organisation> alienOrg = { this, "ORG_ALIEN" };
		std::uniform_int_distribution<int> xyPos(20, 120);

		for (auto vehicleEntry : growth->second->vehicleTypeList)
		{
			auto vehicleType = this->vehicle_types.find(vehicleEntry.first);
			if (vehicleType != this->vehicle_types.end())
			{
				for (int i = 0; i < vehicleEntry.second; i++)
				{
					auto &type = (*vehicleType).second;

					auto v = mksp<Vehicle>();
					v->type = { this, (*vehicleType).first };
					v->name = UString::format("%s %d", type->name.c_str(), ++type->numCreated);
					v->city = city;
					v->owner = alienOrg;
					v->health = type->health;

					// Vehicle::equipDefaultEquipment uses the state reference from itself, so make
					// sure vehicle table has the entry before calling it
					UString vID =
						UString::format("%s%d", Vehicle::getPrefix().c_str(), lastVehicle++);
					this->vehicles[vID] = v;

					v->equipDefaultEquipment(*this);
					if (city->map)
					{
						v->launch(*city->map, *this, { xyPos(rng), xyPos(rng), v->altitude });
					}
					else
					{
						// game is not initialized yet
						v->position = { xyPos(rng), xyPos(rng), v->altitude };
					}
					v->missions.emplace_front(VehicleMission::patrol());
				}
			}
		}
	}
}

void GameState::toggleDimension()
{
	StateRef<City> humanCity = { this, "CITYMAP_HUMAN" };
	StateRef<City> alienCity = { this, "CITYMAP_ALIEN" };
	if (this->current_city == humanCity)
	{
		this->current_city = alienCity;
	}
	else if (this->current_city == alienCity)
	{
		this->current_city = humanCity;
	}
}

void GameState::update() { this->update(1); }

void GameState::updateTurbo()
{
	if (!this->canTurbo())
	{
		LogError("Called when canTurbo() is false");
	}
	unsigned ticksToUpdate = TURBO_TICKS;
	// Turbo always re-aligns to TURBO_TICKS (5 minutes)
	unsigned int align = this->gameTime.getTicks() % TURBO_TICKS;
	if (align != 0)
	{
		ticksToUpdate -= align;
	}
	this->update(ticksToUpdate);
}

}; // namespace OpenApoc
