/*
 * ct_menu.cc
 *
 * Copyright 2009-2026
 * Giuseppe Penone <giuspen@gmail.com>
 * Evgenii Gurianov <https://github.com/txe>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include "ct_actions.h"
#include "ct_menu.h"
#include "ct_const.h"
#include "ct_misc_utils.h"
#include "ct_storage_xml.h"
#include "ct_logging.h"

#include <algorithm>
#include <pango/pangocairo.h> // OrangeArk: pango_cairo_font_map_get_default for system font enumeration
#include <gdkmm/screen.h>
#include <gdkmm/pixbuf.h>
#include <cairomm/surface.h>
#include <cairomm/context.h>

// OrangeArk: make the combobox dropdown arrow and popup scrollbar thumbs clearly
// visible under the win32 GTK theme (the arrow can be blank when its symbolic
// icon is unavailable, and the scrollbar slider can be nearly invisible)
static void _apply_global_gtk_css()
{
    static bool sDone = false;
    if (sDone) {
        return;
    }
    sDone = true;
    try {
        // OrangeArk: classic (non-overlay) scrollbars so the font list popup gets
        // a Word-style scrollbar with draggable slider AND up/down arrow buttons
        if (GtkSettings* pSettings = gtk_settings_get_default()) {
            g_object_set(pSettings, "gtk-overlay-scrolling", FALSE, nullptr);
        }
        auto rCss = Gtk::CssProvider::create();
        // OrangeArk: -GtkScrollbar-has-{backward,forward}-stepper tells GTK3 to
        // actually CREATE the up/down arrow buttons — without these style
        // properties the scrollbar buttons are never instantiated, no matter
        // how they are styled (that is why the arrows did not appear before)
        rCss->load_from_data(
            // OrangeArk: force LIST mode for combobox popups. In the default
            // MENU mode the popup is a GtkMenu — it has NO classic scrollbar
            // the "no slider + blank strip on top" the user kept seeing on
            // the font/size dropdowns. In GTK3 those popups are GtkMenu and
            // cannot show scrollbars at all; the pickers are now popover
            // lists (CtListPickerButton below) with a real ScrolledWindow.
            "combobox arrow { min-width: 14px; min-height: 14px; }\n"
            "scrollbar { -GtkScrollbar-has-backward-stepper: true;\n"
            "  -GtkScrollbar-has-forward-stepper: true;\n"
            "  -GtkScrollbar-has-secondary-backward-stepper: false;\n"
            "  -GtkScrollbar-has-secondary-forward-stepper: false;\n"
            "  background-color: #f0f0f0; min-width: 15px; min-height: 15px; }\n"
            "scrollbar slider { background-color: #b8b8b8; min-width: 13px; min-height: 24px; border-radius: 2px; }\n"
            "scrollbar slider:hover { background-color: #9a9a9a; }\n"
            "scrollbar button { min-width: 15px; min-height: 15px; padding: 0px; margin: 0px;\n"
            "  background-color: #e2e2e2; border: 1px solid #c0c0c0; color: #404040;\n"
            "  -gtk-icon-shadow: none; }\n"
            "scrollbar button.up { -gtk-icon-source: -gtk-icontheme(\"pan-up-symbolic\"); }\n"
            "scrollbar button.down { -gtk-icon-source: -gtk-icontheme(\"pan-down-symbolic\"); }\n"
            "scrollbar button.left { -gtk-icon-source: -gtk-icontheme(\"pan-start-symbolic\"); }\n"
            "scrollbar button.right { -gtk-icon-source: -gtk-icontheme(\"pan-end-symbolic\"); }\n"
            "scrollbar button:hover { background-color: #d2d2d2; }\n"
            "scrollbar button:active { background-color: #b8b8b8; }\n");
        auto rScreen = Gdk::Screen::get_default();
        if (rScreen) {
            Gtk::StyleContext::add_provider_for_screen(rScreen, rCss, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        }
    }
    catch (const Glib::Error& e) {
        spdlog::warn("global gtk css failed: {}", e.what().c_str());
    }
}

// OrangeArk: the win32 GTK theme can render the combobox dropdown arrow blank
// (its pan-down-symbolic icon may be unavailable in the trimmed-down icon set);
// replace it with a self-drawn caret pixbuf so the arrow is always visible
static void _force_combo_arrow_visible(Gtk::Widget* pWidget)
{
    if (GTK_IS_IMAGE(pWidget->gobj())) {
        // OrangeArk: use the C API — the matching gtkmm get_icon_name overload
        // is declared but not exported by libgtkmm
        const gchar* pIconName = nullptr;
        GtkIconSize iconSize{GTK_ICON_SIZE_MENU};
        gtk_image_get_icon_name(GTK_IMAGE(pWidget->gobj()), &pIconName, &iconSize);
        if (pIconName and std::string(pIconName).find("pan-down") != std::string::npos) {
            const int kSize = 12;
            auto rSurface = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, kSize, kSize);
            auto rCr = Cairo::Context::create(rSurface);
            rCr->set_source_rgba(0.25, 0.25, 0.25, 1.0);
            rCr->move_to(1.0, 3.0);
            rCr->line_to(11.0, 3.0);
            rCr->line_to(6.0, 9.0);
            rCr->close_path();
            rCr->fill();
            rSurface->flush();
            GdkPixbuf* pPixbuf = gdk_pixbuf_get_from_surface(rSurface->cobj(), 0, 0, kSize, kSize);
            if (pPixbuf) {
                Glib::wrap(GTK_IMAGE(pWidget->gobj()))->set(Glib::wrap(pPixbuf));
            }
        }
        return;
    }
    if (auto* pContainer = dynamic_cast<Gtk::Container*>(pWidget)) {
        pContainer->foreach([](Gtk::Widget& rChild) { _force_combo_arrow_visible(&rChild); });
    }
}

// OrangeArk: toolbar font/size picker + system-font enumeration live in the
// shared header (also used by the screenshot text panel)
#include "ct_list_picker.h"

#if GTKMM_MAJOR_VERSION >= 4
std::vector<Gtk::Box*> CtMenu::build_toolbars4(Gtk::MenuButton*& pRecentDocsMenuButton, Gtk::Button*& pButtonSave)
{
    pRecentDocsMenuButton = nullptr;
    pButtonSave = nullptr;
    _gtk4ActionWidgets.clear();
    for (auto& action : _actions) {
        action.signal_set_sensitive = nullptr;
        action.signal_set_visible = nullptr;
    }

    std::vector<Gtk::Box*> toolbars;
    auto create_toolbar = []() {
        auto* toolbar = Gtk::manage(new Gtk::Box{Gtk::Orientation::HORIZONTAL, 0});
        toolbar->get_style_context()->add_class("ct-toolbar4");
        toolbar->set_margin_start(2);
        toolbar->set_margin_end(2);
        toolbar->set_margin_top(2);
        toolbar->set_margin_bottom(2);
        return toolbar;
    };

    Gtk::Box* current_toolbar = create_toolbar();
    toolbars.push_back(current_toolbar);

    const std::vector<std::string> toolbar_elements = str::split(_pCtConfig->toolbarUiList, ",");
    for (const std::string& element : toolbar_elements) {
        if (element == CtConst::TOOLBAR_SPLIT) {
            current_toolbar = create_toolbar();
            toolbars.push_back(current_toolbar);
            continue;
        }
        if (element == CtConst::TAG_SEPARATOR) {
            auto* separator = Gtk::manage(new Gtk::Separator{Gtk::Orientation::VERTICAL});
            separator->get_style_context()->add_class("ct-toolbar4-separator");
            current_toolbar->append(*separator);
            continue;
        }
        if (element == CtConst::TOOLBAR_FONT_FAMILY or element == CtConst::TOOLBAR_FONT_SIZE) {
            // OrangeArk: GTK4 fallback toolbar keeps the plain ComboBoxText
            // (the scrolled popover picker is a GTK3-only improvement)
            auto* pCombo = Gtk::manage(new Gtk::ComboBoxText());
            if (element == CtConst::TOOLBAR_FONT_FAMILY) {
                pCombo->append(_("Font"));
                pCombo->set_active(0);
                pCombo->set_size_request(140, -1);
            }
            else {
                pCombo->append(_("Font Size"));
                for (const int size : {8, 9, 10, 11, 12, 13, 14, 16, 18, 20, 22, 24, 28, 32, 36, 48, 72}) {
                    pCombo->append(std::to_string(size));
                }
                pCombo->set_active(0);
                pCombo->set_size_request(72, -1);
            }
            pCombo->set_margin_start(2);
            pCombo->set_margin_end(2);
            pCombo->set_valign(Gtk::Align::CENTER);
            current_toolbar->append(*pCombo);
            continue;
        }
        if (element == CtConst::CHAR_STAR) {
            // Create a container for the open-file button + dropdown menu button
            auto* btn_container = Gtk::manage(new Gtk::Box{Gtk::Orientation::HORIZONTAL, 0});
            btn_container->get_style_context()->add_class("ct-toolbar4-btn-group");
            
            // Main button: open file picker
            auto* open_btn = Gtk::manage(new Gtk::Button());
            open_btn->set_has_frame(false);
            open_btn->get_style_context()->add_class("ct-toolbar4-btn");
            open_btn->get_style_context()->add_class("ct-toolbar4-btn-left");
            
            CtMenuAction* open_file_action = find_action("ct_open_file");
            if (open_file_action and not open_file_action->image.empty()) {
                open_btn->set_icon_name(open_file_action->image);
            } else {
                open_btn->set_icon_name("document-open");
            }
            
            std::string tooltip = open_file_action ? open_file_action->desc : _("Open File");
            open_btn->set_tooltip_text(tooltip);
            
            if (open_file_action and open_file_action->run_action) {
                open_btn->signal_clicked().connect([open_file_action](){
                    open_file_action->run_action();
                });
            }
            btn_container->append(*open_btn);
            
            // Dropdown button: recent documents
            auto* menu_btn = Gtk::manage(new Gtk::MenuButton());
            menu_btn->set_has_frame(false);
            menu_btn->get_style_context()->add_class("ct-toolbar4-btn");
            menu_btn->get_style_context()->add_class("ct-toolbar4-btn-right");
            menu_btn->set_icon_name("pan-down-symbolic");
            menu_btn->set_tooltip_text(_("Recent Documents"));

            // In this split-button setup, force click-to-toggle behavior for the menu arrow.
            auto click = Gtk::GestureClick::create();
            click->set_button(1);
            click->signal_pressed().connect([menu_btn, click](int /*n_press*/, double /*x*/, double /*y*/) {
                click->set_state(Gtk::EventSequenceState::CLAIMED);
                menu_btn->set_active(not menu_btn->get_active());
            });
            menu_btn->add_controller(click);
            
            populate_recent_docs_menu4(menu_btn, _pCtConfig->recentDocsFilepaths);
            btn_container->append(*menu_btn);
            
            current_toolbar->append(*btn_container);
            pRecentDocsMenuButton = menu_btn;
            continue;
        }

        CtMenuAction* action = find_action(element);
        if (not action) {
            continue;
        }

        auto* button = Gtk::manage(new Gtk::Button());
        button->set_has_frame(false);
        button->get_style_context()->add_class("ct-toolbar4-btn");
        if (not action->image.empty()) {
            button->set_icon_name(action->image);
        }
        else {
            button->set_label(action->name);
        }

        std::string tooltip = action->desc;
        const std::string& shortcut = action->get_shortcut(_pCtConfig);
        if (not shortcut.empty()) {
            const std::string shortcut_text = _shortcut_display(shortcut);
            tooltip = tooltip.empty() ? shortcut_text : tooltip + " (" + shortcut_text + ")";
        }
        if (not tooltip.empty()) {
            button->set_tooltip_text(tooltip);
        }

        button->signal_clicked().connect([action](){
            if (action->run_action) {
                action->run_action();
            }
        });
        current_toolbar->append(*button);
        _gtk4ActionWidgets[action->id] = button;
        action->signal_set_sensitive = [button](bool sensitive) {
            button->set_sensitive(sensitive);
        };
        action->signal_set_visible = [button](bool visible) {
            button->set_visible(visible);
        };
        if (action->id == "ct_save") {
            pButtonSave = button;
        }
    }

    return toolbars;
}

