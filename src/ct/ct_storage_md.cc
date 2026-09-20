/*
 * ct_storage_md.cc - OrangeArk Markdown (.md) document storage
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */
#include "ct_storage_md.h"
#include "ct_storage_xml.h"
#include "ct_imports.h"
#include "ct_treestore.h"
#include "ct_main_win.h"
#include "ct_logging.h"

CtStorageMd::~CtStorageMd() = default;

// OrangeArk: wrap the lossless XML snapshot as a trailing HTML comment so the
// document stays a perfectly valid, human readable Markdown file
std::string CtStorageMd::_wrap_embedded_xml_snapshot(const std::string& xml_content)
{
    std::string b64 = Glib::Base64::encode(xml_content);
    // split the base64 payload into reasonably short lines
    std::string chunked;
    const size_t chunkLen = 76;
    for (size_t i = 0; i < b64.size(); i += chunkLen) {
        chunked += b64.substr(i, chunkLen);
        chunked += "\n";
    }
    return "<!-- OrangeArk:document-data v1\n" + chunked + "-->";
}

std::string CtStorageMd::_get_embedded_xml_snapshot(const std::string& file_contents) const
{
    const std::string markerStart = "<!-- OrangeArk:document-data";
    const size_t markerPos = file_contents.rfind(markerStart);
    if (markerPos == std::string::npos) return "";
    const size_t dataStart = file_contents.find('\n', markerPos);
    if (dataStart == std::string::npos) return "";
    const size_t markerEnd = file_contents.find("-->", dataStart);
    if (markerEnd == std::string::npos) return "";
    std::string b64 = file_contents.substr(dataStart + 1, markerEnd - dataStart - 1);
    // strip the whitespace the line splitting may have introduced
    b64 = str::replace(str::replace(str::replace(b64, "\r", ""), "\n", ""), " ", "");
    if (b64.empty()) return "";
    try {
        return Glib::Base64::decode(b64);
    }
    catch (std::exception& e) {
        spdlog::warn("failed to decode the embedded OrangeArk document snapshot: {}", e.what());
        return "";
    }
}

bool CtStorageMd::populate_treestore(const fs::path& file_path, Glib::ustring& error)
{
    try {
        if (not fs::is_regular_file(file_path)) {
            throw std::runtime_error(str::format(_("'%s' is Not a Regular File"), file_path.string()));
        }
        const std::string fileContents = Glib::file_get_contents(file_path.string());
        // 1) OrangeArk documents carry a lossless XML snapshot: restore the full node tree from it
        const std::string xmlContent = _get_embedded_xml_snapshot(fileContents);
        if (not xmlContent.empty()) {
            _pXmlStorage = std::make_unique<CtStorageXml>(_pCtMainWin);
            if (_isDryRun) _pXmlStorage->set_is_dry_run();
            return _pXmlStorage->populate_treestore_from_xml_string(xmlContent, error);
        }
        // 2) foreign Markdown file: parse it into an imported node (cherry-like xml content)
        CtMDImport importer{_pCtMainWin->get_ct_config()};
        std::unique_ptr<CtImportedNode> pNode = importer.import_file(file_path);
        if (not pNode or not pNode->xml_content or not pNode->xml_content->get_root_node()) {
            throw std::runtime_error(str::format(_("Failed to Parse the Markdown File: %s"), file_path.string()));
        }
        // build a single <node> element with the imported content (slot children flattened)
        xmlpp::Document doc;
        xmlpp::Element* pRoot = doc.create_root_node("root");
        xmlpp::Element* pNodeEl = pRoot->add_child("node");
        pNodeEl->set_attribute("name", pNode->node_name);
        pNodeEl->set_attribute("prog_lang", pNode->node_syntax);
        for (xmlpp::Node* pSlot : pNode->xml_content->get_root_node()->get_children("slot")) {
            for (xmlpp::Node* pChild : static_cast<xmlpp::Element*>(pSlot)->get_children()) {
                pNodeEl->import_node(pChild);
            }
        }
        // create the node in the tree store (buffer created immediately thanks to new_id)
        CtTreeStore& ctTreeStore = _pCtMainWin->get_tree_store();
        bool hasDuplicatedId{false};
        bool isSharedNonMaster{false};
        CtStorageXmlHelper{_pCtMainWin}.node_from_xml(pNodeEl,
                                                      1/*sequence*/,
                                                      Gtk::TreeModel::iterator{}/*parent*/,
                                                      ctTreeStore.node_id_get() + 1/*new_id*/,
                                                      &hasDuplicatedId,
                                                      &isSharedNonMaster,
                                                      nullptr/*pImportedIdsRemap*/,
                                                      _delayed_text_buffers,
                                                      _isDryRun,
                                                      ""/*multifile_dir*/);
        return true;
    }
    catch (std::exception& e) {
        spdlog::error(e.what());
        error = e.what();
        return false;
    }
}

