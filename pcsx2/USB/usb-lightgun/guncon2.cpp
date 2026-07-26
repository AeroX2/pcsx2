// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/GS.h"
#include "Host.h"
#include "IconsFontAwesome.h"
#include "IconsPromptFont.h"
#include "ImGui/ImGuiManager.h"
#include "Input/InputManager.h"
#include "Input/ManyMouseInputSource.h"
#include "StateWrapper.h"
#include "USB/USB.h"
#include "USB/deviceproxy.h"
#include "USB/qemu-usb/USBinternal.h"
#include "USB/qemu-usb/desc.h"
#include "USB/usb-lightgun/guncon2.h"
#include "USB/usb-lightgun/game_detection.h"
#include "VMManager.h"

#include "common/Console.h"
#include "common/StringUtil.h"

#include <atomic>
#include <filesystem>
#include <optional>
#include <tuple>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <cstring>
#endif
#include "Memory.h"

namespace usb_lightgun
{
	enum : u32
	{
		GUNCON2_FLAG_PROGRESSIVE = 0x0100,

		GUNCON2_CALIBRATION_DELAY = 12,
		GUNCON2_CALIBRATION_REPORT_DELAY = 5,
	};

	enum : u32
	{
		BID_C = 1,
		BID_B = 2,
		BID_A = 3,
		BID_DPAD_UP = 4,
		BID_DPAD_RIGHT = 5,
		BID_DPAD_DOWN = 6,
		BID_DPAD_LEFT = 7,
		BID_TRIGGER = 13,
		BID_SELECT = 14,
		BID_START = 15,
		BID_SHOOT_OFFSCREEN = 16,
		BID_RECALIBRATE = 17,
		BID_RELATIVE_LEFT = 18,
		BID_RELATIVE_RIGHT = 19,
		BID_RELATIVE_UP = 20,
		BID_RELATIVE_DOWN = 21,
		BID_POINTER_X = 22,
	};

	static std::vector<std::string> GetDetectedSerialPorts()
	{
		std::vector<std::string> ports;
#ifdef _WIN32
		for (u32 port = 1; port <= 99; port++)
		{
			const std::string name = fmt::format("COM{}", port);
			char target[512];
			if (QueryDosDeviceA(name.c_str(), target, std::size(target)) != 0)
				ports.push_back(name);
		}
#else
		std::error_code ec;
		for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator("/dev", ec))
		{
			if (ec)
				break;

			const std::string name = entry.path().filename().string();
			if (name.starts_with("ttyACM") || name.starts_with("ttyUSB"))
				ports.push_back(entry.path().string());
		}
		std::sort(ports.begin(), ports.end());
#endif
		return ports;
	}

	static std::optional<std::string> ResolveSerialPort(int setting, u32 guncon_port)
	{
		if (setting < 0)
			return std::nullopt;

		if (setting > 0)
		{
#ifdef _WIN32
			return fmt::format("COM{}", setting);
#else
			return fmt::format("/dev/ttyACM{}", setting - 1);
#endif
		}

		const std::vector<std::string> detected_ports = GetDetectedSerialPorts();
		if (guncon_port >= detected_ports.size())
			return std::nullopt;

		return detected_ports[guncon_port];
	}

	// Right pain in the arse. Different games seem to have different scales..
	// Not worth putting these in the gamedb for such few games.
	// Values are from the old nuvee plugin.
	struct GameConfig
	{
		const char* serial;
		float scale_x, scale_y;
		u32 center_x, center_y;
		u32 screen_width, screen_height;
	};

	static constexpr const GameConfig s_game_config[] = {
		{"SLES-50930", 90.25f, 94.5f, 390, 132, 640, 256}, // Dino Stalker (E, English)
		{"SLES-51095", 90.25f, 94.5f, 390, 132, 640, 256}, // Dino Stalker (E, French)
		{"SLES-51096", 90.25f, 94.5f, 390, 132, 640, 256}, // Dino Stalker (E, German)
		{"SLUS-20485", 90.25f, 92.5f, 390, 132, 640, 240}, // Dino Stalker (U)
		{"SLUS-20389", 89.25f, 93.5f, 422, 141, 640, 240}, // Endgame (U)
		{"SLES-50936", 112.0f, 100.0f, 320, 120, 512, 256}, // Endgame (E) (Guncon2 needs to be connected to USB port 2)
		{"SLPM-65139", 90.0f, 91.5f, 320, 120, 640, 240}, // Gun Survivor 3: Dino Crisis (J)
		{"SLES-52620", 89.5f, 112.3f, 390, 147, 640, 256}, // Guncom 2 (E)
		{"SLES-51289", 84.5f, 89.0f, 456, 164, 640, 256}, // Gunfighter 2 - Jesse James (E)
		{"SLPS-25165", 90.25f, 98.0f, 390, 138, 640, 240}, // Gunvari Collection (J) (480i)
		// {"SLPS-25165", 86.75f, 96.0f, 454, 164, 640, 256}, // Gunvari Collection (J) (480p)
		{"SCES-50889", 90.25f, 94.5f, 390, 169, 640, 256}, // Ninja Assault (E)
		{"SLPS-20218", 90.0f, 92.0f, 320, 134, 640, 240}, // Ninja Assault (J)
		{"SLUS-20492", 90.25f, 92.5f, 390, 132, 640, 240}, // Ninja Assault (U)
		{"SLES-50650", 90.25f, 107.0f, 425, 135, 640, 240}, // Resident Evil Survivor 2 (E) Fixed, you need to press start to skip guncon calibration
		{"SLES-51448", 90.25f, 95.0f, 420, 132, 640, 240}, // Resident Evil - Dead Aim (E)
		{"SLUS-20669", 90.25f, 93.5f, 420, 132, 640, 240}, // Resident Evil - Dead Aim (U)
		//{"SLUS-20619", 90.25f, 91.75f, 453, 154, 640, 256}, // Starsky & Hutch (U)
		{"SLES-51617", 90.25f, 82.0f, 200, 154, 640, 256}, // Starsky & Hutch (E)
		{"SLUS-20619", 90.25f, 91.75f, 453, 154, 640, 256}, // Starsky & Hutch (U)
		{"SCES-50300", 90.25f, 102.75f, 390, 138, 640, 256}, // Time Crisis II (E)
		{"SLPS-20122", 90.25f, 97.5f, 390, 154, 640, 240}, // Time Crisis II (J)
		{"SLPS-20113", 90.25f, 97.5f, 390, 154, 640, 240}, // Time Crisis II (with GunCon 2) (J)
		{"SLUS-20219", 90.25f, 97.5f, 390, 154, 640, 240}, // Time Crisis 2 (U)
		{"SCES-51844", 90.25f, 102.75f, 390, 138, 640, 256}, // Time Crisis 3 (E)
		{"SLUS-20645", 90.25f, 97.5f, 390, 154, 640, 240}, // Time Crisis 3 (U)
		{"SCES-52530", 90.25f, 99.0f, 390, 153, 640, 256}, // Crisis Zone (E)
		{"SLUS-20927", 90.25f, 99.0f, 390, 153, 640, 240}, // Time Crisis - Crisis Zone (U) (480i)
		// {"SLUS-20927", 94.5f, 104.75f, 423, 407, 768, 768}, // Time Crisis - Crisis Zone (U) (480p)
		{"SCES-50411", 89.8f, 99.9f, 421, 138, 640, 256}, // Vampire Night (E)
		{"SLPS-25077", 90.0f, 97.5f, 422, 118, 640, 240}, // Vampire Night (J)
		{"SLUS-20221", 89.8f, 102.5f, 452, 137, 640, 228}, // Vampire Night (U) //Fixed
		{"SLES-51229", 110.15f, 100.0f, 433, 159, 512, 256}, // Virtua Cop - Elite Edition (E,J) (480i)
		{"SLPM-62205", 110.15f, 100.0f, 433, 159, 512, 256}, // Virtua Cop Re-Birth (J) (480i)
		// {"SLES-51229", 85.75f, 92.0f, 456, 164, 640, 256}, // Virtua Cop - Elite Edition (E,J) (480p)
	};

	static constexpr s32 DEFAULT_SCREEN_WIDTH = 640;
	static constexpr s32 DEFAULT_SCREEN_HEIGHT = 240;
	static constexpr float DEFAULT_CENTER_X = 320.0f;
	static constexpr float DEFAULT_CENTER_Y = 120.0f;
	static constexpr float DEFAULT_SCALE_X = 100.0f;
	static constexpr float DEFAULT_SCALE_Y = 100.0f;

