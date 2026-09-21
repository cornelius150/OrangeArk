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
#include <pangomm/layout.h>
#include <pangomm/fontdescription.h>
#include <gtkmm/cssprovider.h>
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
    std::string           fontName;       // font family used by Text/Counter
    int                   fontSize{16};
    int                   number{0};      // for Counter
};

// The full-screen overlay where the user drags a rectangle to select the region,
// then annotates it QQ-style with a floating toolbar
class CtScreenshotSelector : public Gtk::Window
{
public:
    enum class Tool { None, Pen, Arrow, Rect, Ellipse, Text, Counter, Move };

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
        _rCursorMove = Gdk::Cursor::create(Gdk::CursorType::FLEUR);
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
        _pToolbar = Gtk::manage(new Gtk::Box{Gtk::ORIENTATION_HORIZONTAL, 0});
        _pToolbar->get_style_context()->add_class("toolbar");

        // OneNote-like style: light floating bar so every tool stays visible
        // (a dark bar made the single-color glyphs indistinguishable)
        try {
            auto rCss = Gtk::CssProvider::create();
            rCss->load_from_data(
                ".screenshot-bar { background: rgba(250,250,250,0.98); border: 1px solid #c8c8c8;"
                " border-radius: 8px; padding: 3px; }\n"
                ".screenshot-bar button { background: transparent; border: none; border-radius: 6px;"
                " min-width: 34px; min-height: 32px; padding: 2px 6px; }\n"
                ".screenshot-bar button:hover { background: rgba(0,0,0,0.08); }\n"
                ".screenshot-bar button label { color: #303030; font-size: 17px; font-weight: bold; }\n"
                ".screenshot-bar button.anno-active { background: rgba(255,136,0,0.30); }\n"
                ".screenshot-bar button.anno-ok label { color: #2e7d32; }\n"
                ".screenshot-bar button.anno-cancel label { color: #c62828; }\n"
                ".screenshot-bar combobox, .screenshot-bar entry { min-height: 30px; }\n"
                ".screenshot-bar separator { background: rgba(0,0,0,0.25); min-width: 1px;"
                " min-height: 24px; margin-left: 4px; margin-right: 4px; }\n");
            _pToolbar->get_style_context()->add_class("screenshot-bar");
            _pToolbar->get_style_context()->add_provider(rCss, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        }
        catch (const Glib::Error& e) {
            spdlog::warn("CtScreenshot: toolbar css failed: {}", e.what().c_str());
        }

        // annotation tools: QQ-like grouped layout, big clear glyphs.
        // Clicking any toolbar button first commits a pending text annotation.
        struct ToolDef { const char* glyph; const char* tip; Tool tool; };
        const std::vector<ToolDef> annTools = {
            {"▭", "绘制矩形", Tool::Rect},
            {"◯", "绘制椭圆", Tool::Ellipse},
            {"↗", "绘制箭头", Tool::Arrow},
            {"✎", "自由绘制", Tool::Pen},
            {"T", "插入文字（点空白处落字，点其他位置可继续写）", Tool::Text},
            {"①", "插入自动递增的序号", Tool::Counter},
            {"✥", "拖动已画的标注", Tool::Move},
        };
        for (const ToolDef& td : annTools) {
            auto* pBtn = Gtk::manage(new Gtk::Button(td.glyph));
            pBtn->set_tooltip_text(td.tip);
            pBtn->set_relief(Gtk::RELIEF_NONE);
            pBtn->set_focus_on_click(false);
            pBtn->signal_clicked().connect([this, td, pBtn]() {
                _finish_text_entry();
                _select_tool(td.tool, pBtn);
                _pArea->grab_focus();
            });
            _pToolButtons.push_back({td.tool, pBtn});
            _pToolbar->pack_start(*pBtn, Gtk::PACK_SHRINK);
        }
        // highlight the default tool so the active tool is visible from the start
        for (auto& pair : _pToolButtons) {
            if (pair.first == _tool) { _select_tool(_tool, pair.second); break; }
        }

        _toolbar_add_separator();

        auto* pBtnUndo = Gtk::manage(new Gtk::Button("↶"));
        pBtnUndo->set_tooltip_text("撤销上一个标注");
        pBtnUndo->set_relief(Gtk::RELIEF_NONE);
        pBtnUndo->set_focus_on_click(false);
        pBtnUndo->signal_clicked().connect([this]() { _finish_text_entry(); _undo(); });
        _pToolbar->pack_start(*pBtnUndo, Gtk::PACK_SHRINK);

        auto* pBtnRedo = Gtk::manage(new Gtk::Button("↷"));
        pBtnRedo->set_tooltip_text("重做标注");
        pBtnRedo->set_relief(Gtk::RELIEF_NONE);
        pBtnRedo->set_focus_on_click(false);
        pBtnRedo->signal_clicked().connect([this]() { _finish_text_entry(); _redo(); });
        _pToolbar->pack_start(*pBtnRedo, Gtk::PACK_SHRINK);

        _toolbar_add_separator();

        // OrangeArk: font family and size pickers for the text/counter annotations
        struct FontDef { const char* label; const char* family; };
        const std::vector<FontDef> fonts = {
            {"雅黑", "Microsoft YaHei"}, {"宋体", "SimSun"}, {"黑体", "SimHei"},
            {"楷体", "KaiTi"}, {"仿宋", "FangSong"}, {"Arial", "Arial"}, {"Times", "Times New Roman"},
        };
        auto* pFontCombo = Gtk::manage(new Gtk::ComboBoxText());
        for (const FontDef& f : fonts) pFontCombo->append(f.label);
        pFontCombo->set_active(0);
        pFontCombo->set_tooltip_text("文字标注字体");
        pFontCombo->signal_changed().connect([this, pFontCombo, fonts]() {
            _finish_text_entry();
            const Glib::ustring label = pFontCombo->get_active_text();
            for (const FontDef& f : fonts) {
                if (label == f.label) { _annoFontName = f.family; break; }
            }
        });
        _pToolbar->pack_start(*pFontCombo, Gtk::PACK_SHRINK);

        auto* pSizeCombo = Gtk::manage(new Gtk::ComboBoxText());
        for (const int s : {12, 14, 16, 18, 20, 24, 28, 36, 48}) pSizeCombo->append(std::to_string(s));
        pSizeCombo->set_active(3); // 18
        pSizeCombo->set_tooltip_text("文字标注字号");
        pSizeCombo->signal_changed().connect([this, pSizeCombo]() {
            _finish_text_entry();
            const Glib::ustring text = pSizeCombo->get_active_text();
            if (not text.empty()) _annoFontSize = std::atoi(text.c_str());
        });
        _pToolbar->pack_start(*pSizeCombo, Gtk::PACK_SHRINK);

        _toolbar_add_separator();

        auto* pBtnSave = Gtk::manage(new Gtk::Button("💾"));
        pBtnSave->set_tooltip_text("把截图保存为 PNG 文件");
        pBtnSave->set_relief(Gtk::RELIEF_NONE);
        pBtnSave->set_focus_on_click(false);
        pBtnSave->signal_clicked().connect([this]() { _finish_text_entry(); _on_save_clicked(); });
        _pToolbar->pack_start(*pBtnSave, Gtk::PACK_SHRINK);

        auto* pBtnCancel = Gtk::manage(new Gtk::Button("✕"));
        pBtnCancel->get_style_context()->add_class("anno-cancel");
        pBtnCancel->set_tooltip_text("放弃本次截图");
        pBtnCancel->set_relief(Gtk::RELIEF_NONE);
        pBtnCancel->set_focus_on_click(false);
        pBtnCancel->signal_clicked().connect(sigc::mem_fun(*this, &CtScreenshotSelector::_on_cancel_clicked));
        _pToolbar->pack_start(*pBtnCancel, Gtk::PACK_SHRINK);

        auto* pBtnOk = Gtk::manage(new Gtk::Button("✓"));
        pBtnOk->get_style_context()->add_class("anno-ok");
        pBtnOk->set_tooltip_text("复制到剪贴板（回笔记后 Ctrl+V 粘贴）");
        pBtnOk->set_relief(Gtk::RELIEF_NONE);
        pBtnOk->set_focus_on_click(false);
        pBtnOk->signal_clicked().connect([this]() { _finish_text_entry(); _confirm(); });
        _pToolbar->pack_start(*pBtnOk, Gtk::PACK_SHRINK);
    }