bool CtStorageMd::save_treestore(const fs::path& file_path,
                                 const CtStorageSyncPending& syncPending,
                                 Glib::ustring& error,
                                 const CtExporting /*export_type*/,
                                 const std::map<gint64, gint64>* /*pExpoMasterReassign*/,
                                 const int /*start_offset*/,
                                 const int /*end_offset*/)
{
    try {
        // 1) human readable Markdown body
        const std::string mdContent = _tree_to_markdown();
        // 2) lossless XML snapshot (full node tree + rich text) built on a hidden temp file
        fs::path tmpXmlPath = _pCtMainWin->get_ct_tmp()->getHiddenFilePath(file_path);
        tmpXmlPath += ".snapshot.ctd";
        auto pXmlStorage = std::make_unique<CtStorageXml>(_pCtMainWin);
        if (not pXmlStorage->save_treestore(tmpXmlPath, syncPending, error, CtExporting::NONESAVE)) {
            throw std::runtime_error(error.empty() ? "failed to build the embedded document snapshot" : error);
        }
        const std::string xmlContent = Glib::file_get_contents(tmpXmlPath.string());
        (void)fs::remove(tmpXmlPath);
        std::string out = mdContent;
        if (not str::endswith(out, "\n")) out += "\n";
        out += "\n";
        out += _wrap_embedded_xml_snapshot(xmlContent);
        out += "\n";
        Glib::file_set_contents(file_path.string(), out);
        return true;
    }
    catch (std::exception& e) {
        spdlog::error(e.what());
        error = e.what();
        return false;
    }
}

void CtStorageMd::import_nodes(const fs::path& path, const Gtk::TreeModel::iterator& parent_iter)
{
    try {
        CtMDImport importer{_pCtMainWin->get_ct_config()};
        std::unique_ptr<CtImportedNode> pNode = importer.import_file(path);
        if (not pNode or not pNode->xml_content or not pNode->xml_content->get_root_node()) {
            return;
        }
        xmlpp::Document doc;
        xmlpp::Element* pRoot = doc.create_root_node("root");
        xmlpp::Element* pNodeEl = pRoot->add_child("node");
        pNodeEl->set_attribute("name", pNode->node_name);
        pNodeEl->set_attribute("prog_lang", pNode->node_syntax);
        for (xmlpp::Node* pSlot : pNode->xml_content->get_root_node()->get_children("slot")) {
            for (xmlpp::Node* pChild : static_cast<xmlpp::Element*>(pSlot)->get_children()) {
                pNodeEl->import_node(pChild);
            }
        }
        CtTreeStore& ctTreeStore = _pCtMainWin->get_tree_store();
        bool hasDuplicatedId{false};
        bool isSharedNonMaster{false};
        CtStorageXmlHelper{_pCtMainWin}.node_from_xml(pNodeEl,
                                                      ctTreeStore.get_store()->children().size() + 1/*sequence*/,
                                                      parent_iter,
                                                      ctTreeStore.node_id_get() + 1/*new_id*/,
                                                      &hasDuplicatedId,
                                                      &isSharedNonMaster,
                                                      nullptr/*pImportedIdsRemap*/,
                                                      _delayed_text_buffers,
                                                      false/*isDryRun*/,
                                                      ""/*multifile_dir*/);
    }
    catch (std::exception& e) {
        spdlog::error("{}, what: {}", __FUNCTION__, e.what());
    }
}

