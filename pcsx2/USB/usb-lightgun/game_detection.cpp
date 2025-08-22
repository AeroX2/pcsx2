// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "game_detection.h"
#include "Memory.h"

namespace usb_lightgun
{
	GameDetectionResult DetectGameEvents(GameDetectionState& state, std::chrono::microseconds::rep timestamp)
	{
		GameDetectionResult result = {};

		// Common variables to reduce repetition
		bool valid_query = false;
		u8 ammo_first = 0;
		u8 ammo_second = 0;
		u32 ammo_count = 0;
		u32 life_count = 0;
		u32 weapon_type = 0;
		u32 pointer_ammo = 0;
		long long diff = timestamp - state.trigger_last_press;

		//Dino Stalker (E, English)
		if (state.active_game == "SLES-50930") 
		{
			if (state.port == 0)
			{
				valid_query = true;
				ammo_first = memRead8(0x5986A8);
				ammo_second = memRead8(0x5986F8);
				ammo_count = ammo_first + ammo_second;
			}
			if (valid_query)
			{
				if (ammo_count < state.last_ammo && (state.trigger_is_active || diff < 200000))
				{
					result.output_signal = "gunshot";
					state.trigger_last_press = 0;
				}
				else if (ammo_count > state.last_ammo)
				{
					result.output_signal = "ammo";
				}
				state.last_ammo = ammo_count;
			}
		}		//Dino Stalker (E, French)
		else if (state.active_game == "SLES-51095")
		{
			if (state.port == 0)
			{
				valid_query = true;
				ammo_first = memRead8(0x59AB78);
				ammo_second = memRead8(0x59ABC8);
				ammo_count = ammo_first + ammo_second;
			}
			if (valid_query)
			{
				if (ammo_count < state.last_ammo && (state.trigger_is_active || diff < 200000))
				{
					result.output_signal = "gunshot";
					state.trigger_last_press = 0;
				}
				else if (ammo_count > state.last_ammo)
				{
					result.output_signal = "ammo";
				}
				state.last_ammo = ammo_count;
			}
		}

		//Dino Stalker (USA)
		else if (state.active_game == "SLUS-20485")
		{
			if (state.port == 0)
			{
				valid_query = true;
				ammo_first = memRead8(0x5980E8);
				ammo_second = memRead8(0x598138);
				ammo_count = ammo_first + ammo_second;
			}
			if (valid_query)
			{
				if (ammo_count < state.last_ammo && (state.trigger_is_active || diff < 200000))
				{
					result.output_signal = "gunshot";
					state.trigger_last_press = 0;
				}
				else if (ammo_count > state.last_ammo)
				{
					result.output_signal = "ammo";
				}
				state.last_ammo = ammo_count;
			}
		}

		//EndGame US
		else if (state.active_game == "SLUS-20389")
		{
			if (state.port == 1) //Port 1 = Player 1
			{
				valid_query = true;
				pointer_ammo = memRead32(0xD5BDBC);
				if (pointer_ammo > 0)
					ammo_count = memRead8(pointer_ammo + 0x10);
			}
			if (state.port == 0) //Port 1 = Player 1
			{
				valid_query = true;
				pointer_ammo = memRead32(0xD5BDC0);
				if (pointer_ammo > 0)
					ammo_count = memRead8(pointer_ammo + 0x10);
			}
			if (valid_query)
			{
				if (ammo_count < state.last_ammo && (state.trigger_is_active || diff < 200000))
				{
					result.output_signal = "gunshot";
					state.trigger_last_press = 0;
				}
				else if (ammo_count > state.last_ammo)
				{
					result.output_signal = "ammo";
				}
				state.last_ammo = ammo_count;
			}
		}

		//Gun Survivor 3: Dino Crisis (J)
		else if (state.active_game == "SLPM-65139")
		{
			if (state.port == 0)
			{
				valid_query = true;
				ammo_first = memRead8(0x591778);
				ammo_second = memRead8(0x5917C8);
				ammo_count = ammo_first + ammo_second;
			}
			if (valid_query)
			{
				if (ammo_count < state.last_ammo && (state.trigger_is_active || diff < 200000))
				{
					result.output_signal = "gunshot";
					state.trigger_last_press = 0;
				}
				else if (ammo_count > state.last_ammo)
				{
					result.output_signal = "ammo";
				}
				state.last_ammo = ammo_count;
			}
		}

		//Guncom 2 (E)
		else if (state.active_game == "SLES-52620")
		{
			if (state.port == 0)
			{
				valid_query = true;
				ammo_count = memRead8(0x4139F1);
			}
			if (state.port == 1)
			{
				valid_query = true;
				ammo_count = memRead8(0x413A2D);
			}
			if (valid_query)
			{
				if (ammo_count < state.last_ammo && (state.trigger_is_active || diff < 200000))
				{
					result.output_signal = "gunshot";
					state.trigger_last_press = 0;
				}
				else if (ammo_count > state.last_ammo)
				{
					result.output_signal = "ammo";
				}
				state.last_ammo = ammo_count;
			}
		}

		//Gunfighter 2 - Jesse James (E)
		else if (state.active_game == "SLES-51289")
		{
			if (state.port == 1) //Gun 1 to port 2
			{
				valid_query = true;
				pointer_ammo = memRead32(0x209984);
				if (pointer_ammo > 0)
					ammo_count = memRead8(pointer_ammo + 0x11C);
			}
			if (state.port == 0)
			{
				valid_query = true;
				pointer_ammo = memRead32(0x209984);
				if (pointer_ammo > 0)
					ammo_count = memRead8(pointer_ammo + 0x174);
			}
			if (valid_query)
			{
				if (ammo_count < state.last_ammo && (state.trigger_is_active || diff < 200000))
				{
					result.output_signal = "gunshot";
					state.trigger_last_press = 0;
				}
				else if (ammo_count > state.last_ammo)
				{
					result.output_signal = "ammo";
				}
				state.last_ammo = ammo_count;
			}
		}

		//Gunvari Collection (J) (480i) : Only time crisis
		else if (state.active_game == "SLPS-25165")
		{
			if (state.port == 0)
			{
				valid_query = true;
				ammo_count = memRead8(0x3F4B4C);
			}
			if (valid_query)
			{
				if (ammo_count < state.last_ammo && (state.trigger_is_active || diff < 200000))
				{
					result.output_signal = "gunshot";
					state.trigger_last_press = 0;
				}
				else if (ammo_count > state.last_ammo)
				{
					result.output_signal = "ammo";
				}
				state.last_ammo = ammo_count;
			}
		}

		//Ninja Assault (U)
		else if (state.active_game == "SLUS-20492")
		{
			if (state.port == 0)
			{
				valid_query = true;
				ammo_count = memRead8(0x7DFEB0);
			}
			if (state.port == 1)
			{
				valid_query = true;
				ammo_count = memRead8(0x7DFEB2);
			}
			if (valid_query)
			{
				if (ammo_count < state.last_ammo && (state.trigger_is_active || diff < 100000))
				{
					result.output_signal = "gunshot";
					state.trigger_last_press = 0;
				}
				else if (ammo_count > state.last_ammo)
				{
					result.output_signal = "ammo";
				}
				state.last_ammo = ammo_count;
			}
		}

		//Resident Evil Survivor 2 (E)
		else if (state.active_game == "SLES-50650")
		{
			if (state.port == 0)
			{
				valid_query = true;
				ammo_count = memRead8(0x1DF5DC0);
			}
			if (valid_query)
			{
				if (ammo_count < state.last_ammo && (state.trigger_is_active || diff < 200000))
				{
					result.output_signal = "gunshot";
					state.trigger_last_press = 0;
				}
				else if (ammo_count > state.last_ammo)
				{
					result.output_signal = "ammo";
				}
				state.last_ammo = ammo_count;
			}
		}

		//Resident Evil Dead Aim US
		else if (state.active_game == "SLUS-20669")
		{
			if (state.port == 1)
			{
				valid_query = true;
				ammo_count = memRead8(0x257FD4);
			}
			if (valid_query)
			{
				if (ammo_count < state.last_ammo && (state.trigger_is_active || diff < 100000))
				{
					result.output_signal = "gunshot";
					state.trigger_last_press = 0;
				}
				else if (ammo_count > state.last_ammo)
				{
					result.output_signal = "ammo";
				}
				state.last_ammo = ammo_count;
			}
		}

		//Starsky & Hutch (E)
		else if (state.active_game == "SLES-51617")
		{
			if (state.port == 1)
			{
				valid_query = true;
				ammo_count = memRead8(0x5584D0);
			}
			if (valid_query)
			{
				if (ammo_count < state.last_ammo && (state.trigger_is_active || diff < 100000))
				{
					result.output_signal = "gunshot";
					state.trigger_last_press = 0;
				}
				else if (ammo_count > state.last_ammo)
				{
					result.output_signal = "ammo";
				}
				state.last_ammo = ammo_count;
			}
		}

		//Starsky & Hutch (U)
		else if (state.active_game == "SLUS-20619")
		{
			if (state.port == 1)
			{
				valid_query = true;
				ammo_count = memRead8(0x55D9D0);
			}
			if (valid_query)
			{
				if (ammo_count < state.last_ammo && (state.trigger_is_active || diff < 100000))
				{
					result.output_signal = "gunshot";
					state.trigger_last_press = 0;
				}
				else if (ammo_count > state.last_ammo)
				{
					result.output_signal = "ammo";
				}
				state.last_ammo = ammo_count;
			}
		}

		//Time Crisis 2 EU
		else if (state.active_game == "SCES-50300")
		{
			state.twoplayer_fix = memRead8(0x65CD24) == 1 ? true : false;

			if (state.port == 0)
			{
				valid_query = true;
				ammo_count = memRead32(0x661A04);
			}
			if (state.port == 1)
			{
				valid_query = true;
				ammo_count = memRead32(0x661A34);
			}
			if (valid_query)
			{
				if (ammo_count < state.last_ammo && (state.trigger_is_active || diff < 100000))
				{
					result.output_signal = "gunshot";
				}
				else if (ammo_count > state.last_ammo)
				{
					result.output_signal = "ammo";
				}
				state.last_ammo = ammo_count;
			}
		}

		//Time Crisis 2 US
		else if (state.active_game == "SLUS-20219")
		{
			state.twoplayer_fix = memRead8(0x63EE64) == 1 ? true : false;

			if (state.port == 0)
			{
				valid_query = true;
				ammo_count = memRead32(0x643ABC);
				life_count = memRead32(0x643958);
			}
			if (state.port == 1)
			{
				valid_query = true;
				ammo_count = memRead32(0x643AEC);
				life_count = memRead32(0x6439B8);
			}
			if (valid_query)
			{
				if (ammo_count < state.last_ammo && (state.trigger_is_active || diff < 100000))
				{
					result.output_signal = "gunshot";
				}
				else if (ammo_count > state.last_ammo)
				{
					result.output_signal = "ammo";
				}
				if (life_count < state.last_life)
				{
					result.output_signal = "life";
				}
				state.last_ammo = ammo_count;
				state.last_life = life_count;
			}
		}

		//Time Crisis 3 EU
		else if (state.active_game == "SCES-51844")
		{
			state.twoplayer_fix = memRead8(0x474EEC) == 1 ? true : false;

			if (state.port == 0)
			{
				valid_query = true;
				ammo_count = memRead32(0x1F6B774);
				weapon_type = memRead32(0x1A1C790);
			}
			if (state.port == 1)
			{
				valid_query = true;
				ammo_count = memRead32(0x1F6B824);
				weapon_type = memRead32(0x1A1C7E0);
			}
			if (valid_query)
			{
				if (ammo_count < state.last_ammo && state.trigger_is_active && weapon_type == state.last_weapon)
				{
					result.output_signal = "gunshot";
				}
				else if (ammo_count > state.last_ammo)
				{
					result.output_signal = "ammo";
				}
				state.last_ammo = ammo_count;
				state.last_weapon = weapon_type;
			}
		}

		//Time Crisis 3 US
		else if (state.active_game == "SLUS-20645")
		{
			state.twoplayer_fix = memRead8(0x43A16C) == 1 ? true : false;

			if (state.port == 0)
			{
				valid_query = true;
				ammo_count = memRead32(0x1EF5134);
				weapon_type = memRead32(0x19A67E0);
			}
			if (state.port == 1)
			{
				valid_query = true;
				ammo_count = memRead32(0x1EF51E4);
				weapon_type = memRead32(0x19A6830);
			}
			if (valid_query)
			{
				if (ammo_count < state.last_ammo && state.trigger_is_active && weapon_type == state.last_weapon)
				{
					result.output_signal = "gunshot";
				}
				else if (ammo_count > state.last_ammo)
				{
					result.output_signal = "ammo";
				}
				state.last_ammo = ammo_count;
				state.last_weapon = weapon_type;
			}
		}

		//Time Crisis Crisis Zone US
		else if (state.active_game == "SLUS-20927")
		{
			if (state.port == 0)
			{
				valid_query = true;
				ammo_count = memRead32(0x7D1394);
				weapon_type = memRead32(0x79C688);
			}
			if (state.port == 1)
			{
				valid_query = true;
				ammo_count = memRead32(0x7D13D4);
				weapon_type = 0;
			}
			if (valid_query)
			{
				if (ammo_count < state.last_ammo && state.trigger_is_active && weapon_type == state.last_weapon)
				{
					result.output_signal = "gunshot";
				}
				else if (ammo_count > state.last_ammo)
				{
					result.output_signal = "ammo";
				}
				state.last_ammo = ammo_count;
				state.last_weapon = weapon_type;
			}
		}

		//Vampire Night (U)
		else if (state.active_game == "SLUS-20221")
		{
			if (state.port == 0)
			{
				valid_query = true;
				ammo_count = memRead32(0x49306C);
			}
			if (state.port == 1)
			{
				valid_query = true;
				ammo_count = memRead32(0x4933B4);
			}
			if (valid_query)
			{
				if (ammo_count < state.last_ammo && state.trigger_is_active)
				{
					result.output_signal = "gunshot";
				}
				else if (ammo_count > state.last_ammo)
				{
					result.output_signal = "ammo";
				}
				state.last_ammo = ammo_count;
			}
		}

		//Virtua Cop Elite Edition
		else if (state.active_game == "SLES-51229")
		{
			if (state.port == 0)
			{
				valid_query = true;
				ammo_count = memRead32(0x1FCECC);
			}
			if (state.port == 1)
			{
				valid_query = true;
				ammo_count = memRead32(0x1FCF38);
			}
			if (valid_query)
			{
				if (ammo_count < state.last_ammo && state.trigger_is_active)
				{
					result.output_signal = "gunshot";
				}
				else if (ammo_count > state.last_ammo)
				{
					result.output_signal = "ammo";
				}
				state.last_ammo = ammo_count;
			}
		}

		return result;
	}
} // namespace usb_lightgun

