/*
 * ct_drawing.cc
 *
 * OrangeArk: a Visio-like diagram that lives directly in the document body.
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

#include "ct_drawing.h"
#include "ct_state_machine.h"
#include "ct_storage_sqlite.h"
#include "ct_storage_multifile.h"
#include "ct_logging.h"
#include "ct_main_win.h"
#include "ct_const.h"
#include "ct_dialogs.h"

#include <glibmm/base64.h>
#include <libxml++/libxml++.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <limits>

const char* CtDrawing::XML_MODEL_ATTR = "oa_drawing";
const char* CtDrawing::SQLITE_LINK_PREFIX = "oa-drawing:";

// clang-format off
static const char* kShapeTypeNames[] = {
    "rect", "roundrect", "ellipse", "diamond", "parallelogram",
    "cylinder", "hexagon", "terminator", "document"
};
// clang-format on

static void oa_hex_to_rgb(const std::string& hex, double& r, double& g, double& b)
{
    r = g = b = 0.0;
    if (hex.size() >= 7) {
        unsigned int rr = 0, gg = 0, bb = 0;
        if (3 == std::sscanf(hex.c_str(), "#%02x%02x%02x", &rr, &gg, &bb)) {
            r = rr / 255.0;
            g = gg / 255.0;
            b = bb / 255.0;
        }
    }
}

static std::string oa_rgba_to_hex(const Gdk::RGBA& colour)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x",
                  static_cast<int>(std::lround(colour.get_red() * 255.0)),
                  static_cast<int>(std::lround(colour.get_green() * 255.0)),
                  static_cast<int>(std::lround(colour.get_blue() * 255.0)));
    return std::string{buf};
}

static void oa_rounded_rect(const Cairo::RefPtr<Cairo::Context>& cr,
                            const double x, const double y,
                            const double w, const double h,
                            const double radius)
{
    const double r = std::min(radius, std::min(w, h) / 2.0);
    cr->move_to(x + r, y);
    cr->line_to(x + w - r, y);
    cr->arc(x + w - r, y + r, r, -M_PI / 2.0, 0.0);
    cr->line_to(x + w, y + h - r);
    cr->arc(x + w - r, y + h - r, r, 0.0, M_PI / 2.0);
    cr->line_to(x + r, y + h);
    cr->arc(x + r, y + h - r, r, M_PI / 2.0, M_PI);
    cr->line_to(x, y + r);
    cr->arc(x + r, y + r, r, M_PI, 1.5 * M_PI);
    cr->close_path();
}

static Glib::RefPtr<Gdk::Pixbuf> oa_placeholder_pixbuf()
{
    Glib::RefPtr<Gdk::Pixbuf> rPixbuf = Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB, false, 8, 16, 16);
    if (rPixbuf) rPixbuf->fill(0xffffffffu);
    return rPixbuf;
}

/*static*/ double CtDrawing::_shape_attr_double(xmlpp::Element* pElement, const char* name, const double defVal)
{
    const Glib::ustring val = pElement->get_attribute_value(name);
    if (val.empty()) return defVal;
    try {
        return std::stod(val);
    }
    catch (...) {
        return defVal;
    }
}

/*static*/ int CtDrawing::_shape_attr_int(xmlpp::Element* pElement, const char* name, const int defVal)
{
    const Glib::ustring val = pElement->get_attribute_value(name);
    if (val.empty()) return defVal;
    try {
        return std::stoi(val);
    }
    catch (...) {
        return defVal;
    }
}

/*static*/ std::string CtDrawing::_shape_attr_str(xmlpp::Element* pElement, const char* name, const std::string& defVal)
{
    const Glib::ustring val = pElement->get_attribute_value(name);
    return val.empty() ? defVal : static_cast<std::string>(val);
}

/*static*/ CtDrawing::Model CtDrawing::_model_from_xml(const std::string& xml)
{
    Model model;
    if (xml.empty()) return model;
    try {
        xmlpp::DomParser parser;
        parser.parse_memory(xml);
        xmlpp::Element* pRoot = parser.get_document() ? parser.get_document()->get_root_node() : nullptr;
        if (not pRoot) return model;

        model.width = _shape_attr_int(pRoot, "width", model.width);
        model.height = _shape_attr_int(pRoot, "height", model.height);
        if (model.width < 80) model.width = 80;
        if (model.height < 60) model.height = 60;

        for (xmlpp::Node* pNode : pRoot->get_children("shape")) {
            xmlpp::Element* pElement = dynamic_cast<xmlpp::Element*>(pNode);
            if (not pElement) continue;
            Shape shape;
            shape.id = _shape_attr_int(pElement, "id", 0);
            shape.type = Tool::Rect;
            const std::string typeName = _shape_attr_str(pElement, "type", "rect");
            for (size_t i = 0; i < sizeof(kShapeTypeNames) / sizeof(kShapeTypeNames[0]); ++i) {
                if (typeName == kShapeTypeNames[i]) {
                    shape.type = static_cast<Tool>(static_cast<int>(Tool::Rect) + static_cast<int>(i));
                    break;
                }
            }
            shape.x = _shape_attr_double(pElement, "x", 0.0);
            shape.y = _shape_attr_double(pElement, "y", 0.0);
            shape.w = _shape_attr_double(pElement, "w", 100.0);
            shape.h = _shape_attr_double(pElement, "h", 60.0);
            shape.fill = _shape_attr_str(pElement, "fill", "#e3f2fd");
            shape.stroke = _shape_attr_str(pElement, "stroke", "#1565c0");
            shape.fontSize = _shape_attr_double(pElement, "fs", 13.0);
            shape.bold = 0 != _shape_attr_int(pElement, "bold", 0);
            shape.textColor = _shape_attr_str(pElement, "tc", "#1f2429");
            if (xmlpp::TextNode* pText = pElement->get_child_text()) {
                shape.text = pText->get_content();
            }
            model.shapes.push_back(shape);
        }

        for (xmlpp::Node* pNode : pRoot->get_children("conn")) {
            xmlpp::Element* pElement = dynamic_cast<xmlpp::Element*>(pNode);
            if (not pElement) continue;
            Conn conn;
            conn.id = _shape_attr_int(pElement, "id", 0);
            conn.x1 = _shape_attr_double(pElement, "x1", 0.0);
            conn.y1 = _shape_attr_double(pElement, "y1", 0.0);
            conn.x2 = _shape_attr_double(pElement, "x2", 0.0);
            conn.y2 = _shape_attr_double(pElement, "y2", 0.0);
            conn.fromShape = _shape_attr_int(pElement, "from", -1);
            conn.toShape = _shape_attr_int(pElement, "to", -1);
            conn.fromU = _shape_attr_double(pElement, "fu", -1.0);
            conn.toU = _shape_attr_double(pElement, "tu", -1.0);
            conn.arrowEnd = 0 != _shape_attr_int(pElement, "ae", 1);
            conn.dashed = 0 != _shape_attr_int(pElement, "dash", 0);
            conn.stroke = _shape_attr_str(pElement, "stroke", "#37474f");
            model.conns.push_back(conn);
        }
    }
    catch (std::exception& e) {
        spdlog::warn("!! {} {}", __FUNCTION__, e.what());
    }
    return model;
}

/*static*/ std::string CtDrawing::_model_to_xml(const Model& model)
{
    xmlpp::Document doc;
    xmlpp::Element* pRoot = doc.create_root_node("oa_drawing");
    pRoot->set_attribute("width", std::to_string(model.width));
    pRoot->set_attribute("height", std::to_string(model.height));
    for (const Shape& shape : model.shapes) {
        xmlpp::Element* pShapeNode = pRoot->add_child("shape");
        pShapeNode->set_attribute("id", std::to_string(shape.id));
        const int typeIdx = static_cast<int>(shape.type) - static_cast<int>(Tool::Rect);
        pShapeNode->set_attribute("type", (typeIdx >= 0 and typeIdx < static_cast<int>(sizeof(kShapeTypeNames) / sizeof(kShapeTypeNames[0])))
                                          ? kShapeTypeNames[typeIdx] : "rect");
        pShapeNode->set_attribute("x", std::to_string(shape.x));
        pShapeNode->set_attribute("y", std::to_string(shape.y));
        pShapeNode->set_attribute("w", std::to_string(shape.w));
        pShapeNode->set_attribute("h", std::to_string(shape.h));
        pShapeNode->set_attribute("fill", shape.fill);
        pShapeNode->set_attribute("stroke", shape.stroke);
        pShapeNode->set_attribute("fs", std::to_string(shape.fontSize));
        pShapeNode->set_attribute("bold", shape.bold ? "1" : "0");
        pShapeNode->set_attribute("tc", shape.textColor);
        if (not shape.text.empty()) {
            pShapeNode->add_child_text(shape.text);
        }
    }
    for (const Conn& conn : model.conns) {
        xmlpp::Element* pConnNode = pRoot->add_child("conn");
        pConnNode->set_attribute("id", std::to_string(conn.id));
        pConnNode->set_attribute("x1", std::to_string(conn.x1));
        pConnNode->set_attribute("y1", std::to_string(conn.y1));
        pConnNode->set_attribute("x2", std::to_string(conn.x2));
        pConnNode->set_attribute("y2", std::to_string(conn.y2));
        pConnNode->set_attribute("from", std::to_string(conn.fromShape));
        pConnNode->set_attribute("to", std::to_string(conn.toShape));
        pConnNode->set_attribute("fu", std::to_string(conn.fromU));
        pConnNode->set_attribute("tu", std::to_string(conn.toU));
        pConnNode->set_attribute("ae", conn.arrowEnd ? "1" : "0");
        pConnNode->set_attribute("dash", conn.dashed ? "1" : "0");
        pConnNode->set_attribute("stroke", conn.stroke);
    }
    return doc.write_to_string();
}

CtDrawing::CtDrawing(CtMainWin* pCtMainWin,
                     const std::string& modelXml,
                     const int charOffset,
                     const std::string& justification)
 : CtImage{pCtMainWin, oa_placeholder_pixbuf(), charOffset, justification}
{
    _model = _model_from_xml(modelXml);
    // re-align the connectors on load: documents written by older builds can
    // hold anchor points that no longer sit on the (now exact) shape border
    _recompute_conns();
    _sync_model();

    _fillColor.set("#e3f2fd");
    _strokeColor.set("#1565c0");
    _textColor.set("#1f2429");

    // replace the static image of the base class with: toolbar + canvas
    _frame.remove();   // Gtk::Bin::remove() drops the single child (the base image)

    _pOverlay = Gtk::manage(new Gtk::Overlay{});
    _pCanvas = Gtk::manage(new Gtk::EventBox{});
    _pEditor = Gtk::manage(new Gtk::TextView{});
    _pVBox = Gtk::manage(new Gtk::Box{Gtk::ORIENTATION_VERTICAL, 2});

    _pOverlay->add(_image);

    _pEditor->set_no_show_all(true);
    _pEditor->set_wrap_mode(Gtk::WRAP_WORD_CHAR);
    _pEditor->set_justification(Gtk::JUSTIFY_CENTER);
    _pEditor->set_halign(Gtk::ALIGN_START);
    _pEditor->set_valign(Gtk::ALIGN_START);
    _pEditor->set_border_width(0);
    _pEditor->signal_focus_out_event().connect([this](GdkEventFocus*) {
        _commit_text_editing();
        return false;
    });
    _pEditor->get_buffer()->signal_changed().connect([this]() {
        // keep the text colour tag over the whole text while the user types
        if (_editShape < 0) return;
        Glib::RefPtr<Gtk::TextBuffer> rBuf = _pEditor->get_buffer();
        Glib::RefPtr<Gtk::TextTag> rTag = rBuf->get_tag_table()->lookup("oa_drawing_text_color");
        if (rTag) rBuf->apply_tag(rTag, rBuf->begin(), rBuf->end());
    });
    _pOverlay->add_overlay(*_pEditor);

    _pCanvas->add(*_pOverlay);
    _pCanvas->set_can_focus(true);
    _pCanvas->add_events(Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK |
                         Gdk::BUTTON1_MOTION_MASK | Gdk::POINTER_MOTION_MASK |
                         Gdk::KEY_PRESS_MASK);
    _pCanvas->signal_button_press_event().connect(sigc::mem_fun(*this, &CtDrawing::_on_canvas_press), false);
    _pCanvas->signal_button_release_event().connect(sigc::mem_fun(*this, &CtDrawing::_on_canvas_release), false);
    _pCanvas->signal_motion_notify_event().connect(sigc::mem_fun(*this, &CtDrawing::_on_canvas_motion), false);
    _pCanvas->signal_key_press_event().connect(sigc::mem_fun(*this, &CtDrawing::_on_canvas_key), false);
    _pCanvas->signal_focus_out_event().connect(sigc::mem_fun(*this, &CtDrawing::_on_canvas_focus_out), false);

    _pVBox->pack_start(*_build_toolbar(), false, false);
    _pVBox->pack_start(*_pCanvas, false, false);
    _frame.add(*_pVBox);
    _frame.show_all();

    if (_model.shapes.empty() and _model.conns.empty()) {
        // a brand-new canvas starts with the tools visible, ready to draw
        _pToolbar->show();
        Glib::signal_idle().connect(sigc::mem_fun(*this, &CtDrawing::_grab_focus_on_idle));
    }
    else {
        // an existing diagram opens as a plain picture: the tools appear only
        // while the user works on the canvas and hide again on click-away
        _pToolbar->hide();
    }

    _render();
}

