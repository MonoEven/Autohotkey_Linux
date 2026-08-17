// Linux GTK menu backend (UserMenu / UserMenuItem).
//
// The scripting API and the linked-list bookkeeping are a faithful port of
// script_menu.cpp; the Win32 menu handles (CreateMenu/AppendMenu/... ) are
// replaced by GTK: the Gui menu bar is built by script_gui_linux.cpp from
// the same linked list (GuiType::set_MenuBar / UpdateMenuBars), and popup
// menus (Menu.Show) are constructed here with GtkMenu when displayed.
//
// Item state (checked/enabled) is stored in mMenuState with the Win32
// MFS_* bit values, which script_gui_linux.cpp's menu-bar builder reads.

#include "../../stdafx.h"
#include "../../script.h"
#include "../../globaldata.h"
#include "../../application.h"
#include "../../script_func_impl.h"
#include "../gui/script_gui_linux.h"

#include <gtk/gtk.h>

#include <cstring>

// Win32 menu constants used by the ported logic (not provided by the
// Linux compatibility headers because script_menu.cpp is not built).
#ifndef MFS_ENABLED
# define MFS_ENABLED 0x0
#endif
#ifndef MFS_GRAYED
# define MFS_GRAYED 0x1
#endif
#ifndef MFS_DISABLED
# define MFS_DISABLED 0x2
#endif
#ifndef MFS_CHECKED
# define MFS_CHECKED 0x8
#endif
#ifndef MFS_UNCHECKED
# define MFS_UNCHECKED 0x0
#endif
#ifndef MFT_STRING
# define MFT_STRING 0x0
#endif
#ifndef MFT_SEPARATOR
# define MFT_SEPARATOR 0x800
#endif
#ifndef MFT_RADIOCHECK
# define MFT_RADIOCHECK 0x200
#endif
#ifndef MFT_RIGHTJUSTIFY
# define MFT_RIGHTJUSTIFY 0x4000
#endif
#ifndef MFT_MENUBREAK
# define MFT_MENUBREAK 0x40
#endif
#ifndef MFT_MENUBARBREAK
# define MFT_MENUBARBREAK 0x20
#endif
#ifndef MFT_BITMAP
# define MFT_BITMAP 0x4
#endif
#ifndef MFT_OWNERDRAW
# define MFT_OWNERDRAW 0x100
#endif

// Mirrors the Windows macro in script_menu.cpp: a change to a menu bar must
// be reflected in every window that uses it as its menu bar.
#define UPDATE_GUI_MENU_BARS(menu_type, hmenu) \
	if (menu_type == MENU_TYPE_BAR && g_firstGui) \
		GuiType::UpdateMenuBars(hmenu); // hmenu is unused by the GTK builder.

static std::string menu_utf8(LPCTSTR s)
{
	if (!s) return {};
	std::string out;
	out.reserve(_tcslen(s) * 3);
	for (const TCHAR *p = s; *p; ++p)
	{
		unsigned cp = (unsigned)*p;
		if (cp < 0x80)
			out.push_back((char)cp);
		else if (cp < 0x800)
		{
			out.push_back((char)(0xc0 | (cp >> 6)));
			out.push_back((char)(0x80 | (cp & 0x3f)));
		}
		else if (cp < 0x10000)
		{
			out.push_back((char)(0xe0 | (cp >> 12)));
			out.push_back((char)(0x80 | ((cp >> 6) & 0x3f)));
			out.push_back((char)(0x80 | (cp & 0x3f)));
		}
		else
		{
			out.push_back((char)(0xf0 | (cp >> 18)));
			out.push_back((char)(0x80 | ((cp >> 12) & 0x3f)));
			out.push_back((char)(0x80 | ((cp >> 6) & 0x3f)));
			out.push_back((char)(0x80 | (cp & 0x3f)));
		}
	}
	return out;
}

// Fire the script callback for an activated menu item (A_ThisMenuItem,
// A_ThisMenuItemPos, A_ThisMenu), matching application.cpp's AHK_USER_MENU.
static void FireMenuItem(UserMenuItem *aItem)
{
	if (!aItem || !aItem->mCallback)
		return;
	UserMenu *menu = aItem->mMenu;
	menu->AddRef();
	ExprTokenType param[] = { aItem->mName, (__int64)(aItem->Pos() + 1), menu };
	aItem->mCallback->ExecuteInNewThread(_T("Menu"), param, _countof(param));
	menu->Release();
}

