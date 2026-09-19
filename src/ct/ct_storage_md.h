/*
 * ct_storage_md.h - OrangeArk Markdown (.md) document storage
 *
 * Saves the whole tree as a single Markdown file; loads a .md file
 * back into the tree through the built-in Markdown importer.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include "ct_types.h"
#include "ct_widgets.h"
#include "ct_filesystem.h"
#include <glibmm/refptr.h>
#include <gtkmm/treeiter.h>
#include <gtkmm/textbuffer.h>
#include <libxml++/libxml++.h>

class CtMainWin;
class CtTreeIter;

class CtStorageMd : public CtStorageEntity
{
public:
    CtStorageMd(CtMainWin* pCtMainWin)
     : _pCtMainWin{pCtMainWin}
    {}

    void close_connect() override {}
    void reopen_connect() override {}
    void test_connection() override {}
    void try_reopen() override {}
    void vacuum() override {}

    bool populate_treestore(const fs::path& file_path, Glib::ustring& error) override;
    bool save_treestore(const fs::path& file_path,
                        const CtStorageSyncPending& syncPending,
                        Glib::ustring& error,
                        const CtExporting export_type,
                        const std::map<gint64, gint64>* pExpoMasterReassign = nullptr,
                        const int start_offset = 0,
                        const int end_offset = -1) override;
    void import_nodes(const fs::path& path, const Gtk::TreeModel::iterator& parent_iter) override;

    Glib::RefPtr<Gtk::TextBuffer> get_delayed_text_buffer(const gint64 node_id,
                                                          const std::string& syntax,
                                                          std::list<CtAnchoredWidget*>& widgets) const override;

    fs::path get_embedded_filepath(const CtTreeIter&/*ct_tree_iter*/, const std::string&/*filename*/) const override { return ""; }

private:
    std::string _tree_to_markdown();
    std::string _node_to_markdown(CtTreeIter& ct_tree_iter, const int level);
    std::string _node_xml_to_markdown(xmlpp::Element* p_node_element, const int level);
    std::string _rich_text_to_markdown(xmlpp::Element* p_rich_text_element);
    std::string _table_to_markdown(xmlpp::Element* p_table_element);

private:
    CtMainWin* const _pCtMainWin;
    mutable CtDelayedTextBufferMap _delayed_text_buffers;
};