Gtk::Button* CtDrawing::_tool_button(const Glib::RefPtr<Gdk::Pixbuf>& rIcon, const Glib::ustring& tooltip, const Tool tool)
{
    Gtk::Button* pButton = Gtk::manage(new Gtk::Button{});
    pButton->set_focus_on_click(false);
    pButton->set_relief(Gtk::RELIEF_NONE);
    pButton->set_always_show_image(true);
    pButton->set_image(*Gtk::manage(new Gtk::Image{rIcon}));
    pButton->set_tooltip_text(tooltip);
    pButton->signal_clicked().connect([this, tool]() { _set_tool(tool); });
    _toolButtons.push_back(pButton);
    return pButton;
}

// toolbar icons are rendered programmatically with cairo, so they need no
// extra resource and always match the shapes they stand for
Glib::RefPtr<Gdk::Pixbuf> CtDrawing::_icon_for_tool(const Tool tool)
{
    const int S = 22;
    Cairo::RefPtr<Cairo::ImageSurface> rSurface = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, S, S);
    Cairo::RefPtr<Cairo::Context> cr = Cairo::Context::create(rSurface);
    cr->set_line_join(Cairo::LINE_JOIN_ROUND);
    cr->set_line_cap(Cairo::LINE_CAP_ROUND);

    if (Tool::Connector == tool or Tool::Arrow == tool) {
        cr->set_source_rgb(0.22, 0.28, 0.31);
        cr->set_line_width(1.8);
        cr->move_to(4.5, 17.0);
        cr->line_to(17.5, 5.0);
        cr->stroke();
        if (Tool::Arrow == tool) {
            _arrow_head(cr, 17.5, 5.0, std::atan2(5.0 - 17.0, 17.5 - 4.5), 6.0);
        }
    }
    else if (Tool::Select == tool) {
        cr->set_source_rgb(0.22, 0.28, 0.31);
        cr->move_to(6.0, 3.0);
        cr->line_to(6.0, 16.5);
        cr->line_to(9.7, 13.3);
        cr->line_to(11.9, 18.2);
        cr->line_to(14.1, 17.2);
        cr->line_to(11.9, 12.5);
        cr->line_to(16.6, 12.1);
        cr->close_path();
        cr->fill();
    }
    else {
        Shape iconShape;
        iconShape.type = tool;
        switch (tool) {
        case Tool::Diamond:    iconShape.x = 4.0; iconShape.y = 3.5; iconShape.w = 14.0; iconShape.h = 15.0; break;
        case Tool::Ellipse:    iconShape.x = 3.5; iconShape.y = 5.0; iconShape.w = 15.0; iconShape.h = 12.0; break;
        case Tool::Cylinder:   iconShape.x = 4.5; iconShape.y = 4.5; iconShape.w = 13.0; iconShape.h = 13.0; break;
        case Tool::Terminator: iconShape.x = 3.0; iconShape.y = 7.0; iconShape.w = 16.0; iconShape.h = 8.0;  break;
        case Tool::Document:   iconShape.x = 3.5; iconShape.y = 4.5; iconShape.w = 15.0; iconShape.h = 12.0; break;
        default:               iconShape.x = 3.5; iconShape.y = 5.5; iconShape.w = 15.0; iconShape.h = 11.0; break;
        }
        _shape_path(cr, iconShape);
        cr->set_source_rgb(0.89, 0.95, 0.99);   // the default shape fill
        cr->fill_preserve();
        cr->set_source_rgb(0.08, 0.40, 0.75);   // the default shape stroke
        cr->set_line_width(1.4);
        cr->stroke();
    }

    return Gdk::Pixbuf::create(rSurface, 0, 0, S, S);
}

Glib::RefPtr<Gdk::Pixbuf> CtDrawing::_icon_for_action(const char* kind)
{
    const int S = 22;
    Cairo::RefPtr<Cairo::ImageSurface> rSurface = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, S, S);
    Cairo::RefPtr<Cairo::Context> cr = Cairo::Context::create(rSurface);
    cr->set_line_join(Cairo::LINE_JOIN_ROUND);
    cr->set_line_cap(Cairo::LINE_CAP_ROUND);

    if (0 == std::strcmp(kind, "text")) {
        cr->select_font_face("Sans", Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_BOLD);
        cr->set_font_size(16.0);
        cr->set_source_rgb(0.22, 0.28, 0.31);
        Cairo::TextExtents te;
        cr->get_text_extents("A", te);
        cr->move_to((S - te.width) / 2.0 - te.x_bearing, (S + te.height) / 2.0 - te.y_bearing - 1.0);
        cr->show_text("A");
    }
    else if (0 == std::strcmp(kind, "delete")) {
        cr->set_source_rgb(0.78, 0.16, 0.12);
        cr->set_line_width(1.5);
        cr->move_to(4.5, 6.2);
        cr->line_to(17.5, 6.2);                    // lid
        cr->move_to(8.8, 6.2);
        cr->line_to(8.8, 3.8);
        cr->line_to(13.2, 3.8);
        cr->line_to(13.2, 6.2);                    // handle
        cr->move_to(6.4, 8.6);
        cr->line_to(7.0, 17.6);
        cr->line_to(15.0, 17.6);
        cr->line_to(15.6, 8.6);
        cr->close_path();                          // body
        cr->move_to(9.6, 10.6);
        cr->line_to(9.6, 15.4);                    // inner rib 1
        cr->move_to(12.4, 10.6);
        cr->line_to(12.4, 15.4);                   // inner rib 2
        cr->stroke();
    }
    else {   // "fit"
        cr->set_source_rgb(0.22, 0.28, 0.31);
        cr->set_line_width(1.5);
        cr->move_to(4.0, 9.0);   cr->line_to(4.0, 4.0);   cr->line_to(9.0, 4.0);
        cr->move_to(13.0, 4.0);  cr->line_to(18.0, 4.0);  cr->line_to(18.0, 9.0);
        cr->move_to(18.0, 13.0); cr->line_to(18.0, 18.0); cr->line_to(13.0, 18.0);
        cr->move_to(9.0, 18.0);  cr->line_to(4.0, 18.0);  cr->line_to(4.0, 13.0);
        cr->stroke();
        cr->move_to(8.8, 13.2);
        cr->line_to(13.2, 8.8);
        cr->stroke();
        _arrow_head(cr, 13.6, 8.4, std::atan2(8.4 - 13.2, 13.6 - 8.8), 5.0);
        _arrow_head(cr, 8.4, 13.6, std::atan2(13.6 - 8.8, 8.4 - 13.6), 5.0);
    }

    return Gdk::Pixbuf::create(rSurface, 0, 0, S, S);
}

Gtk::Box* CtDrawing::_build_toolbar()
{
    _pToolbar = Gtk::manage(new Gtk::Box{Gtk::ORIENTATION_VERTICAL, 2});

    Gtk::Box* pRow1 = Gtk::manage(new Gtk::Box{Gtk::ORIENTATION_HORIZONTAL, 1});
    pRow1->pack_start(*_tool_button(_icon_for_tool(Tool::Select),        _("选择 / 移动图形，双击图形输入文字"), Tool::Select), false, false);
    pRow1->pack_start(*_tool_button(_icon_for_tool(Tool::Rect),          _("矩形（流程步骤）"), Tool::Rect), false, false);
    pRow1->pack_start(*_tool_button(_icon_for_tool(Tool::RoundRect),     _("圆角矩形"), Tool::RoundRect), false, false);
    pRow1->pack_start(*_tool_button(_icon_for_tool(Tool::Ellipse),       _("椭圆 / 圆形"), Tool::Ellipse), false, false);
    pRow1->pack_start(*_tool_button(_icon_for_tool(Tool::Diamond),       _("菱形（判断）"), Tool::Diamond), false, false);
    pRow1->pack_start(*_tool_button(_icon_for_tool(Tool::Parallelogram), _("平行四边形（数据输入 / 输出）"), Tool::Parallelogram), false, false);
    pRow1->pack_start(*_tool_button(_icon_for_tool(Tool::Cylinder),      _("圆柱形（数据库 / 存储）"), Tool::Cylinder), false, false);
    pRow1->pack_start(*_tool_button(_icon_for_tool(Tool::Hexagon),       _("六边形（准备 / 预设）"), Tool::Hexagon), false, false);
    pRow1->pack_start(*_tool_button(_icon_for_tool(Tool::Terminator),    _("圆角胶囊（开始 / 结束）"), Tool::Terminator), false, false);
    pRow1->pack_start(*_tool_button(_icon_for_tool(Tool::Document),      _("文档形状（报表 / 输出）"), Tool::Document), false, false);
    _pToolbar->pack_start(*pRow1, false, false);

    Gtk::Box* pRow2 = Gtk::manage(new Gtk::Box{Gtk::ORIENTATION_HORIZONTAL, 2});
    pRow2->pack_start(*_tool_button(_icon_for_tool(Tool::Connector), _("连线：从一个图形拖到另一个图形"), Tool::Connector), false, false);
    pRow2->pack_start(*_tool_button(_icon_for_tool(Tool::Arrow),     _("带箭头的连线（流程方向）"), Tool::Arrow), false, false);

    _pFillBtn = Gtk::manage(new Gtk::ColorButton{_fillColor});
    _pFillBtn->set_size_request(30, 24);
    _pFillBtn->set_focus_on_click(false);
    _pFillBtn->set_tooltip_text(_("填充颜色（选中图形后可直接改色）"));
    _pFillBtn->signal_color_set().connect(sigc::mem_fun(*this, &CtDrawing::_on_fill_color_set));
    pRow2->pack_start(*_pFillBtn, false, false);

    _pStrokeBtn = Gtk::manage(new Gtk::ColorButton{_strokeColor});
    _pStrokeBtn->set_size_request(30, 24);
    _pStrokeBtn->set_focus_on_click(false);
    _pStrokeBtn->set_tooltip_text(_("边框 / 线条颜色"));
    _pStrokeBtn->signal_color_set().connect(sigc::mem_fun(*this, &CtDrawing::_on_stroke_color_set));
    pRow2->pack_start(*_pStrokeBtn, false, false);

    _pTextColBtn = Gtk::manage(new Gtk::ColorButton{_textColor});
    _pTextColBtn->set_size_request(30, 24);
    _pTextColBtn->set_focus_on_click(false);
    _pTextColBtn->set_tooltip_text(_("文字颜色"));
    _pTextColBtn->signal_color_set().connect(sigc::mem_fun(*this, &CtDrawing::_on_text_color_set));
    pRow2->pack_start(*_pTextColBtn, false, false);

    _pSizeCombo = Gtk::manage(new Gtk::ComboBoxText{});
    _syncingStyle = true;
    for (const int size : {10, 11, 12, 13, 14, 16, 18, 20, 24, 28, 32}) {
        _pSizeCombo->append(std::to_string(size));
    }
    _pSizeCombo->set_active(3);   // "13", the default font size
    _syncingStyle = false;
    _pSizeCombo->set_size_request(58, -1);
    _pSizeCombo->set_can_focus(false);
    _pSizeCombo->set_tooltip_text(_("文字大小"));
    _pSizeCombo->signal_changed().connect(sigc::mem_fun(*this, &CtDrawing::_on_font_size_changed));
    pRow2->pack_start(*_pSizeCombo, false, false);

    Gtk::Button* pTextBtn = Gtk::manage(new Gtk::Button{});
    pTextBtn->set_focus_on_click(false);
    pTextBtn->set_relief(Gtk::RELIEF_NONE);
    pTextBtn->set_always_show_image(true);
    pTextBtn->set_image(*Gtk::manage(new Gtk::Image{_icon_for_action("text")}));
    pTextBtn->set_tooltip_text(_("给选中的图形输入 / 修改文字"));
    pTextBtn->signal_clicked().connect([this]() {
        if (_selShape >= 0 and _selShape < static_cast<int>(_model.shapes.size())) {
            _start_text_editing(_selShape);
        }
    });
    pRow2->pack_start(*pTextBtn, false, false);

    Gtk::Button* pDelBtn = Gtk::manage(new Gtk::Button{});
    pDelBtn->set_focus_on_click(false);
    pDelBtn->set_relief(Gtk::RELIEF_NONE);
    pDelBtn->set_always_show_image(true);
    pDelBtn->set_image(*Gtk::manage(new Gtk::Image{_icon_for_action("delete")}));
    pDelBtn->set_tooltip_text(_("删除选中的图形 / 连线（也可按 Delete 键）"));
    pDelBtn->signal_clicked().connect([this]() { _delete_selection(); });
    pRow2->pack_start(*pDelBtn, false, false);

    Gtk::Button* pFitBtn = Gtk::manage(new Gtk::Button{});
    pFitBtn->set_focus_on_click(false);
    pFitBtn->set_relief(Gtk::RELIEF_NONE);
    pFitBtn->set_always_show_image(true);
    pFitBtn->set_image(*Gtk::manage(new Gtk::Image{_icon_for_action("fit")}));
    pFitBtn->set_tooltip_text(_("把画布缩到刚好包住所有图形"));
    pFitBtn->signal_clicked().connect([this]() {
        _fit_canvas();
        _sync_model();
        _render();
        _pCtMainWin->update_window_save_needed(CtSaveNeededUpdType::nbuf, true);
    });
    pRow2->pack_start(*pFitBtn, false, false);

    _pToolbar->pack_start(*pRow2, false, false);
    _update_toolbar_sensitivity();
    return _pToolbar;
}