static void OnMenuActivate(GtkWidget *, gpointer aData)
{
	FireMenuItem(static_cast<UserMenuItem *>(aData));
}

static GtkWidget *BuildPopupMenu(UserMenu &aMenu)
{
	GtkWidget *menu = gtk_menu_new();
	for (UserMenuItem *item = aMenu.mFirstMenuItem; item; item = item->mNextMenuItem)
	{
		GtkWidget *widget;
		if (!item->mName || !*item->mName)
			widget = gtk_separator_menu_item_new();
		else
		{
			// Convert "&" mnemonics to GTK "_" ("" doubles as a literal).
			std::string in = menu_utf8(item->mName), label;
			for (size_t i = 0; i < in.size(); ++i)
			{
				if (in[i] == '&')
				{
					if (i + 1 < in.size() && in[i + 1] == '&') { label += '&'; ++i; }
					else label += '_';
				}
				else if (in[i] == '_') label += "__";
				else label += in[i];
			}
			widget = gtk_menu_item_new_with_mnemonic(label.c_str());
			if (item->mSubmenu)
				gtk_menu_item_set_submenu(GTK_MENU_ITEM(widget), BuildPopupMenu(*item->mSubmenu));
			else if (item->mCallback)
				g_signal_connect(widget, "activate", G_CALLBACK(OnMenuActivate), item);
		}
		gtk_widget_set_sensitive(widget, (item->mMenuState & (MFS_DISABLED | MFS_GRAYED)) ? FALSE : TRUE);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), widget);
	}
	return menu;
}


ObjectMemberMd UserMenu::sMembers[] =
{
	md_member(UserMenu, Add, CALL, (In_Opt, String, Name), (In_Opt, Object, FunctionOrSubmenu), (In_Opt, String, Options)),
	md_member(UserMenu, AddStandard, CALL, md_arg_none),
	md_member(UserMenu, Check, CALL, (In, String, Item)),
	md_property(UserMenu, ClickCount, Int32),
	md_property(UserMenu, Default, String),
	md_member(UserMenu, Delete, CALL, (In_Opt, String, Item)),
	md_member(UserMenu, Disable, CALL, (In, String, Item)),
	md_member(UserMenu, Enable, CALL, (In, String, Item)),
	md_property_get(UserMenu, Handle, UIntPtr),
	md_member(UserMenu, Insert, CALL, (In_Opt, String, Before), (In_Opt, String, Name), (In_Opt, Object, FunctionOrSubmenu), (In_Opt, String, Options)),
	md_member(UserMenu, Rename, CALL, (In, String, Item), (In_Opt, String, NewName)),
	md_member(UserMenu, SetColor, CALL, (In_Opt, Variant, Color), (In_Opt, Bool32, ApplyToSubmenus)),
	md_member(UserMenu, SetIcon, CALL, (In, String, Item), (In, String, File), (In_Opt, Int32, Number), (In_Opt, Int32, Width)),
	md_member(UserMenu, Show, CALL, (In_Opt, Int32, X), (In_Opt, Int32, Y)),
	md_member(UserMenu, ToggleCheck, CALL, (In, String, Item)),
	md_member(UserMenu, ToggleEnable, CALL, (In, String, Item)),
	md_member(UserMenu, Uncheck, CALL, (In, String, Item)),
};
int UserMenu::sMemberCount = _countof(sMembers);


UserMenu::UserMenu(MenuTypeType aMenuType)
	: mMenuType(aMenuType)
{
	SetBase(sPrototype);
	g_script.AddMenu(this);
}


UserMenu::~UserMenu()
{
	g_script.ScriptDeleteMenu(this);
}


void UserMenu::Dispose()
{
	DestroyHandle();
	DeleteAllItems();
}


UserMenu *Script::AddMenu(UserMenu *aMenu)
{
	ASSERT(aMenu);
	if (!mFirstMenu)
		mFirstMenu = mLastMenu = aMenu;
	else
	{
		mLastMenu->mNextMenu = aMenu;
		mLastMenu = aMenu;
	}
	++mMenuCount;
	return aMenu;
}


