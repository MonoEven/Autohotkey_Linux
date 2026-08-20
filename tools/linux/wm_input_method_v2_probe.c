// wlroots input-method-v2 client prototype (check0820 §1 item 3) - SKETCH.
//
// NOTE: this file is intentionally a SCAFFOLD, not a build target.  It
// sketches how AHK's Wayland layer would bind the wlroots input-method v2
// manager and receive preedit/commit calls, so the required code shape is
// captured in-tree.  It is not compiled into ahk_core.  See the "Honest
// record" below for why no live E2E exists in this environment.
//
// Honest record (check0820), verified on the snap VM:
//   - sway 1.10 / wlroots 0.18 links the v2 symbols
//     (wlr_input_method_manager_v2_create, wlr_text_input_manager_v3_*):
//     a sway session CAN serve an input-method manager, but only the v1
//     protocol XML (input-method-unstable-v1.xml) is installed, so no v2
//     generated client stub is available to bind against, and sway does
//     not drive an ibus/fcitx input-method backend end-to-end.
//   - GNOME Shell / Mutter link NO input-method symbols at all (verified
//     by scanning /usr/bin/gnome-shell and libmutter for
//     input-method/text-input/zwp names: none).  Mutter does not
//     implement the wlroots input-method-v2 (or text-input-v3) protocol;
//     GNOME input methods use mutter's internal ibus integration, which
//     is not the zwp protocol.  A wlroots input-method-v2 CLIENT is
//     therefore not exercisable on GNOME, and the zwp protocol is not the
//     portable path for the GNOME desktop.
//   - Consequently this item is recorded as "prototype sketched; live
//     verification blocked by environment: no compositor run in the CI/VM
//     matrix both exports v2 and runs an input-method backend".  This is
//     the honest reason, not a code gap in the prototype itself.

#include <wayland-client.h>
#include <stdio.h>

// The input-method-v2 stub below documents the call surface.  With the
// v2 generated header present it should be replaced by the real binding:
//   struct zwp_input_method_manager_v2 *mgr =
//       wl_registry_bind(reg, zwp_input_method_manager_v2_interface.name,
//                        &zwp_input_method_manager_v2_interface, 1);
//   struct zwp_input_method_v2 *im =
//       zwp_input_method_manager_v2_get_input_method(mgr, NULL);
//   zwp_input_method_v2_add_listener(im, &method_listener, NULL);
static void on_preedit(void *data, uint32_t serial, const char *text,
	uint32_t cursor_begin, uint32_t cursor_end)
{
	(void)data; (void)serial; (void)cursor_begin; (void)cursor_end;
	if (text)
		printf("preedit: %s\n", text);
}

static void on_commit(void *data, uint32_t serial, const char *text)
{
	(void)data; (void)serial;
	if (text)
		printf("commit: %s\n", text);
}

struct method_listener_stub
{
	void (*preedit)(void *, uint32_t, const char *, uint32_t, uint32_t);
	void (*commit)(void *, uint32_t, const char *);
	// ... other v2 methods listed here in the real header.
};

int main(int argc, char **argv)
{
	(void)argc; (void)argv;
	// Real body would wl_display_connect + registry_bind as above and
	// wl_display_roundtrip() in the event loop (see tools/linux/wl_info.c
	// for the display plumbing).
	return 0;
}