void CtDrawing::_set_tool(const Tool tool)
{
    _tool = tool;
    _update_toolbar_sensitivity();
}

void CtDrawing::_update_toolbar_sensitivity()
{
    const int activeIdx = static_cast<int>(_tool);
    for (size_t i = 0; i < _toolButtons.size(); ++i) {
        _toolButtons[i]->set_relief(static_cast<int>(i) == activeIdx ? Gtk::RELIEF_NORMAL : Gtk::RELIEF_NONE);
    }
}

// ----------------------------------------------------- toolbar show / hide

void CtDrawing::_show_toolbar()
{
    if (_pToolbar and not _pToolbar->get_visible()) {
        _pToolbar->show();
    }
}

void CtDrawing::_hide_toolbar()
{
    if (_pToolbar and _pToolbar->get_visible()) {
        _pToolbar->hide();
    }
}

bool CtDrawing::_on_canvas_focus_out(GdkEventFocus*)
{
    // keep the tools while the user works inside this drawing (toolbar
    // controls, colour chooser dialogs...), hide them only when the focus
    // really goes back to the document
    Gtk::Window* pTop = dynamic_cast<Gtk::Window*>(_pVBox->get_toplevel());
    if (pTop and not pTop->is_active()) {
        return false;   // a dialog (e.g. the colour chooser) owns the activation
    }
    Gtk::Widget* pFocus = pTop ? pTop->get_focus() : nullptr;
    if (pFocus and pFocus->is_ancestor(*_pVBox)) {
        return false;   // focus moved to one of our own widgets
    }
    _hide_toolbar();
    return false;
}

bool CtDrawing::_grab_focus_on_idle()
{
    if (_pCanvas) {
        _pCanvas->grab_focus();
    }
    return false;   // one-shot
}

// ----------------------------------------------------- text size and colour

void CtDrawing::_sync_style_controls()
{
    if (not _pSizeCombo or not _pTextColBtn) return;
    double fontSize = _defaultFontSize;
    std::string textCol = _defaultTextColor;
    if (_selShape >= 0 and _selShape < static_cast<int>(_model.shapes.size())) {
        fontSize = _model.shapes[_selShape].fontSize;
        textCol = _model.shapes[_selShape].textColor;
    }
    static const std::vector<int> kSizes = {10, 11, 12, 13, 14, 16, 18, 20, 24, 28, 32};
    size_t best = 0;
    for (size_t i = 0; i < kSizes.size(); ++i) {
        if (std::abs(kSizes[i] - fontSize) < std::abs(kSizes[best] - fontSize)) best = i;
    }
    _syncingStyle = true;
    _pSizeCombo->set_active(static_cast<int>(best));
    _textColor.set(textCol);
    _pTextColBtn->set_rgba(_textColor);
    _syncingStyle = false;
}

void CtDrawing::_on_font_size_changed()
{
    if (_syncingStyle or not _pSizeCombo) return;
    const Glib::ustring val = _pSizeCombo->get_active_text();
    if (val.empty()) return;
    double fs = 13.0;
    try {
        fs = std::stod(val);
    }
    catch (...) {
        return;
    }
    _defaultFontSize = fs;
    if (_selShape >= 0 and _selShape < static_cast<int>(_model.shapes.size())) {
        _model.shapes[_selShape].fontSize = fs;
        _sync_model();
        _render();
        _pCtMainWin->update_window_save_needed(CtSaveNeededUpdType::nbuf, true);
    }
    _apply_editor_text_style();
}

void CtDrawing::_on_text_color_set()
{
    if (_syncingStyle or not _pTextColBtn) return;
    _textColor = _pTextColBtn->get_rgba();
    _defaultTextColor = oa_rgba_to_hex(_textColor);
    if (_selShape >= 0 and _selShape < static_cast<int>(_model.shapes.size())) {
        _model.shapes[_selShape].textColor = _defaultTextColor;
        _sync_model();
        _render();
        _pCtMainWin->update_window_save_needed(CtSaveNeededUpdType::nbuf, true);
    }
    _apply_editor_text_style();
}

void CtDrawing::_apply_editor_text_style()
{
    if (_editShape < 0 or _editShape >= static_cast<int>(_model.shapes.size())) return;
    const Shape& shape = _model.shapes[_editShape];
    Pango::FontDescription fontDesc;
    fontDesc.set_family("Sans");
    fontDesc.set_absolute_size(static_cast<int>(shape.fontSize * PANGO_SCALE));
    if (shape.bold) fontDesc.set_weight(Pango::WEIGHT_BOLD);
    _pEditor->override_font(fontDesc);

    Glib::RefPtr<Gtk::TextBuffer> rBuf = _pEditor->get_buffer();
    Glib::RefPtr<Gtk::TextTag> rTag = rBuf->get_tag_table()->lookup("oa_drawing_text_color");
    if (not rTag) {
        rTag = rBuf->create_tag("oa_drawing_text_color");
    }
    rTag->property_foreground() = shape.textColor;
    rBuf->apply_tag(rTag, rBuf->begin(), rBuf->end());
}

void CtDrawing::_on_fill_color_set()
{
    _fillColor = _pFillBtn->get_rgba();
    _apply_style_to_selection();
}

void CtDrawing::_on_stroke_color_set()
{
    _strokeColor = _pStrokeBtn->get_rgba();
    _apply_style_to_selection();
}

void CtDrawing::_apply_style_to_selection()
{
    if (_selShape >= 0 and _selShape < static_cast<int>(_model.shapes.size())) {
        _model.shapes[_selShape].fill = oa_rgba_to_hex(_fillColor);
        _model.shapes[_selShape].stroke = oa_rgba_to_hex(_strokeColor);
        _sync_model();
        _render();
        _pCtMainWin->update_window_save_needed(CtSaveNeededUpdType::nbuf, true);
    }
    else if (_selConn >= 0 and _selConn < static_cast<int>(_model.conns.size())) {
        _model.conns[_selConn].stroke = oa_rgba_to_hex(_strokeColor);
        _sync_model();
        _render();
        _pCtMainWin->update_window_save_needed(CtSaveNeededUpdType::nbuf, true);
    }
}

void CtDrawing::_sync_model()
{
    _modelXml = _model_to_xml(_model);
}

int CtDrawing::_next_shape_id() const
{
    int maxId = 0;
    for (const Shape& shape : _model.shapes) maxId = std::max(maxId, shape.id);
    return maxId + 1;
}

int CtDrawing::_next_conn_id() const
{
    int maxId = 0;
    for (const Conn& conn : _model.conns) maxId = std::max(maxId, conn.id);
    return maxId + 1;
}

void CtDrawing::_select_only(const int shapeIdx, const int connIdx)
{
    _selShape = shapeIdx;
    _selConn = connIdx;
    _sync_style_controls();
}

bool CtDrawing::_inside_shape(const Shape& shape, const double x, const double y) const
{
    return x >= shape.x and x <= shape.x + shape.w and y >= shape.y and y <= shape.y + shape.h;
}

int CtDrawing::_hit_shape(const double x, const double y) const
{
    for (int i = static_cast<int>(_model.shapes.size()) - 1; i >= 0; --i) {   // topmost first
        if (_inside_shape(_model.shapes[i], x, y)) return i;
    }
    return -1;
}

int CtDrawing::_hit_conn(const double x, const double y) const
{
    for (int i = static_cast<int>(_model.conns.size()) - 1; i >= 0; --i) {
        const Conn& conn = _model.conns[i];
        const double dx = conn.x2 - conn.x1;
        const double dy = conn.y2 - conn.y1;
        const double len2 = dx * dx + dy * dy;
        if (len2 < 1.0) continue;
        double t = ((x - conn.x1) * dx + (y - conn.y1) * dy) / len2;
        t = std::max(0.0, std::min(1.0, t));
        const double px = conn.x1 + t * dx;
        const double py = conn.y1 + t * dy;
        if (std::hypot(x - px, y - py) <= 6.0) return i;
    }
    return -1;
}

void CtDrawing::_handle_pos(const Shape& shape, const int handle, double& hx, double& hy) const
{
    const double xs[3] = {shape.x, shape.x + shape.w / 2.0, shape.x + shape.w};
    const double ys[3] = {shape.y, shape.y + shape.h / 2.0, shape.y + shape.h};
    hx = xs[handle % 3];
    hy = ys[handle / 3];
}

int CtDrawing::_hit_handle(const double x, const double y) const
{
    if (_selShape < 0 or _selShape >= static_cast<int>(_model.shapes.size())) return -1;
    const Shape& shape = _model.shapes[_selShape];
    // the 8 resize grips: every bbox point except the (useless) centre
    for (const int h : {0, 1, 2, 3, 5, 6, 7, 8}) {
        double hx = 0.0, hy = 0.0;
        _handle_pos(shape, h, hx, hy);
        if (std::abs(x - hx) <= 5.0 and std::abs(y - hy) <= 5.0) return h;
    }
    return -1;
}

// hit-test of the border LINES of the selected shape (a few px around them),
// so the user can grab an edge anywhere along it to resize — not only at the
// small grips. Returns the edge's mid-handle: 1 top, 3 left, 5 right, 7 bottom.
int CtDrawing::_hit_edge(const double x, const double y) const
{
    if (_selShape < 0 or _selShape >= static_cast<int>(_model.shapes.size())) return -1;
    const Shape& shape = _model.shapes[_selShape];
    const double tol = 4.0;
    const double x1 = shape.x, x2 = shape.x + shape.w;
    const double y1 = shape.y, y2 = shape.y + shape.h;
    if (x >= x1 - tol and x <= x2 + tol) {
        if (std::abs(y - y1) <= tol and y < (y1 + y2) / 2.0) return 1;   // top edge
        if (std::abs(y - y2) <= tol and y > (y1 + y2) / 2.0) return 7;   // bottom edge
    }
    if (y >= y1 - tol and y <= y2 + tol) {
        if (std::abs(x - x1) <= tol and x < (x1 + x2) / 2.0) return 3;   // left edge
        if (std::abs(x - x2) <= tol and x > (x1 + x2) / 2.0) return 5;   // right edge
    }
    return -1;
}

// outward unit direction of one of the shape's border anchors (shape centre
// -> anchor point); works for any anchor set, diamond included
/*static*/ void CtDrawing::_anchor_dir(const Shape& shape, const int anchorIdx, double& dx, double& dy)
{
    const auto anchors = _border_anchors(shape);
    const double cx = shape.x + shape.w / 2.0;
    const double cy = shape.y + shape.h / 2.0;
    dx = anchors[anchorIdx].first - cx;
    dy = anchors[anchorIdx].second - cy;
    const double len = std::hypot(dx, dy);
    if (len < 0.001) {
        dx = 0.0;
        dy = -1.0;
    }
    else {
        dx /= len;
        dy /= len;
    }
}

// hit-test of the Visio-style quick-connect arrows (drawn ~4..20px outside
// each border anchor of the HOVERED shape); returns 0..3 or -1
int CtDrawing::_hit_quick_arrow(const double x, const double y) const
{
    if (_hoverShape < 0 or _hoverShape >= static_cast<int>(_model.shapes.size())) return -1;
    if (Drag::None != _drag) return -1;
    const Shape& shape = _model.shapes[_hoverShape];
    const auto anchors = _border_anchors(shape);
    for (int d = 0; d < 4; ++d) {
        double dx = 0.0, dy = 0.0;
        _anchor_dir(shape, d, dx, dy);
        const double cx = anchors[d].first + dx * 14.0;
        const double cy = anchors[d].second + dy * 14.0;
        if (std::hypot(x - cx, y - cy) <= 12.0) return d;
    }
    return -1;
}