ResultType Script::ScriptDeleteMenu(UserMenu *aMenu)
{
	UserMenu *aMenu_prev;
	if (aMenu == mFirstMenu)
	{
		mFirstMenu = aMenu->mNextMenu;
		aMenu_prev = NULL;
	}
	else
	{
		for (aMenu_prev = mFirstMenu; aMenu_prev; aMenu_prev = aMenu_prev->mNextMenu)
			if (aMenu_prev->mNextMenu == aMenu)
				break;
	}
	if (aMenu == mLastMenu)
		mLastMenu = aMenu_prev;
	--mMenuCount;
	aMenu->Dispose();
	return OK;
}


UINT Script::GetFreeMenuItemID()
{
	static UINT sLastFreeID = ID_USER_FIRST - 1;
	++sLastFreeID;
	bool id_in_use;
	for (int i = 0; i < (ID_USER_LAST - ID_USER_FIRST + 1); ++i, ++sLastFreeID)
	{
		if (sLastFreeID > ID_USER_LAST)
			sLastFreeID = ID_USER_FIRST;
		id_in_use = false;
		for (UserMenu *m = mFirstMenu; m; m = m->mNextMenu)
		{
			for (UserMenuItem *mi = m->mFirstMenuItem; mi; mi = mi->mNextMenuItem)
				if (mi->mMenuID == sLastFreeID)
				{
					id_in_use = true;
					break;
				}
			if (id_in_use)
				break;
		}
		if (!id_in_use)
			break;
	}
	return id_in_use ? 0 : sLastFreeID;
}


UserMenu *Script::FindMenu(HMENU aMenuHandle)
{
	if (!aMenuHandle) return NULL;
	for (UserMenu *menu = mFirstMenu; menu != NULL; menu = menu->mNextMenu)
		if (menu->mMenu == aMenuHandle)
			return menu;
	return NULL;
}


FResult UserMenu::Add(optl<StrArg> aName, optl<IObject*> aFuncOrSubmenu, optl<StrArg> aOptions)
{
	return Add(aName, aFuncOrSubmenu, aOptions, nullptr);
}


FResult UserMenu::Insert(optl<StrArg> aBefore, optl<StrArg> aName, optl<IObject*> aFuncOrSubmenu, optl<StrArg> aOptions)
{
	UserMenuItem *prev_item = mLastMenuItem;
	if (aBefore.has_nonempty_value())
	{
		bool search_by_pos;
		if (!FindItem(aBefore.value(), prev_item, search_by_pos))
		{
			if (!(search_by_pos && ATOI(aBefore.value()) == (int)mMenuItemCount + 1))
				return ItemNotFoundError(aBefore.value());
		}
	}
	return Add(aName, aFuncOrSubmenu, aOptions
		, prev_item ? &prev_item->mNextMenuItem : &mFirstMenuItem);
}


FResult UserMenu::Add(optl<StrArg> aName, optl<IObject*> aFuncOrSubmenu, optl<StrArg> aOptions, UserMenuItem **aInsertAt)
{
	auto options = aOptions.value_or_empty();
	if (aName.is_blank_or_omitted())
	{
		if (aFuncOrSubmenu.has_value() || *options)
			return FR_E_ARGS;
		return AddItem(_T(""), 0, nullptr, nullptr, _T(""), aInsertAt) ? OK : FR_FAIL;
	}

	bool search_by_pos = false;
	UserMenuItem *menu_item = nullptr, *menu_item_prev = nullptr;
	if (!aInsertAt)
	{
		menu_item = FindItem(aName.value(), menu_item_prev, search_by_pos);
		if (!menu_item && search_by_pos)
			return FR_E_ARG(0);
	}

	bool update_existing_item_options = (menu_item && !aFuncOrSubmenu.has_value() && *options);

	IObject *callback = nullptr;
	UserMenu *submenu = nullptr;
	if (!update_existing_item_options)
	{
		if (!aFuncOrSubmenu.has_value())
			return FR_E_ARG(aInsertAt ? 2 : 1);
		submenu = dynamic_cast<UserMenu *>(aFuncOrSubmenu.value());
		if (submenu)
		{
			if (submenu == this || submenu->ContainsMenu(this)
				|| submenu->mMenuType != MENU_TYPE_POPUP)
				return FR_E_ARG(aInsertAt ? 2 : 1);
		}
		else
		{
			callback = aFuncOrSubmenu.value();
			auto fr = ValidateFunctor(callback, 3);
			if (fr != OK)
				return fr;
		}
	}

	if (menu_item)
		return ModifyItem(menu_item, callback, submenu, options) ? OK : FR_FAIL;
	else
		return AddItem(aName.value(), 0, callback, submenu, options, aInsertAt) ? OK : FR_FAIL;
}


