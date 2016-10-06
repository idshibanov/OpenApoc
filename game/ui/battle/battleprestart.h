#pragma once
#include "forms/forms.h"
#include "framework/stage.h"
#include "library/sp.h"
#include <future>

namespace OpenApoc
{

class GameState;

class BattlePreStart : public Stage
{
  private:
	sp<Form> menuform;

	sp<GameState> state;

  public:
	BattlePreStart(sp<GameState> state);
	// Stage control
	void begin() override;
	void pause() override;
	void resume() override;
	void finish() override;
	void eventOccurred(Event *e) override;
	void update() override;
	void render() override;
	bool isTransition() override;
};

}; // namespace OpenApoc