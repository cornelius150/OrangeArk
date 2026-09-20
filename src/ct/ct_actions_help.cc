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

// OrangeArk: project URLs used by the Help menu
namespace {
    const char* ORANGEARK_URL_WEB      = "https://cornelius150.github.io/OrangeArk/";
    const char* ORANGEARK_URL_SOURCE   = "https://github.com/cornelius150/OrangeArk";
    const char* ORANGEARK_URL_ISSUES   = "https://github.com/cornelius150/OrangeArk/issues";
    const char* ORANGEARK_URL_RELEASES = "https://github.com/cornelius150/OrangeArk/releases";
    const char* ORANGEARK_URL_DONATE   = "https://cornelius150.github.io/OrangeArk/donate.html";
    const char* ORANGEARK_URL_MANUAL   = "https://cornelius150.github.io/OrangeArk/manual.html";
}

void CtActions::online_help()
{
    // OrangeArk: the online manual lives on the project website
    fs::open_weblink(ORANGEARK_URL_MANUAL);
}

void CtActions::help_website()
{
    fs::open_weblink(ORANGEARK_URL_WEB);
}

void CtActions::help_source_code()
{
    fs::open_weblink(ORANGEARK_URL_SOURCE);
}

void CtActions::help_report_bug()
{
    fs::open_weblink(ORANGEARK_URL_ISSUES);
}

void CtActions::help_donate()
{
    fs::open_weblink(ORANGEARK_URL_DONATE);
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

    // OrangeArk: query the latest GitHub release of this project
    const std::string json_from_server = fs::download_file("https://api.github.com/repos/cornelius150/OrangeArk/releases/latest");
    const std::size_t tagPos = json_from_server.find("\"tag_name\"");
    if (json_from_server.empty() or std::string::npos == tagPos) {
        statusbar.update_status(_("Failed to Retrieve Latest Version Information - Try Again Later."));
        return;
    }
    const std::size_t openq = json_from_server.find("\"", tagPos + 10);
    const std::size_t closeq = std::string::npos == openq ? std::string::npos : json_from_server.find("\"", openq + 1);
    if (std::string::npos == openq or std::string::npos == closeq or closeq < openq) {
        statusbar.update_status(_("Failed to Retrieve Latest Version Information - Try Again Later."));
        return;
    }
    Glib::ustring latest_version_from_server = json_from_server.substr(openq + 1, closeq - openq - 1);
    if (not latest_version_from_server.empty() and 'v' == latest_version_from_server[0]) {
        latest_version_from_server = latest_version_from_server.substr(1);
    }
    auto re = Glib::Regex::create("(\\d+)\\.(\\d+)\\.(\\d+)");
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
