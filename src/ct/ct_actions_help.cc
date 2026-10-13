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
#include "ct_misc_utils.h"
#include <glib/gstdio.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/textview.h>
#include <fstream>
#include <vector>
#include <ctime>

// OrangeArk: project URLs used by the Help menu
namespace {
    const char* ORANGEARK_URL_WEB      = "https://cornelius150.github.io/OrangeArk/";
    const char* ORANGEARK_URL_SOURCE   = "https://github.com/cornelius150/OrangeArk";
    const char* ORANGEARK_URL_ISSUES   = "https://github.com/cornelius150/OrangeArk/issues";

    // OrangeArk: the help site base URL is configurable — set [misc]
    // help_base_url in config.cfg (e.g. http://myorange.duckdns.org) to point
    // the manual/website/donate links at a self-hosted help site (see the
    // orangeark-help-site deploy bundle). Empty = the official site.
    std::string help_site_url(const CtConfig* pCtConfig, const char* page)
    {
        std::string base = pCtConfig ? pCtConfig->helpBaseUrl : std::string{};
        while (base.size() > 1u and base.back() == '/') base.pop_back();
        if (base.empty()) return std::string{ORANGEARK_URL_WEB} + page;
        return base + page;
    }

// OrangeArk: 反馈（缺陷与需求）提交端点。
// 关键设计：**此二进制里不存在收件邮箱**。端点是表单服务提供的不透明地址，
// 邮箱地址只存在于服务端，因此：
//   * 逆向这份代码只能得到一个公网 POST 地址，推导不出收件邮箱；
//   * 本地没有任何"加密后又能解开"的邮箱密文，不存在爆破 / 离线破解的靶标；
//   * 收件邮箱刻意不出现在源码里（连注释也不写）——源码是公开仓库，写了等于公开；
//   * 想改收件地址，只在服务端换，或在 config.cfg 里把 feedback_endpoint 指向自己的服务端。
// 换句话说，"加密并放到客户端"是假安全（同一条代码路径必然能解开），
// 真正的做法是让客户端从一开始就不需要知道这个邮箱。
const char* ORANGEARK_FEEDBACK_ENDPOINT = "https://formspree.io/f/OA_FORM_ID";

// 防刷：端点是公网的，限制单台机器每小时提交次数，避免被人脚本刷爆配额
const int    FEEDBACK_MAX_SUBMITS_PER_HOUR = 5;
const size_t FEEDBACK_SUBJECT_MAX          = 200;
const size_t FEEDBACK_BODY_MAX             = 10000;

std::string feedback_json_escape(const std::string& in)
{
    std::string out;
    out.reserve(in.size() + 16u);
    for (const unsigned char c : in) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            default:
                if (c < 0x20u) {
                    out += fmt::format("\\u{:04x}", static_cast<int>(c));
                } else {
                    // UTF-8 多字节序列 (>=0x80) 原样透传，JSON 本身就是 UTF-8
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

// 端点解析顺序：显式 feedback_endpoint > 自建官网 help_base_url + /api/feedback > 内置默认
std::string feedback_endpoint_of(const CtConfig* pCtConfig)
{
    if (pCtConfig and not pCtConfig->feedbackEndpoint.empty()) {
        return pCtConfig->feedbackEndpoint;
    }
    if (pCtConfig and not pCtConfig->helpBaseUrl.empty()) {
        std::string base = pCtConfig->helpBaseUrl;
        while (base.size() > 1u and base.back() == '/') base.pop_back();
        return base + "/api/feedback";
    }
    return ORANGEARK_FEEDBACK_ENDPOINT;
}

// 每小时提交配额：记录最近一小时的时间戳，超限直接拒绝，不消耗网络请求
bool feedback_throttle_take_slot()
{
    const fs::path fpath = fs::get_orangeark_configdir() / "feedback_throttle.txt";
    const std::time_t now = std::time(nullptr);
    std::vector<std::time_t> recent;
    try {
        std::ifstream inf{fpath.string()};
        std::string line;
        size_t lines_read{0u};
        while (lines_read < 10000u and std::getline(inf, line)) {
            ++lines_read;
            try {
                const long long v = std::stoll(line);
                // 必须同时排除未来时间和过于久远的时间：否则有人往这个文件里
                // 塞几个未来时间戳，就能让这台机器永久无法提交反馈。
                if (v > 0 and static_cast<std::time_t>(v) <= now and
                    (now - static_cast<std::time_t>(v)) < 3600) {
                    recent.push_back(static_cast<std::time_t>(v));
                }
            } catch (...) { /* 忽略脏行 */ }
        }
    } catch (...) { /* 读不到就当作没有记录 */ }

    if (recent.size() >= static_cast<size_t>(FEEDBACK_MAX_SUBMITS_PER_HOUR)) {
        return false;
    }
    recent.push_back(now);
    try {
        std::ofstream outf{fpath.string(), std::ios::trunc};
        for (const std::time_t t : recent) {
            outf << static_cast<long long>(t) << "\n";
        }
    } catch (...) { /* 写不进也不影响本次提交 */ }
    return true;
}
}

void CtActions::online_help()
{
    // OrangeArk: the online manual lives on the project website
    fs::open_weblink(help_site_url(_pCtConfig, "/manual.html"));
}

void CtActions::help_website()
{
    fs::open_weblink(help_site_url(_pCtConfig, ""));
}

void CtActions::help_source_code()
{
    fs::open_weblink(ORANGEARK_URL_SOURCE);
}

// OrangeArk: 应用内反馈（缺陷 + 需求）。按需求只提供「主题」与「内容」两个输入框，
// 不显示也不需要填写邮箱：既不用用户留联系方式，也不把收件邮箱暴露出来（见上方端点注释）。
void CtActions::help_report_bug()
{
    Gtk::Dialog dialog{"反馈缺陷和需求", *_pCtMainWin,
        Gtk::DialogFlags::DIALOG_MODAL | Gtk::DialogFlags::DIALOG_DESTROY_WITH_PARENT};
    (void)CtMiscUtil::dialog_add_button(&dialog, "取消(C)", Gtk::RESPONSE_REJECT, "ct_cancel");
    (void)CtMiscUtil::dialog_add_button(&dialog, "提交(S)", Gtk::RESPONSE_ACCEPT, "ct_done", true/*isDefault*/);
    dialog.set_default_size(560, 420);
    dialog.set_position(Gtk::WIN_POS_CENTER_ON_PARENT);

    auto pGrid = Gtk::manage(new Gtk::Grid{});
    pGrid->set_row_spacing(6);
    pGrid->set_column_spacing(8);
    pGrid->set_margin_start(12);
    pGrid->set_margin_end(12);
    pGrid->set_margin_top(12);
    pGrid->set_margin_bottom(12);

    auto pLabelSubject = Gtk::manage(new Gtk::Label{"反馈主题："});
    pLabelSubject->set_halign(Gtk::ALIGN_START);
    pLabelSubject->set_valign(Gtk::ALIGN_START);
    auto pEntrySubject = Gtk::manage(new Gtk::Entry{});
    pEntrySubject->set_placeholder_text("一句话说明遇到的缺陷，或希望增加的功能");
    pEntrySubject->set_max_length(static_cast<int>(FEEDBACK_SUBJECT_MAX));
    pEntrySubject->set_hexpand(true);

    auto pLabelBody = Gtk::manage(new Gtk::Label{"反馈内容："});
    pLabelBody->set_halign(Gtk::ALIGN_START);
    pLabelBody->set_valign(Gtk::ALIGN_START);
    auto pTextViewBody = Gtk::manage(new Gtk::TextView{});
    pTextViewBody->set_wrap_mode(Gtk::WRAP_WORD_CHAR);
    auto pScrolledBody = Gtk::manage(new Gtk::ScrolledWindow{});
    pScrolledBody->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    pScrolledBody->add(*pTextViewBody);
    pScrolledBody->set_min_content_height(200);
    pScrolledBody->set_hexpand(true);
    pScrolledBody->set_vexpand(true);

    auto pLabelNote = Gtk::manage(new Gtk::Label{});
    pLabelNote->set_markup("<small>提交时会自动附加 OrangeArk 版本与系统信息用于定位问题，不会包含你的笔记内容。</small>");
    pLabelNote->set_halign(Gtk::ALIGN_START);
    pLabelNote->set_line_wrap(true);
    pLabelNote->set_max_width_chars(64);
    pLabelNote->set_hexpand(true);

    pGrid->attach(*pLabelSubject, 0, 0, 1, 1);
    pGrid->attach(*pEntrySubject, 1, 0, 1, 1);
    pGrid->attach(*pLabelBody,   0, 1, 1, 1);
    pGrid->attach(*pScrolledBody, 1, 1, 1, 1);
    pGrid->attach(*pLabelNote,    1, 2, 1, 1);
    dialog.get_content_area()->pack_start(*pGrid, true, true);
    dialog.show_all();

    while (true) {
        if (Gtk::RESPONSE_ACCEPT != dialog.run()) return;

        auto trim = [](const Glib::ustring& s) {
            const Glib::ustring blanks{" \t\r\n"};
            const auto first = s.find_first_not_of(blanks);
            if (Glib::ustring::npos == first) return Glib::ustring{};
            const auto last = s.find_last_not_of(blanks);
            return s.substr(first, last - first + 1);
        };

        Glib::ustring subject = trim(pEntrySubject->get_text());
        Glib::ustring body = trim(pTextViewBody->get_buffer()->get_text());
        if (subject.empty() and body.empty()) {
            CtDialogs::warning_dialog("请填写反馈主题或反馈内容。", *_pCtMainWin);
            continue; // 回到对话框让用户补填
        }
        if (subject.size() > FEEDBACK_SUBJECT_MAX) subject = subject.substr(0, FEEDBACK_SUBJECT_MAX);
        if (body.size() > FEEDBACK_BODY_MAX) body = body.substr(0, FEEDBACK_BODY_MAX);
        if (subject.empty()) subject = "(无主题)";

        // 自动附加诊断信息：只包含版本与平台，不含任何用户笔记内容
        Glib::ustring message = body;
        message += "\n\n------------------\n";
        message += "[自动附加]\n";
        message += "OrangeArk " PACKAGE_VERSION "\n";
#ifdef _WIN32
        message += "平台: Windows\n";
#else
        message += "平台: Linux\n";
#endif

        // 默认端点还是占位符：说明这台机器既没配 feedback_endpoint 也没配自建官网，
        // 直接提交只能得到一个 404，不如把情况讲清楚并给出可用的替代渠道。
        if (feedback_endpoint_of(_pCtConfig).find("OA_FORM_ID") != std::string::npos) {
            if (CtDialogs::question_dialog(
                    "尚未配置反馈提交端点。请在 config.cfg 的 [misc] 下加一行：\n"
                    "feedback_endpoint=https://你的提交端点\n\n"
                    "是否先改用 GitHub Issues 页面提交？", *_pCtMainWin)) {
                fs::open_weblink(ORANGEARK_URL_ISSUES);
            }
            return;
        }

        if (not feedback_throttle_take_slot()) {
            CtDialogs::warning_dialog(
                "反馈提交过于频繁，请稍后再试（每小时最多 " +
                std::to_string(FEEDBACK_MAX_SUBMITS_PER_HOUR) + " 条）。", *_pCtMainWin);
            return;
        }

        std::string json{"{"};
        json += "\"subject\":\"" + feedback_json_escape(subject.raw()) + "\"";
        json += ",\"message\":\"" + feedback_json_escape(message.raw()) + "\"";
        json += "}";

        auto& statusbar = _pCtMainWin->get_status_bar();
        statusbar.update_status("正在提交反馈…");
        #if GTKMM_MAJOR_VERSION < 4 && !defined(GTKMM_DISABLE_DEPRECATED)
        while (gtk_events_pending()) gtk_main_iteration();
        #else
        while (g_main_context_pending(nullptr)) g_main_context_iteration(nullptr, false);
        #endif

        long http_code{0};
        // OrangeArk: an optional extra CA bundle lets a self-hosted site with a
        // self-signed certificate (bare IP) be used; verification stays on.
        const bool ok = fs::post_json(feedback_endpoint_of(_pCtConfig), json, http_code,
                                      _pCtConfig->feedbackCaBundle);
        if (ok) {
            statusbar.update_status("反馈已提交");
            CtDialogs::info_dialog("反馈已提交，感谢你的反馈！", *_pCtMainWin);
        }
        else {
            statusbar.update_status("反馈提交失败");
            std::string msg{"反馈提交失败"};
            if (http_code > 0) msg += "（HTTP " + std::to_string(http_code) + "）";
            msg += "。是否改用 GitHub Issues 页面提交？";
            if (CtDialogs::question_dialog(msg, *_pCtMainWin)) {
                fs::open_weblink(ORANGEARK_URL_ISSUES);
            }
        }
        _pCtMainWin->update_selected_node_statusbar_info();
        return;
    }
}

void CtActions::help_donate()
{
    fs::open_weblink(help_site_url(_pCtConfig, "/donate.html"));
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