Gtk::MenuButton* CtMenu::build_menubutton4()
{
    auto* menuBtn = Gtk::manage(new Gtk::MenuButton());
    menuBtn->set_icon_name("open-menu-symbolic");
    menuBtn->set_tooltip_text(_("Menu"));
    menuBtn->set_popover(*_build_actions_popover());
    return menuBtn;
}

Gtk::MenuButton* CtMenu::build_menubutton_model4()
{
    return build_menubutton4();
}

Glib::RefPtr<Gio::Menu> CtMenu::_build_gio_menu4()
{
    _pGioMenuBar4 = Gio::Menu::create();
    _pGioBookmarksMenu4.reset();
    _pGioBookmarksSub4.reset();
    _pGioRecentDocsSub4.reset();

    auto extract_action_attr = [](const std::string& tag) -> std::string {
        const std::size_t pos = tag.find("action=");
        if (pos == std::string::npos || pos + 8 >= tag.size()) {
            return {};
        }
        const char quote = tag[pos + 7];
        if (quote != '\'' && quote != '"') {
            return {};
        }
        const std::size_t value_start = pos + 8;
        const std::size_t value_end = tag.find(quote, value_start);
        if (value_end == std::string::npos) {
            return {};
        }
        return tag.substr(value_start, value_end - value_start);
    };

    struct MenuFrame {
        Glib::RefPtr<Gio::Menu> menu;
        Glib::RefPtr<Gio::Menu> section;
    };
    auto ensure_section = [](MenuFrame& frame) {
        if (not frame.section) {
            frame.section = Gio::Menu::create();
            frame.menu->append_section({}, frame.section);
        }
    };

    std::vector<MenuFrame> stack;
    const std::string markup{_get_ui_str_menu()};
    std::size_t pos = 0;
    while (true) {
        const std::size_t lt = markup.find('<', pos);
        if (lt == std::string::npos) {
            break;
        }
        const std::size_t gt = markup.find('>', lt + 1);
        if (gt == std::string::npos) {
            break;
        }
        pos = gt + 1;

        std::string tag = str::trim(markup.substr(lt + 1, gt - lt - 1));
        if (tag.empty() || tag[0] == '!' || tag[0] == '?') {
            continue;
        }

        const bool is_closing = tag[0] == '/';
        bool self_closing = false;
        if (not is_closing && tag.back() == '/') {
            self_closing = true;
            tag = str::trim(tag.substr(0, tag.size() - 1));
        }

        if (str::startswith(tag, "menubar") || str::startswith(tag, "/menubar")) {
            continue;
        }

        if (str::startswith(tag, "separator")) {
            if (not stack.empty()) {
                stack.back().section.reset();
                ensure_section(stack.back());
            }
            continue;
        }

        if (str::startswith(tag, "menuitem")) {
            if (stack.empty()) {
                continue;
            }
            const std::string action_id = extract_action_attr(tag);
            CtMenuAction* action = action_id.empty() ? nullptr : find_action(action_id);
            if (not action) {
                continue;
            }
            ensure_section(stack.back());
            stack.back().section->append(action->name, std::string("app.") + action->id);
            continue;
        }

        if (str::startswith(tag, "/menu")) {
            if (not stack.empty()) {
                stack.pop_back();
            }
            continue;
        }

        if (str::startswith(tag, "menu")) {
            const std::string action_id = extract_action_attr(tag);
            if (action_id.empty()) {
                continue;
            }

            CtMenuAction* action = find_action(action_id);
            const Glib::ustring label = action ? Glib::ustring(action->name) : Glib::ustring(action_id);
            auto submenu = Gio::Menu::create();

            if (stack.empty()) {
                _pGioMenuBar4->append_submenu(label, submenu);
            }
            else {
                ensure_section(stack.back());
                stack.back().section->append_submenu(label, submenu);
            }

            if (action_id == "BookmarksMenu")     { _pGioBookmarksMenu4 = submenu; }
            else if (action_id == "BookmarksSubMenu") { _pGioBookmarksSub4  = submenu; }
            else if (action_id == "RecentDocsSubMenu"){ _pGioRecentDocsSub4  = submenu; }

            MenuFrame frame;
            frame.menu = submenu;
            ensure_section(frame);
            stack.push_back(frame);

            if (self_closing) {
                stack.pop_back();
            }
        }
    }

    return _pGioMenuBar4;
}

Gtk::PopoverMenuBar* CtMenu::build_popover_menubar4()
{
    auto menu = _build_gio_menu4();
    auto* bar = Gtk::manage(new Gtk::PopoverMenuBar(menu));
    bar->set_name("GioMenuBar");
    return bar;
}

void CtMenu::update_bookmarks_gio_menu4(const std::list<std::tuple<gint64, Glib::ustring, const char*>>& bookmarks)
{
    // Update both the top-level "Bookmarks" menu and the Tree > Bookmarks submenu
    for (auto* gioMenu : {_pGioBookmarksMenu4.get(), _pGioBookmarksSub4.get()}) {
        if (not gioMenu) continue;
        gioMenu->remove_all();
        if (not bookmarks.empty()) {
            for (const auto& bk : bookmarks) {
                const gint64 node_id   = std::get<0>(bk);
                const Glib::ustring& title = std::get<1>(bk);
                auto item = Gio::MenuItem::create(title, "");
                item->set_action_and_target("app.goto_bookmark",
                    Glib::Variant<gint64>::create(node_id));
                gioMenu->append_item(item);
            }
        }
    }
}

void CtMenu::update_recent_docs_gio_menu4(const CtRecentDocsFilepaths& recentDocsFilepaths)
{
    if (not _pGioRecentDocsSub4) return;
    _pGioRecentDocsSub4->remove_all();
    if (not recentDocsFilepaths.empty()) {
        int idx = 0;
        for (const auto& path : recentDocsFilepaths) {
            if (idx >= 10) break;
            const std::string path_str = path.string();
            const Glib::ustring filepath{path_str};
            auto item = Gio::MenuItem::create(filepath, "");
            item->set_action_and_target("app.open_recent_doc",
                Glib::Variant<Glib::ustring>::create(filepath));
            _pGioRecentDocsSub4->append_item(item);
            ++idx;
        }
    }
}

