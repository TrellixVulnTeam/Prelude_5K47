// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/controls/menu/menu_config.h"

#include <windows.h>
#include <uxtheme.h>
#include <Vssym32.h>

#include "base/logging.h"
#include "base/win/scoped_gdi_object.h"
#include "base/win/win_client_metrics.h"
#include "ui/base/l10n/l10n_util_win.h"
#include "ui/base/material_design/material_design_controller.h"
#include "ui/gfx/color_utils.h"
#include "ui/native_theme/native_theme_win.h"

using ui::NativeTheme;

namespace views {

void MenuConfig::Init() {
  if (ui::MaterialDesignController::IsRefreshUi()) {
    InitMaterialMenuConfig();
  } else {
    separator_upper_height = 5;
    separator_lower_height = 7;
  }

  arrow_color = color_utils::GetSysSkColor(COLOR_MENUTEXT);

  NONCLIENTMETRICS_XP metrics;
  base::win::GetNonClientMetrics(&metrics);
  l10n_util::AdjustUIFont(&(metrics.lfMenuFont));
  {
    base::win::ScopedHFONT new_font(CreateFontIndirect(&metrics.lfMenuFont));
    DLOG_ASSERT(new_font.is_valid());
    font_list = gfx::FontList(gfx::Font(new_font.get()));
  }
  NativeTheme::ExtraParams extra;
  gfx::Size arrow_size = NativeTheme::GetInstanceForNativeUi()->GetPartSize(
      NativeTheme::kMenuPopupArrow, NativeTheme::kNormal, extra);
  if (!arrow_size.IsEmpty()) {
    arrow_width = arrow_size.width();
  } else {
    // Sadly I didn't see a specific metric for this.
    arrow_width = GetSystemMetrics(SM_CXMENUCHECK);
  }

  BOOL show_cues;
  show_mnemonics =
      (SystemParametersInfo(SPI_GETKEYBOARDCUES, 0, &show_cues, 0) &&
       show_cues == TRUE);

  SystemParametersInfo(SPI_GETMENUSHOWDELAY, 0, &show_delay, 0);
}

}  // namespace views