Glib::RefPtr<Gtk::TextBuffer> CtStorageMd::get_delayed_text_buffer(const gint64 node_id,
                                                                   const std::string& syntax,
                                                                   std::list<CtAnchoredWidget*>& widgets) const
{
    if (_pXmlStorage) {
        // the xml snapshot path owns the delayed buffers created while populating
        return _pXmlStorage->get_delayed_text_buffer(node_id, syntax, widgets);
    }
    // buffers are created eagerly while importing plain markdown, this is just a safety net
    return const_cast<CtMainWin*>(_pCtMainWin)->get_new_text_buffer();
}

std::string CtStorageMd::_tree_to_markdown()
{
    CtTreeStore& ctTreeStore = _pCtMainWin->get_tree_store();
    std::string mdContent;
    for (Gtk::TreeModel::iterator iter = ctTreeStore.get_store()->children().begin();
         iter and iter != ctTreeStore.get_store()->children().end(); ++iter)
    {
        CtTreeIter ctTreeIter = ctTreeStore.to_ct_tree_iter(iter);
        mdContent += _node_to_markdown(ctTreeIter, 1);
    }
    return mdContent;
}

std::string CtStorageMd::_node_to_markdown(CtTreeIter& ct_tree_iter, const int level)
{
    // serialize this single node to xml (no children) and convert it to markdown
    xmlpp::Document doc;
    xmlpp::Element* pRoot = doc.create_root_node("root");
    CtStorageXmlHelper{_pCtMainWin}.node_to_xml(&ct_tree_iter, pRoot, ""/*multifile_dir*/, nullptr/*storage_cache*/, CtExporting::CURRENT_NODE);
    std::string mdContent;
    for (xmlpp::Node* pNode : pRoot->get_children("node")) {
        mdContent += _node_xml_to_markdown(static_cast<xmlpp::Element*>(pNode), level);
    }
    // children as deeper headings
    CtTreeIter ct_tree_iter_child = ct_tree_iter.first_child();
    while (ct_tree_iter_child) {
        mdContent += _node_to_markdown(ct_tree_iter_child, level + 1);
        ++ct_tree_iter_child;
    }
    return mdContent;
}

std::string CtStorageMd::_node_xml_to_markdown(xmlpp::Element* p_node_element, const int level)
{
    std::string mdContent;
    const Glib::ustring nodeName = p_node_element->get_attribute_value("name");
    const Glib::ustring nodeSyntax = p_node_element->get_attribute_value("prog_lang");

    mdContent += std::string(static_cast<size_t>(level), '#') + " " + nodeName.raw() + "\n\n";

    if (CtConst::RICH_TEXT_ID == nodeSyntax) {
        for (xmlpp::Node* pChild : p_node_element->get_children()) {
            xmlpp::Element* pEl = dynamic_cast<xmlpp::Element*>(pChild);
            if (not pEl) continue;
            const Glib::ustring elName = pEl->get_name();
            if ("rich_text" == elName) {
                mdContent += _rich_text_to_markdown(pEl);
            }
            else if ("encoded_png" == elName) {
                const std::string encodedBlob = pEl->get_child_text() ? pEl->get_child_text()->get_content() : "";
                if (not encodedBlob.empty()) {
                    mdContent += "![image](data:image/png;base64," + encodedBlob + ")\n\n";
                }
            }
            else if ("codebox" == elName) {
                const Glib::ustring codeSyntax = pEl->get_attribute_value("syntax_highlighting");
                const std::string codeText = pEl->get_child_text() ? pEl->get_child_text()->get_content() : "";
                mdContent += "```" + codeSyntax.raw() + "\n" + codeText + "\n```\n\n";
            }
            else if ("table" == elName) {
                mdContent += _table_to_markdown(pEl);
            }
            else if ("latex" == elName) {
                const std::string latexText = pEl->get_child_text() ? pEl->get_child_text()->get_content() : "";
                if (not latexText.empty()) {
                    mdContent += "$$\n" + latexText + "\n$$\n\n";
                }
            }
        }
    }
    else {
        // plain text or code nodes: concatenate the raw text content
        std::string rawText;
        for (xmlpp::Node* pChild : p_node_element->get_children()) {
            xmlpp::Element* pEl = dynamic_cast<xmlpp::Element*>(pChild);
            if (pEl and "rich_text" == pEl->get_name() and pEl->get_child_text()) {
                rawText += pEl->get_child_text()->get_content();
            }
        }
        if (CtConst::PLAIN_TEXT_ID == nodeSyntax) {
            mdContent += rawText;
        }
        else {
            mdContent += "```" + nodeSyntax.raw() + "\n" + rawText + "\n```\n\n";
        }
    }

    // separate the node body from what follows
    while (str::endswith(mdContent, "\n\n")) {
        mdContent.pop_back();
    }
    if (not str::endswith(mdContent, "\n")) {
        mdContent += "\n";
    }
    mdContent += "\n";
    return mdContent;
}