Gtk::Popover* CtMenu::_build_actions_popover()
{
    auto* pop = Gtk::manage(new Gtk::Popover());
    auto* vbox = Gtk::manage(new Gtk::Box{Gtk::Orientation::VERTICAL, 4});
    for (const auto& action : _actions) {
        if (action.id.empty()) {
            continue;
        }
        auto* btn = Gtk::manage(new Gtk::Button(action.name));
        btn->set_halign(Gtk::Align::FILL);
        btn->signal_clicked().connect([&action, pop]{
            if (action.run_action) {
                action.run_action();
            }
            pop->popdown();
        });
        vbox->append(*btn);
    }
    pop->set_child(*vbox);
    return pop;
}

std::string CtMenu::_shortcut_display(const std::string& accel)
{
    return accel;
}

void CtMenu::refresh_shortcuts_gtk4()
{
    auto app = std::dynamic_pointer_cast<Gtk::Application>(Gtk::Application::get_default());
    if (not app) return;
    // Re-apply all accelerators so that any customised shortcuts take effect immediately.
    // Keyboard shortcuts are registered on the application level and therefore work
    // whether or not any menu is currently open.
    for (const auto& act : _actions) {
        if (act.id.empty()) continue;
        const std::string& shortcut = act.get_shortcut(_pCtConfig);
        std::vector<Glib::ustring> accels;
        if (not shortcut.empty()) accels.push_back(shortcut);
        app->set_accels_for_action(std::string("app.") + act.id, accels);
    }
}

void CtMenu::populate_recent_docs_menu4(Gtk::MenuButton* recentBtn, const CtRecentDocsFilepaths& recentDocsFilepaths)
{
    if (not recentBtn) {
        return;
    }
    
    auto* vbox = Gtk::manage(new Gtk::Box{Gtk::Orientation::VERTICAL, 0});
    
    // Add recent documents
    int idx = 0;
    for (const auto& path : recentDocsFilepaths) {
        if (idx >= 10) {
            break;
        }
        auto* hbox = Gtk::manage(new Gtk::Box{Gtk::Orientation::HORIZONTAL, 8});
        
        // Folder icon
        auto* icon = Gtk::manage(new Gtk::Image());
        icon->set_from_icon_name("folder");
        icon->set_icon_size(Gtk::IconSize::NORMAL);
        hbox->append(*icon);
        
        // Document path as label (not button, so it looks like a menu item)
        auto* lbl = Gtk::manage(new Gtk::Label(path.string()));
        lbl->set_halign(Gtk::Align::START);
        lbl->set_hexpand(true);
        lbl->set_ellipsize(Pango::EllipsizeMode::START);
        hbox->append(*lbl);
        
        // Make the whole row clickable by wrapping it in an event box / button without visual styling
        auto* btn = Gtk::manage(new Gtk::Button());
        btn->set_child(*hbox);
        btn->get_style_context()->add_class("flat");
        btn->set_halign(Gtk::Align::FILL);
        btn->signal_clicked().connect([this, path, recentBtn]{
            recentBtn->popdown();
            _pCtMainWin->file_open(path, "", "", "", true);
        });
        vbox->append(*btn);
        ++idx;
    }
    
    if (idx > 0) {
        // Add separator
        auto* sep = Gtk::manage(new Gtk::Separator{Gtk::Orientation::HORIZONTAL});
        vbox->append(*sep);
        
        // Add "Remove from list" with submenu
        
        // Create a regular button for "Remove from list" label
        auto* remove_label_btn = Gtk::manage(new Gtk::Button());
        auto* remove_label_hbox = Gtk::manage(new Gtk::Box{Gtk::Orientation::HORIZONTAL, 8});
        
        auto* remove_icon = Gtk::manage(new Gtk::Image());
        remove_icon->set_from_icon_name("edit-delete");
        remove_icon->set_icon_size(Gtk::IconSize::NORMAL);
        remove_label_hbox->append(*remove_icon);
        
        auto* remove_lbl = Gtk::manage(new Gtk::Label(_("Remove from list")));
        remove_lbl->set_halign(Gtk::Align::START);
        remove_lbl->set_hexpand(true);
        remove_label_hbox->append(*remove_lbl);
        
        remove_label_btn->set_child(*remove_label_hbox);
        remove_label_btn->get_style_context()->add_class("flat");
        remove_label_btn->set_halign(Gtk::Align::FILL);
        
        // Build the submenu for removing documents
        auto* remove_pop = Gtk::manage(new Gtk::Popover());
        auto* remove_vbox = Gtk::manage(new Gtk::Box{Gtk::Orientation::VERTICAL, 0});
        
        for (const auto& path : recentDocsFilepaths) {
            auto* rem_hbox = Gtk::manage(new Gtk::Box{Gtk::Orientation::HORIZONTAL, 8});
            
            auto* rem_icon = Gtk::manage(new Gtk::Image());
            rem_icon->set_from_icon_name("folder");
            rem_icon->set_icon_size(Gtk::IconSize::NORMAL);
            rem_hbox->append(*rem_icon);
            
            auto* rem_lbl = Gtk::manage(new Gtk::Label(path.string()));
            rem_lbl->set_halign(Gtk::Align::START);
            rem_lbl->set_hexpand(true);
            rem_lbl->set_ellipsize(Pango::EllipsizeMode::START);
            rem_hbox->append(*rem_lbl);
            
            auto* rem_btn = Gtk::manage(new Gtk::Button());
            rem_btn->set_child(*rem_hbox);
            rem_btn->get_style_context()->add_class("flat");
            rem_btn->set_halign(Gtk::Align::FILL);
                            rem_btn->signal_clicked().connect([this, path, recentBtn]{
                recentBtn->popdown();
                _pCtConfig->recentDocsFilepaths.remove(path);
                _pCtMainWin->menu_set_items_recent_documents();
            });
            remove_vbox->append(*rem_btn);
        }
        remove_pop->set_child(*remove_vbox);
        remove_pop->set_autohide(true);
        
        // Create MenuButton for the "Remove from list" item with submenu
        auto* remove_menu_btn = Gtk::manage(new Gtk::MenuButton());
        remove_menu_btn->set_child(*remove_label_btn);
        remove_menu_btn->set_popover(*remove_pop);
        remove_menu_btn->set_halign(Gtk::Align::FILL);
        remove_menu_btn->get_style_context()->add_class("flat");
        vbox->append(*remove_menu_btn);
    } else {
        auto* lbl = Gtk::manage(new Gtk::Label(_("No recent documents")));
        lbl->set_margin_start(10);
        lbl->set_margin_end(10);
        lbl->set_margin_top(10);
        lbl->set_margin_bottom(10);
        vbox->append(*lbl);
    }
    
    auto* pop = Gtk::manage(new Gtk::Popover());
    pop->set_child(*vbox);
    pop->set_autohide(true);
    pop->signal_closed().connect([recentBtn]{
        if (recentBtn->get_active()) {
            recentBtn->set_active(false);
        }
    });
    recentBtn->set_popover(*pop);
}

Gtk::MenuButton* CtMenu::build_bookmarks_button4(std::list<std::tuple<gint64, Glib::ustring, const char*>>& bookmarks,
                                                 std::function<void(gint64)> bookmark_action,
                                                 const bool /*isTopMenu*/)
{
    auto* btn = Gtk::manage(new Gtk::MenuButton());
    btn->set_has_frame(false);
    btn->get_style_context()->add_class("ct-toolbar4-btn");
    btn->set_icon_name("bookmark-new");
    btn->set_tooltip_text(_("Bookmarks"));

    auto* pop = Gtk::manage(new Gtk::Popover());
    auto* vbox = Gtk::manage(new Gtk::Box{Gtk::Orientation::VERTICAL, 4});
    for (const auto& bk : bookmarks) {
        const gint64 id = std::get<0>(bk);
        const Glib::ustring title = std::get<1>(bk);
        auto* item = Gtk::manage(new Gtk::Button(title));
        item->set_halign(Gtk::Align::FILL);
        item->signal_clicked().connect([bookmark_action, id]{ bookmark_action(id); });
        vbox->append(*item);
    }
    if (bookmarks.empty()) {
        vbox->append(*Gtk::manage(new Gtk::Label(_("No bookmarks"))));
    }
    pop->set_child(*vbox);
    btn->set_popover(*pop);
    return btn;
}
#endif /* GTKMM_MAJOR_VERSION >= 4 */

#if GTKMM_MAJOR_VERSION < 4
static xmlpp::Attribute* get_attribute(xmlpp::Node* pNode, char const* name)
{
    xmlpp::Element* pElement = static_cast<xmlpp::Element*>(pNode);
    return pElement->get_attribute(name);
}
#endif /* GTKMM_MAJOR_VERSION < 4 */

CtMenuAction::CtMenuAction()
 : run_action(nullptr)
#if GTKMM_MAJOR_VERSION >= 4
 , signal_set_sensitive(nullptr)
 , signal_set_visible(nullptr)
#else
 , signal_set_sensitive(std::make_shared<sigc::signal<void, bool>>())
 , signal_set_visible(std::make_shared<sigc::signal<void, bool>>())
#endif
{}

#if GTKMM_MAJOR_VERSION < 4
static void on_menu_activate(void* /*pObject*/, CtMenuAction* pAction)
{
    if (pAction) {
        // this allows the menu to close before the action is executed
        Glib::signal_idle().connect_once([pAction](){
            pAction->run_action();
        });
    }
}
#endif /* GTKMM_MAJOR_VERSION < 4 */

