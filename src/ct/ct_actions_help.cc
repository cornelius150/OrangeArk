/*
 * ct_actions_help.cc
 *
 * Copyright 2009-2025
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

#include "ct_actions.h"
#include <glib/gstdio.h>

void CtActions::online_help()
{
    // OrangeArk: show a local usage guide instead of an online manual
    const Glib::ustring tips =
        "<b>OrangeArk 橙子笔记 使用说明</b>\n\n"
        "• 左侧树面板：右键可新建/整理节点，节点支持层级结构\n"
        "• 保存：文档保存为 Markdown（.md）文件，其他编辑器也能打开；Ctrl+S 快速保存\n"
        "• 截图：工具栏相机按钮（Shift+Alt+X），拖框选区后弹出工具栏，"
        "可用画笔/箭头/矩形/椭圆/文字标注，可撤销/重做，可保存 PNG；"
        "确认后自动复制到剪贴板并插入笔记\n"
        "• 调整大小：图片/表格/代码框右下角有拖拽手柄，按住拖动即可调整大小\n"
        "• 缩进：编辑框内 Tab 缩进、Shift+Tab 反缩进\n"
        "• 书签：右键节点可添加/移除书签，书签显示在顶部菜单";
    CtDialogs::info_dialog(tips, *_pCtMainWin);
}

void CtActions::dialog_about()
{
#if GTKMM_MAJOR_VERSION >= 4
    auto paintable = _pCtMainWin->get_icon_theme()->lookup_icon(Glib::ustring{CtConst::APP_NAME}, 128, 1, Gtk::TextDirection::NONE, Gtk::IconLookupFlags{});
    CtDialogs::dialog_about(*_pCtMainWin, paintable);
#else
    CtDialogs::dialog_about(*_pCtMainWin, _pCtMainWin->get_icon_theme()->load_icon(CtConst::APP_NAME, 128));
#endif
}

void CtActions::folder_cfg_open()
{
    fs::open_folderpath(fs::get_orangeark_configdir(), _pCtConfig);
}

void CtActions::check_for_newer_version()
{
    auto& statusbar = _pCtMainWin->get_status_bar();
    statusbar.update_status(_("Checking for Newer Version..."));
    #if GTKMM_MAJOR_VERSION < 4 && !defined(GTKMM_DISABLE_DEPRECATED)
    while (gtk_events_pending()) gtk_main_iteration();
    #else
    while (g_main_context_pending(nullptr)) g_main_context_iteration(nullptr, false);
    #endif

    std::string latest_debian_changelog_from_server = fs::download_file("https://raw.githubusercontent.com/giuspen/orangeark/master/debian/changelog");
    std::size_t openp = latest_debian_changelog_from_server.find("(");
    std::size_t closep = latest_debian_changelog_from_server.find(")");
    if (std::string::npos == openp or std::string::npos == closep or closep < openp) {
        statusbar.update_status(_("Failed to Retrieve Latest Version Information - Try Again Later."));
        return;
    }
    Glib::ustring latest_version_from_server = latest_debian_changelog_from_server.substr(openp + 1, closep - openp - 1);
    auto re = Glib::Regex::create("(\\d+)\\.(\\d+)\\.(\\d+)-\\d+");
    Glib::MatchInfo match;
    if (not re->match(latest_version_from_server, match)) {
        statusbar.update_status(_("Failed to Retrieve Latest Version Information - Try Again Later."));
        return;
    }
    std::vector<gint64> splitted_latest_v = {atoi(match.fetch(1).c_str()), atoi(match.fetch(2).c_str()), atoi(match.fetch(3).c_str())};
    std::vector<gint64> splitted_local_v = CtStrUtil::gstring_split_to_int64(PACKAGE_VERSION, ".");
    if (splitted_local_v.size() != 3) {
        spdlog::error("unexpected version {}", PACKAGE_VERSION);
    }
    else {
        gint64 weighted_latest_v = splitted_latest_v[0]*10000 + splitted_latest_v[1]*100 + splitted_latest_v[2];
        gint64 weighted_local_v = splitted_local_v[0]*10000 + splitted_local_v[1]*100 + splitted_local_v[2];
        std::string trail_latest_from_srv = fmt::format(" ({}.{}.{})", splitted_latest_v[0], splitted_latest_v[1], splitted_latest_v[2]);
        if (weighted_latest_v > weighted_local_v) {
            CtDialogs::info_dialog(Glib::ustring{_("A Newer Version Is Available!")} + trail_latest_from_srv, *_pCtMainWin);
            _pCtMainWin->update_selected_node_statusbar_info();
        }
        else {
            if (weighted_latest_v == weighted_local_v) {
                statusbar.update_status(Glib::ustring{_("You Are Using the Latest Version Available.")} + trail_latest_from_srv);
            }
            else {
                statusbar.update_status(_("You Are Using a Development Version."));
                spdlog::debug("latest from server {}.{}.{}", splitted_latest_v[0], splitted_latest_v[1], splitted_latest_v[2]);
            }
        }
    }
}
