// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "Input/InputManager.h"
#include "Input/ManyMouseInputSource.h"
#include "ImGui/ImGuiManager.h"

#include "common/Console.h"

#include "../../3rdparty/manymouse/manymouse.h"

#include <algorithm>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace InputManager
{
	static bool s_manymouse_initialized = false;
	static std::vector<std::string> s_manymouse_device_names;
#ifdef _WIN32
	static HWND s_manymouse_previous_raw_input_window = nullptr;
#endif

	static float NormalizeManyMouseAbsolutePosition(const ManyMouseEvent& event, float window_size)
	{
#ifdef _WIN32
		// Windows Raw Input absolute axes use the normalized 0..65535 desktop range.
		constexpr int min_value = 0;
		constexpr int max_value = 65535;
#else
		const int min_value = event.minval;
		const int max_value = event.maxval;
#endif
		if (max_value <= min_value)
			return 0.0f;

		const float normalized = static_cast<float>(event.value - min_value) /
		                         static_cast<float>(max_value - min_value);
		return std::clamp(normalized, 0.0f, 1.0f) * window_size;
	}

	static bool OpenManyMouseSource()
	{
		if (s_manymouse_initialized)
			return true;

#ifdef _WIN32
		const std::optional<WindowInfo> window_info = Host::GetTopLevelWindowInfo();
		s_manymouse_previous_raw_input_window =
			window_info.has_value() ? static_cast<HWND>(window_info->window_handle) : nullptr;
#endif

		const int device_count = ManyMouse_Init();
		if (device_count < 0)
		{
			Console.Warning("(ManyMouse) No supported multiple-pointer backend was found.");
			return false;
		}

		s_manymouse_initialized = true;
		s_manymouse_device_names.clear();
		s_manymouse_device_names.reserve(std::min<int>(device_count, MAX_MANYMOUSE_DEVICES));

		const char* const driver_name = ManyMouse_DriverName();
		Console.WriteLn("(ManyMouse) Using %s with %d pointer device%s.",
			driver_name ? driver_name : "unknown driver", device_count, device_count == 1 ? "" : "s");

		const u32 stored_device_count = std::min<u32>(static_cast<u32>(device_count), MAX_MANYMOUSE_DEVICES);
		for (u32 i = 0; i < stored_device_count; i++)
		{
			const char* const name = ManyMouse_DeviceName(i);
			s_manymouse_device_names.emplace_back(name ? name : "Unknown pointer");
			Console.WriteLn("(ManyMouse) Pointer-%u: %s", i + MANYMOUSE_POINTER_OFFSET,
				s_manymouse_device_names.back().c_str());
		}

		if (device_count > static_cast<int>(MAX_MANYMOUSE_DEVICES))
		{
			Console.Warning("(ManyMouse) Ignoring %d pointer device%s beyond the supported limit of %u.",
				device_count - static_cast<int>(MAX_MANYMOUSE_DEVICES),
				(device_count - static_cast<int>(MAX_MANYMOUSE_DEVICES)) == 1 ? "" : "s", MAX_MANYMOUSE_DEVICES);
		}

		return true;
	}

	bool UpdateManyMouseSource(bool enabled)
	{
		if (enabled)
			return OpenManyMouseSource();

		CloseManyMouseSource();
		return false;
	}

	void CloseManyMouseSource()
	{
		if (!s_manymouse_initialized)
			return;

		ManyMouse_Quit();
#ifdef _WIN32
		// Windows permits only one raw-input target per process and usage. ManyMouse temporarily replaces the
		// main window's mouse registration, so restore PCSX2's target when the source closes.
		if (s_manymouse_previous_raw_input_window)
		{
			RAWINPUTDEVICE device = {};
			device.usUsagePage = 0x01;
			device.usUsage = 0x02;
			device.dwFlags = RIDEV_INPUTSINK;
			device.hwndTarget = s_manymouse_previous_raw_input_window;
			RegisterRawInputDevices(&device, 1, sizeof(device));
			s_manymouse_previous_raw_input_window = nullptr;
		}
#endif
		s_manymouse_initialized = false;
		s_manymouse_device_names.clear();
	}

	bool ReloadManyMouseDevices()
	{
		if (!s_manymouse_initialized)
			return false;

		std::vector<std::string> old_names = s_manymouse_device_names;
		CloseManyMouseSource();
		OpenManyMouseSource();
		return old_names != s_manymouse_device_names;
	}

	void PollManyMouseSource()
	{
		if (!s_manymouse_initialized)
			return;

		ManyMouseEvent event;
		while (ManyMouse_PollEvent(&event))
		{
			if (event.device >= s_manymouse_device_names.size())
				continue;

			const u32 pointer_index = event.device + MANYMOUSE_POINTER_OFFSET;
			switch (event.type)
			{
				case MANYMOUSE_EVENT_ABSMOTION:
				{
					if (event.item > 1)
						break;

					auto [x, y] = GetPointerAbsolutePosition(pointer_index);
					if (event.item == 0)
						x = NormalizeManyMouseAbsolutePosition(event, ImGuiManager::GetWindowWidth());
					else
						y = NormalizeManyMouseAbsolutePosition(event, ImGuiManager::GetWindowHeight());
					UpdatePointerAbsolutePosition(pointer_index, x, y);
				}
				break;

				case MANYMOUSE_EVENT_RELMOTION:
					if (event.item <= 1)
						UpdatePointerRelativeDelta(pointer_index, static_cast<InputPointerAxis>(event.item),
							static_cast<float>(event.value), true);
					break;

				case MANYMOUSE_EVENT_BUTTON:
					if (event.item < MAX_POINTER_BUTTONS)
						InvokeEvents(MakePointerButtonKey(pointer_index, event.item), event.value != 0 ? 1.0f : 0.0f);
					break;

				case MANYMOUSE_EVENT_SCROLL:
					UpdatePointerRelativeDelta(pointer_index,
						event.item == 0 ? InputPointerAxis::WheelY : InputPointerAxis::WheelX,
						static_cast<float>(event.value), true);
					break;

				case MANYMOUSE_EVENT_DISCONNECT:
					Console.Warning("(ManyMouse) Pointer-%u (%s) disconnected. Reload input devices to detect it again.",
						pointer_index, s_manymouse_device_names[event.device].c_str());
					break;

				default:
					break;
			}
		}
	}

	u32 GetManyMouseDeviceCount()
	{
		return static_cast<u32>(s_manymouse_device_names.size());
	}

	std::string_view GetManyMouseDeviceName(u32 device_index)
	{
		return (device_index < s_manymouse_device_names.size()) ?
		           std::string_view(s_manymouse_device_names[device_index]) :
		           std::string_view();
	}
} // namespace InputManager
