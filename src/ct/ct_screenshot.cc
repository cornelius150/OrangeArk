/*
 * ct_screenshot.cc - OrangeArk QQ-style region screenshot with annotation toolbar
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */
#include "ct_screenshot.h"
#include "ct_main_win.h"
#include "ct_logging.h"
#include <gdkmm/general.h>
#include <cairo.h>
#include <cmath>
#include <vector>

namespace CtScreenshot
{

// One annotation drawn by the user on top of the selection
struct CtAnnoShape
{
    enum class Type { Pen, Arrow, Rect, Ellipse, Text, Counter };
    Type                  type{Type::Pen};
    std::vector<Gdk::Point> pts;          // for Pen
    int                   x1{0}, y1{0}, x2{0}, y2{0}; // bounding for others
    Glib::ustring         text;
    int                   fontSize{16};
    int                   number{0};      // for Counter
};

// The full-screen overlay where the user drags a rectangle to select the region,
// then annotates it QQ-style with a floating toolbar
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

        _pArea = Gtk::manage(new Gtk::DrawingArea());
        _pArea->set_size_request(_rShot->get_width(), _rShot->get_height());
        _pArea->signal_draw().connect(sigc::mem_fun(*this, &CtScreenshotSelector::_on_own_draw), false);
        _pArea->signal_button_press_event().connect(sigc::mem_fun(*this, &CtScreenshotSelector::_on_button_press), false);
        _pArea->signal_button_release_event().connect(sigc::mem_fun(*this, &CtScreenshotSelector::_on_button_release), false);
        _pArea->signal_motion_notify_event().connect(sigc::mem_fun(*this, &CtScreenshotSelector::_on_motion_notify), false);
        _pArea->add_events(Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK | Gdk::POINTER_MOTION_MASK);

        signal_key_press_event().connect(sigc::mem_fun(*this, &CtScreenshotSelector::_on_key_press), false);
        add_events(Gdk::KEY_PRESS_MASK);

        // annotation toolbar (QQ style)
        _build_toolbar();
        _pEntry = Gtk::manage(new Gtk::Entry());
        _pEntry->set_no_show_all(true);
        _pEntry->signal_activate().connect(sigc::mem_fun(*this, &CtScreenshotSelector::_on_text_commit));
        _pEntry->signal_key_press_event().connect(sigc::mem_fun(*this, &CtScreenshotSelector::_on_entry_key_press), false);

        // the fixed container lets us position area/toolbar/entry freely
        _pFixed = Gtk::manage(new Gtk::Fixed());
        _pFixed->put(*_pArea, 0, 0);
        _pFixed->put(*_pToolbar, 10, 10);
        _pFixed->put(*_pEntry, 0, 0);
        add(*_pFixed);

        _rCursorCross = Gdk::Cursor::create(Gdk::CursorType::CROSSHAIR);
    }

    Glib::RefPtr<Gdk::Pixbuf> get_result() const { return _rResult; }

    void start()
    {
        fullscreen();
        show_all();
        _pEntry->hide();
        _pToolbar->hide();
        if (Glib::RefPtr<Gdk::Window> rWin = _pArea->get_window()) {
            rWin->set_cursor(_rCursorCross);
        }
        _pArea->grab_focus();
    }

