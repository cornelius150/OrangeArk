/*
 * ct_screenshot.cc - OrangeArk QQ-style region screenshot
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */
#include "ct_screenshot.h"
#include "ct_main_win.h"
#include "ct_logging.h"
#include <cairo.h>
#include <cmath>

namespace CtScreenshot
{

// The full-screen overlay where the user drags a rectangle to select the region
class CtScreenshotSelector : public Gtk::Window
{
public:
    CtScreenshotSelector(Glib::RefPtr<Gdk::Pixbuf> rShot)
     : _rShot{rShot}
    {
        set_title("screenshot");
        set_decorated(false);
        set_resizable(false);
        set_modal(true);
        set_type_hint(Gdk::WindowTypeHint::WINDOW_TYPE_HINT_NORMAL);
        set_default_size(_rShot->get_width(), _rShot->get_height());

        signal_draw().connect(sigc::mem_fun(*this, &CtScreenshotSelector::_on_own_draw), false);
        signal_button_press_event().connect(sigc::mem_fun(*this, &CtScreenshotSelector::_on_button_press), false);
        signal_button_release_event().connect(sigc::mem_fun(*this, &CtScreenshotSelector::_on_button_release), false);
        signal_motion_notify_event().connect(sigc::mem_fun(*this, &CtScreenshotSelector::_on_motion_notify), false);
        signal_key_press_event().connect(sigc::mem_fun(*this, &CtScreenshotSelector::_on_key_press), false);

        add_events(Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK
                 | Gdk::POINTER_MOTION_MASK | Gdk::KEY_PRESS_MASK);

        _rCursorCross = Gdk::Cursor::create(Gdk::CursorType::CROSSHAIR);
    }

    Glib::RefPtr<Gdk::Pixbuf> get_result() const { return _rResult; }

    void start()
    {
        fullscreen();
        show_all();
        if (Glib::RefPtr<Gdk::Window> rWin = get_window()) {
            rWin->set_cursor(_rCursorCross);
        }
        grab_focus();
    }

protected:
    bool _on_own_draw(const Cairo::RefPtr<Cairo::Context>& cr)
    {
        if (not _rShot) return true;
        const int scrW = _rShot->get_width();
        const int scrH = _rShot->get_height();
        // draw the untouched screenshot
        Gdk::Cairo::set_source_pixbuf(cr, _rShot, 0.0, 0.0);
        cr->paint();

        // normalize selection rectangle
        const int selX = std::min(_selX1, _selX2);
        const int selY = std::min(_selY1, _selY2);
        const int selW = std::abs(_selX2 - _selX1);
        const int selH = std::abs(_selY2 - _selY1);

        // darken everything outside the selection (QQ-like effect)
        if (selW > 0 and selH > 0) {
            cr->set_source_rgba(0.0, 0.0, 0.0, 0.45);
            // top / bottom / left / right strips
            cr->rectangle(0.0, 0.0, scrW, selY);
            cr->rectangle(0.0, selY + selH, scrW, scrH - selY - selH);
            cr->rectangle(0.0, selY, selX, selH);
            cr->rectangle(selX + selW, selY, scrW - selX - selW, selH);
            cr->fill();
            // draw the selected region again, clear
            Gdk::Cairo::set_source_pixbuf(cr, _rShot, 0.0, 0.0);
            cr->rectangle(selX, selY, selW, selH);
            cr->fill();

            // orange selection border (QQ style)
            cr->set_source_rgba(1.0, 0.55, 0.1, 1.0);
            cr->set_line_width(1.6);
            cr->rectangle(selX + 0.5, selY + 0.5, selW - 1.0, selH - 1.0);
            cr->stroke();

            // size hint text
            const std::string hint = std::to_string(selW) + " x " + std::to_string(selH);
            cr->select_font_face("Sans", Cairo::FontSlant::FONT_SLANT_NORMAL, Cairo::FontWeight::FONT_WEIGHT_BOLD);
            cr->set_font_size(13.0);
            Cairo::TextExtents te;
            cr->get_text_extents(hint, te);
            double tx = selX + selW - te.width - 10.0;
            double ty = selY + selH + te.height + 8.0;
            if (ty + te.height > scrH) ty = selY + selH - te.height - 8.0;
            if (tx < 4.0) tx = selX + 6.0;
            cr->set_source_rgba(1.0, 1.0, 1.0, 0.9);
            cr->move_to(tx + 1, ty + 1);
            cr->show_text(hint);
            cr->set_source_rgba(0.9, 0.4, 0.0, 1.0);
            cr->move_to(tx, ty);
            cr->show_text(hint);
        }

        // bottom hint banner
        const Glib::ustring banner
            = _("Drag to select a region") + Glib::ustring{"   |   "}
            + _("Enter or double-click: confirm") + "   |   " + _("Esc: cancel");
        cr->select_font_face("Sans", Cairo::FontSlant::FONT_SLANT_NORMAL, Cairo::FontWeight::FONT_WEIGHT_NORMAL);
        cr->set_font_size(14.0);
        Cairo::TextExtents be;
        cr->get_text_extents(banner, be);
        const double bx = (scrW - be.width) / 2.0;
        const double by = scrH - 16.0;
        cr->set_source_rgba(0.0, 0.0, 0.0, 0.55);
        cr->rectangle(bx - 12.0, by - be.height - 6.0, be.width + 24.0, be.height + 12.0);
        cr->fill();
        cr->set_source_rgba(1.0, 1.0, 1.0, 1.0);
        cr->move_to(bx, by);
        cr->show_text(banner);

        return true;
    }