/*static*/ bool CtDrawing::_clip_line_to_rect(const double cx, const double cy,
                                             const double tx, const double ty,
                                             const double rx, const double ry, const double rw, const double rh,
                                             double& outX, double& outY)
{
    const double dx = tx - cx;
    const double dy = ty - cy;
    if (std::abs(dx) < 0.001 and std::abs(dy) < 0.001) {
        outX = cx;
        outY = cy;
        return false;
    }
    double t = std::numeric_limits<double>::max();
    if (std::abs(dx) > 0.001) {
        t = std::min(t, (dx > 0 ? (rx + rw - cx) : (rx - cx)) / dx);
    }
    if (std::abs(dy) > 0.001) {
        t = std::min(t, (dy > 0 ? (ry + rh - cy) : (ry - cy)) / dy);
    }
    if (t < 0.0 or t > 1.0e6) return false;
    outX = cx + t * dx;
    outY = cy + t * dy;
    return true;
}

void CtDrawing::_recompute_conns()
{
    for (Conn& conn : _model.conns) {
        _update_conn_ends(conn);
    }
}

// refresh the endpoints of one connector from the shapes they are glued to.
// An end placed by hand (u >= 0) keeps its own position along the border; an
// automatic end (u < 0) keeps following the nearest border anchor point
void CtDrawing::_update_conn_ends(Conn& conn)
{
    const int nShapes = static_cast<int>(_model.shapes.size());
    const bool fromOk = conn.fromShape >= 0 and conn.fromShape < nShapes;
    const bool toOk = conn.toShape >= 0 and conn.toShape < nShapes;

    // 1. the hand-placed ends (they must not be moved by the auto logic)
    if (fromOk and conn.fromU >= 0.0) {
        const auto p = _point_on_outline(_model.shapes[conn.fromShape], conn.fromU);
        conn.x1 = p.first;
        conn.y1 = p.second;
    }
    if (toOk and conn.toU >= 0.0) {
        const auto p = _point_on_outline(_model.shapes[conn.toShape], conn.toU);
        conn.x2 = p.first;
        conn.y2 = p.second;
    }

    // 2. the automatic ends
    if (fromOk and toOk and conn.fromU < 0.0 and conn.toU < 0.0) {
        // Visio-style: attach to the closest pair of the four border anchor
        // points, so a vertical flow naturally uses bottom->top and a
        // horizontal one right->left (unchanged: old documents keep rendering
        // exactly the same)
        const Shape& from = _model.shapes[conn.fromShape];
        const Shape& to = _model.shapes[conn.toShape];
        const auto fromAnchors = _border_anchors(from);
        const auto toAnchors = _border_anchors(to);
        double bestDist = 1e18;
        double ax = conn.x1, ay = conn.y1, bx = conn.x2, by = conn.y2;
        for (const auto& a : fromAnchors) {
            for (const auto& b : toAnchors) {
                const double dx = a.first - b.first;
                const double dy = a.second - b.second;
                const double d = dx * dx + dy * dy;
                if (d < bestDist) {
                    bestDist = d;
                    ax = a.first;
                    ay = a.second;
                    bx = b.first;
                    by = b.second;
                }
            }
        }
        conn.x1 = ax;
        conn.y1 = ay;
        conn.x2 = bx;
        conn.y2 = by;
        return;
    }
    if (fromOk and conn.fromU < 0.0) {
        const auto a = _nearest_anchor(_model.shapes[conn.fromShape], conn.x2, conn.y2);
        conn.x1 = a.first;
        conn.y1 = a.second;
    }
    if (toOk and conn.toU < 0.0) {
        const auto b = _nearest_anchor(_model.shapes[conn.toShape], conn.x1, conn.y1);
        conn.x2 = b.first;
        conn.y2 = b.second;
    }
}

// glue one endpoint of a connector to whatever border point is closest to
// (x,y): any point of the border, not only the four anchors. Dropping it on
// empty canvas detaches the end and makes it a free point
void CtDrawing::_set_conn_end(Conn& conn, const bool isStart, const double x, const double y)
{
    const int idx = _shape_at_point(x, y, 6.0);
    if (idx >= 0) {
        const Shape& shape = _model.shapes[idx];
        const double u = _u_on_outline(shape, x, y);
        const auto p = _point_on_outline(shape, u);
        if (isStart) {
            conn.fromShape = idx;
            conn.fromU = u;
            conn.x1 = p.first;
            conn.y1 = p.second;
        }
        else {
            conn.toShape = idx;
            conn.toU = u;
            conn.x2 = p.first;
            conn.y2 = p.second;
        }
    }
    else if (isStart) {
        conn.fromShape = -1;
        conn.fromU = -1.0;
        conn.x1 = std::max(0.0, x);
        conn.y1 = std::max(0.0, y);
    }
    else {
        conn.toShape = -1;
        conn.toU = -1.0;
        conn.x2 = std::max(0.0, x);
        conn.y2 = std::max(0.0, y);
    }
}

// 0 = start grip, 1 = end grip of the given connector, -1 = not on a grip
int CtDrawing::_hit_conn_end(const int connIdx, const double x, const double y) const
{
    if (connIdx < 0 or connIdx >= static_cast<int>(_model.conns.size())) return -1;
    const Conn& conn = _model.conns[connIdx];
    if (std::hypot(x - conn.x1, y - conn.y1) <= 7.0) return 0;
    if (std::hypot(x - conn.x2, y - conn.y2) <= 7.0) return 1;
    return -1;
}

// topmost shape whose bounding box (grown by margin) contains the point
int CtDrawing::_shape_at_point(const double x, const double y, const double margin) const
{
    for (int i = static_cast<int>(_model.shapes.size()) - 1; i >= 0; --i) {
        const Shape& shape = _model.shapes[i];
        if (x >= shape.x - margin and x <= shape.x + shape.w + margin and
            y >= shape.y - margin and y <= shape.y + shape.h + margin) {
            return i;
        }
    }
    return -1;
}

// ------------------------------------------------------- border geometry

namespace {

struct OaBorderHit {
    double u{0.};      // position along the border, in [0,1)
    double dist{0.};   // distance from the queried point to the border
};

// projects (x,y) on the closed polygon pts and reports both the parametric
// position along the outline and the distance to it
OaBorderHit oa_border_project(const std::vector<std::pair<double, double>>& pts, const double x, const double y)
{
    OaBorderHit hit;
    if (pts.size() < 2) return hit;
    double total = 0.0;
    std::vector<double> cum(pts.size() + 1, 0.0);
    for (size_t i = 0; i < pts.size(); ++i) {
        const auto& a = pts[i];
        const auto& b = pts[(i + 1) % pts.size()];
        total += std::hypot(b.first - a.first, b.second - a.second);
        cum[i + 1] = total;
    }
    if (total < 0.001) return hit;
    double bestD2 = 1e18;
    double bestU = 0.0;
    for (size_t i = 0; i < pts.size(); ++i) {
        const auto& a = pts[i];
        const auto& b = pts[(i + 1) % pts.size()];
        const double ex = b.first - a.first;
        const double ey = b.second - a.second;
        const double len2 = ex * ex + ey * ey;
        double t = 0.0;
        if (len2 > 0.000001) {
            t = ((x - a.first) * ex + (y - a.second) * ey) / len2;
            t = std::max(0.0, std::min(1.0, t));
        }
        const double px = a.first + t * ex;
        const double py = a.second + t * ey;
        const double d2 = (x - px) * (x - px) + (y - py) * (y - py);
        if (d2 < bestD2) {
            bestD2 = d2;
            bestU = (cum[i] + t * (cum[i + 1] - cum[i])) / total;
        }
    }
    hit.u = bestU;
    hit.dist = std::sqrt(bestD2);
    return hit;
}

} // namespace

// a polygon that follows the real outline of the shape — this is what makes
// the connect points land ON the border for every shape (parallelogram,
// cylinder, hexagon, document) and not just for plain boxes
std::vector<std::pair<double, double>> CtDrawing::_outline_points(const Shape& shape)
{
    using Pt = std::pair<double, double>;
    std::vector<Pt> pts;
    const double x = shape.x;
    const double y = shape.y;
    const double w = std::max(1.0, shape.w);
    const double h = std::max(1.0, shape.h);
    const double cx = x + w / 2.0;
    const double cy = y + h / 2.0;

    switch (shape.type) {
    case Tool::Ellipse: {
        const int n = 32;
        for (int i = 0; i < n; ++i) {
            const double a = 2.0 * M_PI * static_cast<double>(i) / static_cast<double>(n);
            pts.emplace_back(cx + (w / 2.0) * std::cos(a), cy + (h / 2.0) * std::sin(a));
        }
        break;
    }
    case Tool::Diamond:
        pts = {Pt{cx, y}, Pt{x + w, cy}, Pt{cx, y + h}, Pt{x, cy}};
        break;
    case Tool::Parallelogram: {
        const double skew = std::min(w * 0.2, 24.0);
        pts = {Pt{x + skew, y}, Pt{x + w, y}, Pt{x + w - skew, y + h}, Pt{x, y + h}};
        break;
    }
    case Tool::Hexagon: {
        const double sx = w * 0.22;
        pts = {Pt{x + sx, y}, Pt{x + w - sx, y}, Pt{x + w, cy},
               Pt{x + w - sx, y + h}, Pt{x + sx, y + h}, Pt{x, cy}};
        break;
    }
    case Tool::Cylinder: {
        const double ry = std::min(h * 0.18, 16.0);
        const int n = 10;
        pts.emplace_back(x, y + ry);
        pts.emplace_back(x, y + h - ry);
        for (int i = 1; i <= n; ++i) {   // bottom bulge, left -> right
            const double a = M_PI - M_PI * static_cast<double>(i) / static_cast<double>(n);
            pts.emplace_back(cx + (w / 2.0) * std::cos(a), y + h - ry + ry * std::sin(a));
        }
        pts.emplace_back(x + w, y + ry);
        for (int i = 1; i < n; ++i) {    // top bulge, right -> left
            const double a = -M_PI * static_cast<double>(i) / static_cast<double>(n);
            pts.emplace_back(cx + (w / 2.0) * std::cos(a), y + ry + ry * std::sin(a));
        }
        break;
    }
    case Tool::Document: {
        const double wave = std::min(h * 0.16, 14.0);
        pts = {Pt{x, y}, Pt{x + w, y}, Pt{x + w, y + h - wave}};
        auto cubic = [](const double a, const double b, const double c, const double d, const double t) {
            const double mt = 1.0 - t;
            return mt * mt * mt * a + 3.0 * mt * mt * t * b + 3.0 * mt * t * t * c + t * t * t * d;
        };
        const double p0x = x + w, p0y = y + h - wave;
        const double p3x = x + w * 0.5, p3y = y + h - wave * 0.5;
        const double q3x = x, q3y = y + h - wave;
        const int n = 8;
        for (int i = 1; i <= n; ++i) {   // first half of the wavy bottom
            const double t = static_cast<double>(i) / static_cast<double>(n);
            pts.emplace_back(cubic(p0x, x + w * 0.75, x + w * 0.75, p3x, t),
                             cubic(p0y, y + h, y + h - wave, p3y, t));
        }
        for (int i = 1; i <= n; ++i) {   // second half
            const double t = static_cast<double>(i) / static_cast<double>(n);
            pts.emplace_back(cubic(p3x, x + w * 0.25, x + w * 0.25, q3x, t),
                             cubic(p3y, y + h, y + h, q3y, t));
        }
        break;
    }
    case Tool::RoundRect: [[fallthrough]];
    case Tool::Terminator: [[fallthrough]];
    case Tool::Rect: [[fallthrough]];
    default:
        pts = {Pt{x, y}, Pt{x + w, y}, Pt{x + w, y + h}, Pt{x, y + h}};
        break;
    }
    return pts;
}

std::pair<double, double> CtDrawing::_point_on_outline(const Shape& shape, const double u)
{
    using Pt = std::pair<double, double>;
    const std::vector<Pt> pts = _outline_points(shape);
    if (pts.size() < 2) return Pt{shape.x, shape.y};
    double total = 0.0;
    std::vector<double> cum(pts.size() + 1, 0.0);
    for (size_t i = 0; i < pts.size(); ++i) {
        const auto& a = pts[i];
        const auto& b = pts[(i + 1) % pts.size()];
        total += std::hypot(b.first - a.first, b.second - a.second);
        cum[i + 1] = total;
    }
    if (total < 0.001) return pts[0];
    double uu = u - std::floor(u);
    if (uu < 0.0) uu += 1.0;
    const double target = uu * total;
    for (size_t i = 0; i < pts.size(); ++i) {
        if (target <= cum[i + 1] or i + 1 == pts.size()) {
            const double segLen = cum[i + 1] - cum[i];
            const double t = segLen < 0.001 ? 0.0 : std::min(1.0, (target - cum[i]) / segLen);
            const auto& a = pts[i];
            const auto& b = pts[(i + 1) % pts.size()];
            return Pt{a.first + (b.first - a.first) * t, a.second + (b.second - a.second) * t};
        }
    }
    return pts[0];
}

double CtDrawing::_u_on_outline(const Shape& shape, const double x, const double y)
{
    return oa_border_project(_outline_points(shape), x, y).u;
}

double CtDrawing::_dist_to_outline(const Shape& shape, const double x, const double y)
{
    return oa_border_project(_outline_points(shape), x, y).dist;
}