std::string CtStorageMd::_rich_text_to_markdown(xmlpp::Element* p_rich_text_element)
{
    xmlpp::TextNode* pTextNode = p_rich_text_element->get_child_text();
    if (not pTextNode) return "";
    Glib::ustring text = pTextNode->get_content();
    if (text.empty()) return "";

    const Glib::ustring scale = p_rich_text_element->get_attribute_value(CtConst::TAG_SCALE);
    const Glib::ustring link = p_rich_text_element->get_attribute_value(CtConst::TAG_LINK);

    std::string out;
    if (not scale.empty()) {
        // markdown heading prefix (h1..h6)
        int headingLevel = 0;
        if (CtConst::TAG_PROP_VAL_H1 == scale) headingLevel = 1;
        else if (CtConst::TAG_PROP_VAL_H2 == scale) headingLevel = 2;
        else if (CtConst::TAG_PROP_VAL_H3 == scale) headingLevel = 3;
        else if (CtConst::TAG_PROP_VAL_H4 == scale) headingLevel = 4;
        else if (CtConst::TAG_PROP_VAL_H5 == scale) headingLevel = 5;
        else if (CtConst::TAG_PROP_VAL_H6 == scale) headingLevel = 6;
        if (headingLevel > 0) {
            out += std::string(static_cast<size_t>(headingLevel), '#') + " ";
        }
    }

    std::string body = text.raw();
    if (CtConst::TAG_PROP_VAL_HEAVY == p_rich_text_element->get_attribute_value(CtConst::TAG_WEIGHT)) {
        body = "**" + body + "**";
    }
    if (CtConst::TAG_PROP_VAL_ITALIC == p_rich_text_element->get_attribute_value(CtConst::TAG_STYLE)) {
        body = "*" + body + "*";
    }
    if (CtConst::TAG_PROP_VAL_TRUE == p_rich_text_element->get_attribute_value(CtConst::TAG_STRIKETHROUGH)) {
        body = "~~" + body + "~~";
    }
    if (CtConst::TAG_PROP_VAL_MONOSPACE == p_rich_text_element->get_attribute_value(CtConst::TAG_FAMILY)) {
        body = "`" + body + "`";
    }
    if (str::startswith(link, "webs ")) {
        const std::string url = str::trim(link.substr(5)).raw();
        out += "[" + body + "](" + url + ")";
    }
    else {
        out += body;
    }
    return out;
}

std::string CtStorageMd::_table_to_markdown(xmlpp::Element* p_table_element)
{
    std::vector<std::vector<std::string>> rows;
    for (xmlpp::Node* pRowNode : p_table_element->get_children("row")) {
        xmlpp::Element* pRowEl = static_cast<xmlpp::Element*>(pRowNode);
        std::vector<std::string> row;
        for (xmlpp::Node* pCellNode : pRowEl->get_children("cell")) {
            xmlpp::Element* pCellEl = static_cast<xmlpp::Element*>(pCellNode);
            std::string cellText = pCellEl->get_child_text() ? pCellEl->get_child_text()->get_content() : "";
            // escape pipes so that cells survive the markdown table format
            cellText = str::replace(cellText, "|", "\\|");
            row.push_back(cellText);
        }
        rows.push_back(row);
    }
    if (rows.empty()) return "";

    std::string mdContent;
    const size_t numColumns = rows.front().size();
    // header
    mdContent += "|";
    for (const std::string& cellText : rows.front()) {
        mdContent += " " + cellText + " |";
    }
    mdContent += "\n|";
    for (size_t c = 0u; c < numColumns; ++c) {
        mdContent += " --- |";
    }
    mdContent += "\n";
    // body
    for (size_t r = 1u; r < rows.size(); ++r) {
        mdContent += "|";
        for (const std::string& cellText : rows.at(r)) {
            mdContent += " " + cellText + " |";
        }
        mdContent += "\n";
    }
    mdContent += "\n";
    return mdContent;
}