const std::string& CtMenuAction::get_shortcut(CtConfig* pCtConfig) const
{
    const auto it = pCtConfig->customKbShortcuts.find(id);
    return it != pCtConfig->customKbShortcuts.end() ? it->second : built_in_shortcut;
}

bool CtMenuAction::is_shortcut_overridden(CtConfig* pCtConfig) const
{
    const auto it = pCtConfig->customKbShortcuts.find(id);
    if (pCtConfig->customKbShortcuts.end() == it) return false;
    if (it->second != built_in_shortcut) return true;
    // we have a value in the map but it's just like the default => cleanup
    pCtConfig->customKbShortcuts.erase(id);
    return false;
}

CtMenu::CtMenu(CtMainWin* pCtMainWin)
 : _pCtMainWin{pCtMainWin}
 , _pCtConfig{pCtMainWin->get_ct_config()}
{
#if GTKMM_MAJOR_VERSION < 4 && !defined(GTKMM_DISABLE_DEPRECATED)
    _pAccelGroup = Gtk::AccelGroup::create();
#endif /* GTKMM_MAJOR_VERSION < 4 && !defined(GTKMM_DISABLE_DEPRECATED) */
    _rGtkBuilder = Gtk::Builder::create();
    init_actions(pCtMainWin->get_ct_actions());
}

#if GTKMM_MAJOR_VERSION < 4 && !defined(GTKMM_DISABLE_DEPRECATED)
/*static*/Gtk::MenuItem* CtMenu::create_menu_item(Gtk::Menu* pMenu, const char* name, const char* image, const char* desc)
{
    return _add_menu_item_full(pMenu,
                               name,
                               image,
                               nullptr,
                               Glib::RefPtr<Gtk::AccelGroup>{},
                               desc,
                               nullptr,
                               nullptr,
                               nullptr);
}

/*static*/ Gtk::MenuItem* CtMenu::find_menu_item(Gtk::MenuShell* menuShell, std::string name)
{
    for (Gtk::Widget* child : menuShell->get_children()) {
        if (auto menuItem = dynamic_cast<Gtk::MenuItem*>(child)) {
            if (menuItem->get_name() == name)
                return menuItem;
            if (Gtk::Widget* menuItemChild = menuItem->get_child()) {
                if (menuItemChild->get_name() == name)
                    return menuItem;
            }
        }
    }

    // check first level menu items, these menu items have complicated structure
    for (Gtk::Widget* child : menuShell->get_children())
        if (auto menuItem = dynamic_cast<Gtk::MenuItem*>(child))
            if (Gtk::Menu* subMenu = menuItem->get_submenu())
                for (Gtk::Widget* subChild : subMenu->get_children())
                    if (auto subItem = dynamic_cast<Gtk::MenuItem*>(subChild))
                        if (Gtk::Widget* subItemChild = subItem->get_child())
                            if (subItemChild->get_name() == name)
                                return subItem; // it's right, not a subItemChild

    return nullptr;
}

/*static*/ Gtk::AccelLabel* CtMenu::get_accel_label(Gtk::MenuItem* item)
{
    if (auto box = dynamic_cast<Gtk::Box*>(item->get_child()))
        if (auto label = dynamic_cast<Gtk::AccelLabel*>(box->get_children().back()))
            return label;
    return nullptr;
}
#endif /* GTKMM_MAJOR_VERSION < 4 && !defined(GTKMM_DISABLE_DEPRECATED) */

#if GTKMM_MAJOR_VERSION < 4 && !defined(GTKMM_DISABLE_DEPRECATED)
std::vector<Gtk::Toolbar*> CtMenu::build_toolbars(Gtk::MenuToolButton*& pRecentDocsMenuToolButton, Gtk::ToolButton*& pToolButtonSave)
{
    pRecentDocsMenuToolButton = nullptr;
    std::vector<Gtk::Toolbar*> toolbars;
    _rGtkBuilder = Gtk::Builder::create(); // OrangeArk: rebuild-safe (a second toolbar rebuild must not hit duplicate ids)
    for (const auto& toolbar_str : _get_ui_str_toolbars()) {
        Gtk::Toolbar* pToolbar = nullptr;
        _rGtkBuilder->add_from_string(toolbar_str);
        _rGtkBuilder->get_widget("ToolBar" + std::to_string(toolbars.size()), pToolbar);
        toolbars.push_back(pToolbar);
        if (not pRecentDocsMenuToolButton) {
            _rGtkBuilder->get_widget("RecentDocs", pRecentDocsMenuToolButton);
        }
        if (not pToolButtonSave) {
            _rGtkBuilder->get_widget("ct_save", pToolButtonSave);
        }
    }
    // OrangeArk: populate the font family / size combo placeholders
    _apply_global_gtk_css();
    Gtk::ToolItem* pFontFamilyItem = nullptr;
    _rGtkBuilder->get_widget("FontFamilyCombo", pFontFamilyItem);
    if (pFontFamilyItem) {
        if (auto* pPicker = _setup_font_family_combo()) {
            pFontFamilyItem->add(*pPicker);
            pFontFamilyItem->show_all();
        }
    }
    Gtk::ToolItem* pFontSizeItem = nullptr;
    _rGtkBuilder->get_widget("FontSizeCombo", pFontSizeItem);
    if (pFontSizeItem) {
        if (auto* pPicker = _setup_font_size_combo()) {
            pFontSizeItem->add(*pPicker);
            pFontSizeItem->show_all();
        }
    }
    // OrangeArk: Word-style font colour / highlight buttons (A + live swatch + palette)
    Gtk::ToolItem* pColorFgItem = nullptr;
    _rGtkBuilder->get_widget("ColorFgItem", pColorFgItem);
    if (pColorFgItem) {
        if (auto* pPicker = _setup_colour_tool_button(true/*isForeground*/)) {
            pColorFgItem->add(*pPicker);
            pColorFgItem->show_all();
        }
    }
    Gtk::ToolItem* pColorBgItem = nullptr;
    _rGtkBuilder->get_widget("ColorBgItem", pColorBgItem);
    if (pColorBgItem) {
        if (auto* pPicker = _setup_colour_tool_button(false/*isForeground*/)) {
            pColorBgItem->add(*pPicker);
            pColorBgItem->show_all();
        }
    }
    // OrangeArk: bullet / numbered list-style buttons (icon + marker library dropdown)
    Gtk::ToolItem* pBulletListItem = nullptr;
    _rGtkBuilder->get_widget("BulletListItem", pBulletListItem);
    if (pBulletListItem) {
        if (auto* pPicker = _setup_list_style_tool_button(true/*isBullet*/)) {
            pBulletListItem->add(*pPicker);
            pBulletListItem->show_all();
        }
    }
    Gtk::ToolItem* pNumberListItem = nullptr;
    _rGtkBuilder->get_widget("NumberListItem", pNumberListItem);
    if (pNumberListItem) {
        if (auto* pPicker = _setup_list_style_tool_button(false/*isBullet*/)) {
            pNumberListItem->add(*pPicker);
            pNumberListItem->show_all();
        }
    }
    return toolbars;
}
#endif /* GTKMM_MAJOR_VERSION < 4 && !defined(GTKMM_DISABLE_DEPRECATED) */

#if GTKMM_MAJOR_VERSION < 4 && !defined(GTKMM_DISABLE_DEPRECATED)
// OrangeArk: toolbar font family picker — lists the system fonts (via Pango,
// shared enumeration in ct_list_picker.h)
Gtk::Button* CtMenu::_setup_font_family_combo()
{
    const std::vector<Glib::ustring> ordered = CtListPicker::ordered_font_families();
    std::vector<Glib::ustring> items;
    items.push_back(_("Font")); // prompt row (row 0)
    for (const auto& name : ordered) {
        items.push_back(name);
    }
    auto* pPicker = Gtk::manage(new CtListPicker::CtListPickerButton(items, 140));
    pPicker->signal_selected().connect([this](int idx, const Glib::ustring& text) {
        if (idx > 0) {
            _pCtMainWin->get_ct_actions()->apply_tag_font_family(text);
        }
    });
    return pPicker;
}

// OrangeArk: toolbar font size picker (scrolled popover list, see ct_list_picker.h)
Gtk::Button* CtMenu::_setup_font_size_combo()
{
    const std::vector<int> sizes = {8, 9, 10, 11, 12, 13, 14, 16, 18, 20, 22, 24, 28, 32, 36, 48, 72};
    std::vector<Glib::ustring> items;
    items.push_back(_("Font Size")); // prompt row (row 0)
    for (const int size : sizes) {
        items.push_back(std::to_string(size));
    }
    auto* pPicker = Gtk::manage(new CtListPicker::CtListPickerButton(items, 72));
    pPicker->signal_selected().connect([this](int idx, const Glib::ustring& text) {
        if (idx > 0) {
            _pCtMainWin->get_ct_actions()->apply_tag_font_size(text);
        }
    });
    return pPicker;
}

