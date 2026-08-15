#pragma once

// Initial platform abstraction for the AutoHotkey Linux port.
//
// This header is intentionally not included by the existing Windows source yet.
// It defines the seam where Win32-specific code will be replaced by Linux/X11
// implementations.  Keep this interface small and grow it as modules are ported.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ahk_linux
{
	enum class Modifier : uint32_t
	{
		None = 0,
		Shift = 1u << 0,
		Ctrl = 1u << 1,
		Alt = 1u << 2,
		Super = 1u << 3,
	};

	inline Modifier operator|(Modifier a, Modifier b)
	{
		return static_cast<Modifier>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
	}

	inline Modifier operator&(Modifier a, Modifier b)
	{
		return static_cast<Modifier>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
	}

	struct KeyInfo
	{
		uint32_t keycode = 0;    // X11 keysym/keycode or evdev code.
		uint32_t scancode = 0;   // Hardware scancode when available.
		uint32_t unicode = 0;    // Unicode code point for text input.
		Modifier modifiers = Modifier::None;
	};

	struct WindowInfo
	{
		uint64_t id = 0;         // X11 Window (XID) or Wayland surface handle.
		std::string title;
		std::string class_name;
		bool visible = false;
	};

	class Platform
	{
	public:
		virtual ~Platform() = default;

		virtual bool Initialize() = 0;
		virtual void Shutdown() = 0;
		virtual int RunEventLoop() = 0;
		virtual void PostQuit() = 0;

		// Keyboard / mouse simulation.
		virtual bool SendKey(const KeyInfo& key, bool down) = 0;
		virtual bool SendMouseMove(int x, int y) = 0;
		virtual bool SendMouseButton(uint32_t button, bool down) = 0;
		virtual bool SendMouseWheel(int delta) = 0;

		// Global hotkeys.
		using HotkeyCallback = std::function<void(const KeyInfo&)>;
		virtual bool RegisterHotkey(const KeyInfo& key, HotkeyCallback callback) = 0;
		virtual bool UnregisterHotkey(const KeyInfo& key) = 0;

		// Clipboard.
		virtual std::string GetClipboardText() = 0;
		virtual void SetClipboardText(const std::string& text) = 0;

		// Window management.
		virtual std::vector<WindowInfo> EnumWindows() = 0;
		virtual bool SetActiveWindow(uint64_t id) = 0;
		virtual bool GetActiveWindow(WindowInfo& info) = 0;
	};

	// Creates an X11 backend.  Returns nullptr until the backend is implemented.
	Platform* CreateX11Platform();
}