// casts a ray from the shape centre outwards and returns the point where it
// crosses the real border — this is what puts every anchor ON the outline
std::pair<double, double> CtDrawing::_ray_outline_hit(const Shape& shape, const double dx, const double dy)
{
    using Pt = std::pair<double, double>;
    const std::vector<Pt> pts = _outline_points(shape);
    const double cx = shape.x + shape.w / 2.0;
    const double cy = shape.y + shape.h / 2.0;
    double bestT = 1e18;
    Pt best{cx + dx * 10.0, cy + dy * 10.0};
    if (pts.size() < 2) return best;
    for (size_t i = 0; i < pts.size(); ++i) {
        const auto& a = pts[i];
        const auto& b = pts[(i + 1) % pts.size()];
        const double ex = b.first - a.first;
        const double ey = b.second - a.second;
        const double fx = a.first - cx;
        const double fy = a.second - cy;
        const double det = ex * dy - ey * dx;
        if (std::abs(det) < 0.000001) continue;
        const double t = (ex * fy - ey * fx) / det;
        const double s = (dx * fy - dy * fx) / det;
        if (t > 0.0001 and t < bestT and s >= -0.0001 and s <= 1.0001) {
            bestT = t;
            best = Pt{cx + dx * t, cy + dy * t};
        }
    }
    return best;
}

// the connect points of a shape: the four border points hit by the up/right/
// down/left rays from the centre, so they always sit ON the outline. The
// diamond keeps the midpoints of its four slanted edges ("菱形边线中心点")
std::array<std::pair<double, double>, 4> CtDrawing::_border_anchors(const Shape& shape)
{
    const double cx = shape.x + shape.w / 2.0;
    const double cy = shape.y + shape.h / 2.0;
    if (Tool::Diamond == shape.type) {
        return {std::make_pair((shape.x + cx) / 2.0, (cy + shape.y) / 2.0),            // top-left edge midpoint
                std::make_pair((cx + shape.x + shape.w) / 2.0, (shape.y + cy) / 2.0),  // top-right edge midpoint
                std::make_pair((cx + shape.x) / 2.0, (shape.y + shape.h + cy) / 2.0),  // bottom-left edge midpoint
                std::make_pair((shape.x + shape.w + cx) / 2.0, (cy + shape.y + shape.h) / 2.0)}; // bottom-right edge midpoint
    }
    // every other shape: follow the up / right / down / left rays from the
    // centre to the real outline, so slanted or wavy borders (parallelogram,
    // document, hexagon, ellipse, cylinder) get their true border points
    return {_ray_outline_hit(shape, 0.0, -1.0),
            _ray_outline_hit(shape, 1.0, 0.0),
            _ray_outline_hit(shape, 0.0, 1.0),
            _ray_outline_hit(shape, -1.0, 0.0)};
}

std::pair<double, double> CtDrawing::_nearest_anchor(const Shape& shape, const double x, const double y)
{
    const auto anchors = _border_anchors(shape);
    std::pair<double, double> best = anchors[0];
    double bestDist = 1e18;
    for (const auto& p : anchors) {
        const double dx = p.first - x;
        const double dy = p.second - y;
        const double d = dx * dx + dy * dy;
        if (d < bestDist) {
            bestDist = d;
            best = p;
        }
    }
    return best;
}

// live snapping of the connector being drawn
void CtDrawing::_snap_conn_preview(const double x, const double y)
{
    if (_previewConn.fromShape >= 0 and _previewConn.fromShape < static_cast<int>(_model.shapes.size())) {
        const Shape& from = _model.shapes[_previewConn.fromShape];
        const auto a = _nearest_anchor(from, x, y);
        _previewConn.x1 = a.first;
        _previewConn.y1 = a.second;
    }
    else {
        _previewConn.x1 = _dragStartX;
        _previewConn.y1 = _dragStartY;
    }
    _previewConn.toShape = _hit_shape(x, y);
    if (_previewConn.toShape == _previewConn.fromShape and _previewConn.fromShape >= 0) {
        _previewConn.toShape = -1;
    }
    if (_previewConn.toShape >= 0 and _previewConn.toShape < static_cast<int>(_model.shapes.size())) {
        const Shape& to = _model.shapes[_previewConn.toShape];
        const auto b = _nearest_anchor(to, _previewConn.x1, _previewConn.y1);
        _previewConn.x2 = b.first;
        _previewConn.y2 = b.second;
    }
    else {
        _previewConn.x2 = x;
        _previewConn.y2 = y;
    }
}

void CtDrawing::_grow_canvas_for(const double x, const double y)
{
    // a bounded canvas keeps the anchored widget from taking over the page
    if (x > _model.width - 24.0) _model.width = std::min(1200, static_cast<int>(x) + 80);
    if (y > _model.height - 24.0) _model.height = std::min(1200, static_cast<int>(y) + 80);
}

void CtDrawing::_fit_canvas()
{
    if (_model.shapes.empty()) {
        _model.width = 400;
        _model.height = 240;
        return;
    }
    double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
    for (const Shape& shape : _model.shapes) {
        minX = std::min(minX, shape.x);
        minY = std::min(minY, shape.y);
        maxX = std::max(maxX, shape.x + shape.w);
        maxY = std::max(maxY, shape.y + shape.h);
    }
    for (const Conn& conn : _model.conns) {
        minX = std::min({minX, conn.x1, conn.x2});
        minY = std::min({minY, conn.y1, conn.y2});
        maxX = std::max({maxX, conn.x1, conn.x2});
        maxY = std::max({maxY, conn.y1, conn.y2});
    }
    const double pad = 24.0;
    _model.width = static_cast<int>(maxX - minX + 2.0 * pad);
    _model.height = static_cast<int>(maxY - minY + 2.0 * pad);
    if (_model.width < 120) _model.width = 120;
    if (_model.height < 80) _model.height = 80;
    // shift everything so the content starts at the padding
    const double dx = pad - minX;
    const double dy = pad - minY;
    for (Shape& shape : _model.shapes) {
        shape.x += dx;
        shape.y += dy;
    }
    for (Conn& conn : _model.conns) {
        if (conn.fromShape < 0) conn.x1 += dx;
        if (conn.toShape < 0) conn.x2 += dx;
        if (conn.fromShape < 0) conn.y1 += dy;
        if (conn.toShape < 0) conn.y2 += dy;
    }
    _recompute_conns();
}

void CtDrawing::_delete_selection()
{
    bool changed{false};
    if (_selShape >= 0 and _selShape < static_cast<int>(_model.shapes.size())) {
        _model.shapes.erase(_model.shapes.begin() + _selShape);
        // connectors bound to the removed shape lose their binding
        for (Conn& conn : _model.conns) {
            if (conn.fromShape == _selShape) conn.fromShape = -1;
            else if (conn.fromShape > _selShape) conn.fromShape -= 1;
            if (conn.toShape == _selShape) conn.toShape = -1;
            else if (conn.toShape > _selShape) conn.toShape -= 1;
        }
        _selShape = -1;
        changed = true;
    }
    else if (_selConn >= 0 and _selConn < static_cast<int>(_model.conns.size())) {
        _model.conns.erase(_model.conns.begin() + _selConn);
        _selConn = -1;
        changed = true;
    }
    if (changed) {
        _editShape = -1;
        _pEditor->hide();
        _hoverShape = -1;   // indices shifted; re-armed by the next mouse move
        _sync_model();
        _render();
        _pCtMainWin->update_window_save_needed(CtSaveNeededUpdType::nbuf, true);
    }
}

void CtDrawing::_start_text_editing(const int shapeIdx)
{
    if (shapeIdx < 0 or shapeIdx >= static_cast<int>(_model.shapes.size())) return;
    _commit_text_editing();
    _editShape = shapeIdx;
    const Shape& shape = _model.shapes[shapeIdx];
    _pEditor->get_buffer()->set_text(shape.text);
    _pEditor->set_size_request(static_cast<int>(shape.w) - 12, static_cast<int>(shape.h) - 12);
    _pEditor->set_margin_start(static_cast<int>(shape.x) + 6);
    _pEditor->set_margin_top(static_cast<int>(shape.y) + 6);
    _apply_editor_text_style();
    _pEditor->show();
    _pEditor->grab_focus();
}

void CtDrawing::_commit_text_editing()
{
    if (_editShape < 0) return;
    const int idx = _editShape;
    _editShape = -1;
    if (idx < static_cast<int>(_model.shapes.size())) {
        _model.shapes[idx].text = _pEditor->get_buffer()->get_text();
    }
    _pEditor->hide();
    _sync_model();
    _render();
    _pCtMainWin->update_window_save_needed(CtSaveNeededUpdType::nbuf, true);
}

void CtDrawing::_on_editor_focus_out()
{
    _commit_text_editing();
}

// ---------------------------------------------------------------- rendering

void CtDrawing::_shape_path(const Cairo::RefPtr<Cairo::Context>& cr, const Shape& shape) const
{
    const double x = shape.x;
    const double y = shape.y;
    // a brand-new shape has a zero width/height while the drag just started:
    // cairo throws "invalid matrix (not invertible)" on a degenerate scale
    // (ellipse / cylinder) and the exception would kill the whole program
    const double w = std::max(1.0, shape.w);
    const double h = std::max(1.0, shape.h);

    switch (shape.type) {
    case Tool::RoundRect:
        oa_rounded_rect(cr, x, y, w, h, 12.0);
        break;
    case Tool::Terminator:
        oa_rounded_rect(cr, x, y, w, h, h / 2.0);
        break;
    case Tool::Ellipse:
        cr->save();
        cr->translate(x + w / 2.0, y + h / 2.0);
        cr->scale(w / 2.0, h / 2.0);
        cr->arc(0.0, 0.0, 1.0, 0.0, 2.0 * M_PI);
        cr->restore();
        break;
    case Tool::Diamond:
        cr->move_to(x + w / 2.0, y);
        cr->line_to(x + w, y + h / 2.0);
        cr->line_to(x + w / 2.0, y + h);
        cr->line_to(x, y + h / 2.0);
        cr->close_path();
        break;
    case Tool::Parallelogram: {
        const double skew = std::min(w * 0.2, 24.0);
        cr->move_to(x + skew, y);
        cr->line_to(x + w, y);
        cr->line_to(x + w - skew, y + h);
        cr->line_to(x, y + h);
        cr->close_path();
        break;
    }
    case Tool::Cylinder: {
        const double ry = std::min(h * 0.18, 16.0);
        cr->move_to(x, y + ry);
        cr->line_to(x, y + h - ry);
        cr->save();
        cr->translate(x + w / 2.0, y + h - ry);
        cr->scale(w / 2.0, ry);
        cr->arc(0.0, 0.0, 1.0, 0.0, M_PI);
        cr->restore();
        cr->line_to(x + w, y + ry);
        cr->save();
        cr->translate(x + w / 2.0, y + ry);
        cr->scale(w / 2.0, ry);
        cr->arc(0.0, 0.0, 1.0, M_PI, 2.0 * M_PI);
        cr->restore();
        cr->close_path();
        break;
    }
    case Tool::Hexagon: {
        const double sx = w * 0.22;
        cr->move_to(x + sx, y);
        cr->line_to(x + w - sx, y);
        cr->line_to(x + w, y + h / 2.0);
        cr->line_to(x + w - sx, y + h);
        cr->line_to(x + sx, y + h);
        cr->line_to(x, y + h / 2.0);
        cr->close_path();
        break;
    }
    case Tool::Document: {
        const double wave = std::min(h * 0.16, 14.0);
        cr->move_to(x, y);
        cr->line_to(x + w, y);
        cr->line_to(x + w, y + h - wave);
        cr->curve_to(x + w * 0.75, y + h, x + w * 0.75, y + h - wave, x + w * 0.5, y + h - wave * 0.5);
        cr->curve_to(x + w * 0.25, y + h - wave * 0.0, x + w * 0.25, y + h, x, y + h - wave);
        cr->close_path();
        break;
    }
    case Tool::Rect: [[fallthrough]];
    default:
        cr->rectangle(x, y, w, h);
        break;
    }
}