namespace {
// OrangeArk: a solid colour rectangle as a pixbuf (plain Gdk::Pixbuf, no CSS
// provider and no deprecated colour API — works on every GTK3 build)
Glib::RefPtr<Gdk::Pixbuf> make_colour_swatch(const Glib::ustring& colour, const int width, const int height)
{
    Gdk::RGBA rgba{colour.empty() ? Glib::ustring{"#000000"} : colour};
    auto to8 = [](const double v)->guint8 {
        int i = static_cast<int>(v * 255.0 + 0.5);
        if (i < 0) i = 0;
        if (i > 255) i = 255;
        return static_cast<guint8>(i);
    };
    const guint32 pixel = (static_cast<guint32>(to8(rgba.get_red())) << 24)
                        | (static_cast<guint32>(to8(rgba.get_green())) << 16)
                        | (static_cast<guint32>(to8(rgba.get_blue())) << 8)
                        | 0xffu;
    Glib::RefPtr<Gdk::Pixbuf> rPix = Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB, false/*has_alpha*/, 8, width, height);
    if (not rPix) return rPix;
    rPix->fill(pixel);
    // 1px frame so that white / very light swatches stay visible
    guint8* pPixels = rPix->get_pixels();
    const int stride = rPix->get_rowstride();
    const int nCh = rPix->get_n_channels();
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (0 == x or 0 == y or width - 1 == x or height - 1 == y) {
                guint8* p = pPixels + y * stride + x * nCh;
                p[0] = 0x60; p[1] = 0x60; p[2] = 0x60;
            }
        }
    }
    return rPix;
}

// OrangeArk: Word-style split colour button for the format toolbar
//    ┌────────┬───┐
//    │   A    │ ▾ │   <- main part applies the current colour, arrow opens the palette
//    │ ▬▬▬▬▬  │   │   <- live colour swatch, redrawn whenever the colour changes
//    └────────┴───┘
class CtColourToolButton : public Gtk::Box
{
public:
    CtColourToolButton(const std::string& iconName,
                       const Glib::ustring& mainTooltip,
                       const Glib::ustring& arrowTooltip,
                       Gtk::BuiltinIconSize iconSize)
    : Gtk::Box{Gtk::ORIENTATION_HORIZONTAL, 0}
    {
        get_style_context()->add_class("linked");

        auto* pIconImg = Gtk::manage(new Gtk::Image{});
        pIconImg->set_from_icon_name(iconName, iconSize);
        _pSwatch = Gtk::manage(new Gtk::Image{});
        auto* pVBox = Gtk::manage(new Gtk::Box{Gtk::ORIENTATION_VERTICAL, 1});
        pVBox->pack_start(*pIconImg, false, false);
        pVBox->pack_start(*_pSwatch, false, false);
        _btnMain.set_valign(Gtk::ALIGN_CENTER);
        _btnMain.add(*pVBox);
        _btnMain.set_tooltip_text(mainTooltip);
        _btnMain.signal_clicked().connect([this](){
            if (_colour.empty()) {
                _signalPickCustom.emit(); // nothing chosen yet — ask the user
                return;
            }
            _signalApply.emit(_colour);
        });
        pack_start(_btnMain, false, false);

        auto* pArrowImg = Gtk::manage(new Gtk::Image{});
        pArrowImg->set_from_icon_name("ct_arrow-down", Gtk::ICON_SIZE_MENU);
        _btnArrow.set_valign(Gtk::ALIGN_CENTER);
        _btnArrow.add(*pArrowImg);
        _btnArrow.set_tooltip_text(arrowTooltip);
        pack_start(_btnArrow, false, false);

        _build_palette();
        _btnArrow.set_popover(*_pPopover);
        _pPopover->show_all(); // OrangeArk fix: without this the popup opens but its contents stay invisible (blank popover)
        show_all();
        // OrangeArk: automated-verification hook — auto-open the popover (no input injection)
        if (g_getenv("ORANGEARK_POPOVER_SELFTEST")) {
            static int sSelfTestSeq = 0;
            Gtk::Popover* pPop = _pPopover;
            const int delayMs = 6000 + 5000 * (sSelfTestSeq++);
            Glib::signal_timeout().connect_once([pPop]() { if (pPop) pPop->popup(); }, delayMs);
        }
    }

    void set_colour(const Glib::ustring& colour)
    {
        _colour = colour;
        _pSwatch->set(make_colour_swatch(colour, 20, 5));
    }

    sigc::signal<void, const Glib::ustring&>& signal_apply() { return _signalApply; }
    sigc::signal<void>& signal_pick_custom() { return _signalPickCustom; }

private:
    void _build_palette()
    {
        static const std::vector<std::pair<const char*, const char*>> palette{
            {"#000000", "黑色"}, {"#c00000", "深红"}, {"#ff0000", "红色"},
            {"#ed7d31", "橙色"}, {"#ffc000", "黄色"},
            {"#a9d08e", "浅绿"}, {"#00b050", "绿色"}, {"#00b0f0", "青色"},
            {"#0070c0", "蓝色"}, {"#7030a0", "紫色"},
            {"#ffffff", "白色"}, {"#d9d9d9", "浅灰"}, {"#808080", "灰色"},
            {"#404040", "深灰"}, {"#7f6000", "褐色"}};

        _pPopover = Gtk::manage(new Gtk::Popover{});
        auto* pVBox = Gtk::manage(new Gtk::Box{Gtk::ORIENTATION_VERTICAL, 4});
        pVBox->set_border_width(6);
        auto* pGrid = Gtk::manage(new Gtk::Grid{});
        pGrid->set_row_spacing(3);
        pGrid->set_column_spacing(3);
        int col{0}, row{0};
        for (const auto& swatch : palette) {
            const Glib::ustring hex{swatch.first};
            auto* pBtn = Gtk::manage(new Gtk::Button{});
            pBtn->set_image(*Gtk::manage(new Gtk::Image{make_colour_swatch(hex, 24, 18)}));
            pBtn->set_tooltip_text(_(swatch.second));
            pBtn->signal_clicked().connect([this, hex](){
                _pPopover->popdown();
                set_colour(hex);
                _signalApply.emit(hex);
            });
            pGrid->attach(*pBtn, col, row, 1, 1);
            if (++col >= 5) { col = 0; ++row; }
        }
        pVBox->pack_start(*pGrid, false, false);
        pVBox->pack_start(*Gtk::manage(new Gtk::Separator{Gtk::ORIENTATION_HORIZONTAL}), false, false);

        auto* pBtnNone = Gtk::manage(new Gtk::Button{_("无颜色")});
        pBtnNone->signal_clicked().connect([this](){
            _pPopover->popdown();
            _signalApply.emit("-"); // "-" removes the colour, see CtActions::apply_tag
        });
        pVBox->pack_start(*pBtnNone, false, false);

        auto* pBtnMore = Gtk::manage(new Gtk::Button{_("其他颜色…")});
        pBtnMore->signal_clicked().connect([this](){
            _pPopover->popdown();
            _signalPickCustom.emit();
        });
        pVBox->pack_start(*pBtnMore, false, false);

        _pPopover->add(*pVBox);
    }

    Gtk::Button    _btnMain;
    Gtk::MenuButton _btnArrow;
    Gtk::Popover*  _pPopover{nullptr};
    Gtk::Image*    _pSwatch{nullptr};
    Glib::ustring  _colour;
    sigc::signal<void, const Glib::ustring&> _signalApply;
    sigc::signal<void> _signalPickCustom;
};

// OrangeArk: list-style split button for the format toolbar
//    ┌────────┬───┐
//    │ icon   │ ▾ │   <- main part toggles the list, arrow opens the marker library
//    └────────┴───┘
//    bullet library: the configured bullet chars (● ○ ■ ◆ ★ ✓ …)
//    number library: "1." "1)" "1-" "1>" "(1)" "一、"
class CtListStyleToolButton : public Gtk::Box
{
public:
    CtListStyleToolButton(const bool isBullet,
                          const std::string& iconName,
                          const Glib::ustring& mainTooltip,
                          const Glib::ustring& arrowTooltip,
                          Gtk::BuiltinIconSize iconSize,
                          const CtConfig* pCtConfig)
    : Gtk::Box{Gtk::ORIENTATION_HORIZONTAL, 0}
    , _isBullet{isBullet}
    , _pCtConfig{pCtConfig}
    {
        get_style_context()->add_class("linked");

        auto* pIconImg = Gtk::manage(new Gtk::Image{});
        pIconImg->set_from_icon_name(iconName, iconSize);
        _btnMain.set_valign(Gtk::ALIGN_CENTER);
        _btnMain.add(*pIconImg);
        _btnMain.set_tooltip_text(mainTooltip);
        _btnMain.signal_clicked().connect([this](){ _signalToggle.emit(); });
        pack_start(_btnMain, false, false);

        auto* pArrowImg = Gtk::manage(new Gtk::Image{});
        pArrowImg->set_from_icon_name("ct_arrow-down", Gtk::ICON_SIZE_MENU);
        _btnArrow.set_valign(Gtk::ALIGN_CENTER);
        _btnArrow.add(*pArrowImg);
        _btnArrow.set_tooltip_text(arrowTooltip);
        pack_start(_btnArrow, false, false);

        _build_library();
        _btnArrow.set_popover(*_pPopover);
        _pPopover->show_all(); // OrangeArk fix: without this the popup opens but its contents stay invisible (blank popover)
        show_all();
        // OrangeArk: automated-verification hook — auto-open the popover (no input injection)
        if (g_getenv("ORANGEARK_POPOVER_SELFTEST")) {
            static int sSelfTestSeq = 2; // colour fg/bg popovers take slots 0/1, bullet/number take 2/3
            Gtk::Popover* pPop = _pPopover;
            const int delayMs = 6000 + 5000 * (sSelfTestSeq++);
            Glib::signal_timeout().connect_once([pPop]() { if (pPop) pPop->popup(); }, delayMs);
        }
    }

