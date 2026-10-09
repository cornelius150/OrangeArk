/*
 * ct_drawing.h
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

#pragma once

#include "ct_image.h"

#include <array>
#include <utility>

#include <gtkmm.h>

#include <string>
#include <vector>

// OrangeArk: an editable flow chart / topology diagram anchored in the text.
// It is stored as a plain PNG image (so every other CherryTree/OrangeArk can
// still show it) PLUS the vector model either in the XML attribute
// CtDrawing::XML_MODEL_ATTR (.ctd/.ctz) or, for the SQLite document format,
// in the image "link" column prefixed with CtDrawing::SQLITE_LINK_PREFIX.
class CtDrawing : public CtImage
{
public:
    CtDrawing(CtMainWin* pCtMainWin,
              const std::string& modelXml,
              const int charOffset,
              const std::string& justification);
    ~CtDrawing() override {}

    void to_xml(xmlpp::Element* p_node_parent, const int offset_adjustment, CtStorageCache* cache, const std::string& multifile_dir) override;
    bool to_sqlite(sqlite3* pDb, const gint64 node_id, const int offset_adjustment, CtStorageCache* cache) override;
    CtAnchWidgType get_type() const override { return CtAnchWidgType::ImagePng; }
    std::shared_ptr<CtAnchoredWidgetState> get_state() override;

    const std::string& get_model_xml() const { return _modelXml; }

    static const char* XML_MODEL_ATTR;      // "oa_drawing" - base64 model, XML storage
    static const char* SQLITE_LINK_PREFIX;  // "oa-drawing:" - base64 model, SQLite storage

private:
    enum class Tool {
        Select = 0,
        Rect,
        RoundRect,
        Ellipse,
        Diamond,
        Parallelogram,
        Cylinder,
        Hexagon,
        Terminator,
        Document,
        Connector,   // plain line
        Arrow,       // line with an arrow head at the end
        NumTools
    };

    enum class Drag { None, Create, Move, Resize, Conn, MoveConn, ConnEnd };

    struct Shape {
        int          id{0};
        Tool         type{Tool::Rect};
        double       x{0.}, y{0.}, w{0.}, h{0.};
        std::string  fill{"#e3f2fd"};
        std::string  stroke{"#1565c0"};
        std::string  text;
        double       fontSize{13.};
        bool         bold{false};
        std::string  textColor{"#1f2429"};
    };

    struct Conn {
        int          id{0};
        double       x1{0.}, y1{0.}, x2{0.}, y2{0.};
        int          fromShape{-1};
        int          toShape{-1};
        bool         arrowEnd{true};
        bool         dashed{false};
        std::string  stroke{"#37474f"};
        double       fromU{-1.};   // custom attach point along the origin shape border [0,1); <0 = auto (border anchor)
        double       toU{-1.};     // same for the target end
    };

    struct Model {
        int                width{640};
        int                height{360};
        std::vector<Shape> shapes;
        std::vector<Conn>  conns;
    };

private:
    static Model       _model_from_xml(const std::string& xml);
    static std::string _model_to_xml(const Model& model);
    static double      _shape_attr_double(xmlpp::Element* pElement, const char* name, const double defVal);
    static int         _shape_attr_int(xmlpp::Element* pElement, const char* name, const int defVal);
    static std::string _shape_attr_str(xmlpp::Element* pElement, const char* name, const std::string& defVal);

    void _sync_model();                 // model -> xml string (kept for saving)
    Cairo::RefPtr<Cairo::ImageSurface> _render_surface(const bool withUi); // model -> cairo surface (withUi=false: a clean image for saving)
    void _render();                     // model -> pixbuf -> Gtk::Image
    void _render_grid(const Cairo::RefPtr<Cairo::Context>& cr) const;
    void _render_shape(const Cairo::RefPtr<Cairo::Context>& cr, const Shape& shape);
    void _render_conn(const Cairo::RefPtr<Cairo::Context>& cr, const Conn& conn) const;
    void _render_conn_grips(const Cairo::RefPtr<Cairo::Context>& cr, const Conn& conn) const; // draggable endpoints of the selected connector
    void _shape_path(const Cairo::RefPtr<Cairo::Context>& cr, const Shape& shape) const;
    void _arrow_head(const Cairo::RefPtr<Cairo::Context>& cr, const double x, const double y, const double angle, const double size = 9.0) const;
    void _render_handles(const Cairo::RefPtr<Cairo::Context>& cr, const Shape& shape) const;
    void _render_quick_arrows(const Cairo::RefPtr<Cairo::Context>& cr, const int shapeIdx); // Visio-style connect arrows on the hovered shape

    int  _hit_shape(const double x, const double y) const;
    int  _hit_conn(const double x, const double y) const;
    int  _hit_handle(const double x, const double y) const;   // 0..8 except 4 (center) or -1
    int  _hit_edge(const double x, const double y) const;     // border-line of the selected shape: 1 top, 3 left, 5 right, 7 bottom or -1
    int  _hit_quick_arrow(const double x, const double y) const; // Visio-style outward arrow: 0..3 or -1
    int  _hit_conn_end(const int connIdx, const double x, const double y) const; // 0 = start, 1 = end, -1 = none
    int  _shape_at_point(const double x, const double y, const double margin) const; // topmost shape whose box (grown by margin) contains the point
    void _update_cursor(const double x, const double y);      // resize / pointer cursors while hovering
    void _handle_pos(const Shape& shape, const int handle, double& hx, double& hy) const;
    bool _inside_shape(const Shape& shape, const double x, const double y) const;
    void _recompute_conns();            // snap the endpoints of shape-bound connectors
    void _update_conn_ends(Conn& conn); // refresh one connector's endpoints from the shapes they are glued to
    void _set_conn_end(Conn& conn, const bool isStart, const double x, const double y); // glue/snap one endpoint (any point of a border)
    static void _anchor_dir(const Shape& shape, const int anchorIdx, double& dx, double& dy); // outward unit direction of a border anchor
    static std::array<std::pair<double, double>, 4> _border_anchors(const Shape& shape); // per-shape connect points (diamond: the 4 edge midpoints)
    static std::pair<double, double> _nearest_anchor(const Shape& shape, const double x, const double y);
    void _snap_conn_preview(const double x, const double y);
    // geometry of the real shape border (works for every shape, not only boxes)
    static std::vector<std::pair<double, double>> _outline_points(const Shape& shape);  // polygon approximating the border
    static std::pair<double, double> _point_on_outline(const Shape& shape, const double u); // u in [0,1) along the border
    static double _u_on_outline(const Shape& shape, const double x, const double y);    // u of the closest border point
    static double _dist_to_outline(const Shape& shape, const double x, const double y);
    static std::pair<double, double> _ray_outline_hit(const Shape& shape, const double dx, const double dy); // first border hit from the centre
    static bool _clip_line_to_rect(const double cx, const double cy,
                                   const double tx, const double ty,
                                   const double rx, const double ry, const double rw, const double rh,
                                   double& outX, double& outY);
    void _grow_canvas_for(const double x, const double y);
    void _fit_canvas();
    void _delete_selection();
    void _start_text_editing(const int shapeIdx);
    void _commit_text_editing();
    void _apply_style_to_selection();
    void _select_only(const int shapeIdx, const int connIdx);
    int  _next_shape_id() const;
    int  _next_conn_id() const;

    Gtk::Box* _build_toolbar();
    void      _set_tool(const Tool tool);
    Gtk::Button* _tool_button(const Glib::RefPtr<Gdk::Pixbuf>& rIcon, const Glib::ustring& tooltip, const Tool tool);
    void      _update_toolbar_sensitivity();
    Glib::RefPtr<Gdk::Pixbuf> _icon_for_tool(const Tool tool);
    Glib::RefPtr<Gdk::Pixbuf> _icon_for_action(const char* kind);   // "text", "delete", "fit"

    void _show_toolbar();
    void _hide_toolbar();
    bool _on_canvas_focus_out(GdkEventFocus* event);
    bool _grab_focus_on_idle();
    void _sync_style_controls();
    void _on_font_size_changed();
    void _on_text_color_set();
    void _apply_editor_text_style();

    bool _on_canvas_press(GdkEventButton* event);
    bool _on_canvas_motion(GdkEventMotion* event);
    bool _on_canvas_release(GdkEventButton* event);
    bool _on_canvas_key(GdkEventKey* event);
    bool _on_canvas_draw(const Cairo::RefPtr<Cairo::Context>& cr);
    void _show_popup(GdkEventButton* event);
    void _quick_show_menu(GdkEventButton* event);       // Visio-style: pick a shape to auto-create and connect
    void _quick_create_shape(const Tool shapeType);     // create the picked shape at the release point and connect it
    void _quick_auto_create();                          // a plain click on a quick arrow: clone the origin shape beside it and connect
    void _on_fill_color_set();
    void _on_stroke_color_set();
    void _on_editor_focus_out();
    std::string _png_blob();

private:
    Model          _model;
    std::string    _modelXml;
    Tool           _tool{Tool::Select};
    int            _selShape{-1};
    int            _selConn{-1};
    Drag           _drag{Drag::None};
    int            _dragHandle{-1};
    double         _dragStartX{0.}, _dragStartY{0.};
    double         _origX{0.}, _origY{0.}, _origW{0.}, _origH{0.};
    bool           _preview{false};
    Shape          _previewShape;
    Conn           _previewConn;
    int            _editShape{-1};
    gint64         _lastRenderUs{0};
    int            _hoverShape{-1};        // shape under the mouse (drives the quick-connect arrows)
    double         _quickDirX{0.}, _quickDirY{-1.};  // outward direction of the pressed quick arrow
    int            _dragConnEnd{-1};       // 0 = start / 1 = end of the connector endpoint being dragged
    Conn           _origConn;              // connector geometry when the drag started

    Gtk::Box*      _pVBox{nullptr};
    Gtk::Box*      _pToolbar{nullptr};
    Gtk::EventBox* _pCanvas{nullptr};
    Gtk::Overlay*  _pOverlay{nullptr};
    Gtk::TextView* _pEditor{nullptr};
    Gtk::ColorButton* _pFillBtn{nullptr};
    Gtk::ColorButton* _pStrokeBtn{nullptr};
    Gtk::ColorButton* _pTextColBtn{nullptr};
    Gtk::ComboBoxText* _pSizeCombo{nullptr};
    Gtk::Menu         _popup;        // kept alive: a local menu would die before it is clicked
    Gdk::RGBA      _fillColor;
    Gdk::RGBA      _strokeColor;
    Gdk::RGBA      _textColor;
    double         _defaultFontSize{13.};
    std::string    _defaultTextColor{"#1f2429"};
    bool           _syncingStyle{false};
    std::vector<Gtk::Button*> _toolButtons;
    bool           _connFromQuick{false};  // the connector being dragged started from a quick-connect arrow
    bool           _quickPending{false};   // a quick-connect release awaits the shape pick
    int            _quickFromShape{-1};    // origin shape of the pending quick connection
    double         _quickMenuX{0.}, _quickMenuY{0.};  // canvas coords of the quick-connect release point
    Gtk::Menu      _quickMenu;             // kept alive: the shape picker of the quick-connect flow
    Gdk::CursorType _cursorType{static_cast<Gdk::CursorType>(-1)}; // currently applied canvas cursor (avoid redundant sets)
};
