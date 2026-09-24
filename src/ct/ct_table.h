/*
 * ct_table.h
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

#include "ct_codebox.h"
#include "ct_widgets.h"
#include <cairomm/cairomm.h>
#include <optional>

class CtAnchoredWidgetState_TableCommon;
class CtTableCommon : public CtAnchoredWidget
{
public:
    CtTableCommon(CtMainWin* pCtMainWin,
                  const int colWidthDefault,
                  const int charOffset,
                  const std::string& justification,
                  const CtTableColWidths& colWidths,
                  const size_t currRow,
                  const size_t currCol,
                  const CtTableColWidths& rowHeights = {});

    std::shared_ptr<CtAnchoredWidgetState_TableCommon> get_state_common() const;

    void apply_width_height(const int /*parentTextWidth*/) override {}

    const CtTableColWidths& get_col_widths_raw() const { return _colWidths; }
    int get_col_width_default() const { return _colWidthDefault; }
    // OrangeArk: per-row heights (0 = auto), parallel to _colWidths
    const CtTableColWidths& get_row_heights_raw() const { return _rowHeights; }
    bool get_is_light() const;
    int get_col_width(const std::optional<size_t> optColIdx = std::nullopt) const {
        const size_t colIdx = optColIdx.value_or(_currentColumn);
        return _colWidths.at(colIdx) != 0 ? _colWidths.at(colIdx) : _colWidthDefault;
    }
    CtTableColWidths get_col_widths() const {
        CtTableColWidths colWidths;
        for (size_t colIdx = 0; colIdx < _colWidths.size(); ++colIdx) {
            colWidths.push_back(get_col_width(colIdx));
        }
        return colWidths;
    }
    void to_xml(xmlpp::Element* p_node_parent, const int offset_adjustment, CtStorageCache* cache, const std::string& multifile_dir) override;
    bool to_sqlite(sqlite3* pDb, const gint64 node_id, const int offset_adjustment, CtStorageCache* cache) override;

    // Build a table from csv; The input csv should be compatable with the excel csv format
    static void populate_table_matrix_from_csv(const std::string& filepath,
                                               CtMainWin* main_win,
                                               const bool is_light,
                                               CtTableMatrix& tbl_matrix);

    // Serialise to csv format; The output CSV excel csv with double quotes around cells and newlines for each record
    virtual std::string to_csv() const = 0;

    virtual Glib::ustring get_line_content(const size_t rowIdx, const size_t colIdx, const int match_end_offset) const = 0;

    virtual void write_strings_matrix(std::vector<std::vector<Glib::ustring>>& rows) const = 0;
    virtual size_t get_num_rows() const = 0;
    virtual size_t get_num_columns() const = 0;

    size_t current_row() const { return _currentRow < get_num_rows() ? _currentRow : 0; }
    size_t current_column() const { return _currentColumn < get_num_columns() ? _currentColumn : 0; }
    bool row_sort_asc() { return _row_sort(true/*sortAsc*/); }
    bool row_sort_desc() { return _row_sort(false/*sortAsc*/); }
    void row_move_down(const size_t rowIdx);
    void set_current_row_column(const size_t rowIdx, const size_t colIdx);
    std::pair<size_t, size_t> get_row_idx_col_idx(const size_t cell_idx) const;

    virtual void column_add(const size_t afterColIdx, const std::vector<Glib::ustring>* pNewColumn = nullptr) = 0;
    virtual void column_delete(const size_t colIdx) = 0;
    virtual void column_move_left(const size_t colIdx, const bool from_move_right) = 0;
    virtual void column_move_right(const size_t colIdx) = 0;
    virtual void row_add(const size_t afterRowIdx, const std::vector<Glib::ustring>* pNewRow = nullptr) = 0;
    virtual void row_delete(const size_t rowIdx) = 0;
    virtual void row_move_up(const size_t rowIdx, const bool from_move_down) = 0;

    virtual void set_col_width_default(const int colWidthDefault) = 0;
    virtual void set_col_width(const int colWidth, std::optional<size_t> optColIdx = std::nullopt) = 0;

    virtual void grab_focus() const = 0;
    virtual void exit_cell_edit() const = 0;
    virtual void set_selection_at_offset_n_delta(const int offset, const int delta) const = 0;

    virtual int get_curr_cell_curr_line_num() const = 0;
    virtual int get_curr_cell_max_line_num() const = 0;
    virtual int get_curr_cell_curr_offset() const = 0;
    virtual int get_curr_cell_max_offset() const = 0;

    #if GTKMM_MAJOR_VERSION < 4 && !defined(GTKMM_DISABLE_DEPRECATED)
    bool on_table_button_press_event(GdkEventButton* event);
    void on_cell_populate_popup(Gtk::Menu* menu);
    bool on_cell_key_press_event(GdkEventKey* event);
    #endif

    // OrangeArk: drag the bottom-right grip (or anywhere on the table border) with the mouse to
    // resize the table columns (horizontal) and row heights (vertical)