    sigc::signal<void>& signal_toggle() { return _signalToggle; }
    sigc::signal<void, int>& signal_pick() { return _signalPick; }

private:
    void _build_library()
    {
        _pPopover = Gtk::manage(new Gtk::Popover{});
        auto* pVBox = Gtk::manage(new Gtk::Box{Gtk::ORIENTATION_VERTICAL, 4});
        pVBox->set_border_width(6);
        auto* pGrid = Gtk::manage(new Gtk::Grid{});
        pGrid->set_row_spacing(3);
        pGrid->set_column_spacing(3);
        int col{0}, row{0};
        auto f_attach = [&](Gtk::Button* pBtn, int auxIdx){
            pBtn->signal_clicked().connect([this, auxIdx](){
                _pPopover->popdown();
                _signalPick.emit(auxIdx);
            });
            pGrid->attach(*pBtn, col, row, 1, 1);
            if (++col >= 5) { col = 0; ++row; }
        };
        if (_isBullet) {
            // the configured bullet library (bullet char buttons)
            for (size_t i = 0; i < _pCtConfig->charsListbul.size(); ++i) {
                auto* pBtn = Gtk::manage(new Gtk::Button{_pCtConfig->charsListbul[i]});
                pBtn->set_tooltip_text(Glib::ustring::compose(_("符号 %1"), _pCtConfig->charsListbul[i]));
                f_attach(pBtn, static_cast<int>(i));
            }
        }
        else {
            // the numbered-style library, labels mirror CtList::number_leading_string
            const std::vector<Glib::ustring> labels{"1.", "1)", "1-", "1>", "(1)", "一、"};
            for (size_t i = 0; i < labels.size(); ++i) {
                auto* pBtn = Gtk::manage(new Gtk::Button{labels[i]});
                pBtn->set_tooltip_text(Glib::ustring::compose(_("编号样式 %1"), labels[i]));
                f_attach(pBtn, static_cast<int>(i));
            }
        }
        pVBox->pack_start(*pGrid, false, false);
        _pPopover->add(*pVBox);
    }

    const bool _isBullet;
    const CtConfig* _pCtConfig;
    Gtk::Button    _btnMain;
    Gtk::MenuButton _btnArrow;
    Gtk::Popover*  _pPopover{nullptr};
    sigc::signal<void> _signalToggle;
    sigc::signal<void, int> _signalPick;
};
} // anonymous namespace

// OrangeArk: build the Word-style font colour / highlight toolbar button
Gtk::Widget* CtMenu::_setup_colour_tool_button(const bool isForeground)
{
    auto* pBtn = Gtk::manage(new CtColourToolButton{
        isForeground ? "ct_color_fg" : "ct_color_bg",
        isForeground ? _("字体颜色：按当前颜色着色") : _("突出显示：按当前颜色着色"),
        _("选择颜色"),
        CtMiscUtil::getIconSize(_pCtConfig->toolbarIconSize)});
    pBtn->signal_apply().connect([this, isForeground](const Glib::ustring& colour){
        if (isForeground) _pCtMainWin->get_ct_actions()->apply_tag_foreground_colour(colour);
        else              _pCtMainWin->get_ct_actions()->apply_tag_background_colour(colour);
    });
    pBtn->signal_pick_custom().connect([this, isForeground, pBtn](){
        // open the full colour chooser and remember whatever the user picked
        if (isForeground) _pCtMainWin->get_ct_actions()->apply_tag_foreground();
        else              _pCtMainWin->get_ct_actions()->apply_tag_background();
        pBtn->set_colour(isForeground ? _pCtConfig->currColour_fg : _pCtConfig->currColour_bg);
    });
    Glib::ustring startColour = isForeground ? _pCtConfig->currColour_fg : _pCtConfig->currColour_bg;
    if (startColour.empty()) startColour = isForeground ? "#000000" : "#ffff00";
    pBtn->set_colour(startColour);
    return pBtn;
}

// OrangeArk: bullet / numbered list-style split buttons (icon + marker library)
Gtk::Widget* CtMenu::_setup_list_style_tool_button(const bool isBullet)
{
    auto* pBtn = Gtk::manage(new CtListStyleToolButton{
        isBullet,
        isBullet ? "ct_list_bulleted" : "ct_list_numbered",
        isBullet ? _("设置/取消符号列表") : _("设置/取消编号列表"),
        isBullet ? _("项目符号库") : _("编号样式库"),
        CtMiscUtil::getIconSize(_pCtConfig->toolbarIconSize),
        _pCtConfig});
    pBtn->signal_toggle().connect([this, isBullet](){
        if (isBullet) _pCtMainWin->get_ct_actions()->list_bulleted_handler();
        else          _pCtMainWin->get_ct_actions()->list_numbered_handler();
    });
    pBtn->signal_pick().connect([this, isBullet](const int aux){
        if (isBullet) _pCtMainWin->get_ct_actions()->apply_bullet_list(aux);
        else          _pCtMainWin->get_ct_actions()->apply_number_list(aux);
    });
    return pBtn;
}
#endif /* GTKMM_MAJOR_VERSION < 4 && !defined(GTKMM_DISABLE_DEPRECATED) */

#if GTKMM_MAJOR_VERSION < 4 && !defined(GTKMM_DISABLE_DEPRECATED)
Gtk::MenuBar* CtMenu::build_menubar()
{
    Gtk::MenuBar* pMenuBar = Gtk::manage(new Gtk::MenuBar());
    _walk_menu_xml(pMenuBar, _get_ui_str_menu(), nullptr);
    return pMenuBar;
}
#endif /* GTKMM_MAJOR_VERSION < 4 && !defined(GTKMM_DISABLE_DEPRECATED) */

#if GTKMM_MAJOR_VERSION < 4 && !defined(GTKMM_DISABLE_DEPRECATED)
Gtk::Menu* CtMenu::build_bookmarks_menu(std::list<std::tuple<gint64, Glib::ustring, const char*>>& bookmarks,
                                        sigc::slot<void, gint64>& bookmark_action,
                                        const bool isTopMenu)
{
    Gtk::Menu* pMenu = Gtk::manage(new Gtk::Menu{});
    if (isTopMenu) {
        // disconnect signals connected to prev submenu
        for (sigc::connection& sigc_conn : _curr_bookm_submenu_sigc_conn) {
            sigc_conn.disconnect();
        }
        _curr_bookm_submenu_sigc_conn.clear();
        _add_menu_item(pMenu, find_action("node_bookmark"), &_curr_bookm_submenu_sigc_conn);
        _add_menu_item(pMenu, find_action("node_unbookmark"), &_curr_bookm_submenu_sigc_conn);
        _add_menu_separator(pMenu);
    }
    _add_menu_item(pMenu, find_action("handle_bookmarks"));
    _add_menu_separator(pMenu);
    if (bookmarks.empty()) {
        // OrangeArk: show a placeholder so the menu is never blank
        auto* pEmptyItem = Gtk::manage(new Gtk::MenuItem("（暂无书签：右键节点可添加书签）"));
        pEmptyItem->set_sensitive(false);
        pEmptyItem->show_all();
        pMenu->append(*pEmptyItem);
    }
    for (const auto& bookmark : bookmarks) {
        const gint64& node_id = std::get<0>(bookmark);
        const Glib::ustring& node_name = std::get<1>(bookmark);
        const char* node_icon = std::get<2>(bookmark);
        Gtk::MenuItem* pMenuItem = _add_menu_item_full(pMenu,
                                                       node_name.c_str(),
                                                       node_icon,
                                                       nullptr,
                                                       _pAccelGroup,
                                                       _pCtConfig->menusTooltips ? node_name.c_str() : nullptr,
                                                       nullptr,
                                                       nullptr,
                                                       nullptr);
        pMenuItem->signal_activate().connect(sigc::bind(bookmark_action, node_id));
    }
    return pMenu;
}
#endif /* GTKMM_MAJOR_VERSION < 4 && !defined(GTKMM_DISABLE_DEPRECATED) */