protected:
    void _build_toolbar()
    {
        _pToolbar = Gtk::manage(new Gtk::Box{Gtk::ORIENTATION_HORIZONTAL, 2});
        _pToolbar->get_style_context()->add_class("toolbar");
        _pToolbar->set_margin_top(4);
        _pToolbar->set_margin_bottom(4);
        _pToolbar->set_margin_start(4);
        _pToolbar->set_margin_end(4);

        struct ToolDef { const char* label; const char* tip; };
        const std::vector<ToolDef> tools = {
            {"画笔", "自由绘制"}, {"箭头", "绘制箭头"}, {"矩形", "绘制矩形"},
            {"椭圆", "绘制椭圆"}, {"文字", "插入文字"}, {"序号", "插入自动递增的序号"},
            {"移动", "拖动已画的标注"}, {"撤销", "撤销上一个标注"}, {"重做", "重做标注"},
        };
        for (const ToolDef& td : tools) {
            auto* pBtn = Gtk::manage(new Gtk::Button(td.label));
            pBtn->set_tooltip_text(td.tip);
            pBtn->signal_clicked().connect([this, td]() { _on_tool_clicked(td.label); });
            _pToolbar->pack_start(*pBtn, Gtk::PACK_SHRINK);
        }
        auto* pSep = Gtk::manage(new Gtk::Separator{Gtk::ORIENTATION_VERTICAL});
        _pToolbar->pack_start(*pSep, Gtk::PACK_SHRINK);

        auto* pBtnSave = Gtk::manage(new Gtk::Button("保存"));
        pBtnSave->set_tooltip_text("把截图保存为 PNG 文件");
        pBtnSave->signal_clicked().connect(sigc::mem_fun(*this, &CtScreenshotSelector::_on_save_clicked));
        _pToolbar->pack_start(*pBtnSave, Gtk::PACK_SHRINK);

        auto* pBtnCancel = Gtk::manage(new Gtk::Button("取消"));
        pBtnCancel->set_tooltip_text("放弃本次截图");
        pBtnCancel->signal_clicked().connect(sigc::mem_fun(*this, &CtScreenshotSelector::_on_cancel_clicked));
        _pToolbar->pack_start(*pBtnCancel, Gtk::PACK_SHRINK);

        auto* pBtnOk = Gtk::manage(new Gtk::Button("✓ 确认"));
        pBtnOk->set_tooltip_text("复制到剪贴板并插入笔记");
        pBtnOk->signal_clicked().connect(sigc::mem_fun(*this, &CtScreenshotSelector::_confirm));
        _pToolbar->pack_start(*pBtnOk, Gtk::PACK_SHRINK);
    }

    void _on_tool_clicked(const char* label)
    {
        const std::string lab{label};
        if ("撤销" == lab) {
            if (not _shapes.empty()) {
                _redoShapes.push_back(_shapes.back());
                _shapes.pop_back();
                _pArea->queue_draw();
            }
            return;
        }
        if ("重做" == lab) {
            if (not _redoShapes.empty()) {
                _shapes.push_back(_redoShapes.back());
                _redoShapes.pop_back();
                _pArea->queue_draw();
            }
            return;
        }
        if ("画笔" == lab)    { _tool = Tool::Pen; }
        if ("箭头" == lab)    { _tool = Tool::Arrow; }
        if ("矩形" == lab)    { _tool = Tool::Rect; }
        if ("椭圆" == lab)    { _tool = Tool::Ellipse; }
        if ("文字" == lab)    { _tool = Tool::Text; }
        if ("序号" == lab)    { _tool = Tool::Counter; }
        if ("移动" == lab)    { _tool = Tool::Move; }
        _pArea->grab_focus();
    }

    void _on_save_clicked()
    {
        if (_sel_w() < 3 or _sel_h() < 3) return;
        Gtk::FileChooserDialog dialog(*this, "保存截图", Gtk::FILE_CHOOSER_ACTION_SAVE);
        dialog.add_button("取消", Gtk::RESPONSE_CANCEL);
        dialog.add_button("保存", Gtk::RESPONSE_ACCEPT);
        dialog.set_current_name("screenshot.png");
        dialog.set_do_overwrite_confirmation();
        Glib::RefPtr<Gtk::FileFilter> rFilter = Gtk::FileFilter::create();
        rFilter->set_name("PNG 图片");
        rFilter->add_pattern("*.png");
        dialog.add_filter(rFilter);
        if (Gtk::RESPONSE_ACCEPT != dialog.run()) return;
        const std::string filename = dialog.get_filename();
        dialog.hide();
        Glib::RefPtr<Gdk::Pixbuf> rSnap = _compose_result();
        if (rSnap) {
            try { rSnap->save(filename, "png"); }
            catch (const Glib::Error& e) { spdlog::warn("CtScreenshot: save failed: {}", e.what().c_str()); }
        }
    }

    void _on_cancel_clicked()
    {
        _rResult.reset();
        hide();
    }

    // -- geometry -------------------------------------------------------------
    int _sel_x() const { return std::min(_selX1, _selX2); }
    int _sel_y() const { return std::min(_selY1, _selY2); }
    int _sel_w() const { return std::abs(_selX2 - _selX1); }
    int _sel_h() const { return std::abs(_selY2 - _selY1); }
    bool _in_selection(double x, double y) const
    {
        return x >= _sel_x() and x <= _sel_x() + _sel_w()
           and y >= _sel_y() and y <= _sel_y() + _sel_h();
    }

    // -- drawing --------------------------------------------------------------
    // OrangeArk: pick a font that renders CJK on Windows (Sans alone shows boxes)
    static void _apply_font(const Cairo::RefPtr<Cairo::Context>& cr, const double size,
                            const Cairo::FontWeight weight = Cairo::FontWeight::FONT_WEIGHT_NORMAL)
    {
#ifdef _WIN32
        cr->select_font_face("Microsoft YaHei", Cairo::FontSlant::FONT_SLANT_NORMAL, weight);
#else
        cr->select_font_face("Sans", Cairo::FontSlant::FONT_SLANT_NORMAL, weight);
#endif
        cr->set_font_size(size);
    }

    // bounding box of a shape (for hit-testing when moving)
    void _shape_bbox(const CtAnnoShape& shape, int& bx, int& by, int& bw, int& bh) const
    {
        switch (shape.type) {
            case CtAnnoShape::Type::Pen: {
                int minX = INT_MAX, minY = INT_MAX, maxX = INT_MIN, maxY = INT_MIN;
                for (const Gdk::Point& pt : shape.pts) {
                    minX = std::min(minX, pt.get_x()); minY = std::min(minY, pt.get_y());
                    maxX = std::max(maxX, pt.get_x()); maxY = std::max(maxY, pt.get_y());
                }
                bx = minX; by = minY; bw = maxX - minX; bh = maxY - minY;
                break;
            }
            case CtAnnoShape::Type::Text:
                bx = shape.x1 - 2;
                by = shape.y1 - shape.fontSize;
                bw = static_cast<int>(shape.text.size() * shape.fontSize * 0.6) + 8;
                bh = shape.fontSize + 6;
                break;
            case CtAnnoShape::Type::Counter:
                bx = shape.x1 - shape.fontSize; by = shape.y1 - shape.fontSize;
                bw = shape.fontSize * 2; bh = shape.fontSize * 2;
                break;
            default:
                bx = std::min(shape.x1, shape.x2); by = std::min(shape.y1, shape.y2);
                bw = std::abs(shape.x2 - shape.x1); bh = std::abs(shape.y2 - shape.y1);
                break;
        }
        // generous margin for easier grabbing
        const int margin = 8;
        bx -= margin; by -= margin; bw += 2 * margin; bh += 2 * margin;
    }

    bool _shape_hit(const CtAnnoShape& shape, const int x, const int y) const
    {
        int bx = 0, by = 0, bw = 0, bh = 0;
        _shape_bbox(shape, bx, by, bw, bh);
        return x >= bx and x <= bx + bw and y >= by and y <= by + bh;
    }

    static void _translate_shape(CtAnnoShape& shape, const int dx, const int dy)
    {
        for (Gdk::Point& pt : shape.pts) {
            pt.set_x(pt.get_x() + dx);
            pt.set_y(pt.get_y() + dy);
        }
        shape.x1 += dx; shape.y1 += dy; shape.x2 += dx; shape.y2 += dy;
    }

    void _draw_shapes(const Cairo::RefPtr<Cairo::Context>& cr, const double dx, const double dy) const
    {
        for (const CtAnnoShape& shape : _shapes) {
            _draw_one_shape(cr, dx, dy, shape);
        }
    }

    void _draw_one_shape(const Cairo::RefPtr<Cairo::Context>& cr, const double dx, const double dy, const CtAnnoShape& shape) const
    {
        cr->set_source_rgba(1.0, 0.13, 0.1, 0.95); // QQ-like red pen
        cr->set_line_width(3.0);
            cr->set_line_cap(Cairo::LINE_CAP_ROUND);
            cr->set_line_join(Cairo::LINE_JOIN_ROUND);
            switch (shape.type) {
                case CtAnnoShape::Type::Pen: {
                    bool first = true;
                    for (const Gdk::Point& pt : shape.pts) {
                        if (first) { cr->move_to(pt.get_x() + dx, pt.get_y() + dy); first = false; }
                        else       { cr->line_to(pt.get_x() + dx, pt.get_y() + dy); }
                    }
                    cr->stroke();
                    break;
                }
                case CtAnnoShape::Type::Arrow: {
                    const double x1 = shape.x1 + dx, y1 = shape.y1 + dy;
                    const double x2 = shape.x2 + dx, y2 = shape.y2 + dy;
                    cr->move_to(x1, y1);
                    cr->line_to(x2, y2);
                    cr->stroke();
                    const double angle = std::atan2(y2 - y1, x2 - x1);
                    const double head = 14.0;
                    cr->move_to(x2, y2);
                    cr->line_to(x2 - head * std::cos(angle - 0.45), y2 - head * std::sin(angle - 0.45));
                    cr->line_to(x2 - head * std::cos(angle + 0.45), y2 - head * std::sin(angle + 0.45));
                    cr->close_path();
                    cr->fill();
                    break;
                }
                case CtAnnoShape::Type::Rect: {
                    cr->rectangle(std::min(shape.x1, shape.x2) + dx, std::min(shape.y1, shape.y2) + dy,
                                  std::abs(shape.x2 - shape.x1), std::abs(shape.y2 - shape.y1));
                    cr->stroke();
                    break;
                }
                case CtAnnoShape::Type::Ellipse: {
                    const double rx = std::abs(shape.x2 - shape.x1) / 2.0;
                    const double ry = std::abs(shape.y2 - shape.y1) / 2.0;
                    cr->save();
                    cr->translate(std::min(shape.x1, shape.x2) + dx + rx, std::min(shape.y1, shape.y2) + dy + ry);
                    cr->scale(std::max(rx, 1.0), std::max(ry, 1.0));
                    cr->arc(0.0, 0.0, 1.0, 0.0, 2.0 * M_PI);
                    cr->restore();
                    cr->stroke();
                    break;
                }
                case CtAnnoShape::Type::Text: {
                    _apply_font(cr, shape.fontSize);
                    cr->move_to(shape.x1 + dx, shape.y1 + dy);
                    cr->show_text(shape.text.c_str());
                    cr->stroke();
                    break;
                }
                case CtAnnoShape::Type::Counter: {
                    // QQ-style auto-increment numbered badge: red circle + white number
                    const double cx = shape.x1 + dx, cy = shape.y1 + dy;
                    const double r = std::max(10.0, shape.fontSize * 0.9);
                    cr->set_source_rgba(1.0, 0.13, 0.1, 0.95);
                    cr->arc(cx, cy, r, 0.0, 2.0 * M_PI);
                    cr->fill();
                    const std::string label = std::to_string(shape.number);
                    _apply_font(cr, r, Cairo::FontWeight::FONT_WEIGHT_BOLD);
                    Cairo::TextExtents te;
                    cr->get_text_extents(label, te);
                    cr->set_source_rgba(1.0, 1.0, 1.0, 1.0);
                    cr->move_to(cx - te.width / 2.0, cy + te.height / 2.0 - te.y_bearing / 2.0);
                    cr->show_text(label);
                    cr->stroke();
                    break;
                }
            }
    }

    bool _on_own_draw(const Cairo::RefPtr<Cairo::Context>& cr)
    {
        if (not _rShot) return true;
        const int scrW = _rShot->get_width();
        const int scrH = _rShot->get_height();
        // draw the untouched screenshot
        Gdk::Cairo::set_source_pixbuf(cr, _rShot, 0.0, 0.0);
        cr->paint();

        const int selX = _sel_x();
        const int selY = _sel_y();
        const int selW = _sel_w();
        const int selH = _sel_h();

        // darken everything outside the selection (QQ-like effect)
        if (selW > 0 and selH > 0) {
            cr->set_source_rgba(0.0, 0.0, 0.0, 0.45);
            cr->rectangle(0.0, 0.0, scrW, selY);
            cr->rectangle(0.0, selY + selH, scrW, scrH - selY - selH);
            cr->rectangle(0.0, selY, selX, selH);
            cr->rectangle(selX + selW, selY, scrW - selX - selW, selH);
            cr->fill();
            // draw the selected region again, clear
            Gdk::Cairo::set_source_pixbuf(cr, _rShot, 0.0, 0.0);
            cr->rectangle(selX, selY, selW, selH);
            cr->fill();

            // annotations clipped to the selection
            cr->save();
            cr->rectangle(selX, selY, selW, selH);
            cr->clip();
            _draw_shapes(cr, 0.0, 0.0);
            if (_previewing) {
                _draw_one_shape(cr, 0.0, 0.0, _currShape);
            }
            cr->restore();

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
        const Glib::ustring banner = _toolbar_shown
            ? Glib::ustring{"选工具标注 · 移动可拖动标注 · Enter 确认 · Esc 取消"}
            : _("Drag to select a region") + Glib::ustring{"   |   "}
            + _("Enter or double-click: confirm") + "   |   " + _("Esc: cancel");
        _apply_font(cr, 14.0);
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

    // -- mouse / keyboard ------------------------------------------------------
    bool _on_button_press(GdkEventButton* event)
    {
        if (1 != event->button) return false;
        if (GDK_2BUTTON_PRESS == event->type) {
            _confirm();
            return true;
        }
        if (_toolbar_shown and _in_selection(event->x, event->y)) {
            if (Tool::Text == _tool) {
                _show_text_entry(static_cast<int>(event->x), static_cast<int>(event->y));
                return true;
            }
            if (Tool::Counter == _tool) {
                // QQ-style numbered badge: the number is the count of existing badges + 1
                CtAnnoShape shape{};
                shape.type = CtAnnoShape::Type::Counter;
                shape.x1 = static_cast<int>(event->x);
                shape.y1 = static_cast<int>(event->y);
                shape.fontSize = std::max(16, _sel_h() / 15);
                int num = 1;
                for (const CtAnnoShape& s : _shapes) {
                    if (CtAnnoShape::Type::Counter == s.type) ++num;
                }
                shape.number = num;
                _shapes.push_back(shape);
                _redoShapes.clear();
                _pArea->queue_draw();
                return true;
            }
            if (Tool::Move == _tool) {
                // move the topmost annotation under the cursor
                const int px = static_cast<int>(event->x);
                const int py = static_cast<int>(event->y);
                for (int i = static_cast<int>(_shapes.size()) - 1; i >= 0; --i) {
                    if (_shape_hit(_shapes.at(static_cast<size_t>(i)), px, py)) {
                        _movingIdx = i;
                        _moveLastX = px;
                        _moveLastY = py;
                        return true;
                    }
                }
                return true; // nothing under the cursor; do not start a new selection
            }
            // start a new annotation
            _annotating = true;
            _previewing = true;
            _currShape = CtAnnoShape{};
            switch (_tool) {
                case Tool::Pen:    _currShape.type = CtAnnoShape::Type::Pen; break;
                case Tool::Arrow:  _currShape.type = CtAnnoShape::Type::Arrow; break;
                case Tool::Rect:   _currShape.type = CtAnnoShape::Type::Rect; break;
                case Tool::Ellipse:_currShape.type = CtAnnoShape::Type::Ellipse; break;
                default: _annotating = false; _previewing = false; return true;
            }
            _currShape.x1 = _currShape.x2 = static_cast<int>(event->x);
            _currShape.y1 = _currShape.y2 = static_cast<int>(event->y);
            _currShape.fontSize = std::max(16, _sel_h() / 15);
            if (Tool::Pen == _tool) {
                _currShape.pts.push_back(Gdk::Point(_currShape.x1, _currShape.y1));
            }
            _pArea->queue_draw();
            return true;
        }
        // start (re)selecting
        _selecting = true;
        _toolbar_shown = false;
        _pToolbar->hide();
        _shapes.clear();
        _redoShapes.clear();
        _selX1 = _selX2 = static_cast<int>(event->x);
        _selY1 = _selY2 = static_cast<int>(event->y);
        _pArea->queue_draw();
        return true;
    }

    bool _on_motion_notify(GdkEventMotion* event)
    {
        if (_selecting) {
            _selX2 = static_cast<int>(event->x);
            _selY2 = static_cast<int>(event->y);
            _pArea->queue_draw();
            return true;
        }
        if (_movingIdx >= 0 and static_cast<size_t>(_movingIdx) < _shapes.size()) {
            // translate the picked annotation
            const int px = static_cast<int>(event->x);
            const int py = static_cast<int>(event->y);
            _translate_shape(_shapes.at(static_cast<size_t>(_movingIdx)), px - _moveLastX, py - _moveLastY);
            _moveLastX = px;
            _moveLastY = py;
            _pArea->queue_draw();
            return true;
        }
        if (_annotating) {
            _currShape.x2 = static_cast<int>(event->x);
            _currShape.y2 = static_cast<int>(event->y);
            if (CtAnnoShape::Type::Pen == _currShape.type) {
                _currShape.pts.push_back(Gdk::Point(_currShape.x2, _currShape.y2));
            }
            _pArea->queue_draw(); // _currShape is drawn as a live preview
            return true;
        }
        return false;
    }

    bool _on_button_release(GdkEventButton* event)
    {
        if (1 != event->button) return false;
        if (_movingIdx >= 0) {
            _movingIdx = -1;
            return true;
        }
        if (_annotating) {
            _annotating = false;
            _previewing = false;
            _currShape.x2 = static_cast<int>(event->x);
            _currShape.y2 = static_cast<int>(event->y);
            const bool tiny = CtAnnoShape::Type::Pen != _currShape.type
                          and std::abs(_currShape.x2 - _currShape.x1) < 3
                          and std::abs(_currShape.y2 - _currShape.y1) < 3;
            if (not tiny and CtAnnoShape::Type::Pen != _currShape.type) {
                _shapes.push_back(_currShape);
                _redoShapes.clear();
            }
            else if (CtAnnoShape::Type::Pen == _currShape.type and _currShape.pts.size() > 1) {
                _shapes.push_back(_currShape);
                _redoShapes.clear();
            }
            _pArea->queue_draw();
            return true;
        }
        if (_selecting) {
            _selecting = false;
            _selX2 = static_cast<int>(event->x);
            _selY2 = static_cast<int>(event->y);
            _pArea->queue_draw();
            if (_sel_w() >= 3 and _sel_h() >= 3) {
                _show_toolbar();
            }
            return true;
        }
        return false;
    }

    void _show_toolbar()
    {
        _toolbar_shown = true;
        _pToolbar->show_all();
        // position the toolbar below the right edge of the selection (QQ style)
        _pToolbar->get_allocation(); // force size request computation on show
        int tbW = 560, tbH = 40;
        _pToolbar->get_preferred_width(tbW, tbW);
        int tbHmin = 0;
        _pToolbar->get_preferred_height(tbHmin, tbH);
        const int scrH = _rShot->get_height();
        int tx = _sel_x() + _sel_w() - tbW;
        int ty = _sel_y() + _sel_h() + 10;
        if (tx < 4) tx = _sel_x();
        if (ty + tbH > scrH - 30) ty = _sel_y() - tbH - 10;
        if (ty < 4) ty = std::max(4, scrH - tbH - 30);
        _pFixed->move(*_pToolbar, tx, ty);
    }

    void _show_text_entry(int x, int y)
    {
        _textAnnoX = x;
        _textAnnoY = y;
        _pFixed->move(*_pEntry, x, y);
        _pEntry->set_text("");
        _pEntry->show();
        _pEntry->grab_focus();
    }

    void _on_text_commit()
    {
        const Glib::ustring text = _pEntry->get_text();
        _pEntry->hide();
        _pArea->grab_focus();
        if (text.empty()) return;
        CtAnnoShape shape{};
        shape.type = CtAnnoShape::Type::Text;
        shape.x1 = _textAnnoX;
        shape.y1 = _textAnnoY + std::max(16, _sel_h() / 15); // baseline
        shape.fontSize = std::max(16, _sel_h() / 15);
        shape.text = text;
        _shapes.push_back(shape);
        _redoShapes.clear();
        _pArea->queue_draw();
    }

    bool _on_entry_key_press(GdkEventKey* event)
    {
        if (GDK_KEY_Escape == event->keyval) {
            _pEntry->hide();
            _pArea->grab_focus();
            return true;
        }
        return false;
    }

    bool _on_key_press(GdkEventKey* event)
    {
        if (_pEntry->is_visible()) return false; // let the entry handle typing
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

    // -- result ----------------------------------------------------------------
    Glib::RefPtr<Gdk::Pixbuf> _compose_result() const
    {
        const int selX = _sel_x();
        const int selY = _sel_y();
        const int selW = _sel_w();
        const int selH = _sel_h();
        Cairo::RefPtr<Cairo::ImageSurface> rSurface = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, selW, selH);
        Cairo::RefPtr<Cairo::Context> cr = Cairo::Context::create(rSurface);
        Gdk::Cairo::set_source_pixbuf(cr, _rShot, -static_cast<double>(selX), -static_cast<double>(selY));
        cr->paint();
        _draw_shapes(cr, -static_cast<double>(selX), -static_cast<double>(selY));
        if (_previewing) {
            _draw_one_shape(cr, -static_cast<double>(selX), -static_cast<double>(selY), _currShape);
        }
        GdkPixbuf* pRaw = gdk_pixbuf_get_from_surface(rSurface->cobj(), 0, 0, selW, selH);
        return Glib::wrap(pRaw);
    }

    void _confirm()
    {
        if (_sel_w() < 3 or _sel_h() < 3) return; // too small, keep selecting
        _rResult = _compose_result();
        hide();
    }

private:
    enum class Tool { None, Pen, Arrow, Rect, Ellipse, Text, Counter, Move };

    Glib::RefPtr<Gdk::Pixbuf> _rShot;
    Glib::RefPtr<Gdk::Pixbuf> _rResult;
    Glib::RefPtr<Gdk::Cursor> _rCursorCross;

    Gtk::Fixed*        _pFixed{nullptr};
    Gtk::DrawingArea*  _pArea{nullptr};
    Gtk::Box*          _pToolbar{nullptr};
    Gtk::Entry*        _pEntry{nullptr};

    bool _selecting{false};
    bool _annotating{false};
    bool _previewing{false};
    bool _toolbar_shown{false};
    Tool _tool{Tool::Pen};
    int  _selX1{0}, _selY1{0}, _selX2{-1}, _selY2{-1};
    int  _textAnnoX{0}, _textAnnoY{0};
    int  _movingIdx{-1};
    int  _moveLastX{0}, _moveLastY{0};

    std::vector<CtAnnoShape> _shapes;
    std::vector<CtAnnoShape> _redoShapes;
    CtAnnoShape _currShape;
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