public:
    static const int TABLE_RESIZE_ZONE{32};
    static const int TABLE_BORDER_ZONE{8};
    bool _on_resize_button_press_event(GdkEventButton* event);
    bool _on_resize_motion_notify_event(GdkEventMotion* event);
    bool _on_resize_button_release_event(GdkEventButton* event);
    bool _resize_press_at(const double x, const double y, GdkEventButton* event);
    bool _resize_motion_at(const double x, const double y, GdkEventMotion* event);
    bool _resize_release(GdkEventButton* event);
protected:
    void _setup_resize_grip();  // creates the visible corner grip; subclasses place it on an overlay
    bool _on_grip_draw(const Cairo::RefPtr<Cairo::Context>& cr);
    bool _on_grip_button_press_event(GdkEventButton* event);
    void _resize_drag_begin(const double xRoot, const double yRoot);
    void _resize_drag_update(const double xRoot, const double yRoot);
    void _resize_drag_apply(const double xRoot, const double yRoot); // OrangeArk: unthrottled layout apply
    void _resize_drag_end();
    void _guide_ensure();    // OrangeArk: create the root-level guide-line strips
    void _guide_destroy();   // OrangeArk: remove the guide-line strips
    void _guide_update(const double xRoot, const double yRoot); // OrangeArk: move the guide lines while dragging
    void _apply_border_cursor(const int edges); // OrangeArk: set the resize cursor only when the zone changes
    int  _column_separator_at(const double x) const; // OrangeArk: hit test for a column separator (-1 = none)
    virtual void _set_rows_min_height(const int height) = 0; // OrangeArk: row height control
    virtual int  _get_rows_min_height() const = 0;
    virtual int  _get_rows_min_height_raw() const = 0; // OrangeArk: 0 = not set (no fallback estimate)
    // OrangeArk: per-row height — set_row_height() is the public entry; it stores
    // the value and calls the subclass hook that applies it to the widgets
    void set_row_height(const int height, const size_t rowIdx);
    int  get_row_height(const size_t rowIdx) const; // 0 = auto
    int  get_effective_row_height(const size_t rowIdx) const; // per-row wins, else uniform min, else 0
    void _row_heights_insert(const size_t atIdx);
    void _row_heights_erase(const size_t atIdx);
    void _row_heights_move(const size_t fromIdx, const size_t toIdx);
    virtual void _apply_row_height(const size_t rowIdx) = 0; // subclass: push the height into widgets
    virtual int  _row_separator_at(const double y) const { (void)y; return -1; } // OrangeArk: hit test for a row separator (-1 = none)
    virtual double _row_top_at(const size_t rowIdx) const { (void)rowIdx; return -1.0; } // OrangeArk: y of a row's top edge (-1 = n/a)
    Gtk::DrawingArea* _pResizeGrip{nullptr};
    bool _dragResizeActive{false};
    bool _dragResizeChanged{false};
    int  _dragEdgesMask{0};
    double _dragStartX{0.};
    double _dragStartY{0.};
    int    _dragStartTotalW{0};
    int    _dragStartMinHeight{0}; // OrangeArk: row min-height at drag start (avoids cumulative drift)
    CtTableColWidths _dragStartColWidths;
    int    _dragStartH{0};         // OrangeArk: widget height at drag start (guide line)
    int    _dragColIdx{-1};        // OrangeArk: column dragged by its separator (-1 = whole-table drag)
    int    _dragRowIdx{-1};        // OrangeArk: row dragged by its separator (-1 = not a per-row drag)
    int    _dragStartRowH{0};      // OrangeArk: effective height of the dragged row at drag start
    CtTableColWidths _rowHeights;  // OrangeArk: per-row heights (0 = auto), size == numRows
    Glib::RefPtr<Gdk::Window> _rGuideV; // OrangeArk: vertical guide strip shown while dragging
    Glib::RefPtr<Gdk::Window> _rGuideH; // OrangeArk: horizontal guide strip shown while dragging
    double _lastMotionXRoot{0.};   // OrangeArk: latest pointer position (final exact apply on release)
    double _lastMotionYRoot{0.};
    double _lastGuideX{-1e9};      // OrangeArk: last guide-line position (sub-pixel throttle)
    double _lastGuideY{-1e9};
    int    _lastCursorEdges{-999}; // OrangeArk: last cursor zone (-999 = force refresh)
    Glib::RefPtr<Gdk::Cursor> _rHoverCursor;