    void _toolbar_add_separator()
    {
        auto* pSep = Gtk::manage(new Gtk::Separator{Gtk::ORIENTATION_VERTICAL});
        _pToolbar->pack_start(*pSep, Gtk::PACK_SHRINK);
    }

    // OrangeArk: switch the active annotation tool and highlight its button,
    // so the user can always tell which tool is in use
    void _select_tool(Tool tool, Gtk::Button* pBtn)
    {
        _tool = tool;
        for (auto& pair : _pToolButtons) {
            pair.second->get_style_context()->remove_class("anno-active");
        }
        if (pBtn) {
            pBtn->get_style_context()->add_class("anno-active");
        }
    }

    // OrangeArk: a click anywhere (canvas or toolbar) finishes the pending text
    // annotation — no Enter required. Clicking empty ground with the Text tool
    // then starts a new entry at that point.
    void _finish_text_entry()
    {
        if (_pEntry and _pEntry->get_visible()) {
            _on_text_commit();
        }
    }

    void _undo()
    {
        if (not _shapes.empty()) {
            _redoShapes.push_back(_shapes.back());
            _shapes.pop_back();
            _pArea->queue_draw();
        }
    }

    void _redo()
    {
        if (not _redoShapes.empty()) {
            _shapes.push_back(_redoShapes.back());
            _redoShapes.pop_back();
            _pArea->queue_draw();
        }
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

    // OrangeArk: which selection border handle (bit mask 1=left 2=right 4=top 8=bottom)
    // is under the cursor, 0 when none
    int _sel_handle_at(const double x, const double y) const
    {
        const double m = 7.0;
        const double x1 = _sel_x(), y1 = _sel_y();
        const double x2 = x1 + _sel_w(), y2 = y1 + _sel_h();
        const bool inX = x >= x1 - m and x <= x2 + m;
        const bool inY = y >= y1 - m and y <= y2 + m;
        if (not inX or not inY) return 0;
        int e = 0;
        if (std::abs(x - x1) <= m) e |= 1;
        if (std::abs(x - x2) <= m) e |= 2;
        if (std::abs(y - y1) <= m) e |= 4;
        if (std::abs(y - y2) <= m) e |= 8;
        if (0 == e) return 0;
        const bool nearVert = (e & 0x3) != 0;
        const bool nearHorz = (e & 0xC) != 0;
        if (nearVert and y >= y1 - m and y <= y2 + m) return e;
        if (nearHorz and x >= x1 - m and x <= x2 + m) return e;
        return 0;
    }

    // -- drawing --------------------------------------------------------------
    // OrangeArk: Pango layouts render all overlay text — Pango falls back across
    // installed fonts, so CJK and any user font never turn into hollow boxes
    static Glib::RefPtr<Pango::Layout> _make_layout(const Cairo::RefPtr<Cairo::Context>& cr,
                                                    const Glib::ustring& text,
                                                    const std::string& fontName,
                                                    const double fontSizePx,
                                                    const bool bold = false)
    {
        Glib::RefPtr<Pango::Layout> rLayout = Pango::Layout::create(cr);
        Pango::FontDescription fd;
        std::string face = fontName;
        if (face.empty()) {
#ifdef _WIN32
            face = "Microsoft YaHei";
#else
            face = "Sans";
#endif
        }
        fd.set_family(face);
        fd.set_weight(bold ? Pango::Weight::WEIGHT_BOLD : Pango::Weight::WEIGHT_NORMAL);
        fd.set_absolute_size(static_cast<int>(fontSizePx * Pango::SCALE));
        rLayout->set_font_description(fd);
        rLayout->set_text(text);
        return rLayout;
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
            case CtAnnoShape::Type::Text: {
                // estimate the drawn width: CJK glyphs are about one em wide,
                // ASCII glyphs about 0.55 em (shape.text is UTF-8 encoded);
                // shape.y1 is the TOP edge of the text
                double textW = 0.0;
                for (const gunichar ch : Glib::ustring(shape.text)) {
                    textW += (ch > 0x7F ? 1.0 : 0.55) * shape.fontSize;
                }
                bx = shape.x1 - 2;
                by = shape.y1 - 4;
                bw = static_cast<int>(textW) + 8;
                bh = shape.fontSize + 8;
                break;
            }
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
                // shape.x1/y1 is the TOP-left corner of the text block
                Glib::RefPtr<Pango::Layout> rLayout =
                    _make_layout(cr, shape.text, shape.fontName, shape.fontSize, false);
                cr->set_source_rgba(1.0, 0.13, 0.1, 0.95);
                cr->move_to(shape.x1 + dx, shape.y1 + dy);
                rLayout->show_in_cairo_context(cr);
                break;
            }
            case CtAnnoShape::Type::Counter: {
                // QQ-style auto-increment numbered badge: red circle + white number,
                // the glyph bounding box (ink extents) centred on the circle centre
                const double cx = shape.x1 + dx, cy = shape.y1 + dy;
                const double r = std::max(10.0, shape.fontSize * 0.9);
                cr->set_source_rgba(1.0, 0.13, 0.1, 0.95);
                cr->arc(cx, cy, r, 0.0, 2.0 * M_PI);
                cr->fill();
                const Glib::ustring label = Glib::ustring::format(shape.number);
                Glib::RefPtr<Pango::Layout> rLayout =
                    _make_layout(cr, label, shape.fontName, std::lround(r), true);
                const Pango::Rectangle ink = rLayout->get_pixel_ink_extents();
                cr->set_source_rgba(1.0, 1.0, 1.0, 1.0);
                cr->move_to(cx - ink.get_x() - ink.get_width() / 2.0,
                            cy - ink.get_y() - ink.get_height() / 2.0);
                rLayout->show_in_cairo_context(cr);
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

            // OrangeArk: white square handles on the border (drag to resize the selection)
            {
                const double hs = 4.0; // half size of a handle square
                const double mx = selX + selW / 2.0;
                const double my = selY + selH / 2.0;
                const double hx[4] = {static_cast<double>(selX), mx, static_cast<double>(selX + selW), mx};
                const double hy[4] = {static_cast<double>(selY), my, static_cast<double>(selY + selH), my};
                cr->set_source_rgba(1.0, 1.0, 1.0, 0.95);
                for (int i = 0; i < 4; ++i) {
                    cr->rectangle(hx[i] - hs, hy[i] - hs, 2 * hs, 2 * hs); // four corners
                }
                cr->rectangle(mx - hs, selY - hs, 2 * hs, 2 * hs);              // top mid
                cr->rectangle(mx - hs, selY + selH - hs, 2 * hs, 2 * hs);       // bottom mid
                cr->rectangle(selX - hs, my - hs, 2 * hs, 2 * hs);              // left mid
                cr->rectangle(selX + selW - hs, my - hs, 2 * hs, 2 * hs);       // right mid
                cr->fill();
            }

            // size hint text (Pango: correct metrics and CJK safety)
            const Glib::ustring hint = Glib::ustring::format(selW, " x ", selH);
            Glib::RefPtr<Pango::Layout> rHint = _make_layout(cr, hint, "", 13.0, true);
            const Pango::Rectangle hink = rHint->get_pixel_ink_extents();
            double tx = selX + selW - hink.get_width() - 10.0;
            double ty = selY + selH + 8.0;
            if (ty + hink.get_height() > scrH) ty = selY + selH - hink.get_height() - 8.0;
            if (tx < 4.0) tx = selX + 6.0;
            cr->set_source_rgba(0.0, 0.0, 0.0, 0.55);
            cr->rectangle(tx - 4.0, ty - 2.0, hink.get_width() + 8.0, hink.get_height() + 4.0);
            cr->fill();
            cr->set_source_rgba(1.0, 1.0, 1.0, 0.95);
            cr->move_to(tx, ty);
            rHint->show_in_cairo_context(cr);
        }

        // bottom hint banner
        const Glib::ustring banner = _toolbar_shown
            ? Glib::ustring{"选工具标注 · Enter 确认并复制到剪贴板 · Esc 取消"}
            : _("Drag to select a region") + Glib::ustring{"   |   "}
            + _("Enter or double-click: confirm") + "   |   " + _("Esc: cancel");
        Glib::RefPtr<Pango::Layout> rBanner = _make_layout(cr, banner, "", 14.0, false);
        const Pango::Rectangle bink = rBanner->get_pixel_ink_extents();
        const double bx = (scrW - bink.get_width()) / 2.0;
        const double by = scrH - 16.0 - bink.get_height();
        cr->set_source_rgba(0.0, 0.0, 0.0, 0.55);
        cr->rectangle(bx - 12.0, by - 6.0, bink.get_width() + 24.0, bink.get_height() + 12.0);
        cr->fill();
        cr->set_source_rgba(1.0, 1.0, 1.0, 1.0);
        cr->move_to(bx, by);
        rBanner->show_in_cairo_context(cr);

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
        // OrangeArk: any click on the canvas commits the pending text annotation
        // first (no Enter needed); the click then behaves normally — clicking
        // empty ground with the Text tool starts a new entry at that point
        _finish_text_entry();
        // OrangeArk: selection border handles work just outside the selection too
        const int selHandle = _toolbar_shown ? _sel_handle_at(event->x, event->y) : 0;
        if (_toolbar_shown and (_in_selection(event->x, event->y) or selHandle != 0)) {
            // OrangeArk: selection border handles take priority (drag to resize the selection)
            if (selHandle != 0) {
                _selResizing = true;
                _selEdges = selHandle;
                return true;
            }
            // QQ style: pressing an existing annotation always starts moving it,
            // whatever the active tool is (no need to switch to the Move tool)
            {
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
            }
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
                shape.fontSize = _annoFontSize;
                shape.fontName = _annoFontName;
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
            _currShape.fontSize = _annoFontSize;
            _currShape.fontName = _annoFontName;
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
        // OrangeArk: dragging a selection border handle resizes the selection
        if (_selResizing) {
            const int px = static_cast<int>(event->x);
            const int py = static_cast<int>(event->y);
            if (_selEdges & 1) _selX1 = px;
            if (_selEdges & 2) _selX2 = px;
            if (_selEdges & 4) _selY1 = py;
            if (_selEdges & 8) _selY2 = py;
            _pArea->queue_draw();
            return true;
        }
        // hover feedback: show the move cursor over an existing annotation
        if (_toolbar_shown and not _selecting and not _annotating and _movingIdx < 0 and not _selResizing) {
            if (Glib::RefPtr<Gdk::Window> rWin = _pArea->get_window()) {
                const int px = static_cast<int>(event->x);
                const int py = static_cast<int>(event->y);
                Glib::RefPtr<Gdk::Cursor> rCursor;
                if (_sel_handle_at(px, py) != 0) {
                    rCursor = _rCursorMove;
                }
                else {
                    for (auto it = _shapes.rbegin(); it != _shapes.rend(); ++it) {
                        if (_shape_hit(*it, px, py)) { rCursor = _rCursorMove; break; }
                    }
                }
                rWin->set_cursor(rCursor);
            }
        }
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
        if (_selResizing) {
            _selResizing = false;
            _pArea->queue_draw();
            return true;
        }
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
        int tbW = 420, tbH = 40;
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
        shape.y1 = _textAnnoY; // top-left corner (Pango layouts anchor at the top)
        shape.fontSize = _annoFontSize;
        shape.fontName = _annoFontName;
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
    Glib::RefPtr<Gdk::Pixbuf> _rShot;
    Glib::RefPtr<Gdk::Pixbuf> _rResult;
    Glib::RefPtr<Gdk::Cursor> _rCursorCross;
    Glib::RefPtr<Gdk::Cursor> _rCursorMove;

    Gtk::Fixed*        _pFixed{nullptr};
    Gtk::DrawingArea*  _pArea{nullptr};
    Gtk::Box*          _pToolbar{nullptr};
    Gtk::Entry*        _pEntry{nullptr};

    bool _selecting{false};
    bool _annotating{false};
    bool _previewing{false};
    bool _toolbar_shown{false};
    bool _selResizing{false};   // OrangeArk: dragging a selection border handle
    int  _selEdges{0};
    Tool _tool{Tool::Pen};
    std::vector<std::pair<Tool, Gtk::Button*>> _pToolButtons; // OrangeArk: tool buttons for active highlight
    Glib::ustring _annoFontName{"Microsoft YaHei"}; // OrangeArk: text annotation font
    int  _annoFontSize{18};                         // OrangeArk: text annotation size
    int  _selX1{0}, _selY1{0}, _selX2{-1}, _selY2{-1};
    int  _textAnnoX{0}, _textAnnoY{0};
    int  _movingIdx{-1};
    int  _moveLastX{0}, _moveLastY{0};

    std::vector<CtAnnoShape> _shapes;
    std::vector<CtAnnoShape> _redoShapes;
    CtAnnoShape _currShape;
};

Glib::RefPtr<Gdk::Pixbuf> take_region_screenshot(CtMainWin* pCtMainWin)
{
    // OrangeArk: minimize the main window first (it lands in the taskbar) so the
    // capture cannot contain the notes app itself, then let the animation finish
    if (pCtMainWin) {
        pCtMainWin->iconify();
#if GTKMM_MAJOR_VERSION < 4 && !defined(GTKMM_DISABLE_DEPRECATED)
        while (gtk_events_pending()) gtk_main_iteration();
#else
        while (g_main_context_pending(nullptr)) g_main_context_iteration(nullptr, false);
#endif
        g_usleep(400000); // 400 ms for the minimize animation to settle
    }

    // grab the whole screen BEFORE showing the overlay
    Glib::RefPtr<Gdk::Window> rRoot = Gdk::Screen::get_default()->get_root_window();
    if (not rRoot) return Glib::RefPtr<Gdk::Pixbuf>{};
    Glib::RefPtr<Gdk::Pixbuf> rShot;
    try {
        rShot = Gdk::Pixbuf::create(rRoot, 0, 0, rRoot->get_width(), rRoot->get_height());
    }
    catch (...) {
        spdlog::warn("CtScreenshot: failed to grab the screen");
        if (pCtMainWin) {
            pCtMainWin->deiconify();
            pCtMainWin->present();
        }
        return Glib::RefPtr<Gdk::Pixbuf>{};
    }
    if (not rShot) return Glib::RefPtr<Gdk::Pixbuf>{};

    CtScreenshotSelector selector{rShot};
    selector.start();
    Glib::RefPtr<Glib::MainLoop> rLoop = Glib::MainLoop::create();
    selector.signal_hide().connect([&rLoop]() { rLoop->quit(); });
    rLoop->run();

    // OrangeArk: bring the notes window back once the screenshot flow is over
    if (pCtMainWin) {
        pCtMainWin->deiconify();
        pCtMainWin->present();
    }
    return selector.get_result();
}

} // namespace CtScreenshot