#pragma pack(push, 1)
	union GunCon2Out
	{
		u8 bits[6];

		struct
		{
			u16 buttons;
			s16 pos_x;
			s16 pos_y;
		};
	};
	static_assert(sizeof(GunCon2Out) == 6);
#pragma pack(pop)

	struct GunCon2State
	{
		explicit GunCon2State(u32 port_);
		~GunCon2State();
		bool SendComMessage(const std::string& message, const std::string& end_line = "\r\n");
		void ShowSerialWarning(const std::string_view message);
		bool IsSerialPortValid() const
		{
#ifdef _WIN32
			return serial_port != INVALID_HANDLE_VALUE;
#else
			return serial_port != -1;
#endif
		}
		USBDevice dev{};
		USBDesc desc{};
		USBDescDevice desc_dev{};

		u32 port = 0;

		//////////////////////////////////////////////////////////////////////////
		// Configuration
		//////////////////////////////////////////////////////////////////////////
		bool has_relative_binds = false;
		bool custom_config = false;
		u32 pointer_index = 0;
		u32 screen_width = 640;
		u32 screen_height = 240;
		float center_x = 320;
		float center_y = 120;
		float scale_x = 1.0f;
		float scale_y = 1.0f;

		//////////////////////////////////////////////////////////////////////////
		// Host State (Not Saved)
		//////////////////////////////////////////////////////////////////////////
		u32 button_state = 0;
		std::string cursor_path;
		float cursor_scale = 1.0f;
		u32 cursor_color = 0xFFFFFFFF;
		float relative_pos[4] = {};

		//////////////////////////////////////////////////////////////////////////
		// Device State (Saved)
		//////////////////////////////////////////////////////////////////////////
		s16 param_x = 0;
		s16 param_y = 0;
		u16 param_mode = 0;

		u16 calibration_timer = 0;
		s16 calibration_pos_x = 0;
		s16 calibration_pos_y = 0;

		bool auto_config_done = false;

		bool quit_thread = false;
		bool thread_output_loaded = false;
		int recoil_pool_speed = 10;
		void ThreadOutputs();
		void ThreadAutoConfigure();
		std::thread* recoil_output_thread = nullptr;
		std::thread* auto_config_thread = nullptr;
		std::string active_game = "";
		std::atomic_bool trigger_is_active{false};
		std::atomic<std::chrono::microseconds::rep> trigger_last_press{0};
		std::atomic<std::chrono::microseconds::rep> trigger_last_release{0};
		std::chrono::microseconds::rep last_gun_shot = 0;
		std::chrono::microseconds::rep next_gun_shot = 0;
		int queue_size_gunshot = 0;
		long full_auto_delay = 0;
		long multishot_delay = 0;
		std::chrono::microseconds::rep next_life_signal_off = 0;
		bool life_signal_pending = false;
		std::chrono::microseconds::rep next_recoil_signal_off = 0;
		bool recoil_signal_pending = false;
		u32 last_ammo = UINT32_MAX;
		u32 last_life = UINT32_MAX;
		u32 last_weapon = 0;
		u32 last_charged = 0;
		u32 last_other1 = 0;
		u32 last_other2 = 0;
		bool full_auto_active = false;
		bool twoplayer_fix = false;
		int serial_port_setting = 0;
		bool gamepad_mode = false;
		bool serial_write_error_reported = false;
		std::string active_serial_port_name;
#ifdef _WIN32
		HANDLE serial_port;
#else
		int serial_port;
#endif

		void AutoConfigure();

		std::tuple<s16, s16> CalculatePosition();

		// 0..1, not -1..1.
		std::pair<float, float> GetAbsolutePositionFromRelativeAxes() const;
		u32 GetSoftwarePointerIndex() const;
		void UpdateSoftwarePointerPosition();
	};

	static const USBDescStrings desc_strings = {
		"Namco GunCon2",
	};

	/* mostly the same values as the Bochs USB Keyboard device */
	static const uint8_t guncon2_dev_desc[] = {
		/* bLength             */ 0x12,
		/* bDescriptorType     */ 0x01,
		/* bcdUSB              */ WBVAL(0x0100),
		/* bDeviceClass        */ 0x00,
		/* bDeviceSubClass     */ 0x00,
		/* bDeviceProtocol     */ 0x00,
		/* bMaxPacketSize0     */ 0x08,
		/* idVendor            */ WBVAL(0x0b9a),
		/* idProduct           */ WBVAL(0x016a),
		/* bcdDevice           */ WBVAL(0x0100),
		/* iManufacturer       */ 0x00,
		/* iProduct            */ 0x00,
		/* iSerialNumber       */ 0x00,
		/* bNumConfigurations  */ 0x01,
	};

	static const uint8_t guncon2_config_desc[] = {
		0x09, // Length
		0x02, // Type (Config)
		0x19, 0x00, // Total size

		0x01, // # interfaces
		0x01, // Configuration #
		0x00, // index of string descriptor
		0x80, // Attributes (bus powered)
		0x19, // Max power in mA


		// Interface
		0x09, // Length
		0x04, // Type (Interface)

		0x00, // Interface #
		0x00, // Alternative #
		0x01, // # endpoints

		0xff, // Class
		0x6a, // Subclass
		0x00, // Protocol
		0x00, // index of string descriptor


		// Endpoint
		0x07, // Length
		0x05, // Type (Endpoint)

		0x81, // Address
		0x03, // Attributes (interrupt transfers)
		0x08, 0x00, // Max packet size

		0x08, // Polling interval (frame counts)
	};

	static void guncon2_handle_control(
		USBDevice* dev, USBPacket* p, int request, int value, int index, int length, uint8_t* data)
	{
		GunCon2State* const us = USB_CONTAINER_OF(dev, GunCon2State, dev);

		// Apply configuration on the first control packet.
		// The ELF should be well and truely loaded by then.
		if (!us->auto_config_done && !us->custom_config)
		{
			us->AutoConfigure();
			us->auto_config_done = true;
		}

		DevCon.WriteLn("guncon2: req %04X val: %04X idx: %04X len: %d\n", request, value, index, length);
		if (usb_desc_handle_control(dev, p, request, value, index, length, data) >= 0)
			return;

		if (request == (ClassInterfaceOutRequest | 0x09))
		{
			us->param_x = static_cast<u16>(data[0]) | (static_cast<u16>(data[1]) << 8);
			us->param_y = static_cast<u16>(data[2]) | (static_cast<u16>(data[3]) << 8);
			us->param_mode = static_cast<u16>(data[4]) | (static_cast<u16>(data[5]) << 8);
			DevCon.WriteLn("GunCon2 Set Param %04X %d %d", us->param_mode, us->param_x, us->param_y);
			return;
		}

		p->status = USB_RET_STALL;
	}

	static void guncon2_handle_data(USBDevice* dev, USBPacket* p)
	{
		GunCon2State* const us = USB_CONTAINER_OF(dev, GunCon2State, dev);

		switch (p->pid)
		{
			case USB_TOKEN_IN:
			{
				if (p->ep->nr == 1)
				{
					const auto [pos_x, pos_y] = us->CalculatePosition();

					// Time Crisis games do a "calibration" by displaying a black frame for a single frame,
					// waiting for the gun to report (0, 0), and then computing an offset on the first non-zero
					// value. So, after the trigger is pulled, we wait for a few frames, then send the (0, 0)
					// report, then go back to normal values. To reduce error if the mouse is moving during
					// these frames (unlikely), we store the fire position and keep returning that.
					if (us->button_state & (1u << BID_RECALIBRATE) && us->calibration_timer == 0)
					{
						us->calibration_timer = GUNCON2_CALIBRATION_DELAY;
						us->calibration_pos_x = pos_x;
						us->calibration_pos_y = pos_y;
					}

					// Buttons are active low.
					GunCon2Out out;
					out.buttons = static_cast<u16>(~us->button_state) | (us->param_mode & GUNCON2_FLAG_PROGRESSIVE);
					out.pos_x = pos_x;
					out.pos_y = pos_y;

					if (us->calibration_timer > 0)
					{
						// Force trigger down while calibrating.
						out.buttons &= ~(1u << BID_TRIGGER);
						out.pos_x = us->calibration_pos_x;
						out.pos_y = us->calibration_pos_y;
						us->calibration_timer--;

						if (us->calibration_timer < GUNCON2_CALIBRATION_REPORT_DELAY)
						{
							out.pos_x = 0;
							out.pos_y = 0;
						}
					}
					else if (us->button_state & (1u << BID_SHOOT_OFFSCREEN))
					{
						// Offscreen shot - use 0,0.
						out.buttons &= ~(1u << BID_TRIGGER);
						out.pos_x = 0;
						out.pos_y = 0;
					}

					usb_packet_copy(p, &out, sizeof(out));
					break;
				}
			}
				[[fallthrough]];

			case USB_TOKEN_OUT:
			default:
			{
				Console.Error("Unhandled GunCon2 request pid=%d ep=%u", p->pid, p->ep->nr);
				p->status = USB_RET_STALL;
			}
			break;
		}
	}

	static void usb_hid_unrealize(USBDevice* dev)
	{
		GunCon2State* us = USB_CONTAINER_OF(dev, GunCon2State, dev);

		if (!us->cursor_path.empty())
			ImGuiManager::ClearSoftwareCursor(us->GetSoftwarePointerIndex());

		delete us;
	}

	GunCon2State::GunCon2State(u32 port_)
		: port(port_)
	{
#ifdef _WIN32
		serial_port = INVALID_HANDLE_VALUE;
#else
		serial_port = -1;
#endif
		auto_config_thread = new std::thread(&GunCon2State::ThreadAutoConfigure, this);
	}

	GunCon2State::~GunCon2State()
	{
		if (recoil_output_thread != nullptr)
		{
#ifdef _WIN32
			if (serial_port != INVALID_HANDLE_VALUE)
			{
				GunCon2State::SendComMessage("E", "");
				CloseHandle(serial_port);
				serial_port = INVALID_HANDLE_VALUE;
			}
#else
			if (serial_port != -1)
			{
				GunCon2State::SendComMessage("E", "");
				close(serial_port);
				serial_port = -1;
			}
#endif
			active_game = "";
			quit_thread = true;
			recoil_output_thread->join();
		}
		if (auto_config_thread != nullptr)
		{
			quit_thread = true;
			auto_config_thread->join();
		}


		Console.WriteLn("NIXX : GunCon2State -> Destroy");
	}

	void GunCon2State::ShowSerialWarning(const std::string_view message)
	{
		Console.Warning("(GunCon2) %.*s", static_cast<int>(message.size()), message.data());
		Host::AddIconOSDMessage(fmt::format("GunConSerialWarning{}", port), ICON_FA_TRIANGLE_EXCLAMATION,
			message, Host::OSD_WARNING_DURATION);
	}

	bool GunCon2State::SendComMessage(const std::string& message, const std::string& end_line)
	{
		bool success = false;
#ifdef _WIN32
		if (serial_port != INVALID_HANDLE_VALUE)
		{
			DWORD bytes_written = 0;
			const std::string message_with_crlf = message + end_line;
			const DWORD message_length = static_cast<DWORD>(message_with_crlf.length());
			success = (WriteFile(serial_port, message_with_crlf.c_str(), message_length, &bytes_written, nullptr) &&
					   bytes_written == message_length);
			FlushFileBuffers(serial_port);
		}
#else
		if (serial_port != -1)
		{
			const std::string message_with_crlf = message + end_line;
			const ssize_t bytes_written = write(serial_port, message_with_crlf.c_str(), message_with_crlf.length());
			success = (bytes_written == static_cast<ssize_t>(message_with_crlf.length()));
			fsync(serial_port);
		}
#endif
		if (!success && IsSerialPortValid() && !serial_write_error_reported)
		{
			serial_write_error_reported = true;
			ShowSerialWarning(fmt::format("GunCon {} could not write to {}. Recoil and rumble feedback may not work.",
				port + 1, active_serial_port_name.empty() ? "its serial port" : active_serial_port_name));
		}
		return success;
	}

	void GunCon2State::ThreadOutputs()
	{
		thread_output_loaded = true;
		Console.WriteLn("THREAD : Thread Start");

		if (active_game != "")
		{
			bool valid_com = false;
			const std::optional<std::string> serial_port_name = ResolveSerialPort(serial_port_setting, port);
			if (serial_port_name.has_value())
			{
				valid_com = true;
#ifdef _WIN32
				const std::string open_name =
					serial_port_name->starts_with("COM") && serial_port_name->size() > 4 ?
						fmt::format("\\\\.\\{}", *serial_port_name) :
						*serial_port_name;
				serial_port = CreateFileA(open_name.c_str(), GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
					FILE_ATTRIBUTE_NORMAL, NULL);

				if (serial_port == INVALID_HANDLE_VALUE)
				{
					valid_com = false;
				}
				if (valid_com)
				{
					DCB dcb_serial_params = {0};
					dcb_serial_params.DCBlength = sizeof(dcb_serial_params);

					if (!GetCommState(serial_port, &dcb_serial_params))
					{
						valid_com = false;
					}
					if (valid_com)
					{
						dcb_serial_params.BaudRate = 9600;
						dcb_serial_params.ByteSize = 8;
						dcb_serial_params.StopBits = ONESTOPBIT;
						dcb_serial_params.Parity = NOPARITY;

						// Set timeouts for non-blocking behavior
						COMMTIMEOUTS timeouts = {0};
						timeouts.WriteTotalTimeoutConstant = 50; // 50ms max write timeout
						timeouts.WriteTotalTimeoutMultiplier = 0; // No per-byte timeout
						SetCommTimeouts(serial_port, &timeouts);
					}
					if (!SetCommState(serial_port, &dcb_serial_params))
					{
						valid_com = false;
					}
				}
#else
				serial_port = open(serial_port_name->c_str(), O_WRONLY | O_NOCTTY | O_NONBLOCK);

				if (serial_port == -1)
				{
					valid_com = false;
				}
				else
				{
					struct termios tty;
					memset(&tty, 0, sizeof(tty));

					if (tcgetattr(serial_port, &tty) != 0)
					{
						valid_com = false;
					}
					else
					{
						// Set baud rate to 9600
						cfsetospeed(&tty, B9600);
						cfsetispeed(&tty, B9600);

						// 8 bits, no parity, 1 stop bit
						tty.c_cflag &= ~PARENB; // No parity
						tty.c_cflag &= ~CSTOPB; // 1 stop bit
						tty.c_cflag &= ~CSIZE;
						tty.c_cflag |= CS8; // 8 data bits
						tty.c_cflag &= ~CRTSCTS; // No hardware flow control
						tty.c_cflag |= CREAD | CLOCAL; // Enable reading and ignore ctrl lines

						// Raw mode
						tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHONL | ISIG | IEXTEN);
						tty.c_iflag &= ~(IXON | IXOFF | IXANY | IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
						tty.c_oflag &= ~OPOST;

						// Set timeouts for non-blocking behavior
						tty.c_cc[VTIME] = 5; // 0.5 second timeout
						tty.c_cc[VMIN] = 0; // Return immediately

						if (tcsetattr(serial_port, TCSANOW, &tty) != 0)
						{
							valid_com = false;
						}
					}
				}
#endif
			}
			if (valid_com)
			{
				active_serial_port_name = *serial_port_name;
				serial_write_error_reported = false;
				Host::RemoveKeyedOSDMessage(fmt::format("GunConSerialWarning{}", port));
				Console.WriteLn("(GunCon2) Gun %u using serial feedback on %s.", port + 1, serial_port_name->c_str());
				GunCon2State::SendComMessage("S0x");
				GunCon2State::SendComMessage("S1x");
				if (gamepad_mode)
				{
					GunCon2State::SendComMessage("M0x1");
				}
			}
			else
			{
				if (serial_port_setting >= 0)
				{
					const std::string target =
						serial_port_name.has_value() ? *serial_port_name : fmt::format("automatic port {}", port + 1);
					ShowSerialWarning(fmt::format(
						"GunCon {} could not open {}. Check the connection and close other serial applications.",
						port + 1, target));
				}
#ifdef _WIN32
				if (serial_port != INVALID_HANDLE_VALUE)
					CloseHandle(serial_port);
				serial_port = INVALID_HANDLE_VALUE;
#else
				if (serial_port != -1)
					close(serial_port);
				serial_port = -1;
#endif
			}
		}


		while (VMManager::HasValidVM() && active_game != "" && !quit_thread)
		{
			std::chrono::microseconds::rep timestamp =
				std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
					.count();

			// Use the new game detection module
			GameDetectionState detection_state = {};
			detection_state.active_game = active_game;
			detection_state.port = port;
			detection_state.last_ammo = last_ammo;
			detection_state.last_life = last_life;
			detection_state.last_weapon = last_weapon;
			detection_state.last_charged = last_charged;
			detection_state.last_other1 = last_other1;
			detection_state.last_other2 = last_other2;
			detection_state.trigger_is_active = trigger_is_active.load(std::memory_order_relaxed);
			detection_state.trigger_last_press = trigger_last_press.load(std::memory_order_relaxed);
			detection_state.trigger_last_release = trigger_last_release.load(std::memory_order_relaxed);
			detection_state.twoplayer_fix = twoplayer_fix;
			detection_state.full_auto_active = full_auto_active;

			GameDetectionResult detection_result = DetectGameEvents(detection_state, timestamp);

			// Copy the modified state back to our instance variables
			last_ammo = detection_state.last_ammo;
			last_life = detection_state.last_life;
			last_weapon = detection_state.last_weapon;
			last_charged = detection_state.last_charged;
			last_other1 = detection_state.last_other1;
			last_other2 = detection_state.last_other2;
			twoplayer_fix = detection_state.twoplayer_fix;
			full_auto_active = detection_state.full_auto_active;

			// Extract the signal for processing
			std::string output_signal = detection_result.output_signal;

			bool do_recoil = false;
			if (output_signal != "")
			{
				if (port == 0)
				{
					Console.WriteLn("GUN A : %s", output_signal.c_str());
				}
				if (port == 1)
				{
					Console.WriteLn("GUN B : %s", output_signal.c_str());
				}
				if (output_signal == "gunshot")
				{
					next_gun_shot = 0;
					full_auto_delay = 0;
					queue_size_gunshot = 0;
					multishot_delay = 0;
					do_recoil = true;
				}
				if (output_signal.starts_with("multishot:"))
				{
					size_t first_colon_pos = output_signal.find(':');
					size_t second_colon_pos = output_signal.find(':', first_colon_pos + 1);

					std::string num1_str = output_signal.substr(first_colon_pos + 1, second_colon_pos - first_colon_pos - 1);
					std::string num2_str = output_signal.substr(second_colon_pos + 1);

					int number_of_shot = std::stoi(num1_str);
					int delay_shot = std::stoi(num2_str);

					Console.WriteLn("MULTISHOT DELAY = %d", delay_shot);


					delay_shot *= 1000;
					next_gun_shot = timestamp + delay_shot;
					full_auto_delay = 0;
					queue_size_gunshot = number_of_shot - 1;
					multishot_delay = delay_shot;
					do_recoil = true;
				}
				if (output_signal.starts_with("machinegun_on:"))
				{
					size_t colon_pos = output_signal.find(':');
					std::string value_str = output_signal.substr(colon_pos + 1);
					int delay_shot = std::stoi(value_str);

					delay_shot *= 1000;
					next_gun_shot = timestamp + delay_shot;
					full_auto_delay = delay_shot;
					queue_size_gunshot = 0;
					delay_shot = 0;
					do_recoil = true;
				}
				if (output_signal == "machinegun_off")
				{
					next_gun_shot = 0;
					full_auto_delay = 0;
					if (IsSerialPortValid())
						GunCon2State::SendComMessage("F0x0x");
					recoil_signal_pending = false;
				}
			}
			else
			{
				if (queue_size_gunshot > 0 && timestamp > next_gun_shot)
				{
					do_recoil = true;
					queue_size_gunshot--;
					if (queue_size_gunshot > 0)
					{
						next_gun_shot = timestamp + multishot_delay;
					}
				}
				if (full_auto_delay > 0 && timestamp > next_gun_shot)
				{
					do_recoil = true;
					next_gun_shot = timestamp + full_auto_delay;
				}
			}

			if (detection_result.life_lost)
			{
				Console.WriteLn("GUN %c : DAMAGE", port == 0 ? 'A' : 'B');
				if (IsSerialPortValid())
				{
					// Rumble strength is required by GUN4IR; omitting it can result in no motor output.
					GunCon2State::SendComMessage("F1x1x255x");
					next_life_signal_off = timestamp + 500000;
					life_signal_pending = true;
				}
			}

			// Handle delayed life signal off
			if (life_signal_pending && timestamp > next_life_signal_off)
			{
				if (IsSerialPortValid())
				{
					GunCon2State::SendComMessage("F1x0x");
				}
				life_signal_pending = false;
			}

			// Handle delayed recoil signal off
			if (recoil_signal_pending && timestamp > next_recoil_signal_off)
			{
				if (IsSerialPortValid())
				{
					GunCon2State::SendComMessage("F0x0x"); // Turn recoil OFF
				}
				recoil_signal_pending = false;
			}

			if (do_recoil)
			{
				long long diff_gunshot = timestamp - last_gun_shot;
				last_gun_shot = timestamp;

				if (port == 0)
				{
					Console.WriteLn("GUN A : SHOT (%lld)", diff_gunshot);
				}
				if (port == 1)
				{
					Console.WriteLn("GUN B : SHOT (%lld)", diff_gunshot);
				}

				if (full_auto_delay > 0)
				{
					// Machine gun - enqueue one pulse at each detected firing interval.
					GunCon2State::SendComMessage("F0x2x1x");
				}
				else if (queue_size_gunshot > 0)
				{
					// Multishot - use pulse system
					GunCon2State::SendComMessage("F0x2x3x");
				}
				else
				{
					// Single shot - use toggle system with 45ms delay
					if (IsSerialPortValid())
					{
						GunCon2State::SendComMessage("F0x1x"); // Turn recoil ON
						next_recoil_signal_off = timestamp + 45000; // 45ms in microseconds
						recoil_signal_pending = true;
					}
				}

				//// Send ammo count
				//if (serial_port != INVALID_HANDLE_VALUE)
				//{
				//	Console.WriteLn(fmt::format("GUN {} : AMMO ({})", port + 1, last_ammo));
				//	GunCon2State::SendComMessage(fmt::format("FDAx{}xxx", last_ammo));
				//}
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
#ifdef _WIN32
		if (serial_port != INVALID_HANDLE_VALUE)
		{
			GunCon2State::SendComMessage("E", "");
			CloseHandle(serial_port);
		}
		serial_port = INVALID_HANDLE_VALUE;
#else
		if (serial_port != -1)
		{
			GunCon2State::SendComMessage("E", "");
			close(serial_port);
		}
		serial_port = -1;
#endif
		Console.WriteLn("THREAD : Thread stop");
	}

	void GunCon2State::ThreadAutoConfigure()
	{

		std::vector<std::string> liste_ids_recoil = {
			"SLES-50930", "SLES-51095", "SLUS-20485", "SLUS-20389", "SLPM-65139",
			"SLES-52620", "SLES-51289", "SLPS-25165", "SLUS-20492", "SLES-50650",
			"SLUS-20669", "SLES-51617", "SLUS-20619", "SCES-50300", "SLUS-20219",
			"SCES-51844", "SLUS-20645", "SLUS-20927", "SLUS-20221", "SLES-51229"};


		int i = 0;
		while (thread_output_loaded == false)
		{
			if (quit_thread)
				return;
			if (i < 50)
			{
				i++;
			}
			else
			{
				Console.WriteLn("ThreadLOAD INIT");
				std::string serial = VMManager::GetDiscSerial();
				if (serial != "" && active_game == "" && VMManager::HasValidVM())
				{
					active_game = serial;

					bool id_present = false;
					for (const std::string& id : liste_ids_recoil)
					{
						if (id == active_game)
						{
							id_present = true;
							break;
						}
					}
					if (id_present == false)
						return;

					recoil_output_thread = new std::thread(&GunCon2State::ThreadOutputs, this);
					//AutoConfigure();
					return;
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	}

	void GunCon2State::AutoConfigure()
	{
		const std::string serial = VMManager::GetDiscSerial();
		for (const GameConfig& gc : s_game_config)
		{
			if (serial != gc.serial)
				continue;

			Console.WriteLn(fmt::format("(GunCon2) Using automatic config for '{}'", serial));
			Console.WriteLn(fmt::format("  Scale: {}x{}", gc.scale_x / 100.0f, gc.scale_y / 100.0f));
			Console.WriteLn(fmt::format("  Center Position: {}x{}", gc.center_x, gc.center_y));
			Console.WriteLn(fmt::format("  Screen Size: {}x{}", gc.screen_width, gc.screen_height));

			scale_x = gc.scale_x / 100.0f;
			scale_y = gc.scale_y / 100.0f;
			center_x = static_cast<float>(gc.center_x);
			center_y = static_cast<float>(gc.center_y);
			screen_width = gc.screen_width;
			screen_height = gc.screen_height;
			return;
		}

		Console.Warning(fmt::format("(GunCon2) No automatic config found for '{}'.", serial));
	}

	std::tuple<s16, s16> GunCon2State::CalculatePosition()
	{
		float pointer_x, pointer_y;
		const auto& [window_x, window_y] =
			(has_relative_binds) ? GetAbsolutePositionFromRelativeAxes() :
								   InputManager::GetPointerAbsolutePosition(pointer_index);
		GSTranslateWindowToDisplayCoordinates(window_x, window_y, &pointer_x, &pointer_y);

		// Apply game-specific two-player aiming corrections for Time Crisis 2 and 3.
		if (twoplayer_fix)
		{
			const float original_pointer_y = pointer_y;
			const auto apply_adjustment = [&](float min_x, float max_x, float min_y, float max_y, float curve_scale) {
				pointer_x = (pointer_x * (max_x - min_x)) + min_x;
				pointer_y = (pointer_y * (max_y - min_y)) + min_y;
				if (pointer_y > 0.0f && pointer_y < 1.0f)
				{
					pointer_y +=
						((-0.04f * (original_pointer_y * original_pointer_y)) + (0.04f * original_pointer_y)) * curve_scale;
				}
			};

			if (active_game == "SLUS-20219")
			{
				if (port == 0)
					apply_adjustment(0.035f, 0.9035f, 0.25f, 0.69f, 2.7f);
				else if (port == 1)
					apply_adjustment(0.093f, 0.970f, 0.247f, 0.690f, 2.7f);
			}
			else if (active_game == "SCES-50300")
			{
				if (port == 0)
					apply_adjustment(0.02798462f, 0.90f, 0.25f, 0.6950202f, 2.7f);
				else if (port == 1)
					apply_adjustment(0.093f, 0.970f, 0.247f, 0.690f, 2.7f);
			}
			else if (active_game == "SCES-51844")
			{
				if (port == 0)
					apply_adjustment(0.035f, 0.9035f, 0.247f, 0.690f, 3.0f);
				else if (port == 1)
					apply_adjustment(0.095f, 0.97f, 0.247f, 0.690f, 3.0f);
			}
			else if (active_game == "SLUS-20645")
			{
				if (port == 0)
					apply_adjustment(0.035f, 0.9035f, 0.247f, 0.690f, 3.1f);
				else if (port == 1)
					apply_adjustment(0.095f, 0.97f, 0.247f, 0.690f, 3.1f);
			}
		}
		s16 pos_x, pos_y;
		if (pointer_x < 0.0f || pointer_y < 0.0f)
		{
			// off-screen
			pos_x = 0;
			pos_y = 0;
		}
		else
		{
			// scale to internal coordinate system and center
			float fx = (pointer_x * static_cast<float>(screen_width)) - static_cast<float>(screen_width / 2u);
			float fy = (pointer_y * static_cast<float>(screen_height)) - static_cast<float>(screen_height / 2u);

			// apply curvature scale
			fx *= scale_x;
			fy *= scale_y;

			// and re-center based on game center
			s32 x = static_cast<s32>(std::round(fx + center_x));
			s32 y = static_cast<s32>(std::round(fy + center_y));

			// apply game-configured offset
			if (param_mode & GUNCON2_FLAG_PROGRESSIVE)
			{
				x -= param_x / 2;
				y -= param_y / 2;
			}
			else
			{
				x -= param_x;
				y -= param_y;
			}

			// 0,0 is reserved for offscreen, so ensure we don't send that
			pos_x = static_cast<s16>((std::max)(x, 1));
			pos_y = static_cast<s16>((std::max)(y, 1));
		}

		return std::tie(pos_x, pos_y);
	}

	std::pair<float, float> GunCon2State::GetAbsolutePositionFromRelativeAxes() const
	{
		const float screen_rel_x = (((relative_pos[1] > 0.0f) ? relative_pos[1] : -relative_pos[0]) + 1.0f) * 0.5f;
		const float screen_rel_y = (((relative_pos[3] > 0.0f) ? relative_pos[3] : -relative_pos[2]) + 1.0f) * 0.5f;
		return std::make_pair(
			screen_rel_x * ImGuiManager::GetWindowWidth(), screen_rel_y * ImGuiManager::GetWindowHeight());
	}

	u32 GunCon2State::GetSoftwarePointerIndex() const
	{
		return has_relative_binds ? (InputManager::MAX_POINTER_DEVICES + port) : pointer_index;
	}

	void GunCon2State::UpdateSoftwarePointerPosition()
	{
		if (cursor_path.empty())
			return;

		const auto& [window_x, window_y] = GetAbsolutePositionFromRelativeAxes();
		ImGuiManager::SetSoftwareCursorPosition(GetSoftwarePointerIndex(), window_x, window_y);
	}

	const char* GunCon2Device::Name() const
	{
		return TRANSLATE_NOOP("USB", "GunCon 2");
	}

	const char* GunCon2Device::TypeName() const
	{
		return "guncon2";
	}

	const char* GunCon2Device::IconName() const
	{
		return ICON_PF_GUNCON2;
	}

	USBDevice* GunCon2Device::CreateDevice(SettingsInterface& si, u32 port, u32 subtype) const
	{
		GunCon2State* s = new GunCon2State(port);
		s->desc.full = &s->desc_dev;
		s->desc.str = desc_strings;

		if (usb_desc_parse_dev(guncon2_dev_desc, sizeof(guncon2_dev_desc), s->desc, s->desc_dev) < 0)
			goto fail;
		if (usb_desc_parse_config(guncon2_config_desc, sizeof(guncon2_config_desc), s->desc_dev) < 0)
			goto fail;

		s->dev.speed = USB_SPEED_FULL;
		s->dev.klass.handle_attach = usb_desc_attach;
		s->dev.klass.handle_control = guncon2_handle_control;
		s->dev.klass.handle_data = guncon2_handle_data;
		s->dev.klass.unrealize = usb_hid_unrealize;
		s->dev.klass.usb_desc = &s->desc;
		s->dev.klass.product_desc = s->desc.str[2];

		usb_desc_init(&s->dev);
		usb_ep_init(&s->dev);

		UpdateSettings(&s->dev, si);

		return &s->dev;
	fail:
		usb_hid_unrealize(&s->dev);
		return nullptr;
	}

	void GunCon2Device::UpdateSettings(USBDevice* dev, SettingsInterface& si) const
	{
		GunCon2State* s = USB_CONTAINER_OF(dev, GunCon2State, dev);

		s->custom_config = USB::GetConfigBool(si, s->port, TypeName(), "custom_config", false);
		s->serial_port_setting = USB::GetConfigInt(si, s->port, TypeName(), "lightgun_port", 0);
		s->gamepad_mode = USB::GetConfigBool(si, s->port, TypeName(), "gamepad_mode", false);

		// Don't override auto config if we've set it.
		if (!s->auto_config_done || s->custom_config)
		{
			s->screen_width = USB::GetConfigInt(si, s->port, TypeName(), "screen_width", DEFAULT_SCREEN_WIDTH);
			s->screen_height = USB::GetConfigInt(si, s->port, TypeName(), "screen_height", DEFAULT_SCREEN_HEIGHT);
			s->center_x = USB::GetConfigFloat(si, s->port, TypeName(), "center_x", DEFAULT_CENTER_X);
			s->center_y = USB::GetConfigFloat(si, s->port, TypeName(), "center_y", DEFAULT_CENTER_Y);
			s->scale_x = USB::GetConfigFloat(si, s->port, TypeName(), "scale_x", DEFAULT_SCALE_X) / 100.0f;
			s->scale_y = USB::GetConfigFloat(si, s->port, TypeName(), "scale_y", DEFAULT_SCALE_Y) / 100.0f;
		}

		// Pointer settings. Pointer-0 is the combined system cursor; ManyMouse devices begin at Pointer-1.
		const s32 prev_pointer_index = s->GetSoftwarePointerIndex();
		const std::string pointer_binding = USB::GetConfigString(si, s->port, TypeName(), "Pointer", "");
		const std::optional<u32> configured_pointer = InputManager::GetIndexFromPointerBinding(pointer_binding);
		if (configured_pointer.has_value())
		{
			s->pointer_index = configured_pointer.value();
		}
		else if (s->port < InputManager::GetManyMouseDeviceCount())
		{
			s->pointer_index = s->port + InputManager::MANYMOUSE_POINTER_OFFSET;
		}
		else
		{
			s->pointer_index = 0;
		}

		std::string cursor_path(USB::GetConfigString(si, s->port, TypeName(), "cursor_path"));
		const float cursor_scale = USB::GetConfigFloat(si, s->port, TypeName(), "cursor_scale", 1.0f);
		u32 cursor_color = 0xFFFFFF;
		if (std::string cursor_color_str(USB::GetConfigString(si, s->port, TypeName(), "cursor_color")); !cursor_color_str.empty())
		{
			// Strip the leading hash, if it's a CSS style colour.
			const std::optional<u32> cursor_color_opt(
				StringUtil::FromChars<u32>(cursor_color_str[0] == '#' ?
											   std::string_view(cursor_color_str).substr(1) :
											   std::string_view(cursor_color_str),
					16));
			if (cursor_color_opt.has_value())
				cursor_color = cursor_color_opt.value();
		}

		s->has_relative_binds = (USB::ConfigKeyExists(si, s->port, TypeName(), "RelativeLeft") ||
								 USB::ConfigKeyExists(si, s->port, TypeName(), "RelativeRight") ||
								 USB::ConfigKeyExists(si, s->port, TypeName(), "RelativeUp") ||
								 USB::ConfigKeyExists(si, s->port, TypeName(), "RelativeDown"));

		const s32 new_pointer_index = s->GetSoftwarePointerIndex();

		if (prev_pointer_index != new_pointer_index || s->cursor_path != cursor_path ||
			s->cursor_scale != cursor_scale || s->cursor_color != cursor_color)
		{
			if (prev_pointer_index != new_pointer_index)
				ImGuiManager::ClearSoftwareCursor(prev_pointer_index);

			// Pointer changed, so need to update software cursor.
			const bool had_software_cursor = !s->cursor_path.empty();
			s->cursor_path = std::move(cursor_path);
			s->cursor_scale = cursor_scale;
			s->cursor_color = cursor_color;
			if (!s->cursor_path.empty())
			{
				ImGuiManager::SetSoftwareCursor(new_pointer_index, s->cursor_path, s->cursor_scale, s->cursor_color);
				s->UpdateSoftwarePointerPosition();
			}
			else if (had_software_cursor)
			{
				ImGuiManager::ClearSoftwareCursor(new_pointer_index);
			}
		}
	}

	float GunCon2Device::GetBindingValue(const USBDevice* dev, u32 bind_index) const
	{
		GunCon2State* s = USB_CONTAINER_OF(dev, GunCon2State, dev);

		const u32 bit = 1u << bind_index;
		return ((s->button_state & bit) != 0) ? 1.0f : 0.0f;
	}

	void GunCon2Device::SetBindingValue(USBDevice* dev, u32 bind_index, float value) const
	{
		GunCon2State* s = USB_CONTAINER_OF(dev, GunCon2State, dev);

		if (bind_index < BID_RELATIVE_LEFT)
		{
			const u32 bit = 1u << bind_index;
			if (value >= 0.5f)
				s->button_state |= bit;
			else
				s->button_state &= ~bit;

			if (bind_index == BID_TRIGGER || bind_index == BID_SHOOT_OFFSCREEN)
			{
				const bool trigger_active =
					(s->button_state & ((1u << BID_TRIGGER) | (1u << BID_SHOOT_OFFSCREEN))) != 0;
				const bool was_active = s->trigger_is_active.exchange(trigger_active, std::memory_order_relaxed);
				if (trigger_active != was_active)
				{
					const std::chrono::microseconds::rep timestamp =
						std::chrono::duration_cast<std::chrono::microseconds>(
							std::chrono::steady_clock::now().time_since_epoch())
							.count();
					if (trigger_active)
						s->trigger_last_press.store(timestamp, std::memory_order_relaxed);
					else
						s->trigger_last_release.store(timestamp, std::memory_order_relaxed);
				}
			}
		}
		else if (bind_index <= BID_RELATIVE_DOWN)
		{
			const u32 rel_index = bind_index - BID_RELATIVE_LEFT;
			if (s->relative_pos[rel_index] != value)
			{
				s->relative_pos[rel_index] = value;
				s->UpdateSoftwarePointerPosition();
			}
		}
	}

	std::span<const InputBindingInfo> GunCon2Device::Bindings(u32 subtype) const
	{
		static constexpr const InputBindingInfo bindings[] = {
			{"Pointer", TRANSLATE_NOOP("USB", "Pointer/Aiming"), nullptr, InputBindingInfo::Type::Device, BID_POINTER_X,
				GenericInputBinding::Unknown},
			{"Up", TRANSLATE_NOOP("USB", "D-Pad Up"), nullptr, InputBindingInfo::Type::Button, BID_DPAD_UP, GenericInputBinding::DPadUp},
			{"Down", TRANSLATE_NOOP("USB", "D-Pad Down"), nullptr, InputBindingInfo::Type::Button, BID_DPAD_DOWN, GenericInputBinding::DPadDown},
			{"Left", TRANSLATE_NOOP("USB", "D-Pad Left"), nullptr, InputBindingInfo::Type::Button, BID_DPAD_LEFT, GenericInputBinding::DPadLeft},
			{"Right", TRANSLATE_NOOP("USB", "D-Pad Right"), nullptr, InputBindingInfo::Type::Button, BID_DPAD_RIGHT,
				GenericInputBinding::DPadRight},
			{"Trigger", TRANSLATE_NOOP("USB", "Trigger"), nullptr, InputBindingInfo::Type::Button, BID_TRIGGER, GenericInputBinding::R2},
			{"ShootOffscreen", TRANSLATE_NOOP("USB", "Shoot Offscreen"), nullptr, InputBindingInfo::Type::Button, BID_SHOOT_OFFSCREEN,
				GenericInputBinding::R1},
			{"Recalibrate", TRANSLATE_NOOP("USB", "Calibration Shot"), nullptr, InputBindingInfo::Type::Button, BID_RECALIBRATE,
				GenericInputBinding::Unknown},
			{"A", TRANSLATE_NOOP("USB", "A"), nullptr, InputBindingInfo::Type::Button, BID_A, GenericInputBinding::Cross},
			{"B", TRANSLATE_NOOP("USB", "B"), nullptr, InputBindingInfo::Type::Button, BID_B, GenericInputBinding::Circle},
			{"C", TRANSLATE_NOOP("USB", "C"), nullptr, InputBindingInfo::Type::Button, BID_C, GenericInputBinding::Triangle},
			{"Select", TRANSLATE_NOOP("USB", "Select"), nullptr, InputBindingInfo::Type::Button, BID_SELECT, GenericInputBinding::Select},
			{"Start", TRANSLATE_NOOP("USB", "Start"), nullptr, InputBindingInfo::Type::Button, BID_START, GenericInputBinding::Start},
			{"RelativeLeft", TRANSLATE_NOOP("USB", "Relative Left"), nullptr, InputBindingInfo::Type::HalfAxis, BID_RELATIVE_LEFT, GenericInputBinding::Unknown},
			{"RelativeRight", TRANSLATE_NOOP("USB", "Relative Right"), nullptr, InputBindingInfo::Type::HalfAxis, BID_RELATIVE_RIGHT, GenericInputBinding::Unknown},
			{"RelativeUp", TRANSLATE_NOOP("USB", "Relative Up"), nullptr, InputBindingInfo::Type::HalfAxis, BID_RELATIVE_UP, GenericInputBinding::Unknown},
			{"RelativeDown", TRANSLATE_NOOP("USB", "Relative Down"), nullptr, InputBindingInfo::Type::HalfAxis, BID_RELATIVE_DOWN, GenericInputBinding::Unknown},
		};

		return bindings;
	}

	std::span<const SettingInfo> GunCon2Device::Settings(u32 subtype) const
	{
		static constexpr const SettingInfo info[] = {
			{SettingInfo::Type::Path, "cursor_path", TRANSLATE_NOOP("USB", "Cursor Path"),
				TRANSLATE_NOOP("USB", "Sets the crosshair image that this lightgun will use. Setting a crosshair image "
									  "will disable the system cursor."),
				""},
			{SettingInfo::Type::Float, "cursor_scale", TRANSLATE_NOOP("USB", "Cursor Scale"),
				TRANSLATE_NOOP("USB", "Scales the crosshair image set above."), "1", "0.01", "10", "0.01", TRANSLATE_NOOP("USB", "%.0f%%"),
				nullptr, nullptr, 100.0f},
			{SettingInfo::Type::String, "cursor_color", TRANSLATE_NOOP("USB", "Cursor Color"),
				TRANSLATE_NOOP("USB", "Applies a color to the chosen crosshair images, can be used for multiple "
									  "players. Specify in HTML/CSS format (e.g. #aabbcc)"),
				"#ffffff"},
			{SettingInfo::Type::Integer, "lightgun_port", TRANSLATE_NOOP("USB", "Lightgun COM port"),
				TRANSLATE_NOOP("USB",
					"Serial feedback port. Automatic assigns the first detected port to GunCon 1 and the second to "
					"GunCon 2. Use -1 to disable, 0 for automatic, or a positive COM port number to override."),
				"0", "-1", "99", "1", TRANSLATE_NOOP("USB", "%d"),
				nullptr, nullptr, 1.0f},
			{
				SettingInfo::Type::Boolean,
				"gamepad_mode",
				TRANSLATE_NOOP("USB", "Gamepad Mode"),
				TRANSLATE_NOOP("USB", "If enabled switches the lightgun to gamepad mode"),
			},
			{SettingInfo::Type::Boolean, "custom_config", TRANSLATE_NOOP("USB", "Manual Screen Configuration"),
				TRANSLATE_NOOP("USB",
					"Forces the use of the screen parameters below, instead of automatic parameters if available."),
				"false"},
			{SettingInfo::Type::Float, "scale_x", TRANSLATE_NOOP("USB", "X Scale (Sensitivity)"),
				TRANSLATE_NOOP("USB", "Scales the position to simulate CRT curvature."), "100", "0", "200", "0.1",
				TRANSLATE_NOOP("USB", "%.2f%%"), nullptr, nullptr, 1.0f},
			{SettingInfo::Type::Float, "scale_y", TRANSLATE_NOOP("USB", "Y Scale (Sensitivity)"),
				TRANSLATE_NOOP("USB", "Scales the position to simulate CRT curvature."), "100", "0", "200", "0.1",
				TRANSLATE_NOOP("USB", "%.2f%%"), nullptr, nullptr, 1.0f},
			{SettingInfo::Type::Float, "center_x", TRANSLATE_NOOP("USB", "Center X"),
				TRANSLATE_NOOP("USB", "Sets the horizontal center position of the simulated screen."), "320", "0",
				"1024", "1", TRANSLATE_NOOP("USB", "%.0fpx"), nullptr, nullptr, 1.0f},
			{SettingInfo::Type::Float, "center_y", TRANSLATE_NOOP("USB", "Center Y"),
				TRANSLATE_NOOP("USB", "Sets the vertical center position of the simulated screen."), "120", "0", "1024",
				"1", TRANSLATE_NOOP("USB", "%.0fpx"), nullptr, nullptr, 1.0f},
			{SettingInfo::Type::Integer, "screen_width", TRANSLATE_NOOP("USB", "Screen Width"),
				TRANSLATE_NOOP("USB", "Sets the width of the simulated screen."), "640", "1", "1024", "1", TRANSLATE_NOOP("USB", "%dpx"),
				nullptr, nullptr, 1.0f},
			{SettingInfo::Type::Integer, "screen_height", TRANSLATE_NOOP("USB", "Screen Height"),
				TRANSLATE_NOOP("USB", "Sets the height of the simulated screen."), "240", "1", "1024", "1", TRANSLATE_NOOP("USB", "%dpx"),
				nullptr, nullptr, 1.0f},
		};
		return info;
	}

	bool GunCon2Device::Freeze(USBDevice* dev, StateWrapper& sw) const
	{
		GunCon2State* s = USB_CONTAINER_OF(dev, GunCon2State, dev);

		if (!sw.DoMarker("GunCon2Device"))
			return false;

		sw.Do(&s->param_x);
		sw.Do(&s->param_y);
		sw.Do(&s->param_mode);
		sw.Do(&s->calibration_timer);
		sw.Do(&s->calibration_pos_x);
		sw.Do(&s->calibration_pos_y);
		sw.Do(&s->auto_config_done);

		float scale_x = s->scale_x;
		float scale_y = s->scale_y;
		float center_x = s->center_x;
		float center_y = s->center_y;
		u32 screen_width = s->screen_width;
		u32 screen_height = s->screen_height;
		sw.Do(&scale_x);
		sw.Do(&scale_y);
		sw.Do(&center_x);
		sw.Do(&center_y);
		sw.Do(&screen_width);
		sw.Do(&screen_height);

		// Only save automatic settings to state.
		if (sw.IsReading() && !s->custom_config && s->auto_config_done)
		{
			s->scale_x = scale_x;
			s->scale_y = scale_y;
			s->center_x = center_x;
			s->center_y = center_y;
			s->screen_width = screen_width;
			s->screen_height = screen_height;
		}

		return !sw.HasError();
	}
} // namespace usb_lightgun