ResultType UserMenu::AddItem(LPCTSTR aName, UINT aMenuID, IObject *aCallback, UserMenu *aSubmenu, LPCTSTR aOptions
	, UserMenuItem **aInsertAt)
{
	size_t length = _tcslen(aName);
	if (length > MAX_MENU_NAME_LENGTH)
		return g_script.RuntimeError(_T("Menu item name too long."), aName);
	if (!aMenuID && !(aMenuID = g_script.GetFreeMenuItemID()))
		return g_script.RuntimeError(_T("Too many menu items."));
	LPTSTR name_dynamic;
	if (length)
	{
		if (!(name_dynamic = tmalloc(length + 1)))
			return MemoryError();
		_tcscpy(name_dynamic, aName);
	}
	else
		name_dynamic = Var::sEmptyString;
	UserMenuItem *menu_item = new UserMenuItem(name_dynamic, length + 1, aMenuID, aCallback, aSubmenu, this);
	if (*aOptions && !UpdateOptions(menu_item, aOptions))
	{
		delete menu_item;
		return FAIL;
	}
	if (mMenu)
	{
		InternalAppendMenu(menu_item, aInsertAt ? *aInsertAt : NULL);
		UPDATE_GUI_MENU_BARS(mMenuType, mMenu)
	}
	if (aInsertAt)
	{
		menu_item->mNextMenuItem = *aInsertAt;
		if (!*aInsertAt)
			mLastMenuItem = menu_item;
		*aInsertAt = menu_item;
	}
	else
	{
		if (!mFirstMenuItem)
			mFirstMenuItem = menu_item;
		else
			mLastMenuItem->mNextMenuItem = menu_item;
		mLastMenuItem = menu_item;
	}
	++mMenuItemCount;
	if (_tcschr(aName, '\t'))
		UpdateAccelerators();
	return OK;
}


ResultType UserMenu::InternalAppendMenu(UserMenuItem *, UserMenuItem *)
{
	// The GTK menu bar is rebuilt from the linked list by
	// GuiType::set_MenuBar()/UpdateMenuBars(); popup menus are rebuilt when
	// displayed.  Nothing to do here.
	return OK;
}


UserMenuItem::UserMenuItem(LPTSTR aName, size_t aNameCapacity, UINT aMenuID, IObject *aCallback, UserMenu *aSubmenu, UserMenu *aMenu)
	: mName(aName), mNameCapacity(aNameCapacity), mMenuID(aMenuID), mCallback(aCallback), mSubmenu(aSubmenu), mMenu(aMenu)
	, mPriority(0)
	, mMenuState(MFS_ENABLED | MFS_UNCHECKED), mMenuType(*aName ? MFT_STRING : MFT_SEPARATOR)
	, mNextMenuItem(NULL)
	, mBitmap(NULL)
{
	if (aSubmenu)
		aSubmenu->AddRef();
}


UserMenuItem::~UserMenuItem()
{
	if (mName != Var::sEmptyString)
		free(mName);
	if (mSubmenu)
		mSubmenu->Release();
}


UINT UserMenuItem::Pos()
{
	UINT pos = 0;
	for (UserMenuItem *i = mMenu ? mMenu->mFirstMenuItem : nullptr; i; i = i->mNextMenuItem, ++pos)
		if (i == this)
			break;
	return pos;
}


