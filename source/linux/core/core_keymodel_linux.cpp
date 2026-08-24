// Layout-aware three-layer key model (check_detail0824 M1-K).
#include "../../stdafx.h"
#include "core_keymodel_linux.h"
#include <X11/XKBlib.h>
#include <xcb/xcb.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon-x11.h>
#include <cstring>
#include <string>

namespace {

xcb_connection_t *sXcb = nullptr;
xkb_context *sContext = nullptr;
xkb_keymap *sKeymap = nullptr;
xkb_state *sState = nullptr;
int32_t sDevice = -1;
std::string sDisplayName;

void ClearModel(bool aConnection)
{
	if (sState) xkb_state_unref(sState);
	if (sKeymap) xkb_keymap_unref(sKeymap);
	if (sContext) xkb_context_unref(sContext);
	sState = nullptr;
	sKeymap = nullptr;
	sContext = nullptr;
	sDevice = -1;
	if (aConnection && sXcb)
	{
		xcb_disconnect(sXcb);
		sXcb = nullptr;
		sDisplayName.clear();
	}
}

struct ModelLifetime
{
	~ModelLifetime() { ClearModel(true); }
};
ModelLifetime sLifetime;

bool EnsureConnection(Display *aDisplay)
{
	if (!aDisplay)
		return false;
	const char *display_name = DisplayString(aDisplay);
	std::string wanted = display_name ? display_name : "";
	if (sXcb && wanted == sDisplayName && !xcb_connection_has_error(sXcb))
		return true;
	ClearModel(true);
	int screen = 0;
	sXcb = xcb_connect(wanted.empty() ? nullptr : wanted.c_str(), &screen);
	(void)screen;
	if (!sXcb || xcb_connection_has_error(sXcb))
	{
		ClearModel(true);
		return false;
	}
	uint16_t major = 0, minor = 0;
	if (!xkb_x11_setup_xkb_extension(sXcb,
		XKB_X11_MIN_MAJOR_XKB_VERSION, XKB_X11_MIN_MINOR_XKB_VERSION,
		XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS, &major, &minor, nullptr, nullptr))
	{
		ClearModel(true);
		return false;
	}
	sDisplayName = wanted;
	return true;
}

bool BuildModel(Display *aDisplay)
{
	if (!EnsureConnection(aDisplay))
		return false;
	if (sState) xkb_state_unref(sState);
	if (sKeymap) xkb_keymap_unref(sKeymap);
	if (sContext) xkb_context_unref(sContext);
	sState = nullptr;
	sKeymap = nullptr;
	sContext = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!sContext)
		return false;
	sDevice = xkb_x11_get_core_keyboard_device_id(sXcb);
	if (sDevice < 0)
		return false;
	sKeymap = xkb_x11_keymap_new_from_device(sContext, sXcb, sDevice,
		XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (!sKeymap)
		return false;
	sState = xkb_x11_state_new_from_device(sKeymap, sXcb, sDevice);
	return sState != nullptr;
}

bool EnsureModel(Display *aDisplay)
{
	if (!EnsureConnection(aDisplay))
		return false;
	return (sKeymap && sState) || BuildModel(aDisplay);
}

bool UpdateState(Display *aDisplay)
{
	if (!EnsureModel(aDisplay))
		return false;
	XkbStateRec state;
	if (XkbGetState(aDisplay, XkbUseCoreKbd, &state) != Success)
		return false;
	xkb_state_update_mask(sState,
		(xkb_mod_mask_t)state.base_mods,
		(xkb_mod_mask_t)state.latched_mods,
		(xkb_mod_mask_t)state.locked_mods,
		0, 0, (xkb_layout_index_t)state.group);
	return true;
}

vk_type KeysymToVk(xkb_keysym_t aSym)
{
	if (aSym >= XKB_KEY_a && aSym <= XKB_KEY_z)
		return (vk_type)(0x41 + aSym - XKB_KEY_a);
	if (aSym >= XKB_KEY_A && aSym <= XKB_KEY_Z)
		return (vk_type)(0x41 + aSym - XKB_KEY_A);
	if (aSym >= XKB_KEY_0 && aSym <= XKB_KEY_9)
		return (vk_type)(0x30 + aSym - XKB_KEY_0);
	if (aSym >= XKB_KEY_F1 && aSym <= XKB_KEY_F24)
		return (vk_type)(0x70 + aSym - XKB_KEY_F1);
	switch (aSym)
	{
	case XKB_KEY_BackSpace: return 0x08;
	case XKB_KEY_Tab: return 0x09;
	case XKB_KEY_Return: case XKB_KEY_KP_Enter: return 0x0D;
	case XKB_KEY_Shift_L: return 0xA0;
	case XKB_KEY_Shift_R: return 0xA1;
	case XKB_KEY_Control_L: return 0xA2;
	case XKB_KEY_Control_R: return 0xA3;
	case XKB_KEY_Alt_L: return 0xA4;
	case XKB_KEY_Alt_R: case XKB_KEY_ISO_Level3_Shift: return 0xA5;
	case XKB_KEY_Pause: return 0x13;
	case XKB_KEY_Caps_Lock: return 0x14;
	case XKB_KEY_Escape: return 0x1B;
	case XKB_KEY_space: return 0x20;
	case XKB_KEY_Prior: return 0x21;
	case XKB_KEY_Next: return 0x22;
	case XKB_KEY_End: return 0x23;
	case XKB_KEY_Home: return 0x24;
	case XKB_KEY_Left: return 0x25;
	case XKB_KEY_Up: return 0x26;
	case XKB_KEY_Right: return 0x27;
	case XKB_KEY_Down: return 0x28;
	case XKB_KEY_Print: case XKB_KEY_Sys_Req: return 0x2C;
	case XKB_KEY_Insert: return 0x2D;
	case XKB_KEY_Delete: return 0x2E;
	case XKB_KEY_Super_L: return 0x5B;
	case XKB_KEY_Super_R: return 0x5C;
	case XKB_KEY_Menu: return 0x5D;
	case XKB_KEY_Num_Lock: return 0x90;
	case XKB_KEY_Scroll_Lock: return 0x91;
	case XKB_KEY_semicolon: return 0xBA;
	case XKB_KEY_equal: return 0xBB;
	case XKB_KEY_comma: return 0xBC;
	case XKB_KEY_minus: return 0xBD;
	case XKB_KEY_period: return 0xBE;
	case XKB_KEY_slash: return 0xBF;
	case XKB_KEY_grave: return 0xC0;
	case XKB_KEY_bracketleft: return 0xDB;
	case XKB_KEY_backslash: return 0xDC;
	case XKB_KEY_bracketright: return 0xDD;
	case XKB_KEY_apostrophe: return 0xDE;
	default: return 0;
	}
}

xkb_mod_mask_t ModMask(const char *aName)
{
	xkb_mod_index_t idx = xkb_keymap_mod_get_index(sKeymap, aName);
	return idx == XKB_MOD_INVALID || idx >= 32 ? 0 : ((xkb_mod_mask_t)1u << idx);
}

} // namespace