protected:
    virtual void _populate_xml_rows_cells(xmlpp::Element* p_table_node) const = 0;
    virtual bool _row_sort(const bool sortAsc) = 0;
    virtual bool _on_cell_key_press_alt_or_ctrl_enter() { return false; /* propagate signal */ }

    int              _colWidthDefault;
    CtTableColWidths _colWidths;
    size_t           _currentRow{0u};
    size_t           _currentColumn{0u};
};

struct CtTableLightColumns : public Gtk::TreeModelColumnRecord
{
    CtTableLightColumns(const size_t numColumns) {
        columnsText.resize(numColumns);
        for (size_t i = 0u; i < numColumns; ++i) {
            add(columnsText.at(i));
        }
        add(columnWeight);
    }
    std::vector<Gtk::TreeModelColumn<Glib::ustring>> columnsText;
    Gtk::TreeModelColumn<int>                        columnWeight;
};

class CtTableLight : public CtTableCommon
{
public:
    CtTableLight(CtMainWin* pCtMainWin,
                 CtTableMatrix& tableMatrix,
                 const int colWidthDefault,
                 const int charOffset,
                 const std::string& justification,
                 const CtTableColWidths& colWidths,
                 const size_t currRow = 0,
                 const size_t currCol = 0,
                 const CtTableColWidths& rowHeights = {});

    const CtTableLightColumns& get_columns() const { return *_pColumns; }

    Glib::ustring get_cell_text(const size_t rowIdx, const size_t colIdx) const;
    Glib::ustring get_curr_cell_text() const;
    bool has_focus_or_active_edit() const;
    void set_cell_text(const size_t rowIdx, const size_t colIdx, const Glib::ustring& cell_text);

    void apply_syntax_highlighting(const bool /*forceReApply*/) override {}
    std::string to_csv() const override;
    Glib::ustring get_line_content(const size_t rowIdx, const size_t colIdx, const int match_end_offset) const override;
    void set_modified_false() override {}
    CtAnchWidgType get_type() const override { return CtAnchWidgType::TableLight; }
    std::shared_ptr<CtAnchoredWidgetState> get_state() override;

    void write_strings_matrix(std::vector<std::vector<Glib::ustring>>& rows) const override;
    size_t get_num_rows() const override { return _pListStore->children().size(); }
    size_t get_num_columns() const override { return _pColumns->columnsText.size(); }

    void column_add(const size_t afterColIdx, const std::vector<Glib::ustring>* pNewColumn = nullptr) override;
    void column_delete(const size_t colIdx) override;
    void column_move_left(const size_t colIdx, const bool from_move_right) override;
    void column_move_right(const size_t colIdx) override;
    void row_add(const size_t afterRowIdx, const std::vector<Glib::ustring>* pNewRow = nullptr) override;
    void row_delete(const size_t rowIdx) override;
    void row_move_up(const size_t rowIdx, const bool from_move_down) override;

    void set_col_width_default(const int colWidthDefault) override;
    void set_col_width(const int colWidth, std::optional<size_t> optColIdx = std::nullopt) override;

    void grab_focus() const override;
    void exit_cell_edit() const override;
    void set_selection_at_offset_n_delta(const int offset, const int delta) const override;

    int get_curr_cell_curr_line_num() const override;
    int get_curr_cell_max_line_num() const override;
    int get_curr_cell_curr_offset() const override;
    int get_curr_cell_max_offset() const override;

protected:
    void _reset(CtTableMatrix& tableMatrix);
    static void _free_matrix(CtTableMatrix& tableMatrix);

    void _populate_xml_rows_cells(xmlpp::Element* p_table_node) const override;
    bool _row_sort(const bool sortAsc) override;
    bool _on_cell_key_press_alt_or_ctrl_enter() override;