void UserMenu::DeleteItem(UserMenuItem *aMenuItem, UserMenuItem *aMenuItemPrev, bool aUpdateGuiMenuBars)
{
	if (aMenuItem == mLastMenuItem)
		mLastMenuItem = aMenuItemPrev;
	if (aMenuItemPrev)
		aMenuItemPrev->mNextMenuItem = aMenuItem->mNextMenuItem;
	else
		mFirstMenuItem = aMenuItem->mNextMenuItem;
	RemoveItemIcon(aMenuItem);
	if (mDefault == aMenuItem)
		mDefault = NULL;
	delete aMenuItem;
	--mMenuItemCount;
	if (aUpdateGuiMenuBars)
		UPDATE_GUI_MENU_BARS(mMenuType, mMenu)
}


void UserMenu::DeleteAllItems()
{
	if (!mFirstMenuItem)
		return;
	UserMenuItem *menu_item_to_delete;
	for (UserMenuItem *mi = mFirstMenuItem; mi;)
	{
		menu_item_to_delete = mi;
		mi = mi->mNextMenuItem;
		RemoveItemIcon(menu_item_to_delete);
		delete menu_item_to_delete;
	}
	mFirstMenuItem = mLastMenuItem = NULL;
	mMenuItemCount = 0;
	mDefault = NULL;
	UPDATE_GUI_MENU_BARS(mMenuType, mMenu)
}


ResultType UserMenu::ModifyItem(UserMenuItem *aMenuItem, IObject *aCallback, UserMenu *aSubmenu, LPCTSTR aOptions)
{
	if (*aOptions && UpdateOptions(aMenuItem, aOptions) != OK)
		return FAIL;
	if (!aCallback && !aSubmenu)
		return OK;

	if (aMenuItem->mMenuID >= ID_TRAY_FIRST && aCallback)
		aMenuItem->mMenuID = g_script.GetFreeMenuItemID();

	aMenuItem->mCallback = aCallback;
	if (aMenuItem->mSubmenu == aSubmenu)
		return OK;

	if (aSubmenu)
		aSubmenu->AddRef();
	if (aMenuItem->mSubmenu)
		aMenuItem->mSubmenu->Release();
	aMenuItem->mSubmenu = aSubmenu;
	if (mMenu)
		UPDATE_GUI_MENU_BARS(mMenuType, mMenu)
	return OK;
}


ResultType UserMenu::UpdateOptions(UserMenuItem *aMenuItem, LPCTSTR aOptions)
{
	UINT new_type = aMenuItem->mMenuType;
	TCHAR option_word[16];
	LPCTSTR next_option, option_end;
	bool adding;
	for (next_option = aOptions; *next_option; next_option = option_end)
	{
		next_option = omit_leading_whitespace(next_option);
		if (*next_option == '-')
		{
			adding = false;
			++next_option;
		}
		else
		{
			adding = true;
			if (*next_option == '+')
				++next_option;
		}
		if (!*next_option)
			break;
		for (option_end = next_option; *option_end && !IS_SPACE_OR_TAB(*option_end); ++option_end);
		if (option_end == next_option)
			continue;
		tcslcpy(option_word, next_option, (size_t)min((ptrdiff_t)(option_end - next_option) + 1, (ptrdiff_t)_countof(option_word)));

		if (!_tcsicmp(option_word, _T("Radio"))) if (adding) new_type |= MFT_RADIOCHECK; else new_type &= ~MFT_RADIOCHECK;
		else if (mMenuType == MENU_TYPE_BAR && !_tcsicmp(option_word, _T("Right"))) if (adding) new_type |= MFT_RIGHTJUSTIFY; else new_type &= ~MFT_RIGHTJUSTIFY;
		else if (!_tcsicmp(option_word, _T("Break"))) if (adding) new_type |= MFT_MENUBREAK; else new_type &= ~MFT_MENUBREAK;
		else if (!_tcsicmp(option_word, _T("BarBreak"))) if (adding) new_type |= MFT_MENUBARBREAK; else new_type &= ~MFT_MENUBARBREAK;
		else if (ctoupper(*option_word) == 'P')
			aMenuItem->mPriority = ATOI(option_word + 1);
		else
		{
			if (!ValueError(ERR_INVALID_OPTION, option_word, FAIL_OR_OK))
				return FAIL;
		}
	}
	aMenuItem->mMenuType = (WORD)new_type;
	return OK;
}