uint32_t LinuxEvdevCodeForScanCode(sc_type aSC)
{
	// Most basic set-1 make codes intentionally match Linux KEY_*. Pause and
	// NumLock are the exception: AHK uses sc045 for Pause and extended sc145
	// for NumLock, while evdev uses KEY_PAUSE=119 and KEY_NUMLOCK=69.
	if (aSC == 0x045) return 119;
	if (aSC == 0x145) return 69;
	if (aSC >= 0x01 && aSC <= 0x58)
		return (uint32_t)aSC;
	switch ((unsigned int)aSC)
	{
	case 0x064: return 183; // F13
	case 0x065: return 184; case 0x066: return 185; case 0x067: return 186;
	case 0x068: return 187; case 0x069: return 188; case 0x06A: return 189;
	case 0x06B: return 190; case 0x06C: return 191; case 0x06D: return 192;
	case 0x06E: return 193; case 0x076: return 194; // F14..F24
	case 0x11C: return 96;  // KP Enter
	case 0x11D: return 97;  // Right Ctrl
	case 0x135: return 98;  // KP /
	case 0x137: return 99;  // Print Screen
	case 0x138: return 100; // Right Alt / AltGr
	case 0x147: return 102; case 0x148: return 103; case 0x149: return 104;
	case 0x14B: return 105; case 0x14D: return 106; case 0x14F: return 107;
	case 0x150: return 108; case 0x151: return 109; case 0x152: return 110;
	case 0x153: return 111; case 0x15B: return 125; case 0x15C: return 126;
	case 0x15D: return 127;
	default: return 0;
	}
}

sc_type LinuxScanCodeForEvdev(uint32_t aEvdevCode)
{
	if (aEvdevCode == 119) return 0x045;
	if (aEvdevCode == 69) return 0x145;
	if (aEvdevCode >= 0x01 && aEvdevCode <= 0x58)
		return (sc_type)aEvdevCode;
	for (unsigned int sc = 0x59; sc <= 0x17F; ++sc)
		if (LinuxEvdevCodeForScanCode((sc_type)sc) == aEvdevCode)
			return (sc_type)sc;
	return 0;
}

KeyCode LinuxX11KeycodeForScanCode(sc_type aSC)
{
	uint32_t evdev = LinuxEvdevCodeForScanCode(aSC);
	return evdev && evdev + 8 <= 255 ? (KeyCode)(evdev + 8) : 0;
}

bool LinuxKeyModelX11Refresh(Display *aDisplay)
{
	return BuildModel(aDisplay);
}