#if GTKMM_MAJOR_VERSION < 4 && !defined(GTKMM_DISABLE_DEPRECATED)
Gtk::Menu* CtMenu::build_recent_docs_menu(const CtRecentDocsFilepaths& recentDocsFilepaths,
                                          sigc::slot<void, const std::string&>& recent_doc_open_action,
                                          sigc::slot<void, const std::string&>& recent_doc_rm_action)
{
    Gtk::Menu* pMenu = Gtk::manage(new Gtk::Menu());
    for (const fs::path& filepath : recentDocsFilepaths) {
        bool file_exists = fs::exists(filepath);
        Gtk::MenuItem* pMenuItem = _add_menu_item_full(pMenu,
                                                       filepath.c_str(),
                                                       file_exists ? "ct_open" : "ct_urgent",
                                                       nullptr,
                                                       _pAccelGroup,
                                                       _pCtConfig->menusTooltips ? filepath.c_str() : nullptr,
                                                       nullptr,
                                                       nullptr,
                                                       nullptr,
                                                       nullptr,
                                                       false/*use_underline*/);
        pMenuItem->signal_activate().connect(sigc::bind(recent_doc_open_action, filepath.string()));
    }
    Gtk::MenuItem* pMenuItemRm = _add_menu_item_full(pMenu,
                                                     _("Remove from list"),
                                                     "ct_edit_delete",
                                                     nullptr,
                                                     _pAccelGroup,
                                                     _pCtConfig->menusTooltips ? _("Remove from list") : nullptr,
                                                     nullptr,
                                                     nullptr,
                                                     nullptr);
    Gtk::Menu* pMenuRm = Gtk::manage(new Gtk::Menu());
    pMenuItemRm->set_submenu(*pMenuRm);
    for (const fs::path& filepath : recentDocsFilepaths) {
        bool file_exists = fs::exists(filepath);
        Gtk::MenuItem* pMenuItem = _add_menu_item_full(pMenuRm,
                                                       filepath.c_str(),
                                                       file_exists ? "ct_edit_delete" : "ct_urgent",
                                                       nullptr,
                                                       _pAccelGroup,
                                                       _pCtConfig->menusTooltips ? filepath.c_str() : nullptr,
                                                       nullptr,
                                                       nullptr,
                                                       nullptr);
        pMenuItem->signal_activate().connect(sigc::bind(recent_doc_rm_action, filepath.string()));
    }
    return pMenu;
}

Gtk::Menu* CtMenu::get_popup_menu(POPUP_MENU_TYPE popupMenuType)
{
    if (_popupMenus[popupMenuType] == nullptr) {
        Gtk::Menu* pMenu = Gtk::manage(new Gtk::Menu{});
        build_popup_menu(pMenu, popupMenuType);
        pMenu->attach_to_widget(*_pCtMainWin);
        if (popupMenuType == POPUP_MENU_TYPE::Image or
            popupMenuType == POPUP_MENU_TYPE::Anchor or
            popupMenuType == POPUP_MENU_TYPE::Latex or
            popupMenuType == POPUP_MENU_TYPE::EmbFile) {
            const char* typeName =
                popupMenuType == POPUP_MENU_TYPE::Image  ? "Image"  :
                popupMenuType == POPUP_MENU_TYPE::Anchor ? "Anchor" :
                popupMenuType == POPUP_MENU_TYPE::Latex  ? "Latex"  : "EmbFile";
            pMenu->signal_deactivate().connect([typeName]{
                spdlog::debug("popup_deactivate {}", typeName);
            });
        }
        _popupMenus[popupMenuType] = pMenu;
    }
    return _popupMenus[popupMenuType];
}

void CtMenu::build_popup_menu(Gtk::Menu* pMenu, POPUP_MENU_TYPE popupMenuType)
{
    switch (popupMenuType) {
        case CtMenu::POPUP_MENU_TYPE::Node: _walk_menu_xml(pMenu, _get_ui_str_menu(), "/menubar/menu[@action='TreeMenu']/*"); break;
        case CtMenu::POPUP_MENU_TYPE::Text: _walk_menu_xml(pMenu, _get_popup_menu_ui_str_text(), nullptr); break;
        case CtMenu::POPUP_MENU_TYPE::Code: _walk_menu_xml(pMenu, _get_popup_menu_ui_str_code(), nullptr); break;
        case CtMenu::POPUP_MENU_TYPE::Image: _walk_menu_xml(pMenu, _get_popup_menu_ui_str_image(), nullptr); break;
        case CtMenu::POPUP_MENU_TYPE::Latex: _walk_menu_xml(pMenu, _get_popup_menu_ui_str_latex(), nullptr); break;
        case CtMenu::POPUP_MENU_TYPE::Anchor: _walk_menu_xml(pMenu, _get_popup_menu_ui_str_anchor(), nullptr); break;
        case CtMenu::POPUP_MENU_TYPE::EmbFile: _walk_menu_xml(pMenu, _get_popup_menu_ui_str_embfile(), nullptr); break;
        case CtMenu::POPUP_MENU_TYPE::Terminal: _walk_menu_xml(pMenu, _get_popup_menu_ui_str_terminal(), nullptr); break;
        case CtMenu::POPUP_MENU_TYPE::Link: {
            _add_menu_separator(pMenu);
            _add_menu_item(pMenu, find_action("apply_tag_link"));
            _add_menu_separator(pMenu);
            _add_menu_item(pMenu, find_action("link_cut"));
            _add_menu_item(pMenu, find_action("link_copy"));
            _add_menu_item(pMenu, find_action("link_dismiss"));
            _add_menu_item(pMenu, find_action("link_delete"));
        } break;
        case CtMenu::POPUP_MENU_TYPE::Codebox: {
            _add_menu_separator(pMenu);
            _add_menu_item(pMenu, find_action("cut_plain"));
            _add_menu_item(pMenu, find_action("copy_plain"));
            _add_menu_separator(pMenu);
            _add_menu_item(pMenu, find_action("codebox_load_from_file"));
            _add_menu_item(pMenu, find_action("codebox_save_to_file"));
            _add_menu_separator(pMenu);
            _add_menu_item(pMenu, find_action("codebox_cut"));
            _add_menu_item(pMenu, find_action("codebox_copy"));
            _add_menu_item(pMenu, find_action("codebox_copy_content"));
            _add_menu_item(pMenu, find_action("codebox_delete"));
            _add_menu_item(pMenu, find_action("codebox_delete_keeping_text"));
            _add_menu_separator(pMenu);
            _add_menu_item(pMenu, find_action("codebox_increase_width"));
            _add_menu_item(pMenu, find_action("codebox_decrease_width"));
            _add_menu_item(pMenu, find_action("codebox_increase_height"));
            _add_menu_item(pMenu, find_action("codebox_decrease_height"));
            _add_menu_separator(pMenu);
            _add_menu_item(pMenu, find_action("exec_code_all"));
            _add_menu_item(pMenu, find_action("exec_code_los"));
            _add_menu_item(pMenu, find_action("strip_trail_spaces"));
            _add_menu_item(pMenu, find_action("repl_tabs_spaces"));
            _add_menu_separator(pMenu);
            _add_menu_item(pMenu, find_action("codebox_change_properties"));
        } break;
        case CtMenu::POPUP_MENU_TYPE::PopupMenuNum: {
        } break;
    }
}

void CtMenu::build_popup_menu_table_cell(Gtk::Menu* pMenu,
                                         const bool first_row,
                                         const bool first_col,
                                         const bool last_row,
                                         const bool last_col)
{
    _add_menu_separator(pMenu);
    _add_menu_item(pMenu, find_action("table_cut"));
    _add_menu_item(pMenu, find_action("table_copy"));
    _add_menu_item(pMenu, find_action("table_delete"));
    _add_menu_separator(pMenu);
    _add_menu_item(pMenu, find_action("table_column_add"));
    _add_menu_item(pMenu, find_action("table_column_cut"));
    _add_menu_item(pMenu, find_action("table_column_copy"));
    _add_menu_item(pMenu, find_action("table_column_paste"));
    _add_menu_item(pMenu, find_action("table_column_delete"));
    _add_menu_separator(pMenu);
    if (not first_col) _add_menu_item(pMenu, find_action("table_column_left"));
    if (not last_col) _add_menu_item(pMenu, find_action("table_column_right"));
    _add_menu_separator(pMenu);
    _add_menu_item(pMenu, find_action("table_column_increase_width"));
    _add_menu_item(pMenu, find_action("table_column_decrease_width"));
    _add_menu_separator(pMenu);
    _add_menu_item(pMenu, find_action("table_row_add"));
    _add_menu_item(pMenu, find_action("table_row_cut"));
    _add_menu_item(pMenu, find_action("table_row_copy"));
    _add_menu_item(pMenu, find_action("table_row_paste"));
    _add_menu_item(pMenu, find_action("table_row_delete"));
    _add_menu_separator(pMenu);
    if (not first_row) _add_menu_item(pMenu, find_action("table_row_up"));
    if (not last_row) _add_menu_item(pMenu, find_action("table_row_down"));
    _add_menu_item(pMenu, find_action("table_rows_sort_ascending"));
    _add_menu_item(pMenu, find_action("table_rows_sort_descending"));
    _add_menu_separator(pMenu);
    _add_menu_item(pMenu, find_action("table_edit_properties"));
    _add_menu_item(pMenu, find_action("table_export"));
}

void CtMenu::_walk_menu_xml(Gtk::MenuShell* pMenuShell, const char* document, const char* xpath)
{
    xmlpp::DomParser parser;
    if (not CtXmlHelper::safe_parse_memory(parser, document)) {
        return;
    }
    if (xpath) {
        _walk_menu_xml(pMenuShell, parser.get_document()->get_root_node()->find(xpath)[0]);
    }
    else {
        _walk_menu_xml(pMenuShell, parser.get_document()->get_root_node());
    }
}