ResultType UserMenu::RenameItem(UserMenuItem *aMenuItem, LPCTSTR aNewName)
{
	if (_tcslen(aNewName) > MAX_MENU_NAME_LENGTH)
		return FAIL;
	UINT new_type = (aMenuItem->mMenuType & ~(MFT_BITMAP | MFT_SEPARATOR | MFT_STRING | MFT_OWNERDRAW))
		| (*aNewName ? MFT_STRING : MFT_SEPARATOR);
	if (!UpdateName(aMenuItem, aNewName))
		return FAIL;
	aMenuItem->mMenuType = (WORD)new_type;
	UPDATE_GUI_MENU_BARS(mMenuType, mMenu)
	return OK;
}


ResultType UserMenu::UpdateName(UserMenuItem *aMenuItem, LPCTSTR aNewName)
{
	size_t new_length = _tcslen(aNewName);
	if (new_length)
	{
		if (new_length >= aMenuItem->mNameCapacity)
		{
			LPTSTR temp = tmalloc(new_length + 1);
			if (!temp)
				return FAIL;
			if (aMenuItem->mName != Var::sEmptyString)
				free(aMenuItem->mName);
			aMenuItem->mName = temp;
			aMenuItem->mNameCapacity = new_length + 1;
		}
		_tcscpy(aMenuItem->mName, aNewName);
	}
	else
		*aMenuItem->mName = '\0';
	return OK;
}


void UserMenu::SetItemState(UserMenuItem *aMenuItem, UINT aState, UINT aStateMask)
{
	aMenuItem->mMenuState = (WORD)((aMenuItem->mMenuState & ~aStateMask) ^ aState);
	if (aStateMask & MFS_DISABLED)
		UPDATE_GUI_MENU_BARS(mMenuType, mMenu)
}


FResult UserMenu::SetItemState(StrArg aItemName, UINT aState, UINT aStateMask)
{
	UserMenuItem *item;
	auto fr = GetItem(aItemName, item);
	if (fr != OK)
		return fr;
	SetItemState(item, aState, aStateMask);
	return OK;
}


void UserMenu::SetDefault(UserMenuItem *aMenuItem, bool aUpdateGuiMenuBars)
{
	if (mDefault == aMenuItem)
		return;
	mDefault = aMenuItem;
	if (aUpdateGuiMenuBars)
		UPDATE_GUI_MENU_BARS(mMenuType, mMenu)
}


ResultType UserMenu::CreateHandle()
{
	if (mMenu)
		return OK;
	if (!ahk_gtk::GtkAvailable())
		return FAIL;
	GtkWidget *widget = (mMenuType == MENU_TYPE_BAR) ? gtk_menu_bar_new() : gtk_menu_new();
	g_object_ref_sink(widget);
	mMenu = (HMENU)widget;
	return OK;
}


void UserMenu::DestroyHandle()
{
	if (!mMenu)
		return;
	GtkWidget *widget = (GtkWidget *)mMenu;
	mMenu = NULL;
	if (widget && GTK_IS_WIDGET(widget))
		g_object_unref(widget);
}


void UserMenu::SetColor(COLORREF aColor, bool aApplyToSubmenus)
{
	// GTK menus follow the theme; the color is remembered for compatibility.
	mColor = aColor;
	if (aApplyToSubmenus)
		for (UserMenuItem *mi = mFirstMenuItem; mi; mi = mi->mNextMenuItem)
			if (mi->mSubmenu)
				mi->mSubmenu->SetColor(aColor, aApplyToSubmenus);
}


void UserMenu::ApplyColor(bool)
{
}


ResultType UserMenu::AppendStandardItems()
{
	// The tray menu is a Windows feature; on Linux AddStandard() adds
	// nothing (documented in LINUX_PORT.md).
	return OK;
}


ResultType UserMenu::EnableStandardOpenItem(bool)
{
	return OK;
}


