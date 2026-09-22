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
#include "ct_list_picker.h" // OrangeArk: shared scrolling picker + system fonts
#include <gdkmm/general.h>
#include <pangomm/layout.h>
#include <pangomm/fontdescription.h>
#include <gtkmm/cssprovider.h>
#include <giomm/memoryinputstream.h>
#include <cairo.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace CtScreenshot
{

// OrangeArk: plain POD point — Gdk::Point deletes its copy-assignment (it has
// a move ctor), which makes std::vector<Gdk::Point> non-copy-assignable and
// therefore breaks copying CtAnnoShape (needed for the resize snapshot).
struct CtPt
{
    int x{0};
    int y{0};
};

// One annotation drawn by the user on top of the selection
struct CtAnnoShape
{
    enum class Type { Pen, Arrow, Rect, Ellipse, Text, Counter, Mosaic, Blur };
    Type                  type{Type::Pen};
    std::vector<CtPt> pts;                // for Pen
    int                   x1{0}, y1{0}, x2{0}, y2{0}; // bounding for others
    int                   thickness{6};   // OrangeArk: line thickness chosen in the toolbar
    Glib::ustring         color{"#e53935"}; // OrangeArk: annotation colour chosen in the toolbar
    Glib::ustring         text;
    std::string           fontName;       // font family used by Text/Counter
    int                   fontSize{16};
    int                   number{0};      // for Counter
};

// OrangeArk: render a small inline SVG into a pixbuf for the toolbar icons —
// real vector shapes (rounded rectangles, a solid arrow head, …) instead of
// text glyphs, so the toolbar reads like the QQ screenshot toolbar
static Glib::RefPtr<Gdk::Pixbuf> _svg_icon(const char* pSvg, const int sizePx)
{
    try {
        Glib::RefPtr<Gio::MemoryInputStream> rStream = Gio::MemoryInputStream::create();
        rStream->add_data(pSvg, std::strlen(pSvg));
        return Gdk::Pixbuf::create_from_stream_at_scale(rStream, sizePx, sizePx, true);
    }
    catch (const Glib::Error& e) {
        spdlog::warn("CtScreenshot: svg icon failed: {}", e.what().c_str());
        return Glib::RefPtr<Gdk::Pixbuf>{};
    }
}

// OrangeArk: the screenshot text panel offers the REAL system fonts (same
// Pango enumeration as the main toolbar font picker, shared in ct_list_picker.h)

// toolbar icon artwork (16x16 viewBox, ink #444, the arrow in QQ blue)
static const char* kSvgRect =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
    "<rect x='1.8' y='3.4' width='12.4' height='9.2' rx='1.8' fill='none' stroke='#444444' stroke-width='1.6'/>"
    "</svg>";
static const char* kSvgEllipse =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
    "<circle cx='8' cy='8' r='5.7' fill='none' stroke='#444444' stroke-width='1.6'/>"
    "</svg>";
static const char* kSvgArrow =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
    "<path d='M2.5 13.5 L8.9 7.1' stroke='#2196f3' stroke-width='3.4' stroke-linecap='round' fill='none'/>"
    "<path d='M13.7 2.3 L5.8 4.3 L11.7 10.2 Z' fill='#2196f3' stroke='none'/>"
    "</svg>";
static const char* kSvgPen =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
    "<path d='M10.6 2.9 l2.5 2.5 -7.6 7.6 -3.3 0.8 0.8 -3.3 z' fill='none' stroke='#444444' stroke-width='1.5' stroke-linejoin='round'/>"
    "<path d='M9.2 4.3 l2.5 2.5' stroke='#444444' stroke-width='1.4'/>"
    "</svg>";
static const char* kSvgText =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
    "<path d='M3.2 12.6 L8 3.4 L12.8 12.6 M5.1 9.4 H10.9' stroke='#444444' stroke-width='1.7' fill='none' stroke-linecap='round'/>"
    "</svg>";
static const char* kSvgCounter =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
    "<circle cx='8' cy='8' r='5.9' fill='none' stroke='#444444' stroke-width='1.5'/>"
    "<path d='M6.9 6.3 L8.4 5.3 V10.9' stroke='#444444' stroke-width='1.5' fill='none' stroke-linecap='round'/>"
    "</svg>";
static const char* kSvgMosaic =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
    "<rect x='2' y='2' width='5' height='5' fill='#8a8a8a'/>"
    "<rect x='9' y='2' width='5' height='5' fill='#c9c9c9'/>"
    "<rect x='2' y='9' width='5' height='5' fill='#c9c9c9'/>"
    "<rect x='9' y='9' width='5' height='5' fill='#8a8a8a'/>"
    "</svg>";
static const char* kSvgBlur =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
    "<path d='M3 13 L13 3' stroke='#444444' stroke-width='2.2' stroke-linecap='round' opacity='0.9'/>"
    "<path d='M3 8.5 L8.5 3' stroke='#444444' stroke-width='2.0' stroke-linecap='round' opacity='0.55'/>"
    "<path d='M8 13 L13 8' stroke='#444444' stroke-width='2.0' stroke-linecap='round' opacity='0.55'/>"
    "</svg>";
static const char* kSvgUndo =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
    "<path d='M5 3.8 L2.4 6.4 L5 9' stroke='#444444' stroke-width='1.6' fill='none' stroke-linecap='round' stroke-linejoin='round'/>"
    "<path d='M2.8 6.4 H9.8 A3.4 3.4 0 0 1 9.8 13.2 H6.8' stroke='#444444' stroke-width='1.6' fill='none' stroke-linecap='round'/>"
    "</svg>";
static const char* kSvgRedo =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
    "<path d='M11 3.8 L13.6 6.4 L11 9' stroke='#444444' stroke-width='1.6' fill='none' stroke-linecap='round' stroke-linejoin='round'/>"
    "<path d='M13.2 6.4 H6.2 A3.4 3.4 0 0 0 6.2 13.2 H9.2' stroke='#444444' stroke-width='1.6' fill='none' stroke-linecap='round'/>"
    "</svg>";
static const char* kSvgSave =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
    "<path d='M8 2.2 V9.4 M4.9 6.6 L8 9.7 L11.1 6.6' stroke='#444444' stroke-width='1.6' fill='none' stroke-linecap='round' stroke-linejoin='round'/>"
    "<path d='M2.6 10.8 V13.4 H13.4 V10.8' stroke='#444444' stroke-width='1.6' fill='none' stroke-linecap='round' stroke-linejoin='round'/>"
    "</svg>";
static const char* kSvgCancel =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
    "<path d='M4 4 L12 12 M12 4 L4 12' stroke='#e53935' stroke-width='1.9' stroke-linecap='round'/>"
    "</svg>";
static const char* kSvgOk =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
    "<path d='M2.8 8.6 L6.4 12.2 L13.2 4.6' stroke='#2e7d32' stroke-width='2' fill='none' stroke-linecap='round' stroke-linejoin='round'/>"
    "</svg>";

// The full-screen overlay where the user drags a rectangle to select the region,
// then annotates it QQ-style with a floating toolbar
class CtScreenshotSelector : public Gtk::Window
{
public:
    enum class Tool { None, Pen, Arrow, Rect, Ellipse, Text, Counter, Mosaic, Blur, Move };
    struct ToolDef { const char* svg; const char* tip; Tool tool; };

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
        if (_rToolbarCss) {
            _pEntry->get_style_context()->add_provider(_rToolbarCss, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        }

        // the fixed container lets us position area/toolbar/entry freely
        _pFixed = Gtk::manage(new Gtk::Fixed());
        _pFixed->put(*_pArea, 0, 0);
        _pFixed->put(*_pToolbar, 10, 10);
        _pFixed->put(*_pEntry, 0, 0);
        // OrangeArk: the tool-options panel MUST become a child of _pFixed
        // before any _pFixed->move() touches it — GTK 3.24.52's
        // gtk_fixed_move dereferences a NULL GtkFixedChild for a widget that
        // is not a child (no assertion, straight segfault in libgtk).
        // This was the crash on every screenshot tool click.
        _pFixed->put(*_pPanel, 0, 0);
        _pPanel->hide();
        add(*_pFixed);

        _rCursorCross = Gdk::Cursor::create(Gdk::CursorType::CROSSHAIR);
        _rCursorMove = Gdk::Cursor::create(Gdk::CursorType::FLEUR);
    }

    Glib::RefPtr<Gdk::Pixbuf> get_result() const { return _rResult; }

    void start()
    {
        fullscreen();
        // the screenshot overlay must float above every other window —
        // otherwise whatever the user has focused covers it and the
        // selection never receives the drag
        set_keep_above(true);
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
        // QQ-style two-row floating bar: row 1 = tool icons, row 2 = thickness
        // slider + colour swatch (like the QQ screenshot toolbar)
        _pToolbar = Gtk::manage(new Gtk::Box{Gtk::ORIENTATION_VERTICAL, 2});
        _pToolbar->get_style_context()->add_class("toolbar");
        auto* pRow1 = Gtk::manage(new Gtk::Box{Gtk::ORIENTATION_HORIZONTAL, 0});
        _pRow1 = pRow1;
        _pToolbar->pack_start(*pRow1, Gtk::PACK_SHRINK);

        // OneNote-like style: light floating bar so every tool stays visible
        _rToolbarCss = Gtk::CssProvider::create();
        try {
            auto rCss = _rToolbarCss;
            rCss->load_from_data(
                ".screenshot-bar { background: rgba(250,250,250,0.98); border: 1px solid #c8c8c8;"
                " border-radius: 8px; padding: 3px; }\n"
                ".screenshot-bar button { background: transparent; border: none; border-radius: 6px;"
                " min-width: 34px; min-height: 32px; padding: 2px 6px; }\n"
                ".screenshot-bar button:hover { background: rgba(0,0,0,0.08); }\n"
                ".screenshot-bar button image { min-width: 20px; min-height: 20px; }\n"
                ".screenshot-bar button.anno-active { background: rgba(255,136,0,0.30); }\n"
                ".screenshot-bar combobox, .screenshot-bar entry { min-height: 30px; }\n"
                ".screenshot-bar label { color: #303030; }\n"
                ".screenshot-bar .anno-row2 { padding: 1px 6px 3px 6px; }\n"
                ".screenshot-bar .anno-row2 label { font-size: 12px; color: #303030; }\n"
                ".screenshot-bar .anno-row2 scale { min-width: 140px; }\n"
                ".screenshot-bar separator { background: rgba(0,0,0,0.25); min-width: 1px;"
                " min-height: 24px; margin-left: 4px; margin-right: 4px; }\n");
            _pToolbar->get_style_context()->add_class("screenshot-bar");
            _pToolbar->get_style_context()->add_provider(rCss, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        }
        catch (const Glib::Error& e) {
            spdlog::warn("CtScreenshot: toolbar css failed: {}", e.what().c_str());
        }

        // annotation tools, laid out exactly like the QQ screenshot bar:
        // rect / ellipse / arrow / pen / text / counter | mosaic / blur
        // Clicking any toolbar button first commits a pending text annotation.
        const std::vector<ToolDef> annTools = {
            {kSvgRect,    "绘制矩形", Tool::Rect},
            {kSvgEllipse, "绘制椭圆", Tool::Ellipse},
            {kSvgArrow,   "绘制箭头", Tool::Arrow},
            {kSvgPen,     "自由绘制", Tool::Pen},
            {kSvgText,    "文字：点空白处落字；点住已写的文字拖动可移动，单击选中后可在下方调整字体字号颜色", Tool::Text},
            {kSvgCounter, "插入自动递增的序号", Tool::Counter},
        };
        const std::vector<ToolDef> hideTools = {
            {kSvgMosaic, "马赛克：拖动框住需要打码的区域", Tool::Mosaic},
            {kSvgBlur,   "模糊：拖动框住需要模糊的区域", Tool::Blur},
        };
        for (const ToolDef& td : annTools) {
            _toolbar_add_tool_button(td);
        }
        _toolbar_add_separator();
        for (const ToolDef& td : hideTools) {
            _toolbar_add_tool_button(td);
        }
        _toolbar_add_separator();
        // highlight the default tool so the active tool is visible from the start
        for (auto& pair : _pToolButtons) {
            if (pair.first == _tool) { _select_tool(_tool, pair.second); break; }
        }

        auto* pBtnUndo = Gtk::manage(new Gtk::Button());
        pBtnUndo->set_tooltip_text("撤销上一个标注");
        pBtnUndo->set_relief(Gtk::RELIEF_NONE);
        pBtnUndo->set_focus_on_click(false);
        if (Glib::RefPtr<Gdk::Pixbuf> rPix = _svg_icon(kSvgUndo, 18)) {
            pBtnUndo->set_image(*Gtk::manage(new Gtk::Image(rPix)));
            pBtnUndo->set_always_show_image(true);
        }
        else pBtnUndo->set_label("↶");
        pBtnUndo->signal_clicked().connect([this]() { _finish_text_entry(); _undo(); });
        _pRow1->pack_start(*pBtnUndo, Gtk::PACK_SHRINK);

        auto* pBtnRedo = Gtk::manage(new Gtk::Button());
        pBtnRedo->set_tooltip_text("重做标注");
        pBtnRedo->set_relief(Gtk::RELIEF_NONE);
        pBtnRedo->set_focus_on_click(false);
        if (Glib::RefPtr<Gdk::Pixbuf> rPix = _svg_icon(kSvgRedo, 18)) {
            pBtnRedo->set_image(*Gtk::manage(new Gtk::Image(rPix)));
            pBtnRedo->set_always_show_image(true);
        }
        else pBtnRedo->set_label("↷");
        pBtnRedo->signal_clicked().connect([this]() { _finish_text_entry(); _redo(); });
        _pRow1->pack_start(*pBtnRedo, Gtk::PACK_SHRINK);

        _toolbar_add_separator();

        auto* pBtnSave = Gtk::manage(new Gtk::Button());
        pBtnSave->set_tooltip_text("把截图保存为 PNG 文件");
        pBtnSave->set_relief(Gtk::RELIEF_NONE);
        pBtnSave->set_focus_on_click(false);
        if (Glib::RefPtr<Gdk::Pixbuf> rPix = _svg_icon(kSvgSave, 18)) {
            pBtnSave->set_image(*Gtk::manage(new Gtk::Image(rPix)));
            pBtnSave->set_always_show_image(true);
        }
        else pBtnSave->set_label("💾");
        pBtnSave->signal_clicked().connect([this]() { _finish_text_entry(); _on_save_clicked(); });
        _pRow1->pack_start(*pBtnSave, Gtk::PACK_SHRINK);

        auto* pBtnCancel = Gtk::manage(new Gtk::Button());
        pBtnCancel->set_tooltip_text("放弃本次截图");
        pBtnCancel->set_relief(Gtk::RELIEF_NONE);
        pBtnCancel->set_focus_on_click(false);
        if (Glib::RefPtr<Gdk::Pixbuf> rPix = _svg_icon(kSvgCancel, 18)) {
            pBtnCancel->set_image(*Gtk::manage(new Gtk::Image(rPix)));
            pBtnCancel->set_always_show_image(true);
        }
        else pBtnCancel->set_label("✕");
        pBtnCancel->signal_clicked().connect(sigc::mem_fun(*this, &CtScreenshotSelector::_on_cancel_clicked));
        _pRow1->pack_start(*pBtnCancel, Gtk::PACK_SHRINK);

        auto* pBtnOk = Gtk::manage(new Gtk::Button());
        pBtnOk->set_tooltip_text("复制到剪贴板（回笔记后 Ctrl+V 粘贴）");
        pBtnOk->set_relief(Gtk::RELIEF_NONE);
        pBtnOk->set_focus_on_click(false);
        if (Glib::RefPtr<Gdk::Pixbuf> rPix = _svg_icon(kSvgOk, 18)) {
            pBtnOk->set_image(*Gtk::manage(new Gtk::Image(rPix)));
            pBtnOk->set_always_show_image(true);
        }
        else pBtnOk->set_label("✓");
        pBtnOk->signal_clicked().connect([this]() { _finish_text_entry(); _confirm(); });
        _pRow1->pack_start(*pBtnOk, Gtk::PACK_SHRINK);

        // -- OrangeArk: contextual options panel --------------------------------
        // Clicking a tool button pops this up right below the toolbar:
        //   shapes (rect/ellipse/arrow/pen) -> 粗细 slider + 颜色
        //   text / counter                  -> 字体 + 字号 slider + 颜色
        // Changes apply to NEW annotations and, when one is selected with the
        // text tool, live-edit that annotation.
        _pPanel = Gtk::manage(new Gtk::Box{Gtk::ORIENTATION_VERTICAL, 2});
        _pPanel->get_style_context()->add_class("screenshot-bar");
        // OrangeArk: give the panel an explicit size — a freshly-shown widget
        // measured 0x0 in GTK 3.24.52 and GtkFixed sizes children by their
        // requisition, so without this the options panel stayed invisible
        // (the "点工具没反应 / 字体字号面板不见了" bug)
        _pPanel->set_size_request(560, 44);
        _pPanel->set_no_show_all(true);
        if (_rToolbarCss) {
            _pPanel->get_style_context()->add_provider(_rToolbarCss, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        }

        _pPanelShape = Gtk::manage(new Gtk::Box{Gtk::ORIENTATION_HORIZONTAL, 8});
        _pPanelShape->get_style_context()->add_class("anno-row2");
        {
            _pThicknessLabel = Gtk::manage(new Gtk::Label("粗细"));
            _pPanelShape->pack_start(*_pThicknessLabel, Gtk::PACK_SHRINK);
            _pShapeScale = Gtk::manage(new Gtk::Scale(Gtk::ORIENTATION_HORIZONTAL));
            _pShapeScale->set_range(1, 48);
            _pShapeScale->set_increments(1, 4);
            _pShapeScale->set_draw_value(false);
            _pShapeScale->set_size_request(140, -1);
            _pShapeScale->set_tooltip_text("线条粗细（箭头/矩形/椭圆/画笔）");
            _pShapeVal = Gtk::manage(new Gtk::Label());
            _pShapeVal->set_size_request(24, -1);
            _pShapeScale->signal_value_changed().connect([this]() {
                _annoThickness = static_cast<int>(_pShapeScale->get_value());
                _pShapeVal->set_text(std::to_string(_annoThickness));
                _apply_panel_edit();
            });
            _pPanelShape->pack_start(*_pShapeScale, Gtk::PACK_SHRINK);
            _pPanelShape->pack_start(*_pShapeVal, Gtk::PACK_SHRINK);
            _pShapeColorBtn = Gtk::manage(new Gtk::ColorButton(Gdk::RGBA(_annoColor)));
            _pShapeColorBtn->set_tooltip_text("标注颜色");
            _pShapeColorBtn->set_title("标注颜色");
            _pShapeColorBtn->signal_color_set().connect([this]() {
                _annoColor = _pShapeColorBtn->get_rgba().to_string();
                _pTextColorBtn->set_rgba(_pShapeColorBtn->get_rgba());
                _apply_panel_edit();
            });
            _pPanelShape->pack_start(*_pShapeColorBtn, Gtk::PACK_SHRINK);
        }
        _pPanel->pack_start(*_pPanelShape, Gtk::PACK_SHRINK);

        _pPanelText = Gtk::manage(new Gtk::Box{Gtk::ORIENTATION_HORIZONTAL, 8});
        _pPanelText->get_style_context()->add_class("anno-row2");
        {
            // OrangeArk: REAL system fonts (enumerated via Pango, shared with
            // the main toolbar picker) — common desktop/CJK fonts first.
            // The list opens INSIDE the overlay (a Gtk::Fixed child placed
            // above the panel): a separate popup Gtk::Window floating over
            // the fullscreen overlay never receives input on this GTK/Windows
            // build, while in-overlay widgets demonstrably do.
            _fontFamilies = CtListPicker::ordered_font_families();
            spdlog::info("selftest: system font families found: {}", _fontFamilies.size());
            _pFontPicker = Gtk::manage(new Gtk::Button("字体"));
            _pFontPicker->set_size_request(120, -1);
            _pFontPicker->set_relief(Gtk::RELIEF_NONE);
            _pFontPicker->set_tooltip_text("文字字体（系统全部字体）");
            _pFontPicker->signal_clicked().connect([this]() {
                if (_pFontList) _hide_font_list();
                else _show_font_list();
            });
            _pPanelText->pack_start(*_pFontPicker, Gtk::PACK_SHRINK);

            auto* pSizeLabel = Gtk::manage(new Gtk::Label("字号"));
            _pPanelText->pack_start(*pSizeLabel, Gtk::PACK_SHRINK);
            _pTextSizeScale = Gtk::manage(new Gtk::Scale(Gtk::ORIENTATION_HORIZONTAL));
            _pTextSizeScale->set_range(8, 72);
            _pTextSizeScale->set_increments(1, 4);
            _pTextSizeScale->set_draw_value(false);
            _pTextSizeScale->set_size_request(140, -1);
            _pTextSizeScale->set_tooltip_text("文字大小");
            _pTextSizeVal = Gtk::manage(new Gtk::Label());
            _pTextSizeVal->set_size_request(24, -1);
            _pTextSizeScale->signal_value_changed().connect([this]() {
                _annoFontSize = static_cast<int>(_pTextSizeScale->get_value());
                _pTextSizeVal->set_text(std::to_string(_annoFontSize));
                _apply_panel_edit();
            });
            _pPanelText->pack_start(*_pTextSizeScale, Gtk::PACK_SHRINK);
            _pPanelText->pack_start(*_pTextSizeVal, Gtk::PACK_SHRINK);

            _pTextColorBtn = Gtk::manage(new Gtk::ColorButton(Gdk::RGBA(_annoColor)));
            _pTextColorBtn->set_tooltip_text("文字颜色");
            _pTextColorBtn->set_title("文字颜色");
            _pTextColorBtn->signal_color_set().connect([this]() {
                _annoColor = _pTextColorBtn->get_rgba().to_string();
                _pShapeColorBtn->set_rgba(_pTextColorBtn->get_rgba());
                _apply_panel_edit();
            });
            _pPanelText->pack_start(*_pTextColorBtn, Gtk::PACK_SHRINK);
        }
        _pPanel->pack_start(*_pPanelText, Gtk::PACK_SHRINK);
    }

    void _toolbar_add_separator()
    {
        auto* pSep = Gtk::manage(new Gtk::Separator{Gtk::ORIENTATION_VERTICAL});
        _pRow1->pack_start(*pSep, Gtk::PACK_SHRINK);
    }

    // OrangeArk: one tool button in the toolbar row — clicking it selects the
    // tool and pops up ITS options panel right below the toolbar
    void _toolbar_add_tool_button(const ToolDef& td)
    {
        auto* pBtn = Gtk::manage(new Gtk::Button());
        pBtn->set_tooltip_text(td.tip);
        pBtn->set_relief(Gtk::RELIEF_NONE);
        pBtn->set_focus_on_click(false);
        if (Glib::RefPtr<Gdk::Pixbuf> rPix = _svg_icon(td.svg, 20)) {
            auto* pImg = Gtk::manage(new Gtk::Image(rPix));
            pBtn->set_image(*pImg);
            pBtn->set_always_show_image(true);
        }
        else {
            pBtn->set_label("·");
        }
        pBtn->signal_clicked().connect([this, td, pBtn]() {
            spdlog::info("selftest: tool button clicked: {}", static_cast<int>(td.tool));
            _finish_text_entry();
            _select_tool(td.tool, pBtn);
            _editIdx = -1;
            // OrangeArk: clicking a tool pops up ITS options below the
            // toolbar (QQ screenshot style) — shapes get 粗细/强度+颜色,
            // text/counter get 字体+字号+颜色
            _show_tool_panel(td.tool);
            _pArea->grab_focus();
        });
        _pToolButtons.push_back({td.tool, pBtn});
        _pRow1->pack_start(*pBtn, Gtk::PACK_SHRINK);
    }

    // OrangeArk: is this tool a text-ish tool (font/size/colour panel)?
    static bool _is_text_tool(Tool tool)
    {
        return Tool::Text == tool or Tool::Counter == tool;
    }

    // OrangeArk: is this tool a hide/filter tool (strength slider panel)?
    static bool _is_filter_tool(Tool tool)
    {
        return Tool::Mosaic == tool or Tool::Blur == tool;
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

    // -- OrangeArk: contextual options panel -----------------------------------
    // pops up right below the toolbar and shows the options of the active tool
    void _show_tool_panel(Tool tool)
    {
        _hide_font_list(); // OrangeArk: switching tools dismisses the font list
        _panelTool = tool;
        if (_is_text_tool(tool)) {
            _pPanelShape->hide();
            _pPanelText->show_all();
        }
        else {
            _pPanelText->hide();
            _pPanelShape->show_all();
            // OrangeArk: the slider means thickness for shape tools and
            // strength (mosaic block size / blur amount) for the hide tools
            if (_is_filter_tool(tool)) {
                _pThicknessLabel->set_text("强度");
                _pShapeScale->set_tooltip_text(Tool::Mosaic == tool ? "马赛克块大小" : "模糊强度");
            }
            else {
                _pThicknessLabel->set_text("粗细");
                _pShapeScale->set_tooltip_text("线条粗细（箭头/矩形/椭圆/画笔）");
            }
        }
        _pShapeScale->set_value(_annoThickness);
        _pShapeVal->set_text(std::to_string(_annoThickness));
        _pTextSizeScale->set_value(_annoFontSize);
        _pTextSizeVal->set_text(std::to_string(_annoFontSize));
        _pShapeColorBtn->set_rgba(Gdk::RGBA(_annoColor));
        _pTextColorBtn->set_rgba(Gdk::RGBA(_annoColor));
        // OrangeArk: show() (not show_all()) — the panel has no_show_all set
        // (so the initial fullscreen show_all() keeps it hidden), and
        // gtk_widget_show_all() skips a no_show_all widget ITSELF, which kept
        // the options panel invisible no matter what
        _pPanel->show();
        // reposition: right below the toolbar, QQ style
        int panW = 10, panH = 10, panHmin = 0;
        _pPanel->get_preferred_width(panW, panW);
        _pPanel->get_preferred_height(panHmin, panH);
        // OrangeArk: defensive fallback — some GTK builds measure a freshly
        // shown widget as 0x0 before its first allocation
        if (panW < 60) panW = 560;
        if (panH < 20) panH = 44;
        int tbW = 10, tbH = 10, tbHmin = 0;
        _pToolbar->get_preferred_width(tbW, tbW);
        _pToolbar->get_preferred_height(tbHmin, tbH);
        const int scrH = _rShot->get_height();
        const int scrW = _rShot->get_width();
        int px = _toolbarX;
        int py = _toolbarY + tbH + 2;
        if (py + panH > scrH - 30) py = std::max(4, _toolbarY - panH - 2);
        if (px + panW > scrW - 4) px = std::max(4, scrW - panW - 4);
        _pFixed->move(*_pPanel, px, py);
        spdlog::info("selftest: panel at {},{} size {}x{}", px, py, panW, panH);
    }

    void _hide_tool_panel()
    {
        _pPanel->hide();
        _panelTool = Tool::None;
    }

    // OrangeArk: load an existing annotation into the panel controls so the
    // user can tweak a text that was already written
    void _sync_panel_from_shape(const int idx)
    {
        const CtAnnoShape& shape = _shapes.at(static_cast<size_t>(idx));
        _annoFontSize = shape.fontSize > 0 ? shape.fontSize : _annoFontSize;
        _annoColor = shape.color;
        if (not shape.fontName.empty()) {
            _pFontPicker->set_label(Glib::ustring(shape.fontName));
        }
        _annoFontName = shape.fontName.empty() ? _annoFontName : Glib::ustring(shape.fontName);
        const bool isText = CtAnnoShape::Type::Text == shape.type or CtAnnoShape::Type::Counter == shape.type;
        if (isText) {
            _show_tool_panel(Tool::Text);
        }
        else {
            _annoThickness = shape.thickness;
            if (CtAnnoShape::Type::Mosaic == shape.type)      _show_tool_panel(Tool::Mosaic);
            else if (CtAnnoShape::Type::Blur == shape.type)   _show_tool_panel(Tool::Blur);
            else                                              _show_tool_panel(Tool::Rect);
        }
    }

    // OrangeArk: in-overlay font list — a Gtk::Fixed child placed right above
    // the font button. A separate toplevel popup window never receives input
    // while the fullscreen overlay is up on this GTK/Windows build.
    void _hide_font_list()
    {
        if (not _pFontList) return;
        _pFixed->remove(*_pFontList); // managed — removed means destroyed
        _pFontList = nullptr;
    }

    void _show_font_list()
    {
        _hide_font_list();
        _pFontList = Gtk::manage(new Gtk::ScrolledWindow());
        _pFontList->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_ALWAYS);
        _pFontList->set_size_request(240, 320);
        auto* pList = Gtk::manage(new Gtk::ListBox());
        pList->set_activate_on_single_click(true);
        for (const Glib::ustring& fam : _fontFamilies) {
            auto* pRow = Gtk::manage(new Gtk::ListBoxRow());
            auto* pL = Gtk::manage(new Gtk::Label(fam));
            pL->set_halign(Gtk::Align::ALIGN_START);
            pL->set_margin_top(2);
            pL->set_margin_bottom(2);
            pL->set_margin_start(8);
            pRow->add(*pL);
            pList->append(*pRow);
        }
        pList->signal_row_activated().connect([this](Gtk::ListBoxRow* pRow) {
            if (not pRow) return;
            const int idx = pRow->get_index();
            if (idx < 0 or idx >= static_cast<int>(_fontFamilies.size())) return;
            _annoFontName = _fontFamilies.at(static_cast<size_t>(idx));
            _pFontPicker->set_label(_annoFontName);
            _hide_font_list();
            _apply_panel_edit();
        });
        _pFontList->add(*pList);
        // place right above the font button (the panel sits near the bottom)
        int rx = 0, ry = 0;
        if (Glib::RefPtr<Gdk::Window> rWin = get_window()) {
            rWin->get_origin(rx, ry);
        }
        const Gtk::Allocation alloc = _pFontPicker->get_allocation();
        const int bx = rx + alloc.get_x();
        const int by = ry + alloc.get_y();
        int px = bx - 8;
        int py = by - 320 - 4;
        if (py < 4) py = 4;
        if (px < 4) px = 4;
        _pFixed->put(*_pFontList, px, py);
        _pFontList->show_all();
        if (Glib::RefPtr<Gdk::Window> rLWin = _pFontList->get_window()) {
            rLWin->raise();
        }
    }

    // OrangeArk: when an annotation is selected for editing, panel changes
    // apply to it immediately (in addition to becoming the new defaults)
    void _apply_panel_edit()
    {
        if (_editIdx < 0 or _editIdx >= static_cast<int>(_shapes.size())) {
            _pArea->queue_draw();
            return;
        }
        CtAnnoShape& shape = _shapes.at(static_cast<size_t>(_editIdx));
        shape.color = _annoColor;
        if (CtAnnoShape::Type::Text == shape.type or CtAnnoShape::Type::Counter == shape.type) {
            shape.fontSize = _annoFontSize;
            shape.fontName = _annoFontName;
        }
        else {
            shape.thickness = _annoThickness;
        }
        _pArea->queue_draw();
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
    void _shape_raw_bbox(const CtAnnoShape& shape, int& bx, int& by, int& bw, int& bh) const
    {
        switch (shape.type) {
            case CtAnnoShape::Type::Pen: {
                int minX = INT_MAX, minY = INT_MAX, maxX = INT_MIN, maxY = INT_MIN;
                for (const CtPt& pt : shape.pts) {
                    minX = std::min(minX, pt.x); minY = std::min(minY, pt.y);
                    maxX = std::max(maxX, pt.x); maxY = std::max(maxY, pt.y);
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
    }

    void _shape_bbox(const CtAnnoShape& shape, int& bx, int& by, int& bw, int& bh) const
    {
        _shape_raw_bbox(shape, bx, by, bw, bh);
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

    // OrangeArk: hit-test the resize handles of the SELECTED annotation.
    // Mask bits match the selection handles: 1=left 2=right 4=top 8=bottom.
    // Arrows expose two endpoint handles (1=start, 2=end); text and counter
    // badges are resized via the panel font-size slider instead.
    int _shape_handle_at(const double x, const double y) const
    {
        if (_editIdx < 0 or _editIdx >= static_cast<int>(_shapes.size())) return 0;
        const CtAnnoShape& s = _shapes.at(static_cast<size_t>(_editIdx));
        const double hs = 7.0;
        if (CtAnnoShape::Type::Arrow == s.type) {
            if (std::abs(x - s.x1) <= hs and std::abs(y - s.y1) <= hs) return 1;
            if (std::abs(x - s.x2) <= hs and std::abs(y - s.y2) <= hs) return 2;
            return 0;
        }
        if (CtAnnoShape::Type::Text == s.type or CtAnnoShape::Type::Counter == s.type) return 0;
        int bx = 0, by = 0, bw = 0, bh = 0;
        _shape_raw_bbox(s, bx, by, bw, bh);
        if (std::abs(x - bx) <= hs and std::abs(y - by) <= hs) return 1 | 4;
        if (std::abs(x - (bx + bw)) <= hs and std::abs(y - by) <= hs) return 2 | 4;
        if (std::abs(x - bx) <= hs and std::abs(y - (by + bh)) <= hs) return 1 | 8;
        if (std::abs(x - (bx + bw)) <= hs and std::abs(y - (by + bh)) <= hs) return 2 | 8;
        return 0;
    }

    static void _translate_shape(CtAnnoShape& shape, const int dx, const int dy)
    {
        for (CtPt& pt : shape.pts) {
            pt.x += dx;
            pt.y += dy;
        }
        shape.x1 += dx; shape.y1 += dy; shape.x2 += dx; shape.y2 += dy;
    }

    void _draw_shapes(const Cairo::RefPtr<Cairo::Context>& cr, const double dx, const double dy) const
    {
        for (const CtAnnoShape& shape : _shapes) {
            _draw_one_shape(cr, dx, dy, shape);
        }
    }

    // OrangeArk: hide/filter shapes (mosaic / blur) — both sample the ORIGINAL
    // screen capture, so undo/redo and the final compose stay pixel-accurate
    void _draw_filter(const Cairo::RefPtr<Cairo::Context>& cr, const double dx, const double dy,
                      const CtAnnoShape& shape) const
    {
        int rx1 = static_cast<int>(std::min(shape.x1, shape.x2) + dx);
        int ry1 = static_cast<int>(std::min(shape.y1, shape.y2) + dy);
        int w = std::abs(shape.x2 - shape.x1);
        int h = std::abs(shape.y2 - shape.y1);
        if (w < 2 or h < 2) return;
        const int scrW = _rShot->get_width();
        const int scrH = _rShot->get_height();
        // the SOURCE region in shot coordinates is the same rect minus the offset
        int sx = static_cast<int>(std::min(shape.x1, shape.x2) - dx);
        int sy = static_cast<int>(std::min(shape.y1, shape.y2) - dy);
        if (sx < 0) { rx1 -= sx; w += sx; sx = 0; }
        if (sy < 0) { ry1 -= sy; h += sy; sy = 0; }
        if (sx + w > scrW) w = scrW - sx;
        if (sy + h > scrH) h = scrH - sy;
        if (w < 2 or h < 2) return;

        if (CtAnnoShape::Type::Mosaic == shape.type) {
            // pixelate: fill each block with the colour of the original pixel
            // at the centre of that block
            const int block = std::min(60, std::max(6, shape.thickness * 2));
            const guint8* pPixels = _rShot->get_pixels();
            const int rowstride = _rShot->get_rowstride();
            const int nch = _rShot->get_n_channels();
            for (int by = 0; by < h; by += block) {
                for (int bx = 0; bx < w; bx += block) {
                    const int cx = sx + std::min(bx + block / 2, w - 1);
                    const int cy = sy + std::min(by + block / 2, h - 1);
                    const guint8* p = pPixels + cy * rowstride + cx * nch;
                    cr->set_source_rgba(p[0] / 255.0, p[1] / 255.0, p[2] / 255.0, 1.0);
                    cr->rectangle(rx1 + bx, ry1 + by, std::min(block, w - bx), std::min(block, h - by));
                    cr->fill();
                }
            }
        }
        else {
            // gaussian-like blur: downscale the region, then upscale it back
            const int f = std::min(16, std::max(2, shape.thickness / 2));
            try {
                Glib::RefPtr<Gdk::Pixbuf> rRegion = Gdk::Pixbuf::create_subpixbuf(_rShot, sx, sy, w, h);
                const int w2 = std::max(1, w / f);
                const int h2 = std::max(1, h / f);
                Glib::RefPtr<Gdk::Pixbuf> rSmall =
                    Gdk::Pixbuf::create(rRegion->get_colorspace(), rRegion->get_has_alpha(), 8, w2, h2);
                if (rSmall) {
                    rSmall->fill(0);
                    rRegion->scale(rSmall, 0, 0, w2, h2, 0.0, 0.0,
                                   static_cast<double>(w2) / w, static_cast<double>(h2) / h,
                                   Gdk::INTERP_BILINEAR);
                    Glib::RefPtr<Gdk::Pixbuf> rBig =
                        Gdk::Pixbuf::create(rRegion->get_colorspace(), rRegion->get_has_alpha(), 8, w, h);
                    rBig->fill(0);
                    rSmall->scale(rBig, 0, 0, w, h, 0.0, 0.0,
                                  static_cast<double>(w) / w2, static_cast<double>(h) / h2,
                                  Gdk::INTERP_BILINEAR);
                    cr->save();
                    cr->rectangle(rx1, ry1, w, h);
                    cr->clip();
                    Gdk::Cairo::set_source_pixbuf(cr, rBig, rx1, ry1);
                    cr->paint();
                    cr->restore();
                }
            }
            catch (const Glib::Error& e) {
                spdlog::warn("CtScreenshot: blur failed: {}", e.what().c_str());
            }
        }
        // dashed orange outline while the region is being dragged (live preview)
        if (_previewing and &shape == &_currShape) {
            std::valarray<double> dashes{4.0, 3.0};
            cr->set_source_rgba(1.0, 0.55, 0.0, 0.9);
            cr->set_dash(dashes, 0.0);
            cr->set_line_width(1.2);
            cr->rectangle(rx1, ry1, w, h);
            cr->stroke();
            cr->unset_dash();
        }
    }

    void _draw_one_shape(const Cairo::RefPtr<Cairo::Context>& cr, const double dx, const double dy, const CtAnnoShape& shape) const
    {
        // OrangeArk: annotation colour and thickness come from the toolbar
        const Gdk::RGBA annoColor{shape.color};
        cr->set_source_rgba(annoColor.get_red(), annoColor.get_green(), annoColor.get_blue(), 0.95);
        cr->set_line_width(std::max(1.0, static_cast<double>(shape.thickness)));
        cr->set_line_cap(Cairo::LINE_CAP_ROUND);
        cr->set_line_join(Cairo::LINE_JOIN_ROUND);
        switch (shape.type) {
            case CtAnnoShape::Type::Pen: {
                bool first = true;
                for (const CtPt& pt : shape.pts) {
                    if (first) { cr->move_to(pt.x + dx, pt.y + dy); first = false; }
                    else       { cr->line_to(pt.x + dx, pt.y + dy); }
                }
                cr->stroke();
                break;
            }
            case CtAnnoShape::Type::Arrow: {
                // OrangeArk: bold arrow like the user's reference artwork —
                // a thick shaft merging into a large solid triangular head
                cr->save();
                const double x1 = shape.x1 + dx, y1 = shape.y1 + dy;
                const double x2 = shape.x2 + dx, y2 = shape.y2 + dy;
                const double angle = std::atan2(y2 - y1, x2 - x1);
                const double t = std::max(1.0, static_cast<double>(shape.thickness));
                const double shaftW = 2.2 * t + 2.0;               // bold shaft
                const double headL  = 2.2 * shaftW + 5.0;          // head length
                const double headHW = (2.8 * shaftW + 6.0) / 2.0;  // head half width
                const double backX = x2 - headL * std::cos(angle);
                const double backY = y2 - headL * std::sin(angle);
                // thick shaft with round caps, stopping where the head begins
                cr->set_line_width(shaftW);
                cr->set_line_cap(Cairo::LINE_CAP_ROUND);
                cr->move_to(x1, y1);
                cr->line_to(backX, backY);
                cr->stroke();
                // large solid triangular head
                cr->move_to(x2, y2);
                cr->line_to(backX - headHW * std::sin(angle), backY + headHW * std::cos(angle));
                cr->line_to(backX + headHW * std::sin(angle), backY - headHW * std::cos(angle));
                cr->close_path();
                cr->fill();
                cr->restore();
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
            case CtAnnoShape::Type::Mosaic:
            case CtAnnoShape::Type::Blur:
                _draw_filter(cr, dx, dy, shape);
                break;
            case CtAnnoShape::Type::Text: {
                // shape.x1/y1 is the TOP-left corner of the text block
                Glib::RefPtr<Pango::Layout> rLayout =
                    _make_layout(cr, shape.text, shape.fontName, shape.fontSize, false);
                cr->set_source_rgba(annoColor.get_red(), annoColor.get_green(), annoColor.get_blue(), 0.95);
                cr->move_to(shape.x1 + dx, shape.y1 + dy);
                rLayout->show_in_cairo_context(cr);
                break;
            }
            case CtAnnoShape::Type::Counter: {
                // QQ-style auto-increment numbered badge: circle + white number,
                // the glyph bounding box (ink extents) centred on the circle centre
                const double cx = shape.x1 + dx, cy = shape.y1 + dy;
                const double r = std::max(10.0, shape.fontSize * 0.9);
                cr->set_source_rgba(annoColor.get_red(), annoColor.get_green(), annoColor.get_blue(), 0.95);
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
            // OrangeArk: dashed orange box around the annotation being edited
            if (_editIdx >= 0 and _editIdx < static_cast<int>(_shapes.size())) {
                int bx = 0, by = 0, bw = 0, bh = 0;
                _shape_bbox(_shapes.at(static_cast<size_t>(_editIdx)), bx, by, bw, bh);
                std::valarray<double> dashes{4.0, 3.0};
                cr->set_source_rgba(1.0, 0.55, 0.0, 0.95);
                cr->set_dash(dashes, 0.0);
                cr->set_line_width(1.4);
                cr->rectangle(bx, by, bw, bh);
                cr->stroke();
                cr->unset_dash();
                // OrangeArk: white resize handles on the selected annotation —
                // corners for shapes, endpoints for arrows (drag to resize)
                const CtAnnoShape& s = _shapes.at(static_cast<size_t>(_editIdx));
                std::vector<std::pair<double, double>> handlePts;
                if (CtAnnoShape::Type::Arrow == s.type) {
                    handlePts.emplace_back(s.x1, s.y1);
                    handlePts.emplace_back(s.x2, s.y2);
                }
                else if (CtAnnoShape::Type::Text != s.type and CtAnnoShape::Type::Counter != s.type) {
                    int rx = 0, ry = 0, rw = 0, rh = 0;
                    _shape_raw_bbox(s, rx, ry, rw, rh);
                    handlePts.emplace_back(rx, ry);
                    handlePts.emplace_back(rx + rw, ry);
                    handlePts.emplace_back(rx, ry + rh);
                    handlePts.emplace_back(rx + rw, ry + rh);
                }
                if (not handlePts.empty()) {
                    const double hhs = 5.0;
                    if (CtAnnoShape::Type::Arrow == s.type) {
                        // OrangeArk: round endpoint handles like the reference
                        // artwork — white circles with a soft blue rim
                        for (const auto& h : handlePts) {
                            cr->begin_new_sub_path();
                            cr->arc(h.first, h.second, hhs, 0.0, 2.0 * M_PI);
                        }
                        cr->set_source_rgba(1.0, 1.0, 1.0, 0.97);
                        cr->fill();
                        for (const auto& h : handlePts) {
                            cr->begin_new_sub_path();
                            cr->arc(h.first, h.second, hhs, 0.0, 2.0 * M_PI);
                        }
                        cr->set_source_rgba(0.55, 0.75, 0.95, 1.0);
                        cr->set_line_width(1.4);
                        cr->stroke();
                    }
                    else {
                        cr->set_source_rgba(1.0, 1.0, 1.0, 0.95);
                        for (const auto& h : handlePts) {
                            cr->rectangle(h.first - hhs, h.second - hhs, 2 * hhs, 2 * hhs);
                        }
                        cr->fill();
                        cr->set_source_rgba(1.0, 0.55, 0.0, 0.95);
                        cr->set_line_width(1.0);
                        for (const auto& h : handlePts) {
                            cr->rectangle(h.first - hhs, h.second - hhs, 2 * hhs, 2 * hhs);
                        }
                        cr->stroke();
                    }
                }
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
        _hide_font_list(); // OrangeArk: clicking the canvas dismisses the font list
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
            // OrangeArk: dragging a handle of the SELECTED annotation resizes
            // that annotation (corner handles for shapes, endpoints for arrows)
            if (_editIdx >= 0 and _editIdx < static_cast<int>(_shapes.size())) {
                const int shapeHandle = _shape_handle_at(event->x, event->y);
                if (shapeHandle != 0) {
                    _shapeResizing = true;
                    _shapeEdge = shapeHandle;
                    _resizeOrig = _shapes.at(static_cast<size_t>(_editIdx));
                    return true;
                }
            }
            // QQ style: pressing an existing annotation always starts moving it,
            // whatever the active tool is (no need to switch to the Move tool)
            {
                const int px = static_cast<int>(event->x);
                const int py = static_cast<int>(event->y);
                // OrangeArk: with the Text tool, pressing an existing text or
                // counter starts a DRAG (move it); if the mouse is released
                // without moving, that very press turns into "select for
                // editing" (the options panel loads its font/size/colour).
                // The old behaviour (click = edit, never move) made existing
                // text impossible to reposition with the text tool.
                if (Tool::Text == _tool) {
                    for (int i = static_cast<int>(_shapes.size()) - 1; i >= 0; --i) {
                        const CtAnnoShape& shape = _shapes.at(static_cast<size_t>(i));
                        if ((CtAnnoShape::Type::Text == shape.type or CtAnnoShape::Type::Counter == shape.type)
                            and _shape_hit(shape, px, py)) {
                            _movingIdx = i;
                            _moveLastX = px;
                            _moveLastY = py;
                            _pressIdx = i;          // candidate for click-to-edit
                            _pressOriginX = px;
                            _pressOriginY = py;
                            if (_editIdx != i) {
                                _editIdx = i;       // select right away: the panel
                                _sync_panel_from_shape(i); // loads its font/size/colour
                            }
                            return true;
                        }
                    }
                }
                for (int i = static_cast<int>(_shapes.size()) - 1; i >= 0; --i) {
                    if (_shape_hit(_shapes.at(static_cast<size_t>(i)), px, py)) {
                        _movingIdx = i;
                        _moveLastX = px;
                        _moveLastY = py;
                        _pressIdx = i;          // click without drag = select for editing
                        _pressOriginX = px;
                        _pressOriginY = py;
                        if (_editIdx != i) {
                            _editIdx = i;       // OrangeArk: ANY tool can select an
                            _sync_panel_from_shape(i); // annotation to tweak it
                        }
                        return true;
                    }
                }
            }
            if (Tool::Text == _tool) {
                _editIdx = -1; // clicking empty ground starts a new text
                _pressIdx = -1;
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
                shape.color = _annoColor;
                shape.thickness = _annoThickness;
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
            _pressIdx = -1;
            _editIdx = -1; // drawing a new shape deselects the edited one
            _annotating = true;
            _previewing = true;
            _currShape = CtAnnoShape{};
            switch (_tool) {
                case Tool::Pen:    _currShape.type = CtAnnoShape::Type::Pen; break;
                case Tool::Arrow:  _currShape.type = CtAnnoShape::Type::Arrow; break;
                case Tool::Rect:   _currShape.type = CtAnnoShape::Type::Rect; break;
                case Tool::Ellipse:_currShape.type = CtAnnoShape::Type::Ellipse; break;
                case Tool::Mosaic: _currShape.type = CtAnnoShape::Type::Mosaic; break;
                case Tool::Blur:   _currShape.type = CtAnnoShape::Type::Blur; break;
                default: _annotating = false; _previewing = false; return true;
            }
            _currShape.x1 = _currShape.x2 = static_cast<int>(event->x);
            _currShape.y1 = _currShape.y2 = static_cast<int>(event->y);
            _currShape.fontSize = _annoFontSize;
            _currShape.fontName = _annoFontName;
            // OrangeArk: apply the panel's colour/thickness to the new shape
            _currShape.color = _annoColor;
            _currShape.thickness = _annoThickness;
            if (Tool::Pen == _tool) {
                _currShape.pts.push_back(CtPt{_currShape.x1, _currShape.y1});
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
        // OrangeArk: dragging a handle of the SELECTED annotation resizes it —
        // arrows move the dragged endpoint, shapes move the dragged bbox corner,
        // freehand pen scales its points around the fixed opposite corner
        if (_shapeResizing and _editIdx >= 0 and _editIdx < static_cast<int>(_shapes.size())) {
            const int px = static_cast<int>(event->x);
            const int py = static_cast<int>(event->y);
            CtAnnoShape& s = _shapes.at(static_cast<size_t>(_editIdx));
            if (CtAnnoShape::Type::Arrow == s.type) {
                if (_shapeEdge & 1) { s.x1 = px; s.y1 = py; }
                if (_shapeEdge & 2) { s.x2 = px; s.y2 = py; }
            }
            else if (CtAnnoShape::Type::Pen == s.type) {
                int bx = 0, by = 0, bw = 0, bh = 0;
                _shape_raw_bbox(_resizeOrig, bx, by, bw, bh);
                if (bw > 2 and bh > 2) {
                    const double anchorX = (_shapeEdge & 1) ? bx + bw : bx;
                    const double anchorY = (_shapeEdge & 4) ? by + bh : by;
                    double sx = 1.0, sy = 1.0;
                    if (_shapeEdge & 1) sx = (anchorX - px) / static_cast<double>(bw);
                    if (_shapeEdge & 2) sx = (px - anchorX) / static_cast<double>(bw);
                    if (_shapeEdge & 4) sy = (anchorY - py) / static_cast<double>(bh);
                    if (_shapeEdge & 8) sy = (py - anchorY) / static_cast<double>(bh);
                    sx = std::max(0.05, std::min(sx, 40.0));
                    sy = std::max(0.05, std::min(sy, 40.0));
                    for (size_t k = 0; k < s.pts.size() and k < _resizeOrig.pts.size(); ++k) {
                        s.pts.at(k).x = static_cast<int>(anchorX + (_resizeOrig.pts.at(k).x - anchorX) * sx);
                        s.pts.at(k).y = static_cast<int>(anchorY + (_resizeOrig.pts.at(k).y - anchorY) * sy);
                    }
                }
            }
            else {
                const int nx1 = (_shapeEdge & 1) ? px : s.x1;
                const int nx2 = (_shapeEdge & 2) ? px : s.x2;
                const int ny1 = (_shapeEdge & 4) ? py : s.y1;
                const int ny2 = (_shapeEdge & 8) ? py : s.y2;
                if (std::abs(nx2 - nx1) >= 3) { s.x1 = nx1; s.x2 = nx2; }
                if (std::abs(ny2 - ny1) >= 3) { s.y1 = ny1; s.y2 = ny2; }
            }
            _pArea->queue_draw();
            return true;
        }
        // hover feedback: show the move cursor over an existing annotation
        if (_toolbar_shown and not _selecting and not _annotating and _movingIdx < 0 and not _selResizing) {
            if (Glib::RefPtr<Gdk::Window> rWin = _pArea->get_window()) {
                const int px = static_cast<int>(event->x);
                const int py = static_cast<int>(event->y);
                Glib::RefPtr<Gdk::Cursor> rCursor;
                if (_sel_handle_at(px, py) != 0 or _shape_handle_at(px, py) != 0) {
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
            // OrangeArk: if the press that started this drag has moved more than
            // a few pixels it is a real move, no longer a click-to-edit candidate
            if (_pressIdx == _movingIdx
                and std::abs(px - _pressOriginX) + std::abs(py - _pressOriginY) > 4) {
                _pressIdx = -1;
            }
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
                _currShape.pts.push_back(CtPt{_currShape.x2, _currShape.y2});
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
        // OrangeArk: a handle drag on the selected annotation ends here
        if (_shapeResizing) {
            _shapeResizing = false;
            _pArea->queue_draw();
            return true;
        }
        // OrangeArk: a press on an existing text/counter with the Text tool that
        // ended WITHOUT a drag was a click — select it for editing (its font,
        // size and colour load into the options panel for live tweaking)
        if (_movingIdx >= 0 and _pressIdx >= 0) {
            _movingIdx = -1;
            _editIdx = _pressIdx;
            _pressIdx = -1;
            _sync_panel_from_shape(_editIdx);
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
        // OrangeArk: remember where the toolbar really is — the options panel
        // anchors right below it. (Not storing these made the panel pop up at
        // the stale initial position in the top-left corner of the screen,
        // which looked like "font/size panel missing, tools broken".)
        _toolbarX = tx;
        _toolbarY = ty;
        spdlog::info("selftest: toolbar at {},{} size {}x{} (screen {}x{})",
                     tx, ty, tbW, tbH, scrH > 0 ? _rShot->get_width() : 0, scrH);
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
        shape.color = _annoColor; // OrangeArk: honour the panel colour
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
    Glib::RefPtr<Gtk::CssProvider> _rToolbarCss; // OrangeArk: shared toolbar/panel/entry CSS

    Gtk::Fixed*        _pFixed{nullptr};
    Gtk::DrawingArea*  _pArea{nullptr};
    Gtk::Box*          _pToolbar{nullptr};
    Gtk::Box*          _pRow1{nullptr};   // OrangeArk: toolbar icon row
    Gtk::Box*          _pPanel{nullptr};      // OrangeArk: contextual options panel
    Gtk::Box*          _pPanelShape{nullptr}; // OrangeArk: 粗细+颜色 (shape tools)
    Gtk::Box*          _pPanelText{nullptr};  // OrangeArk: 字体+字号+颜色 (text tools)
    Gtk::Scale*        _pShapeScale{nullptr};
    Gtk::Label*        _pShapeVal{nullptr};
    Gtk::Label*        _pThicknessLabel{nullptr}; // OrangeArk: 粗细/强度 (per tool)
    Gtk::ColorButton*  _pShapeColorBtn{nullptr};
    Gtk::Button*       _pFontPicker{nullptr}; // OrangeArk: font button (label = current font)
    std::vector<Glib::ustring> _fontFamilies; // OrangeArk: Pango system fonts
    Gtk::ScrolledWindow* _pFontList{nullptr}; // OrangeArk: in-overlay font list
    Gtk::Scale*        _pTextSizeScale{nullptr};
    Gtk::Label*        _pTextSizeVal{nullptr};
    Gtk::ColorButton*  _pTextColorBtn{nullptr};
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
    int  _annoThickness{6};                         // OrangeArk: annotation line thickness (toolbar slider)
    Glib::ustring _annoColor{"#e53935"};            // OrangeArk: annotation colour (toolbar swatch)
    int  _selX1{0}, _selY1{0}, _selX2{-1}, _selY2{-1};
    int  _textAnnoX{0}, _textAnnoY{0};
    int  _movingIdx{-1};
    int  _moveLastX{0}, _moveLastY{0};
    int  _editIdx{-1};        // OrangeArk: index of the annotation being edited
    int  _pressIdx{-1};       // OrangeArk: press on text/counter — click-to-edit candidate
    int  _pressOriginX{0}, _pressOriginY{0}; // OrangeArk: where that press started
    bool _shapeResizing{false}; // OrangeArk: dragging a handle of the selected annotation
    int  _shapeEdge{0};         // OrangeArk: which handle (1=left/start 2=right/end 4=top 8=bottom)
    CtAnnoShape _resizeOrig{};  // OrangeArk: shape snapshot when the resize started
    Tool _panelTool{Tool::None};
    int  _toolbarX{10}, _toolbarY{10}; // OrangeArk: current toolbar position (panel anchors below it)

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
