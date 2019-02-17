// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/views/chrome_views_test_base.h"

#include "chrome/test/views/chrome_test_views_delegate.h"
#include "content/public/test/test_browser_thread_bundle.h"

ChromeViewsTestBase::ChromeViewsTestBase() = default;
ChromeViewsTestBase::~ChromeViewsTestBase() = default;

void ChromeViewsTestBase::SetUp() {
  set_scoped_task_environment(
      std::make_unique<content::TestBrowserThreadBundle>());
  set_views_delegate(std::make_unique<ChromeTestViewsDelegate>());
  views::ViewsTestBase::SetUp();
}