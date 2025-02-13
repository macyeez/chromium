// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/feed_internals/feed_internals_ui.h"

#include <utility>

#include "base/bind.h"
#include "chrome/browser/android/feed/feed_host_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/feed_internals/feed_internals_page_handler.h"
#include "chrome/common/url_constants.h"
#include "chrome/grit/browser_resources.h"
#include "content/public/browser/web_ui_data_source.h"

FeedInternalsUI::FeedInternalsUI(content::WebUI* web_ui)
    : ui::MojoWebUIController(web_ui) {
  content::WebUIDataSource* source =
      content::WebUIDataSource::Create(chrome::kChromeUISnippetsInternalsHost);

  source->AddResourcePath("feed_internals.js", IDR_FEED_INTERNALS_JS);
  source->AddResourcePath("feed_internals.mojom-lite.js",
                          IDR_FEED_INTERNALS_MOJO_JS);
  source->AddResourcePath("feed_internals.css", IDR_FEED_INTERNALS_CSS);
  source->SetDefaultResource(IDR_FEED_INTERNALS_HTML);
  source->UseGzip();

  Profile* profile = Profile::FromWebUI(web_ui);
  feed_host_service_ =
      feed::FeedHostServiceFactory::GetForBrowserContext(profile);

  content::WebUIDataSource::Add(profile, source);
  // This class is the caller of the callback when an observer interface is
  // triggered. So this base::Unretained is safe.
  AddHandlerToRegistry(base::BindRepeating(
      &FeedInternalsUI::BindFeedInternalsPageHandler, base::Unretained(this)));
}

FeedInternalsUI::~FeedInternalsUI() = default;

void FeedInternalsUI::BindFeedInternalsPageHandler(
    feed_internals::mojom::PageHandlerRequest request) {
  page_handler_ = std::make_unique<FeedInternalsPageHandler>(
      std::move(request), feed_host_service_);
}
