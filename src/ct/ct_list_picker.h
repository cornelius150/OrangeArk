/*
 * ct_list_picker.h - OrangeArk shared scrolling list picker button
 *
 * A MenuButton whose popover holds a Gtk::ListBox inside a
 * Gtk::ScrolledWindow, so the popup has a REAL classic scrollbar (draggable
 * slider + up/down stepper arrows). GTK3 renders ComboBoxText popups as
 * GtkMenu, which can never show a scrollbar (the
 * -GtkComboBox-appears-as-list style property is ignored in GTK3) — that was
 * exactly the "no slider + blank strip on top" dropdown complaint.
 *
 * Also hosts the shared system-font enumeration (via Pango) used by both the
 * main toolbar font picker and the screenshot text panel.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <gtkmm.h>
#include <pango/pangocairo.h>
#include <algorithm>
#include <vector>

#if GTKMM_MAJOR_VERSION < 4 && !defined(GTKMM_DISABLE_DEPRECATED)

namespace CtListPicker
{

// OrangeArk: enumerate the installed font families straight from the system
// (via the stable Pango C API — pangomm-1.4 lacks CairoFontMap::get_default)
inline std::vector<Glib::ustring> raw_font_families()
{
    std::vector<Glib::ustring> familyNames;
    try {
        if (PangoFontMap* pFontMap = pango_cairo_font_map_get_default()) {
            int nFamilies = 0;
            PangoFontFamily** ppFamilies = nullptr;
            pango_font_map_list_families(pFontMap, &ppFamilies, &nFamilies);
            for (int idx = 0; idx < nFamilies; ++idx) {
                if (const char* pName = pango_font_family_get_name(ppFamilies[idx])) {
                    if (*pName != '\0') {
                        familyNames.push_back(pName);
                    }
                }
            }
            g_free(ppFamilies);
        }
    }
    catch (const Glib::Error& e) {
        // enumeration is best-effort; an empty list falls back to the defaults below
    }
    catch (const std::exception&) {
    }
    std::sort(familyNames.begin(), familyNames.end(),
              [](const Glib::ustring& a, const Glib::ustring& b) { return a.lowercase() < b.lowercase(); });
    return familyNames;
}

// OrangeArk: drop the fonts that would render CJK text as boxes/garbage —
// symbol/dingbat fonts, Japanese-only families and ExtB glyph extensions
inline bool is_junk_font(const Glib::ustring& name)
{
    static const char* kExcluded[] = {
        "symbol", "wingdings", "wingdings 2", "wingdings 3", "webdings", "marlett",
        "mt extra", "ms gothic", "ms pgothic", "ms ui gothic", "yu gothic", "yu gothic ui",
        "meiryo", "meiryo ui", "malgun gothic", "gulim", "dotum", "batang", "mingliu",
        "eudc", "system", "fixedsys", "terminal", "small fonts", "ms sans serif", "ms serif",
        "modern", "roman", "script", "802", "outlook"};
    const Glib::ustring lower = name.lowercase();
    for (const char* bad : kExcluded) {
        if (lower == bad) return true;
    }
    // partial matches: glyph extension subsets and vertical variants
    if (lower.find("extb") != Glib::ustring::npos or lower.find("exta") != Glib::ustring::npos) return true;
    if (lower.rfind("@", 0) == 0) return true;
    return false;
}

// OrangeArk: system fonts for the pickers — common desktop fonts (incl. CJK)
// first, everything else alphabetically after them. Each preferred font
// matches either its English or localized name.
inline std::vector<Glib::ustring> ordered_font_families()
{
    const std::vector<std::pair<const char*, const char*>> common = {
        {"Microsoft YaHei", "微软雅黑"}, {"SimSun", "宋体"}, {"NSimSun", "新宋体"},
        {"SimHei", "黑体"}, {"KaiTi", "楷体"}, {"FangSong", "仿宋"}, {"DengXian", "等线"},
        {"Segoe UI", nullptr}, {"Arial", nullptr}, {"Times New Roman", nullptr},
        {"Courier New", nullptr}, {"Calibri", nullptr}, {"Consolas", nullptr},
        {"Verdana", nullptr}, {"Tahoma", nullptr}};
    std::vector<Glib::ustring> familyNames;
    for (const Glib::ustring& name : raw_font_families()) {
        if (not is_junk_font(name)) familyNames.push_back(name);
    }
    if (familyNames.empty()) {
        // Pango enumeration failed: at least offer the usual suspects
        for (const auto& preferred : common) familyNames.push_back(preferred.first);
        familyNames.push_back("Georgia");
        familyNames.push_back("Cambria");
        return familyNames;
    }
    std::vector<Glib::ustring> ordered;
    for (const auto& preferred : common) {
        for (const auto& name : familyNames) {
            const Glib::ustring lower = name.lowercase();
            if (lower == Glib::ustring(preferred.first).lowercase() or
                (preferred.second and lower == Glib::ustring(preferred.second))) {
                ordered.push_back(name);
                break;
            }
        }
    }
    for (const auto& name : familyNames) {
        bool isCommon = false;
        for (const auto& preferred : common) {
            const Glib::ustring lower = name.lowercase();
            if (lower == Glib::ustring(preferred.first).lowercase() or
                (preferred.second and lower == Glib::ustring(preferred.second))) {
                isCommon = true;
                break;
            }
        }
        if (not isCommon) ordered.push_back(name);
    }
    return ordered;
}

// OrangeArk: toolbar font/size picker — see the class comment at the top
// OrangeArk: toolbar font/size picker — see the class comment at the top.
// NOTE: the list opens in a small borderless popup Gtk::Window instead of a
// Gtk::Popover — this GTK 3.24.52 build renders GtkPopover bodies attached to
// widgets near the screen edge at wrong/off-screen positions, which made the
// list impossible to use. A plain popup window is deterministic.
class CtListPickerButton : public Gtk::Button
{
public:
    CtListPickerButton(const std::vector<Glib::ustring>& items, int viewWidthPx,
                       Gtk::PositionType pos = Gtk::POS_BOTTOM)
    {
        set_relief(Gtk::RELIEF_NONE);
        set_size_request(viewWidthPx, -1);
        set_halign(Gtk::Align::ALIGN_FILL);
        _pLabel = Gtk::manage(new Gtk::Label(items.empty() ? Glib::ustring{} : items.front()));
        _pLabel->set_halign(Gtk::Align::ALIGN_START);
        _pLabel->set_ellipsize(Pango::ELLIPSIZE_END);
        _pLabel->set_margin_start(4);
        add(*_pLabel);
        _pos = pos;

        for (const Glib::ustring& text : items) {
            auto* pRow = Gtk::manage(new Gtk::ListBoxRow());
            auto* pL = Gtk::manage(new Gtk::Label(text));
            pL->set_halign(Gtk::Align::ALIGN_START);
            pL->set_margin_top(2);
            pL->set_margin_bottom(2);
            pL->set_margin_start(8);
            pRow->add(*pL);
            _pListBox->append(*pRow);
            _pRowText.push_back(text);
        }
        _pListBox->set_activate_on_single_click(true);
        _pListBox->signal_row_activated().connect([this](Gtk::ListBoxRow* pRow) {
            if (not pRow) return;
            const int idx = pRow->get_index();
            if (idx < 0 or idx >= static_cast<int>(_pRowText.size())) return;
            _activeIdx = idx;
            _pLabel->set_text(_pRowText.at(static_cast<size_t>(idx)));
            _close_popup();
            _sigSelected.emit(idx, _pRowText.at(static_cast<size_t>(idx)));
        });

        _pScroll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_ALWAYS);
        // ~10 visible rows, but never taller than the item list itself
        const int rowH = 30;
        const int wantH = static_cast<int>(_pRowText.size()) * rowH + 8;
        _popupH = std::min(320, std::max(rowH, wantH));
        _popupW = viewWidthPx + 40;
        _pScroll->set_min_content_height(_popupH);
        _pScroll->set_min_content_width(_popupW);
        _pScroll->add(*_pListBox);

        signal_clicked().connect([this]() {
            if (_pPopup) _close_popup();
            else if (g_get_monotonic_time() - _lastCloseUs > 400000) _open_popup();
            // else: the popup was closed by focus-out moments ago because the
            // button press took focus — do not instantly reopen it
        });
    }

    ~CtListPickerButton()
    {
        _close_popup();
        _pScroll->unreference(); // we own the scroll (it is not managed)
    }

    int active_index() const { return _activeIdx; }
    Glib::ustring active_text() const
    {
        return (_activeIdx >= 0 and _activeIdx < static_cast<int>(_pRowText.size()))
            ? _pRowText.at(static_cast<size_t>(_activeIdx)) : Glib::ustring{};
    }
    void set_active_index(int idx)
    {
        _activeIdx = idx;
        if (idx >= 0 and idx < static_cast<int>(_pRowText.size())) {
            _pLabel->set_text(_pRowText.at(static_cast<size_t>(idx)));
        }
    }
    // OrangeArk: select a row by its text (case-insensitive), no signal emitted
    void set_active_text(const Glib::ustring& text)
    {
        for (size_t i = 0; i < _pRowText.size(); ++i) {
            if (_pRowText.at(i).lowercase() == text.lowercase()) {
                set_active_index(static_cast<int>(i));
                return;
            }
        }
    }
    sigc::signal<void(int, const Glib::ustring&)>& signal_selected() { return _sigSelected; }

private:
    // open a small borderless popup window right above (or below) the button
    void _open_popup()
    {
        if (_pPopup) return;
        _pPopup = new Gtk::Window(Gtk::WINDOW_TOPLEVEL);
        _pPopup->set_decorated(false);
        _pPopup->set_skip_taskbar_hint(true);
        _pPopup->set_skip_pager_hint(true);
        _pPopup->set_resizable(false);
        _pPopup->set_accept_focus(true);
        // NOTE: do NOT set a window type hint (e.g. COMBO) here — on the
        // Windows GDK backend hint windows can become click-through, which
        // silently breaks every row click
        // CRITICAL: keep the popup ABOVE the fullscreen screenshot overlay —
        // on Windows a plain toplevel would open underneath the overlay
        // window and be completely invisible
        if (Gtk::Window* pTop = dynamic_cast<Gtk::Window*>(get_toplevel())) {
            _pPopup->set_transient_for(*pTop);
        }
        _pPopup->set_keep_above(true);

        // place next to the button: above by default (the picker often sits
        // near the bottom edge), below when there is no room above
        int rx = 0, ry = 0;
        if (Glib::RefPtr<Gdk::Window> rWin = get_window()) {
            rWin->get_origin(rx, ry);
        }
        const Gtk::Allocation alloc = get_allocation();
        const int bx = rx + alloc.get_x();
        const int by = ry + alloc.get_y();
        int px = bx - 8;
        int py = (Gtk::POS_TOP == _pos) ? (by - _popupH - 4) : (by + alloc.get_height() + 4);
        if (Gtk::POS_BOTTOM == _pos) {
            // keep on-screen: flip above when the list would cross the bottom
            int scrH = 1080, scrW = 1920;
            try {
                const Glib::RefPtr<Gdk::Screen> rScreen = get_screen();
                if (rScreen) {
                    scrH = rScreen->get_height();
                    scrW = rScreen->get_width();
                }
            } catch (...) {}
            if (py + _popupH > scrH - 8) py = by - _popupH - 4;
        }
        if (px + _popupW > scr_width() - 8) px = std::max(4, scr_width() - _popupW - 8);
        if (px < 4) px = 4;
        _pPopup->move(px, std::max(4, py));
        _pScroll->set_size_request(_popupW, _popupH);
        _pPopup->add(*_pScroll);
        _pPopup->add_events(Gdk::BUTTON_PRESS_MASK | Gdk::FOCUS_CHANGE_MASK);
        // no pointer grab here: grabbing redirects the row clicks to the
        // popup toplevel on this GTK build, so the list rows never receive
        // them. Dismissal is handled by focus-out instead (clicking anything
        // outside the popup takes focus away and closes it).
        _pPopup->signal_focus_out_event().connect([this](GdkEventFocus*) {
            _close_popup();
            return false;
        });
        _pPopup->show_all();
        if (Glib::RefPtr<Gdk::Window> rPWin = _pPopup->get_window()) {
            rPWin->raise();
        }
        _pPopup->get_window()->get_origin(_popupX, _popupY);
    }

    void _close_popup()
    {
        if (not _pPopup) return;
        _lastCloseUs = g_get_monotonic_time();
        _pPopup->remove(); // detach the scroll so it survives the window
        _pPopup->hide();
        delete _pPopup;
        _pPopup = nullptr;
    }

    static int scr_width()
    {
        int w = 1920;
        try {
            const Glib::RefPtr<Gdk::Screen> rScreen = Gdk::Screen::get_default();
            if (rScreen) w = rScreen->get_width();
        } catch (...) {}
        return w;
    }

    Gtk::PositionType _pos{Gtk::POS_BOTTOM};
    Gtk::Window*  _pPopup{nullptr};
    gint64 _lastCloseUs{0}; // monotonic us of last popup close (toggle guard)
    // deliberately NOT Gtk::manage(): the scroll is moved in/out of the popup
    // window on every open/close, so it must hold its own reference —
    // a managed widget loses its ref on remove() and gets destroyed
    Gtk::ScrolledWindow* _pScroll{new Gtk::ScrolledWindow()};
    Gtk::ListBox* _pListBox{Gtk::manage(new Gtk::ListBox())};
    Gtk::Label*   _pLabel{nullptr};
    std::vector<Glib::ustring> _pRowText;
    int _activeIdx{-1};
    int _popupW{180}, _popupH{320}, _popupX{0}, _popupY{0};
    sigc::signal<void(int, const Glib::ustring&)> _sigSelected;
};

} // namespace CtListPicker

#endif /* GTKMM_MAJOR_VERSION < 4 && !defined(GTKMM_DISABLE_DEPRECATED) */
