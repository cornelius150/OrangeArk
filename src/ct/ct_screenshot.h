/*
 * ct_screenshot.h - OrangeArk QQ-style region screenshot
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <gtkmm.h>

class CtMainWin;

// OrangeArk: QQ-style region screenshot (full-screen overlay, drag to select a region)
namespace CtScreenshot
{
    // Shows a full-screen selection overlay, returns the cropped screenshot pixbuf.
    // Returns an empty RefPtr when the user cancels (Esc).
    Glib::RefPtr<Gdk::Pixbuf> take_region_screenshot(CtMainWin* pCtMainWin);
}