void CtDrawing::_render_shape(const Cairo::RefPtr<Cairo::Context>& cr, const Shape& shape)
{
    double fr = 1.0, fg = 1.0, fb = 1.0;
    double sr = 0.0, sg = 0.0, sb = 0.0;
    oa_hex_to_rgb(shape.fill, fr, fg, fb);
    oa_hex_to_rgb(shape.stroke, sr, sg, sb);

    _shape_path(cr, shape);
    cr->set_source_rgb(fr, fg, fb);
    cr->fill_preserve();
    cr->set_source_rgb(sr, sg, sb);
    cr->set_line_width(1.6);
    cr->stroke();

    if (shape.text.empty()) return;

    Glib::RefPtr<Pango::Layout> rLayout = _image.create_pango_layout(shape.text);
    Pango::FontDescription fontDesc;
    fontDesc.set_family("Sans");
    fontDesc.set_absolute_size(static_cast<int>(shape.fontSize * PANGO_SCALE));
    if (shape.bold) fontDesc.set_weight(Pango::WEIGHT_BOLD);
    rLayout->set_font_description(fontDesc);
    const double innerW = std::max(12.0, shape.w - 14.0);
    rLayout->set_width(static_cast<int>(innerW * PANGO_SCALE));
    rLayout->set_wrap(Pango::WRAP_WORD_CHAR);
    rLayout->set_alignment(Pango::ALIGN_CENTER);

    int textW = 0, textH = 0;
    rLayout->get_pixel_size(textW, textH);
    const double textX = shape.x + (shape.w - innerW) / 2.0;
    const double textY = shape.y + std::max(0.0, (shape.h - textH) / 2.0);
    double tr = 0.12, tg = 0.14, tb = 0.16;
    oa_hex_to_rgb(shape.textColor, tr, tg, tb);
    cr->move_to(textX, textY);
    cr->set_source_rgb(tr, tg, tb);
    rLayout->show_in_cairo_context(cr);
}

void CtDrawing::_arrow_head(const Cairo::RefPtr<Cairo::Context>& cr, const double x, const double y, const double angle, const double size) const
{
    const double spread = 0.42;
    cr->save();
    cr->move_to(x, y);
    cr->line_to(x - size * std::cos(angle - spread), y - size * std::sin(angle - spread));
    cr->line_to(x - size * std::cos(angle + spread), y - size * std::sin(angle + spread));
    cr->close_path();
    cr->fill();
    cr->restore();
}

void CtDrawing::_render_conn(const Cairo::RefPtr<Cairo::Context>& cr, const Conn& conn) const
{
    double sr = 0.2, sg = 0.25, sb = 0.3;
    oa_hex_to_rgb(conn.stroke, sr, sg, sb);
    cr->set_source_rgb(sr, sg, sb);
    cr->set_line_width(1.6);
    if (conn.dashed) {
        std::vector<double> dashes{6.0, 4.0};
        cr->set_dash(dashes, 0.0);
    }
    cr->move_to(conn.x1, conn.y1);
    cr->line_to(conn.x2, conn.y2);
    cr->stroke();
    cr->unset_dash();
    if (conn.arrowEnd) {
        const double angle = std::atan2(conn.y2 - conn.y1, conn.x2 - conn.x1);
        _arrow_head(cr, conn.x2, conn.y2, angle);
    }
}

// the two square grips of the selected connector: drag one to move that end
// anywhere along a shape border (or off it, to detach the end)
void CtDrawing::_render_conn_grips(const Cairo::RefPtr<Cairo::Context>& cr, const Conn& conn) const
{
    const std::pair<double, double> ends[2] = {std::make_pair(conn.x1, conn.y1), std::make_pair(conn.x2, conn.y2)};
    for (const auto& p : ends) {
        cr->set_source_rgb(1.0, 1.0, 1.0);
        cr->rectangle(p.first - 4.0, p.second - 4.0, 8.0, 8.0);
        cr->fill();
        cr->set_source_rgb(0.09, 0.34, 0.75);
        cr->set_line_width(1.2);
        cr->rectangle(p.first - 4.0, p.second - 4.0, 8.0, 8.0);
        cr->stroke();
    }
}

void CtDrawing::_render_handles(const Cairo::RefPtr<Cairo::Context>& cr, const Shape& shape) const
{
    cr->set_source_rgb(0.09, 0.34, 0.75);
    cr->set_line_width(1.0);
    const double dash[] = {4.0, 3.0};
    std::vector<double> dashes(dash, dash + 2);
    cr->set_dash(dashes, 0.0);
    cr->rectangle(shape.x - 2.0, shape.y - 2.0, shape.w + 4.0, shape.h + 4.0);
    cr->stroke();
    cr->unset_dash();
    for (const int h : {0, 1, 2, 3, 5, 6, 7, 8}) {
        double hx = 0.0, hy = 0.0;
        _handle_pos(shape, h, hx, hy);
        cr->set_source_rgb(1.0, 1.0, 1.0);
        cr->rectangle(hx - 3.5, hy - 3.5, 7.0, 7.0);
        cr->fill();
        cr->set_source_rgb(0.09, 0.34, 0.75);
        cr->rectangle(hx - 3.5, hy - 3.5, 7.0, 7.0);
        cr->stroke();
    }
}

// Visio-style: four small outward arrows on the border anchors of the shape
// under the mouse — press one to connect, or just click it to drop a copy of
// the shape beside and connect the two. Never shown while not hovering.
void CtDrawing::_render_quick_arrows(const Cairo::RefPtr<Cairo::Context>& cr, const int shapeIdx)
{
    if (_drag != Drag::None) return;   // hidden while any interaction is going on
    if (shapeIdx < 0 or shapeIdx >= static_cast<int>(_model.shapes.size())) return;
    const Shape& shape = _model.shapes[shapeIdx];
    const auto anchors = _border_anchors(shape);
    for (int d = 0; d < 4; ++d) {
        double dx = 0.0, dy = 0.0;
        _anchor_dir(shape, d, dx, dy);
        const double sx = anchors[d].first + dx * 4.0;
        const double sy = anchors[d].second + dy * 4.0;
        const double ex = anchors[d].first + dx * 20.0;
        const double ey = anchors[d].second + dy * 20.0;
        // a small dot marks the connect point itself (the edge midpoint)
        cr->set_source_rgba(0.96, 0.49, 0.0, 0.9);
        cr->arc(anchors[d].first, anchors[d].second, 2.5, 0.0, 2.0 * M_PI);
        cr->fill();
        cr->set_source_rgba(0.96, 0.49, 0.0, 0.95);
        cr->set_line_width(2.0);
        cr->move_to(sx, sy);
        cr->line_to(ex, ey);
        cr->stroke();
        _arrow_head(cr, ex, ey, std::atan2(dy, dx), 7.0);
    }
}

void CtDrawing::_render_grid(const Cairo::RefPtr<Cairo::Context>& cr) const
{
    cr->set_source_rgba(0.35, 0.45, 0.55, 0.10);
    cr->set_line_width(1.0);
    for (int x = 20; x < _model.width; x += 20) {
        cr->move_to(x + 0.5, 0.0);
        cr->line_to(x + 0.5, _model.height);
    }
    for (int y = 20; y < _model.height; y += 20) {
        cr->move_to(0.0, y + 0.5);
        cr->line_to(_model.width, y + 0.5);
    }
    cr->stroke();
}

Cairo::RefPtr<Cairo::ImageSurface> CtDrawing::_render_surface(const bool withUi)
{
    const int width = std::max(80, _model.width);
    const int height = std::max(60, _model.height);
    Cairo::RefPtr<Cairo::ImageSurface> rSurface = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, width, height);
    Cairo::RefPtr<Cairo::Context> cr = Cairo::Context::create(rSurface);

    cr->set_source_rgb(1.0, 1.0, 1.0);
    cr->paint();
    _render_grid(cr);

    for (const Conn& conn : _model.conns) {
        _render_conn(cr, conn);
    }
    if (withUi and _preview and Drag::Conn == _drag) {
        _render_conn(cr, _previewConn);
        // highlight the hovered target shape and show its four snap points
        if (_previewConn.toShape >= 0 and _previewConn.toShape < static_cast<int>(_model.shapes.size())) {
            const Shape& t = _model.shapes[_previewConn.toShape];
            cr->set_source_rgba(0.96, 0.49, 0.0, 0.9);
            cr->set_line_width(2.0);
            cr->rectangle(t.x - 3.0, t.y - 3.0, t.w + 6.0, t.h + 6.0);
            cr->stroke();
            cr->set_source_rgba(0.96, 0.49, 0.0, 1.0);
            for (const auto& p : _border_anchors(t)) {
                cr->arc(p.first, p.second, 3.5, 0.0, 2.0 * M_PI);
                cr->fill();
            }
        }
    }
    for (const Shape& shape : _model.shapes) {
        _render_shape(cr, shape);
    }
    if (withUi and _preview and Drag::Create == _drag) {
        _render_shape(cr, _previewShape);
    }
    if (withUi and _selShape >= 0 and _selShape < static_cast<int>(_model.shapes.size())) {
        _render_handles(cr, _model.shapes[_selShape]);
    }
    if (withUi and _selConn >= 0 and _selConn < static_cast<int>(_model.conns.size())) {
        _render_conn_grips(cr, _model.conns[_selConn]);
    }
    if (withUi and _editShape < 0) {
        // the quick arrows belong to whichever shape the mouse hovers —
        // after drawing they disappear until the pointer comes back
        _render_quick_arrows(cr, _hoverShape);
    }
    return rSurface;
}

void CtDrawing::_render()
{
    // cairomm throws on any illegal operation (a degenerate matrix used to
    // abort the whole program), so a broken canvas must never escape here
    try {
        const Cairo::RefPtr<Cairo::ImageSurface> rSurface = _render_surface(true);
        const int width = rSurface->get_width();
        const int height = rSurface->get_height();
        Glib::RefPtr<Gdk::Pixbuf> rPixbuf = Gdk::Pixbuf::create(rSurface, 0, 0, width, height);
        if (not rPixbuf) return;
        _rPixbuf = rPixbuf;
        _image.set(_rPixbuf);
        _image.queue_draw();
    }
    catch (const std::exception& e) {
        spdlog::error("!! {} {}", __FUNCTION__, e.what());
    }
}

// ---------------------------------------------------------------- events

// while the mouse just hovers (no drag going on), show what a press would do:
// the resize arrows over the selected shape's grips and border lines, a
// pointing hand over the quick-connect arrows
void CtDrawing::_update_cursor(const double x, const double y)
{
    // NOTE: cursors MUST be picked from the Gdk::CursorType enum — the Win32
    // GDK backend does not resolve the CSS cursor NAMES ("ew-resize", ...)
    // and silently hands back an unusable cursor, which is why the resize
    // arrows never showed up on Windows
    Gdk::CursorType type = Gdk::ARROW;
    if (_editShape < 0) {
        int handle = _hit_handle(x, y);
        if (handle < 0) handle = _hit_edge(x, y);
        if (handle >= 0) {
            switch (handle) {
            case 1: case 7: type = Gdk::SB_V_DOUBLE_ARROW; break;   // top / bottom edge: up-down double arrow
            case 3: case 5: type = Gdk::SB_H_DOUBLE_ARROW; break;   // left / right edge: the requested left-right double arrow
            case 0: case 8: type = Gdk::TOP_LEFT_CORNER; break;     // top-left / bottom-right corner
            case 2: case 6: type = Gdk::TOP_RIGHT_CORNER; break;    // top-right / bottom-left corner
            default: break;
            }
        }
        else if (_hit_quick_arrow(x, y) >= 0) {
            type = Gdk::HAND2;                                   // a quick-connect arrow
        }
        else if (_selConn >= 0 and _hit_conn_end(_selConn, x, y) >= 0) {
            type = Gdk::HAND2;                                   // an endpoint grip of the selected connector
        }
        else if (_hit_conn(x, y) >= 0) {
            type = Gdk::FLEUR;                                   // the connector body can be dragged
        }
        else if (Tool::Select == _tool and _hit_shape(x, y) >= 0) {
            type = Gdk::FLEUR;                                   // the shape itself can be dragged
        }
        else if (Tool::Select != _tool) {
            type = Gdk::CROSSHAIR;                               // ready to draw the next shape
        }
    }
    if (type == _cursorType) return;
    _cursorType = type;
    if (Glib::RefPtr<Gdk::Window> rWindow = _pCanvas->get_window()) {
        rWindow->set_cursor(Gdk::Cursor::create(rWindow->get_display(), type));
    }
}