void CtMenu::_walk_menu_xml(Gtk::MenuShell* pMenuShell, xmlpp::Node* pNode)
{
    for (xmlpp::Node* pNodeIter = pNode; pNodeIter; pNodeIter = pNodeIter->get_next_sibling()) {
        if (pNodeIter->get_name() == "menubar" || pNodeIter->get_name() == "popup") {
            _walk_menu_xml(pMenuShell, pNodeIter->get_first_child());
        }
        else if (pNodeIter->get_name() == "menu") {
            if (xmlpp::Attribute* pAttrName = get_attribute(pNodeIter, "_name")) // menu name which need to be translated
            {
                xmlpp::Attribute* pAttrImage = get_attribute(pNodeIter, "image");
                Gtk::Menu* pSubmenu = _add_menu_submenu(pMenuShell, pAttrName->get_value().c_str(), _(pAttrName->get_value().c_str()), pAttrImage->get_value().c_str());
                _walk_menu_xml(pSubmenu, pNodeIter->get_first_child());
            }
            else { // otherwise it is an action id
                CtMenuAction const* pAction = find_action(get_attribute(pNodeIter, "action")->get_value());
                Gtk::Menu* pSubmenu = _add_menu_submenu(pMenuShell, pAction->id.c_str(), pAction->name.c_str(), pAction->image.c_str());
                _walk_menu_xml(pSubmenu, pNodeIter->get_first_child());
            }
        }
        else if (pNodeIter->get_name() == "menuitem") {
            CtMenuAction* pAction = find_action(get_attribute(pNodeIter, "action")->get_value());
            if (pAction) _add_menu_item(pMenuShell, pAction);
        }
        else if (pNodeIter->get_name() == "separator") {
            _add_menu_separator(pMenuShell);
        }
    }
}

Gtk::Menu* CtMenu::_add_menu_submenu(Gtk::MenuShell* pMenuShell, const char* id, const char* name, const char* image)
{
    Gtk::MenuItem* pMenuItem = Gtk::manage(new Gtk::MenuItem{});
    pMenuItem->set_name(id);
    Gtk::AccelLabel* pLabel = Gtk::manage(new Gtk::AccelLabel{name, true});
    pLabel->set_xalign(0.0);
    pLabel->set_accel_widget(*pMenuItem);

    _add_menu_item_image_or_label(pMenuItem, image, pLabel);
    pMenuItem->get_child()->set_name(id); // for find_menu_item()
    pMenuItem->show_all();

    Gtk::Menu* pSubMenu = Gtk::manage(new Gtk::Menu{});
    pMenuItem->set_submenu(*pSubMenu);
    pMenuShell->append(*pMenuItem);
    return pSubMenu;
}

Gtk::MenuItem* CtMenu::_add_menu_item(Gtk::MenuShell* pMenuShell,
                                      CtMenuAction* pAction,
                                      std::list<sigc::connection>* pListConnections/*= nullptr*/)
{
    std::string shortcut = pAction->get_shortcut(_pCtConfig);
    Gtk::MenuItem* pMenuItem = _add_menu_item_full(pMenuShell,
                                                   pAction->name.c_str(),
                                                   pAction->image.c_str(),
                                                   shortcut.c_str(),
                                                   _pAccelGroup,
                                                   _pCtConfig->menusTooltips ? pAction->desc.c_str() : nullptr,
                                                   (gpointer)pAction,
                                                   pAction->signal_set_sensitive.get(),
                                                   pAction->signal_set_visible.get(),
                                                   pListConnections);
    pMenuItem->get_child()->set_name(pAction->id); // for find_menu_item();
    return pMenuItem;
}

// based on inkscape/src/ui/desktop/menubar.cpp
/*static*/Gtk::MenuItem* CtMenu::_add_menu_item_full(Gtk::MenuShell* pMenuShell,
                                                     const char* name,
                                                     const char* image,
                                                     const char* shortcut,
                                                     Glib::RefPtr<Gtk::AccelGroup> accelGroup,
                                                     const char* desc,
                                                     gpointer action_data,
                                                     sigc::signal<void, bool>* signal_set_sensitive,
                                                     sigc::signal<void, bool>* signal_set_visible,
                                                     std::list<sigc::connection>* pListConnections/*= nullptr*/,
                                                     const bool use_underline/*= true*/)
{
    Gtk::MenuItem* pMenuItem = Gtk::manage(new Gtk::MenuItem{});

    if (desc && strlen(desc)) {
        pMenuItem->set_tooltip_text(desc);
    }
    // Now create the label and add it to the menu item
    Gtk::AccelLabel* pLabel = Gtk::manage(new Gtk::AccelLabel{name, use_underline});
    pLabel->set_xalign(0.0);
    pLabel->set_accel_widget(*pMenuItem);
    if (shortcut && strlen(shortcut)) {
        Gtk::AccelKey accel_key(shortcut);
        pMenuItem->add_accelerator("activate", accelGroup, accel_key.get_key(), accel_key.get_mod(), Gtk::ACCEL_VISIBLE);
    }

    _add_menu_item_image_or_label(pMenuItem, image, pLabel);

    if (signal_set_sensitive) {
        sigc::connection conn = signal_set_sensitive->connect(
            sigc::bind<0>(
                sigc::ptr_fun(&gtk_widget_set_sensitive),
                GTK_WIDGET(pMenuItem->gobj())));
        if (pListConnections) {
            pListConnections->push_back(std::move(conn));
        }
    }
    if (signal_set_visible) {
        sigc::connection conn = signal_set_visible->connect(
            sigc::bind<0>(
                sigc::ptr_fun(&gtk_widget_set_visible),
                GTK_WIDGET(pMenuItem->gobj())));
        if (pListConnections) {
            pListConnections->push_back(std::move(conn));
        }
    }
    if (action_data) {
        gtk_widget_set_events(GTK_WIDGET(pMenuItem->gobj()), GDK_KEY_PRESS_MASK);
        g_signal_connect(G_OBJECT(pMenuItem->gobj()), "activate", G_CALLBACK(on_menu_activate), action_data);
    }

    pMenuItem->show_all();
    pMenuShell->append(*pMenuItem);

    return pMenuItem;
}

/*static*/ void CtMenu::_add_menu_item_image_or_label(Gtk::MenuItem* pMenuItem, const char* image, Gtk::AccelLabel* pLabel)
{
    if (image && strlen(image)) {
        pMenuItem->set_name("ImageMenuItem");  // custom name to identify our "ImageMenuItems"
        Gtk::Image* pIcon = Gtk::manage(new Gtk::Image{});
        pIcon->set_from_icon_name(image, Gtk::ICON_SIZE_MENU);

        // create a box to hold icon and label as GtkMenuItem derives from GtkBin and can only hold one child
        static const int cImageMenuItemSpacing{8};
        auto pBox = Gtk::manage(new Gtk::Box{Gtk::ORIENTATION_HORIZONTAL, cImageMenuItemSpacing});
        if (pMenuItem->get_direction() == Gtk::TEXT_DIR_RTL) {
            pBox->pack_end(*pIcon, false, false);
            pBox->pack_end(*pLabel, true, true);
        }
        else {
            pBox->pack_start(*pIcon, false, false);
            pBox->pack_start(*pLabel, true, true);
        }
        pMenuItem->add(*pBox);

        // to fix image placement in MenuBar /context menu
        // based on inkscape: src/ui/desktop/menu-icon-shift.cpp
        // we don't know which MenuItem will be first mapped, so connect all of them
        auto f_calculate_image_shift = [](Gtk::MenuItem* menuItem)->int{
            if (menuItem) {
                if (auto box = dynamic_cast<Gtk::Box*>(menuItem->get_child())) {
                    if (auto image = dynamic_cast<Gtk::Image*>(box->get_children()[0])) {
                        auto allocation_menuitem = menuItem->get_allocation();
                        auto allocation_image = image->get_allocation();
                        if (menuItem->get_direction() == Gtk::TEXT_DIR_RTL) {
                            return allocation_menuitem.get_width() - allocation_image.get_x() - allocation_image.get_width();
                        }
                        return -allocation_image.get_x();
                    }
                }
            }
            return 0;
        };
        static std::list<sigc::connection>* static_map_connectons = new std::list<sigc::connection>();
        if (static_map_connectons != nullptr) { // if null then the fix was applied
            static_map_connectons->push_back(pMenuItem->signal_map().connect([f_calculate_image_shift, pMenuItem]{
                spdlog::debug("shift images in MenuBar/context menu");
                const int shift = f_calculate_image_shift(pMenuItem);
                if (shift != 0) {
                    auto provider = Gtk::CssProvider::create();
                    auto const screen = Gdk::Screen::get_default();
                    Gtk::StyleContext::add_provider_for_screen(screen, provider, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
                    if (pMenuItem->get_direction() == Gtk::TEXT_DIR_RTL) {
                        provider->load_from_data("menuitem box { margin-right:" + std::to_string(shift - cImageMenuItemSpacing) + "px; }");
                    }
                    else {
                        provider->load_from_data("menuitem box { margin-left:" + std::to_string(shift + cImageMenuItemSpacing) + "px; }");
                    }
                }
                // we don't need to call this again, so kill all map connections
                for (auto& connections : *static_map_connectons)
                    connections.disconnect();
                delete static_map_connectons;
                static_map_connectons = nullptr;
            }));
        }
    }
    else {
        pMenuItem->add(*pLabel);
    }
}

Gtk::SeparatorMenuItem* CtMenu::_add_menu_separator(Gtk::MenuShell* pMenuShell)
{
    Gtk::SeparatorMenuItem* pSeparatorItem = Gtk::manage(new Gtk::SeparatorMenuItem());
    pSeparatorItem->show_all();
    pMenuShell->append(*pSeparatorItem);
    return pSeparatorItem;
}
#endif /* GTKMM_MAJOR_VERSION < 4 && !defined(GTKMM_DISABLE_DEPRECATED) */