ResultType UserMenu::Display(bool aForceToForeground, int aX, int aY)
{
	(void)aForceToForeground;
	if (!ahk_gtk::GtkAvailable())
		return FAIL;
	if (!mMenu && !CreateHandle())
		return FAIL;
	GtkWidget *menu = (GtkWidget *)mMenu;
	// Rebuild the items (the menu widget itself is kept for Handle stability).
	while (GtkWidget *child = gtk_menu_get_attach_widget(GTK_MENU(menu)))
		gtk_container_remove(GTK_CONTAINER(menu), child);
	// Remove children: iterate the shell instead.
	{
		GList *children = gtk_container_get_children(GTK_CONTAINER(menu));
		for (GList *l = children; l; l = l->next)
			gtk_container_remove(GTK_CONTAINER(menu), GTK_WIDGET(l->data));
		g_list_free(children);
	}
	if (mFirstMenuItem)
	{
		GtkWidget *fresh = BuildPopupMenu(*this);
		// Move children of fresh into menu.
		GList *children = gtk_container_get_children(GTK_CONTAINER(fresh));
		for (GList *l = children; l; l = l->next)
		{
			gtk_container_remove(GTK_CONTAINER(fresh), GTK_WIDGET(l->data));
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), GTK_WIDGET(l->data));
		}
		g_list_free(children);
		gtk_widget_destroy(fresh);
	}
	gtk_widget_show_all(menu);
	if (aX != COORD_UNSPECIFIED && aY != COORD_UNSPECIFIED)
	{
		GdkRectangle rect = { aX, aY, 1, 1 };
		gtk_menu_popup_at_rect(GTK_MENU(menu),
			gdk_screen_get_root_window(gdk_screen_get_default()),
			&rect, GDK_GRAVITY_NORTH_WEST, GDK_GRAVITY_NORTH_WEST, nullptr);
	}
	else
		gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);
	ahk_gtk::GtkPump();
	return OK;
}


FResult UserMenu::GetItem(LPCTSTR aNameOrPos, UserMenuItem *&aItem)
{
	bool bypos;
	UserMenuItem *prev;
	aItem = FindItem(aNameOrPos, prev, bypos);
	if (aItem)
		return OK;
	return ItemNotFoundError(aNameOrPos);
}


FResult UserMenu::ItemNotFoundError(LPCTSTR aItem)
{
	return FError(ERR_INVALID_MENU_ITEM, aItem, ErrorPrototype::Target);
}


UserMenuItem *UserMenu::FindItem(LPCTSTR aNameOrPos, UserMenuItem *&aPrevItem, bool &aByPos)
{
	int index_to_find = -1;
	size_t length = _tcslen(aNameOrPos);
	if (length > 1
		&& aNameOrPos[length - 1] == '&'
		&& aNameOrPos[length - 2] != '&')
		index_to_find = ATOI(aNameOrPos) - 1;
	aByPos = index_to_find > -1;
	int current_index = 0;
	UserMenuItem *menu_item_prev = NULL, *menu_item;
	for (menu_item = mFirstMenuItem
		; menu_item
		; menu_item_prev = menu_item, menu_item = menu_item->mNextMenuItem, ++current_index)
		if (current_index == index_to_find
			|| !lstrcmpi(menu_item->mName, aNameOrPos))
			break;
	aPrevItem = menu_item_prev;
	return menu_item;
}


UserMenuItem *UserMenu::FindItemByID(UINT aID)
{
	for (UserMenuItem *mi = mFirstMenuItem; mi; mi = mi->mNextMenuItem)
		if (mi->mMenuID == aID)
			return mi;
	return NULL;
}


bool UserMenu::ContainsMenu(UserMenu *aMenu)
{
	for (UserMenuItem *mi = mFirstMenuItem; mi; mi = mi->mNextMenuItem)
		if (mi->mSubmenu && (mi->mSubmenu == aMenu || mi->mSubmenu->ContainsMenu(aMenu)))
			return true;
	return false;
}


void UserMenu::UpdateAccelerators()
{
	// Accelerator tables are cosmetic on GTK; the label suffix is kept.
}


ResultType UserMenu::SetItemIcon(UserMenuItem *aMenuItem, LPCTSTR aFilename, int aIconNumber, int aWidth)
{
	(void)aMenuItem; (void)aIconNumber; (void)aWidth;
	// Menu icons are not rendered on GTK; validate the file for parity.
	std::string path = menu_utf8(aFilename);
	GError *e = nullptr;
	GdkPixbuf *pb = gdk_pixbuf_new_from_file(path.c_str(), &e);
	if (!pb)
	{
		if (e) g_error_free(e);
		return FAIL;
	}
	g_object_unref(pb);
	return OK;
}


void UserMenu::ApplyItemIcon(UserMenuItem *)
{
}