bool CtDrawing::_on_canvas_press(GdkEventButton* event)
{
    const double x = event->x;
    const double y = event->y;

    // any click on the canvas brings the tools up (they hide again on focus-out)
    _show_toolbar();

    if (3 == event->button) {
        _show_popup(event);
        return true;
    }
    if (1 != event->button) return false;

    if (_editShape >= 0) {
        _commit_text_editing();
    }

    if (GDK_2BUTTON_PRESS == event->type) {
        const int idx = _hit_shape(x, y);
        if (idx >= 0) {
            _select_only(idx, -1);
            _render();
            _start_text_editing(idx);
            return true;
        }
    }

    _pCanvas->grab_focus();

    // dragging a resize grip OR anywhere along a border line of the selected
    // shape works with ANY tool — the user never has to switch tools
    int handle = _hit_handle(x, y);
    if (handle < 0) handle = _hit_edge(x, y);
    if (handle >= 0) {
        _drag = Drag::Resize;
        _dragHandle = handle;
        _dragStartX = x;
        _dragStartY = y;
        const Shape& shape = _model.shapes[_selShape];
        _origX = shape.x;
        _origY = shape.y;
        _origW = shape.w;
        _origH = shape.h;
        return true;
    }

    // an endpoint grip of the SELECTED connector: dragging it moves the
    // attach point anywhere along a shape border (and detaches it when the
    // pointer leaves every shape)
    if (_selConn >= 0 and _selConn < static_cast<int>(_model.conns.size())) {
        const int end = _hit_conn_end(_selConn, x, y);
        if (end >= 0) {
            _drag = Drag::ConnEnd;
            _dragConnEnd = end;
            _origConn = _model.conns[_selConn];
            _dragStartX = x;
            _dragStartY = y;
            return true;
        }
    }

    // Visio-style: pressing one of the four outward quick-connect arrows
    // starts a connection from that border anchor point (a plain click, with
    // no drag, auto-creates the connected shape on release)
    const int qdir = _hit_quick_arrow(x, y);
    if (qdir >= 0) {
        const int fromIdx = _hoverShape;
        const Shape& fromShape = _model.shapes[fromIdx];
        const auto anchors = _border_anchors(fromShape);
        _anchor_dir(fromShape, qdir, _quickDirX, _quickDirY);
        _preview = true;
        _previewConn = Conn{};
        _previewConn.id = _next_conn_id();
        _previewConn.fromShape = fromIdx;
        _previewConn.x1 = anchors[qdir].first;
        _previewConn.y1 = anchors[qdir].second;
        _previewConn.x2 = x;
        _previewConn.y2 = y;
        _previewConn.arrowEnd = true;
        _previewConn.dashed = false;
        _previewConn.stroke = oa_rgba_to_hex(_strokeColor);
        _connFromQuick = true;
        _quickFromShape = fromIdx;
        _drag = Drag::Conn;
        _dragStartX = x;
        _dragStartY = y;
        _render();
        return true;
    }

    if (Tool::Select == _tool) {
        const int idx = _hit_shape(x, y);
        if (idx >= 0) {
            _select_only(idx, -1);
            _drag = Drag::Move;
            _dragStartX = x;
            _dragStartY = y;
            const Shape& shape = _model.shapes[idx];
            _origX = shape.x;
            _origY = shape.y;
            _origW = shape.w;
            _origH = shape.h;
            _render();
            return true;
        }
        const int cidx = _hit_conn(x, y);
        _select_only(-1, cidx);
        if (cidx >= 0) {
            // a connector can be picked up and moved as a whole
            _drag = Drag::MoveConn;
            _origConn = _model.conns[cidx];
            _dragStartX = x;
            _dragStartY = y;
        }
        _render();
        return true;
    }

    if (Tool::Connector == _tool or Tool::Arrow == _tool) {
        _preview = true;
        _previewConn = Conn{};
        _previewConn.id = _next_conn_id();
        _previewConn.x1 = x;
        _previewConn.y1 = y;
        _previewConn.x2 = x;
        _previewConn.y2 = y;
        _previewConn.fromShape = _hit_shape(x, y);
        _previewConn.toShape = _previewConn.fromShape;
        _previewConn.arrowEnd = (Tool::Arrow == _tool);
        _previewConn.dashed = false;
        _previewConn.stroke = oa_rgba_to_hex(_strokeColor);
        _drag = Drag::Conn;
        _dragStartX = x;
        _dragStartY = y;
        _snap_conn_preview(x, y);
        _render();
        return true;
    }

    // a shape tool is active: pressing an existing shape selects/moves it
    // (no tool switching needed); dragging on empty canvas draws a new one.
    // A plain click NEVER creates a shape — a drag of 8px or more is required
    const int hitIdx = _hit_shape(x, y);
    if (hitIdx >= 0) {
        _select_only(hitIdx, -1);
        _drag = Drag::Move;
        _dragStartX = x;
        _dragStartY = y;
        const Shape& shape = _model.shapes[hitIdx];
        _origX = shape.x;
        _origY = shape.y;
        _origW = shape.w;
        _origH = shape.h;
        _render();
        return true;
    }

    _preview = true;
    _previewShape = Shape{};
    _previewShape.id = _next_shape_id();
    _previewShape.type = _tool;
    _previewShape.x = x;
    _previewShape.y = y;
    _previewShape.w = 0.0;
    _previewShape.h = 0.0;
    _previewShape.fill = oa_rgba_to_hex(_fillColor);
    _previewShape.stroke = oa_rgba_to_hex(_strokeColor);
    _previewShape.fontSize = _defaultFontSize;
    _previewShape.textColor = _defaultTextColor;
    _drag = Drag::Create;
    _dragStartX = x;
    _dragStartY = y;
    _render();
    return true;
}

bool CtDrawing::_on_canvas_motion(GdkEventMotion* event)
{
    const double x = event->x;
    const double y = event->y;

    if (Drag::None == _drag) {
        // track which shape the mouse is over: the quick-connect arrows of
        // that shape appear (and disappear when the mouse leaves)
        const int hover = _hit_shape(x, y);
        if (hover != _hoverShape) {
            _hoverShape = hover;
            _render();
        }
        _update_cursor(x, y);
        return false;
    }

    if (Drag::Create == _drag) {
        _previewShape.x = std::min(_dragStartX, x);
        _previewShape.y = std::min(_dragStartY, y);
        _previewShape.w = std::abs(x - _dragStartX);
        _previewShape.h = std::abs(y - _dragStartY);
    }
    else if (Drag::Move == _drag and _selShape >= 0 and _selShape < static_cast<int>(_model.shapes.size())) {
        Shape& shape = _model.shapes[_selShape];
        shape.x = std::max(0.0, _origX + (x - _dragStartX));
        shape.y = std::max(0.0, _origY + (y - _dragStartY));
        _grow_canvas_for(shape.x + shape.w, shape.y + shape.h);
        _recompute_conns();
    }
    else if (Drag::Resize == _drag and _selShape >= 0 and _selShape < static_cast<int>(_model.shapes.size())) {
        Shape& shape = _model.shapes[_selShape];
        const double dx = x - _dragStartX;
        const double dy = y - _dragStartY;
        double nx = _origX, ny = _origY, nw = _origW, nh = _origH;
        if (0 != (_dragHandle % 3)) {          // left or right column
            if (1 == (_dragHandle % 3)) {      // left edge
                nx = _origX + dx;
                nw = _origW - dx;
            }
            else {                             // right edge
                nw = _origW + dx;
            }
        }
        if (_dragHandle < 3) {                 // top row
            ny = _origY + dy;
            nh = _origH - dy;
        }
        else if (_dragHandle >= 6) {           // bottom row
            nh = _origH + dy;
        }
        if (nw < 20.0) { nw = 20.0; }
        if (nh < 16.0) { nh = 16.0; }
        shape.x = nx;
        shape.y = ny;
        shape.w = nw;
        shape.h = nh;
        _grow_canvas_for(shape.x + shape.w, shape.y + shape.h);
        _recompute_conns();
    }
    else if (Drag::ConnEnd == _drag and _selConn >= 0 and _selConn < static_cast<int>(_model.conns.size())) {
        Conn& conn = _model.conns[_selConn];
        const double px = (0 == _dragConnEnd ? _origConn.x1 : _origConn.x2) + (x - _dragStartX);
        const double py = (0 == _dragConnEnd ? _origConn.y1 : _origConn.y2) + (y - _dragStartY);
        // glues to ANY point of the border of the shape under the pointer
        // (and detaches when dropped on empty canvas)
        _set_conn_end(conn, 0 == _dragConnEnd, px, py);
    }
    else if (Drag::MoveConn == _drag and _selConn >= 0 and _selConn < static_cast<int>(_model.conns.size())) {
        Conn& conn = _model.conns[_selConn];
        const double dx = x - _dragStartX;
        const double dy = y - _dragStartY;
        // an end glued to a shape keeps gluing (sliding along its border) as
        // long as it stays close to it, otherwise it becomes a free point
        const double sx = _origConn.x1 + dx;
        const double sy = _origConn.y1 + dy;
        const double ex = _origConn.x2 + dx;
        const double ey = _origConn.y2 + dy;
        const int nShapes = static_cast<int>(_model.shapes.size());
        if (_origConn.fromShape >= 0 and _origConn.fromShape < nShapes) {
            const Shape& shape = _model.shapes[_origConn.fromShape];
            const double u = _u_on_outline(shape, sx, sy);
            const auto p = _point_on_outline(shape, u);
            if (std::hypot(p.first - sx, p.second - sy) <= 24.0) {
                conn.fromShape = _origConn.fromShape;
                conn.fromU = u;
                conn.x1 = p.first;
                conn.y1 = p.second;
            }
            else { conn.fromShape = -1; conn.fromU = -1.0; conn.x1 = std::max(0.0, sx); conn.y1 = std::max(0.0, sy); }
        }
        else { conn.x1 = std::max(0.0, sx); conn.y1 = std::max(0.0, sy); }
        if (_origConn.toShape >= 0 and _origConn.toShape < nShapes) {
            const Shape& shape = _model.shapes[_origConn.toShape];
            const double u = _u_on_outline(shape, ex, ey);
            const auto p = _point_on_outline(shape, u);
            if (std::hypot(p.first - ex, p.second - ey) <= 24.0) {
                conn.toShape = _origConn.toShape;
                conn.toU = u;
                conn.x2 = p.first;
                conn.y2 = p.second;
            }
            else { conn.toShape = -1; conn.toU = -1.0; conn.x2 = std::max(0.0, ex); conn.y2 = std::max(0.0, ey); }
        }
        else { conn.x2 = std::max(0.0, ex); conn.y2 = std::max(0.0, ey); }
        _grow_canvas_for(std::max(conn.x1, conn.x2), std::max(conn.y1, conn.y2));
    }
    else if (Drag::Conn == _drag) {
        _snap_conn_preview(x, y);
    }

    // throttle the re-render so dragging stays smooth
    const gint64 nowUs = g_get_monotonic_time();
    if (nowUs - _lastRenderUs >= 16000) {   // ~60 fps at most
        _lastRenderUs = nowUs;
        _render();
    }
    return true;
}

bool CtDrawing::_on_canvas_release(GdkEventButton* event)
{
    if (1 != event->button) return false;
    const double x = event->x;
    const double y = event->y;

    if (Drag::Create == _drag) {
        // only a real drag creates a shape; a plain click stays a no-op
        if (std::hypot(x - _dragStartX, y - _dragStartY) >= 8.0) {
            _previewShape.x = std::min(_dragStartX, x);
            _previewShape.y = std::min(_dragStartY, y);
            _previewShape.w = std::max(12.0, std::abs(x - _dragStartX));
            _previewShape.h = std::max(12.0, std::abs(y - _dragStartY));
            _model.shapes.push_back(_previewShape);
            _select_only(static_cast<int>(_model.shapes.size()) - 1, -1);
            _grow_canvas_for(_previewShape.x + _previewShape.w, _previewShape.y + _previewShape.h);
            // the tool stays active: keep holding and dragging draws the next
            // shape of the same kind right away (a click still creates nothing)
        }
    }
    else if (Drag::Conn == _drag) {
        _snap_conn_preview(x, y);
        const double dragDist = std::hypot(x - _dragStartX, y - _dragStartY);
        if (_connFromQuick and dragDist < 8.0) {
            // a plain click on a quick arrow: drop a copy of the shape in the
            // arrow direction and connect the two right away
            _quick_auto_create();
        }
        else if (dragDist >= 8.0) {
            if (_connFromQuick and _previewConn.fromShape >= 0 and _previewConn.toShape < 0) {
                // quick-connect released over empty canvas: ask which shape to
                // drop there — it gets created and connected automatically
                _quickPending = true;
                _quickFromShape = _previewConn.fromShape;
                _quickMenuX = x;
                _quickMenuY = y;
            }
            else {
                _model.conns.push_back(_previewConn);
                if (_previewConn.fromShape >= 0 and _previewConn.toShape >= 0) {
                    // both ends snapped: the line keeps following the shapes
                    _recompute_conns();
                    if (_connFromQuick) {
                        // chain-friendly: the target becomes selected, its own
                        // quick arrows show up right away
                        _select_only(_previewConn.toShape, -1);
                    }
                    else {
                        _select_only(-1, static_cast<int>(_model.conns.size()) - 1);
                    }
                }
                else {
                    _select_only(-1, static_cast<int>(_model.conns.size()) - 1);
                }
            }
        }
        _connFromQuick = false;
    }
    else if (Drag::Move == _drag or Drag::Resize == _drag or
             Drag::ConnEnd == _drag or Drag::MoveConn == _drag) {
        _recompute_conns();
    }

    _preview = false;
    _drag = Drag::None;
    _dragHandle = -1;
    _dragConnEnd = -1;
    _hoverShape = _hit_shape(x, y);   // arrows follow the pointer right away
    _sync_model();
    _render();
    _update_cursor(x, y);
    _pCtMainWin->update_window_save_needed(CtSaveNeededUpdType::nbuf, true);

    if (_quickPending) {
        // open the shape picker after the release bookkeeping is done
        _quick_show_menu(event);
    }
    return true;
}

