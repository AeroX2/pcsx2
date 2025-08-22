// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/Pcsx2Types.h"
#include <string>
#include <chrono>

namespace usb_lightgun
{
	struct GameDetectionState
	{
		std::string active_game;
		u32 port;
		int last_ammo;
		int last_life;
		int last_weapon;
		int last_charged;
		int last_other1;
		int last_other2;
		bool trigger_is_active;
		std::chrono::microseconds::rep trigger_last_press;
		std::chrono::microseconds::rep trigger_last_release;
		bool twoplayer_fix;
	};

	struct GameDetectionResult
	{
		std::string output_signal;
	};

	GameDetectionResult DetectGameEvents(GameDetectionState& state, std::chrono::microseconds::rep timestamp);
}