void UserMenu::RemoveItemIcon(UserMenuItem *)
{
}


FResult UserMenu::AddStandard()
{
	return AppendStandardItems() ? OK : FR_FAIL;
}


FResult UserMenu::Delete(optl<StrArg> aItemName)
{
	if (!aItemName.has_value())
	{
		DeleteAllItems();
		return OK;
	}
	if (aItemName.is_blank())
		return FR_E_ARG(0);
	bool search_by_pos = false;
	UserMenuItem *menu_item_prev
		, *menu_item = FindItem(aItemName.value(), menu_item_prev, search_by_pos);
	if (!menu_item)
		return ItemNotFoundError(aItemName.value());
	DeleteItem(menu_item, menu_item_prev);
	return OK;
}


FResult UserMenu::Rename(StrArg aItemName, optl<StrArg> aNewName)
{
	UserMenuItem *item;
	auto fr = GetItem(aItemName, item);
	if (fr != OK)
		return fr;
	auto new_name = aNewName.value_or_empty();
	if (!RenameItem(item, new_name))
		return FError(_T("Rename failed (name too long?)."), new_name);
	return OK;
}


FResult UserMenu::SetColor(ExprTokenType *aColor, optl<BOOL> aApplyToSubmenus)
{
	COLORREF color = CLR_DEFAULT;
	if (aColor && !ColorToBGR(*aColor, color))
		return FR_E_ARG(0);
	SetColor(color, aApplyToSubmenus.value_or(TRUE) != FALSE);
	return OK;
}


FResult UserMenu::SetIcon(StrArg aItemName, StrArg aIconFile, optl<int> aIconNumber, optl<int> aIconWidth)
{
	UserMenuItem *item;
	auto fr = GetItem(aItemName, item);
	if (fr != OK)
		return fr;
	if (!SetItemIcon(item, aIconFile, aIconNumber.value_or(0), aIconWidth.has_value() ? aIconWidth.value() : GetSystemMetrics(SM_CXSMICON)))
		return FError(ERR_LOAD_ICON, aIconFile);
	return OK;
}


FResult UserMenu::Show(optl<int> aX, optl<int> aY)
{
	return Display(true, aX.value_or(COORD_UNSPECIFIED), aY.value_or(COORD_UNSPECIFIED)) ? OK : FR_FAIL;
}


FResult UserMenu::Check(StrArg aItemName)
{
	return SetItemState(aItemName, MFS_CHECKED, MFS_CHECKED);
}

FResult UserMenu::ToggleCheck(StrArg aItemName)
{
	return SetItemState(aItemName, MFS_CHECKED, 0);
}

FResult UserMenu::Uncheck(StrArg aItemName)
{
	return SetItemState(aItemName, 0, MFS_CHECKED);
}

FResult UserMenu::Disable(StrArg aItemName)
{
	return SetItemState(aItemName, MFS_DISABLED, MFS_DISABLED);
}

FResult UserMenu::Enable(StrArg aItemName)
{
	return SetItemState(aItemName, 0, MFS_DISABLED);
}

FResult UserMenu::ToggleEnable(StrArg aItemName)
{
	return SetItemState(aItemName, MFS_DISABLED, 0);
}


FResult UserMenu::get_ClickCount(int &aRetVal)
{
	aRetVal = mClickCount;
	return OK;
}

FResult UserMenu::set_ClickCount(int aValue)
{
	if (aValue < 1 || aValue > 2)
		return FR_E_ARG(0);
	mClickCount = aValue;
	return OK;
}


FResult UserMenu::get_Default(StrRet &aRetVal)
{
	aRetVal.SetTemp(mDefault ? mDefault->mName : _T(""));
	return OK;
}

FResult UserMenu::set_Default(StrArg aItemName)
{
	UserMenuItem *item = nullptr;
	if (*aItemName)
	{
		auto fr = GetItem(aItemName, item);
		if (fr != OK)
			return fr;
	}
	SetDefault(item);
	return OK;
}


FResult UserMenu::get_Handle(UINT_PTR &aRetVal)
{
	if (!mMenu)
		if (!CreateHandle())
			return FR_FAIL;
	aRetVal = (UINT_PTR)mMenu;
	return OK;
}