bool CtDrawing::_on_canvas_key(GdkEventKey* event)
{
    if (GDK_KEY_Escape == event->keyval) {
        if (_editShape >= 0) {
            _commit_text_editing();
            return true;
        }
        _select_only(-1, -1);
        _set_tool(Tool::Select);
        _render();
        return true;
    }
    if (GDK_KEY_Delete == event->keyval or GDK_KEY_KP_Delete == event->keyval) {
        _delete_selection();
        return true;
    }
    if ((GDK_KEY_Return == event->keyval or GDK_KEY_F2 == event->keyval) and _selShape >= 0) {
        _start_text_editing(_selShape);
        return true;
    }
    if (GDK_KEY_Tab == event->keyval) {
        // let Tab move the focus out of the drawing instead of inserting a tab
        return false;
    }
    return false;
}

bool CtDrawing::_on_canvas_draw(const Cairo::RefPtr<Cairo::Context>& /*cr*/)
{
    return false;
}

void CtDrawing::_show_popup(GdkEventButton* event)
{
    const int hitIdx = _hit_shape(event->x, event->y);
    if (hitIdx >= 0) {
        _select_only(hitIdx, -1);
        _render();
    }

    // the menu is a member: a local one would be destroyed before the click
    for (Gtk::Widget* pChild : _popup.get_children()) {
        _popup.remove(*pChild);
    }

    Gtk::MenuItem* pItemEdit = Gtk::manage(new Gtk::MenuItem{_("输入文字…")});
    pItemEdit->signal_activate().connect([this, hitIdx]() {
        _start_text_editing(hitIdx >= 0 ? hitIdx : _selShape);
    });
    pItemEdit->set_sensitive(hitIdx >= 0 or _selShape >= 0);
    _popup.append(*pItemEdit);

    Gtk::MenuItem* pItemDelete = Gtk::manage(new Gtk::MenuItem{_("删除")});
    pItemDelete->signal_activate().connect([this]() { _delete_selection(); });
    pItemDelete->set_sensitive(_selShape >= 0 or _selConn >= 0);
    _popup.append(*pItemDelete);

    _popup.append(*Gtk::manage(new Gtk::SeparatorMenuItem{}));

    Gtk::MenuItem* pItemFront = Gtk::manage(new Gtk::MenuItem{_("置于顶层")});
    pItemFront->signal_activate().connect([this]() {
        if (_selShape >= 0 and _selShape < static_cast<int>(_model.shapes.size())) {
            Shape shape = _model.shapes[_selShape];
            _model.shapes.erase(_model.shapes.begin() + _selShape);
            _model.shapes.push_back(shape);
            _selShape = static_cast<int>(_model.shapes.size()) - 1;
            _sync_model();
            _render();
            _pCtMainWin->update_window_save_needed(CtSaveNeededUpdType::nbuf, true);
        }
    });
    pItemFront->set_sensitive(_selShape >= 0);
    _popup.append(*pItemFront);

    Gtk::MenuItem* pItemSave = Gtk::manage(new Gtk::MenuItem{_("导出为 PNG 图片…")});
    pItemSave->signal_activate().connect([this]() {
        CtDialogs::CtFileSelectArgs args{};
        args.curr_folder = _pCtConfig->pickDirFile;
        args.curr_file_name = "diagram.png";
        args.filter_name = _("PNG Image");
        args.filter_pattern = {"*.png"};
        const std::string filepath = CtDialogs::file_save_as_dialog(_pCtMainWin, args);
        if (filepath.empty()) return;
        _pCtConfig->pickDirFile = Glib::path_get_dirname(filepath);
        save(fs::path{filepath}, "png");
    });
    _popup.append(*pItemSave);

    _popup.show_all();
    _popup.popup(event->button, event->time);
}

// Visio-style: after a quick-connect drag ended on empty canvas, let the user
// pick which shape to drop there — it is created at the release point and
// connected back to the origin shape automatically
void CtDrawing::_quick_show_menu(GdkEventButton* event)
{
    for (Gtk::Widget* pChild : _quickMenu.get_children()) {
        _quickMenu.remove(*pChild);
    }

    const std::vector<std::pair<Tool, const char*>> items = {
        {Tool::Rect,          _("矩形")},
        {Tool::RoundRect,     _("圆角矩形")},
        {Tool::Ellipse,       _("椭圆")},
        {Tool::Diamond,       _("菱形")},
        {Tool::Parallelogram, _("平行四边形")},
        {Tool::Cylinder,      _("圆柱")},
        {Tool::Hexagon,       _("六边形")},
        {Tool::Terminator,    _("胶囊")},
        {Tool::Document,      _("文档")},
    };
    for (const auto& item : items) {
        // MenuItem is a Bin: pack an icon + label box into it (set_image is
        // not available in this gtkmm build)
        Gtk::MenuItem* pItem = Gtk::manage(new Gtk::MenuItem{});
        Gtk::Box* pBox = Gtk::manage(new Gtk::Box{Gtk::ORIENTATION_HORIZONTAL, 6});
        pBox->pack_start(*Gtk::manage(new Gtk::Image{_icon_for_tool(item.first)}), false, false);
        pBox->pack_start(*Gtk::manage(new Gtk::Label{item.second}), false, false);
        pItem->add(*pBox);
        const Tool shapeType = item.first;
        pItem->signal_activate().connect([this, shapeType]() { _quick_create_shape(shapeType); });
        _quickMenu.append(*pItem);
    }

    _quickMenu.show_all();
    _quickMenu.popup(event->button, event->time);
}

void CtDrawing::_quick_create_shape(const Tool shapeType)
{
    if (not _quickPending or _quickFromShape < 0 or _quickFromShape >= static_cast<int>(_model.shapes.size())) {
        _quickPending = false;
        return;
    }
    _quickPending = false;

    Shape shape{};
    shape.id = _next_shape_id();
    shape.type = shapeType;
    shape.w = 140.0;
    shape.h = 64.0;
    shape.x = std::max(0.0, _quickMenuX - shape.w / 2.0);
    shape.y = std::max(0.0, _quickMenuY - shape.h / 2.0);
    shape.fill = oa_rgba_to_hex(_fillColor);
    shape.stroke = oa_rgba_to_hex(_strokeColor);
    shape.fontSize = _defaultFontSize;
    shape.textColor = _defaultTextColor;
    _model.shapes.push_back(shape);
    const int newIdx = static_cast<int>(_model.shapes.size()) - 1;

    // connect it back to the shape the arrow was dragged from; the endpoints
    // follow the border anchor midpoints from now on
    Conn conn{};
    conn.id = _next_conn_id();
    conn.fromShape = _quickFromShape;
    conn.toShape = newIdx;
    conn.arrowEnd = true;
    conn.dashed = false;
    conn.stroke = oa_rgba_to_hex(_strokeColor);
    _model.conns.push_back(conn);
    _recompute_conns();

    _grow_canvas_for(shape.x + shape.w, shape.y + shape.h);
    _select_only(newIdx, -1);
    _sync_model();
    _render();
    _pCtMainWin->update_window_save_needed(CtSaveNeededUpdType::nbuf, true);
}

// a plain click (no drag) on one of the hovered shape's quick arrows: clone
// that shape one step out in the arrow direction and connect the two
void CtDrawing::_quick_auto_create()
{
    if (_quickFromShape < 0 or _quickFromShape >= static_cast<int>(_model.shapes.size())) return;
    const Shape from = _model.shapes[_quickFromShape];   // a copy: we push below

    Shape shape{};
    shape.id = _next_shape_id();
    shape.type = from.type;
    shape.w = from.w;
    shape.h = from.h;
    shape.fill = from.fill;
    shape.stroke = from.stroke;
    shape.fontSize = from.fontSize;
    shape.bold = from.bold;
    shape.textColor = from.textColor;
    const double gap = 44.0;
    const double cx = from.x + from.w / 2.0 + _quickDirX * (from.w / 2.0 + gap + shape.w / 2.0);
    const double cy = from.y + from.h / 2.0 + _quickDirY * (from.h / 2.0 + gap + shape.h / 2.0);
    shape.x = std::max(4.0, cx - shape.w / 2.0);
    shape.y = std::max(4.0, cy - shape.h / 2.0);
    _grow_canvas_for(shape.x + shape.w, shape.y + shape.h);
    // the canvas growth is capped: keep the new shape inside it
    if (shape.x + shape.w > _model.width - 4.0) shape.x = std::max(4.0, _model.width - 4.0 - shape.w);
    if (shape.y + shape.h > _model.height - 4.0) shape.y = std::max(4.0, _model.height - 4.0 - shape.h);
    _model.shapes.push_back(shape);
    const int newIdx = static_cast<int>(_model.shapes.size()) - 1;

    Conn conn{};
    conn.id = _next_conn_id();
    conn.fromShape = _quickFromShape;
    conn.toShape = newIdx;
    conn.arrowEnd = true;
    conn.dashed = false;
    conn.stroke = oa_rgba_to_hex(_strokeColor);
    _model.conns.push_back(conn);
    _recompute_conns();

    _select_only(newIdx, -1);
}

// ---------------------------------------------------------------- storage

std::string CtDrawing::_png_blob()
{
    // a clean re-render: the on-screen pixbuf carries selection UI (handles,
    // quick-connect arrows) that must never leak into the saved image
    Cairo::RefPtr<Cairo::ImageSurface> rSurface;
    try {
        rSurface = _render_surface(false);
    }
    catch (const std::exception& e) {
        spdlog::error("!! {} {}", __FUNCTION__, e.what());
        return std::string{};
    }
    Glib::RefPtr<Gdk::Pixbuf> rPixbuf = Gdk::Pixbuf::create(rSurface, 0, 0, rSurface->get_width(), rSurface->get_height());
    if (not rPixbuf) return std::string{};
    g_autofree gchar* pBuffer{nullptr};
    gsize buffer_size{0};
    rPixbuf->save_to_buffer(pBuffer, buffer_size, "png");
    return std::string{pBuffer, buffer_size};
}

void CtDrawing::to_xml(xmlpp::Element* p_node_parent, const int offset_adjustment, CtStorageCache* /*cache*/, const std::string& multifile_dir)
{
    xmlpp::Element* p_image_node = p_node_parent->add_child("encoded_png");
    p_image_node->set_attribute("char_offset", std::to_string(_charOffset + offset_adjustment));
    p_image_node->set_attribute(CtConst::TAG_JUSTIFICATION, _justification);
    p_image_node->set_attribute("link", "");   // a drawing carries no link, the model travels in its own attribute
    p_image_node->set_attribute(XML_MODEL_ATTR, Glib::Base64::encode(_modelXml));
    if (multifile_dir.empty()) {
        p_image_node->add_child_text(Glib::Base64::encode(_png_blob()));
    }
    else {
        const std::string sha256sum = CtStorageMultiFile::save_blob(_png_blob(), multifile_dir, ".png");
        p_image_node->set_attribute("sha256sum", sha256sum);
    }
}

bool CtDrawing::to_sqlite(sqlite3* pDb, const gint64 node_id, const int offset_adjustment, CtStorageCache* /*cache*/)
{
    bool retVal{true};
    sqlite3_stmt* p_stmt{nullptr};
    if (SQLITE_OK != sqlite3_prepare_v2(pDb, CtStorageSqlite::TABLE_IMAGE_INSERT, -1, &p_stmt, nullptr)) {
        spdlog::error("{}: {}", CtStorageSqlite::ERR_SQLITE_PREPV2, sqlite3_errmsg(pDb));
        retVal = false;
    }
    else {
        const std::string rawBlob = _png_blob();
        // the SQLite schema has no room for the vector model: it rides along in
        // the link column, prefixed so it is never mistaken for a real link
        const std::string link = std::string{SQLITE_LINK_PREFIX} + Glib::Base64::encode(_modelXml);

        sqlite3_bind_int64(p_stmt, 1, node_id);
        sqlite3_bind_int64(p_stmt, 2, _charOffset + offset_adjustment);
        sqlite3_bind_text(p_stmt, 3, _justification.c_str(), _justification.size(), SQLITE_STATIC);
        sqlite3_bind_text(p_stmt, 4, "", -1, SQLITE_STATIC); // anchor name
        sqlite3_bind_blob(p_stmt, 5, rawBlob.c_str(), rawBlob.size(), SQLITE_STATIC);
        sqlite3_bind_text(p_stmt, 6, "", -1, SQLITE_STATIC); // filename
        sqlite3_bind_text(p_stmt, 7, link.c_str(), link.size(), SQLITE_STATIC);
        sqlite3_bind_int64(p_stmt, 8, 0); // time
        if (SQLITE_DONE != sqlite3_step(p_stmt)) {
            spdlog::error("{}: {}", CtStorageSqlite::ERR_SQLITE_STEP, sqlite3_errmsg(pDb));
            retVal = false;
        }
        sqlite3_finalize(p_stmt);
    }
    return retVal;
}

std::shared_ptr<CtAnchoredWidgetState> CtDrawing::get_state()
{
    return std::shared_ptr<CtAnchoredWidgetState>(new CtAnchoredWidgetState_Drawing{this});
}