bool LinuxKeyModelX11Decode(Display *aDisplay, KeyCode aKeycode, unsigned int aXState,
	AhkLinuxKeyIdentity &aOut)
{
	memset(&aOut, 0, sizeof(aOut));
	if (!aKeycode || !EnsureModel(aDisplay))
		return false;
	// Decode the state attached to THIS event. Querying the current server
	// state here loses Shift/AltGr when press+release were queued together.
	xkb_state *event_state = xkb_state_new(sKeymap);
	if (!event_state)
		return false;
	xkb_mod_mask_t mods = (xkb_mod_mask_t)(aXState & 0xffu);
	xkb_layout_index_t group = (xkb_layout_index_t)XkbGroupForCoreState(aXState);
	xkb_state_update_mask(event_state, mods, 0, 0, 0, 0, group);
	aOut.evdev_code = aKeycode >= 8 ? (uint32_t)aKeycode - 8 : 0;
	aOut.sc = LinuxScanCodeForEvdev(aOut.evdev_code);
	aOut.keysym = (KeySym)xkb_state_key_get_one_sym(event_state, (xkb_keycode_t)aKeycode);
	aOut.text = xkb_state_key_get_utf32(event_state, (xkb_keycode_t)aKeycode);
	aOut.vk = KeysymToVk((xkb_keysym_t)aOut.keysym);
	// Some synthetic/headless X servers expose canonical F13-F24 physical
	// keycodes without a usable keysym on release. Preserve the logical VK from
	// the canonical set-1 layer instead of emitting VK=0.
	if (!aOut.vk)
	{
		unsigned int sc = (unsigned int)aOut.sc;
		if (sc >= 0x064 && sc <= 0x06E)
			aOut.vk = (vk_type)(0x7C + sc - 0x064); // F13..F23.
		else if (sc == 0x076)
			aOut.vk = (vk_type)0x87; // F24.
	}
	xkb_state_unref(event_state);
	return true;
}

bool LinuxKeyModelX11FindUtf32(Display *aDisplay, uint32_t aCodepoint, AhkLinuxKeyStroke &aOut)
{
	aOut = AhkLinuxKeyStroke{0, false, false};
	if (!aCodepoint || !UpdateState(aDisplay))
		return false;
	XkbStateRec server_state;
	if (XkbGetState(aDisplay, XkbUseCoreKbd, &server_state) != Success)
		return false;
	xkb_layout_index_t layout = (xkb_layout_index_t)server_state.group;
	xkb_mod_mask_t shift_mask = ModMask(XKB_MOD_NAME_SHIFT);
	xkb_mod_mask_t altgr_mask = ModMask("Mod5");
	if (!altgr_mask)
		altgr_mask = ModMask("LevelThree");
	struct Candidate { bool shift, altgr; xkb_mod_mask_t mask; } candidates[] = {
		{false, false, 0}, {true, false, shift_mask},
		{false, true, altgr_mask}, {true, true, shift_mask | altgr_mask},
	};
	for (const Candidate &candidate : candidates)
	{
		if ((candidate.shift && !shift_mask) || (candidate.altgr && !altgr_mask))
			continue;
		xkb_state *state = xkb_state_new(sKeymap);
		if (!state)
			return false;
		xkb_state_update_mask(state, candidate.mask, 0, 0, 0, 0, layout);
		xkb_keycode_t min = xkb_keymap_min_keycode(sKeymap);
		xkb_keycode_t max = xkb_keymap_max_keycode(sKeymap);
		for (xkb_keycode_t code = min; code <= max; ++code)
		{
			if (xkb_state_key_get_utf32(state, code) == aCodepoint)
			{
				aOut.keycode = code <= 255 ? (KeyCode)code : 0;
				aOut.shift = candidate.shift;
				aOut.altgr = candidate.altgr;
				xkb_state_unref(state);
				return aOut.keycode != 0;
			}
		}
		xkb_state_unref(state);
	}
	return false;
}

bool LinuxKeyModelX11KeyProducesText(Display *aDisplay, KeyCode aKeycode)
{
	if (!aKeycode || !EnsureModel(aDisplay))
		return false;
	xkb_layout_index_t layouts = xkb_keymap_num_layouts_for_key(sKeymap, aKeycode);
	for (xkb_layout_index_t layout = 0; layout < layouts; ++layout)
	{
		xkb_level_index_t levels = xkb_keymap_num_levels_for_key(sKeymap, aKeycode, layout);
		for (xkb_level_index_t level = 0; level < levels; ++level)
		{
			const xkb_keysym_t *syms = nullptr;
			int n = xkb_keymap_key_get_syms_by_level(sKeymap, aKeycode, layout, level, &syms);
			for (int i = 0; i < n; ++i)
			{
				uint32_t cp = xkb_keysym_to_utf32(syms[i]);
				if (cp >= 0x20 && cp != 0x7f)
					return true;
			}
		}
	}
	return false;
}