    bool _on_button_press(GdkEventButton* event)
    {
        if (1 != event->button) return false;
        if (GDK_2BUTTON_PRESS == event->type) {
            _confirm();
            return true;
        }
        // start (re)selecting
        _selecting = true;
        _selX1 = _selX2 = static_cast<int>(event->x);
        _selY1 = _selY2 = static_cast<int>(event->y);
        queue_draw();
        return true;
    }

    bool _on_motion_notify(GdkEventMotion* event)
    {
        if (not _selecting) return false;
        _selX2 = static_cast<int>(event->x);
        _selY2 = static_cast<int>(event->y);
        queue_draw();
        return true;
    }

    bool _on_button_release(GdkEventButton* event)
    {
        if (not _selecting or 1 != event->button) return false;
        _selecting = false;
        _selX2 = static_cast<int>(event->x);
        _selY2 = static_cast<int>(event->y);
        queue_draw();
        return true;
    }

    bool _on_key_press(GdkEventKey* event)
    {
        if (GDK_KEY_Escape == event->keyval) {
            _rResult.reset();
            hide();
            return true;
        }
        if (GDK_KEY_Return == event->keyval or GDK_KEY_KP_Enter == event->keyval) {
            _confirm();
            return true;
        }
        return false;
    }

private:
    void _confirm()
    {
        const int selX = std::min(_selX1, _selX2);
        const int selY = std::min(_selY1, _selY2);
        const int selW = std::abs(_selX2 - _selX1);
        const int selH = std::abs(_selY2 - _selY1);
        if (selW < 3 or selH < 3) return; // too small, keep selecting
        _rResult = Gdk::Pixbuf::create(_rShot->get_colorspace(), true/*has_alpha*/,
                                       _rShot->get_bits_per_sample(), selW, selH);
        _rShot->copy_area(selX, selY, selW, selH, _rResult, 0, 0);
        hide();
    }

private:
    Glib::RefPtr<Gdk::Pixbuf> _rShot;
    Glib::RefPtr<Gdk::Pixbuf> _rResult;
    Glib::RefPtr<Gdk::Cursor> _rCursorCross;
    bool _selecting{false};
    int  _selX1{0}, _selY1{0}, _selX2{-1}, _selY2{-1};
};

Glib::RefPtr<Gdk::Pixbuf> take_region_screenshot(CtMainWin* /*pCtMainWin*/)
{
    // grab the whole screen BEFORE showing the overlay
    Glib::RefPtr<Gdk::Window> rRoot = Gdk::Screen::get_default()->get_root_window();
    if (not rRoot) return Glib::RefPtr<Gdk::Pixbuf>{};
    Glib::RefPtr<Gdk::Pixbuf> rShot;
    try {
        rShot = Gdk::Pixbuf::create(rRoot, 0, 0, rRoot->get_width(), rRoot->get_height());
    }
    catch (...) {
        spdlog::warn("CtScreenshot: failed to grab the screen");
        return Glib::RefPtr<Gdk::Pixbuf>{};
    }
    if (not rShot) return Glib::RefPtr<Gdk::Pixbuf>{};

    CtScreenshotSelector selector{rShot};
    selector.start();
    Glib::RefPtr<Glib::MainLoop> rLoop = Glib::MainLoop::create();
    selector.signal_hide().connect([&rLoop]() { rLoop->quit(); });
    rLoop->run();
    return selector.get_result();
}

} // namespace CtScreenshot
