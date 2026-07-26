// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/Pcsx2Defs.h"

#include <string_view>

namespace InputManager
{
	bool UpdateManyMouseSource(bool enabled);
	void CloseManyMouseSource();
	bool ReloadManyMouseDevices();
	void PollManyMouseSource();

	u32 GetManyMouseDeviceCount();
	std::string_view GetManyMouseDeviceName(u32 device_index);
} // namespace InputManager