    // OrangeArk: row height control
    void _set_rows_min_height(const int height) override;
    int  _get_rows_min_height() const override;
    int  _get_rows_min_height_raw() const override { return _rowsMinHeight; }
    void _apply_row_height(const size_t rowIdx) override;
    int  _rowsMinHeight{0};

    #if GTKMM_MAJOR_VERSION < 4 && !defined(GTKMM_DISABLE_DEPRECATED)
    void _on_treeview_event_after(GdkEvent* event);
    bool _on_entry_focus_out_event(GdkEventFocus* gdk_event, Gtk::Entry* pEntry, const Glib::ustring& path, const size_t column);
    #endif
    void _on_cell_renderer_text_edited(const Glib::ustring& path, const Glib::ustring& new_text, const size_t column);
    void _on_cell_renderer_editing_started(Gtk::CellEditable* editable, const Glib::ustring& path, const size_t column);

    std::unique_ptr<CtTableLightColumns> _pColumns;
    Gtk::TreeView* _pManagedTreeView{nullptr};
    Glib::RefPtr<Gtk::ListStore> _pListStore;
    Gtk::Entry* _pEditingCellEntry{nullptr};
};

class CtTableHeavy : public CtTableCommon
{
public:
    CtTableHeavy(CtMainWin* pCtMainWin,
            CtTableMatrix& tableMatrix,
            const int colWidthDefault,
            const int charOffset,
            const std::string& justification,
            const CtTableColWidths& colWidths,
            const size_t currRow = 0,
            const size_t currCol = 0,
            const CtTableColWidths& rowHeights = {});
    ~CtTableHeavy() override;

    void apply_syntax_highlighting(const bool forceReApply) override;
    std::string to_csv() const override;
    Glib::ustring get_line_content(const size_t rowIdx, const size_t colIdx, const int match_end_offset) const override;
    void set_modified_false() override;
    CtAnchWidgType get_type() const override { return CtAnchWidgType::TableHeavy; }
    std::shared_ptr<CtAnchoredWidgetState> get_state() override;

    CtTextView& curr_cell_text_view() const;
    Glib::RefPtr<Gtk::TextBuffer> get_buffer(const size_t rowIdx, const size_t colIdx) const;

    void write_strings_matrix(std::vector<std::vector<Glib::ustring>>& rows) const override;
    size_t get_num_rows() const override { return _tableMatrix.size(); }
    size_t get_num_columns() const override { return _tableMatrix.front().size(); }

    void column_add(const size_t afterColIdx, const std::vector<Glib::ustring>* pNewColumn = nullptr) override;
    void column_delete(const size_t colIdx) override;
    void column_move_left(const size_t colIdx, const bool from_move_right) override;
    void column_move_right(const size_t colIdx) override;
    void row_add(const size_t afterRowIdx, const std::vector<Glib::ustring>* pNewRow = nullptr) override;
    void row_delete(const size_t rowIdx) override;
    void row_move_up(const size_t rowIdx, const bool from_move_down) override;

    void set_col_width_default(const int colWidthDefault) override;
    void set_col_width(const int colWidth, std::optional<size_t> optColIdx = std::nullopt) override;

    void grab_focus() const override;
    void exit_cell_edit() const override {}
    void set_selection_at_offset_n_delta(const int offset, const int delta) const override;

    int get_curr_cell_curr_line_num() const override;
    int get_curr_cell_max_line_num() const override;
    int get_curr_cell_curr_offset() const override;
    int get_curr_cell_max_offset() const override;

    // OrangeArk: row height control
protected:
    void _set_rows_min_height(const int height) override;
    int  _get_rows_min_height() const override;
    int  _get_rows_min_height_raw() const override { return _rowsMinHeight; }
    void _apply_row_height(const size_t rowIdx) override;
    int  _row_separator_at(const double y) const override; // OrangeArk: per-row resize hit test
    double _row_top_at(const size_t rowIdx) const override;
    int  _rowsMinHeight{0};

protected:
    void _apply_styles_to_cells(const bool forceReApply);
    void _new_text_cell_attach(const size_t rowIdx, const size_t colIdx, CtTextCell* pTextCell);
    void _apply_remove_header_style(const bool isApply, CtTextView& textView);

    bool _row_sort(const bool sortAsc) override;
    void _populate_xml_rows_cells(xmlpp::Element* p_table_node) const override;

    void _on_grid_set_focus_child(Gtk::Widget* pWidget);

protected:
    CtTableMatrix    _tableMatrix;
    Gtk::Grid        _grid;
};
