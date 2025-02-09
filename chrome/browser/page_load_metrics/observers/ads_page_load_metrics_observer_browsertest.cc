// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/macros.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/page_load_metrics/observers/ads_page_load_metrics_observer.h"
#include "chrome/browser/page_load_metrics/page_load_metrics_test_waiter.h"
#include "chrome/browser/subresource_filter/subresource_filter_browser_test_harness.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/common/chrome_features.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/subresource_filter/content/browser/content_ruleset_service.h"
#include "components/subresource_filter/core/browser/subresource_filter_features.h"
#include "components/subresource_filter/core/common/activation_level.h"
#include "components/subresource_filter/core/common/activation_scope.h"
#include "components/subresource_filter/core/common/test_ruleset_utils.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace {
const char kCrossOriginHistogramId[] =
    "PageLoad.Clients.Ads.Google.FrameCounts.AdFrames.PerFrame.OriginStatus";
}  // namespace

class AdsPageLoadMetricsObserverBrowserTest
    : public subresource_filter::SubresourceFilterBrowserTest {
 public:
  AdsPageLoadMetricsObserverBrowserTest()
      : subresource_filter::SubresourceFilterBrowserTest() {
    scoped_feature_list_.InitAndEnableFeature(features::kAdsFeature);
  }
  ~AdsPageLoadMetricsObserverBrowserTest() override {}

 private:
  base::test::ScopedFeatureList scoped_feature_list_;

  DISALLOW_COPY_AND_ASSIGN(AdsPageLoadMetricsObserverBrowserTest);
};

// Test that an embedded ad is same origin.
IN_PROC_BROWSER_TEST_F(AdsPageLoadMetricsObserverBrowserTest,
                       OriginStatusMetricEmbedded) {
  base::HistogramTester histogram_tester;
  ui_test_utils::NavigateToURL(
      browser(),
      embedded_test_server()->GetURL("/ads_observer/srcdoc_embedded_ad.html"));
  ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL));
  histogram_tester.ExpectUniqueSample(
      kCrossOriginHistogramId,
      AdsPageLoadMetricsObserver::AdOriginStatus::kSame, 1);
}

// Test that an empty embedded ad isn't reported at all.
IN_PROC_BROWSER_TEST_F(AdsPageLoadMetricsObserverBrowserTest,
                       OriginStatusMetricEmbeddedEmpty) {
  base::HistogramTester histogram_tester;
  ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL(
                     "/ads_observer/srcdoc_embedded_ad_empty.html"));
  ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL));
  histogram_tester.ExpectTotalCount(kCrossOriginHistogramId, 0);
}

// Test that an ad with the same origin as the main page is same origin.
IN_PROC_BROWSER_TEST_F(AdsPageLoadMetricsObserverBrowserTest,
                       OriginStatusMetricSame) {
  base::HistogramTester histogram_tester;
  ui_test_utils::NavigateToURL(
      browser(),
      embedded_test_server()->GetURL("/ads_observer/same_origin_ad.html"));
  ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL));
  histogram_tester.ExpectUniqueSample(
      kCrossOriginHistogramId,
      AdsPageLoadMetricsObserver::AdOriginStatus::kSame, 1);
}

// Test that an ad with a different origin as the main page is cross origin.
IN_PROC_BROWSER_TEST_F(AdsPageLoadMetricsObserverBrowserTest,
                       OriginStatusMetricCross) {
  // Note: Cannot navigate cross-origin without dynamically generating the URL.
  base::HistogramTester histogram_tester;
  ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/iframe_blank.html"));
  // Note that the initial iframe is not an ad, so the metric doesn't observe
  // it initially as same origin.  However, on re-navigating to a cross
  // origin site that has an ad at its origin, the ad on that page is cross
  // origin from the original page.
  NavigateIframeToURL(web_contents(), "test",
                      embedded_test_server()->GetURL(
                          "a.com", "/ads_observer/same_origin_ad.html"));
  ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL));
  histogram_tester.ExpectUniqueSample(
      kCrossOriginHistogramId,
      AdsPageLoadMetricsObserver::AdOriginStatus::kCross, 1);
}

// Test that a subframe that aborts (due to doc.write) doesn't cause a crash
// if it continues to load resources.
IN_PROC_BROWSER_TEST_F(AdsPageLoadMetricsObserverBrowserTest,
                       DocOverwritesNavigation) {
  content::DOMMessageQueue msg_queue;

  base::HistogramTester histogram_tester;

  ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL(
                     "/ads_observer/docwrite_provisional_frame.html"));
  std::string status;
  EXPECT_TRUE(msg_queue.WaitForMessage(&status));
  EXPECT_EQ("\"loaded\"", status);

  // Navigate away to force the histogram recording.
  ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL));

  histogram_tester.ExpectUniqueSample(
      "PageLoad.Clients.Ads.Google.FrameCounts.AnyParentFrame.AdFrames", 1, 1);
  histogram_tester.ExpectUniqueSample(
      "PageLoad.Clients.Ads.Google.Bytes.AdFrames.Aggregate.Total",
      0 /* < 1 KB */, 1);
}

IN_PROC_BROWSER_TEST_F(AdsPageLoadMetricsObserverBrowserTest,
                       SubresourceFilter) {
  ResetConfiguration(subresource_filter::Configuration(
      subresource_filter::ActivationLevel::DRYRUN,
      subresource_filter::ActivationScope::ALL_SITES));
  base::HistogramTester histogram_tester;

  // cross_site_iframe_factory loads URLs like:
  // http://b.com:40919/cross_site_iframe_factory.html?b()
  SetRulesetToDisallowURLsWithPathSuffix("b()");
  const GURL main_url(embedded_test_server()->GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(b,b,c,d)"));

  ui_test_utils::NavigateToURL(browser(), main_url);
  // Navigate away to force the histogram recording.
  ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL));

  histogram_tester.ExpectUniqueSample(
      "PageLoad.Clients.Ads.SubresourceFilter.FrameCounts.AnyParentFrame."
      "AdFrames",
      2, 1);
  histogram_tester.ExpectUniqueSample(
      "PageLoad.Clients.Ads.All.FrameCounts.AnyParentFrame.AdFrames", 2, 1);
}

class AdsPageLoadMetricsTestWaiter
    : public page_load_metrics::PageLoadMetricsTestWaiter {
 public:
  explicit AdsPageLoadMetricsTestWaiter(content::WebContents* web_contents)
      : page_load_metrics::PageLoadMetricsTestWaiter(web_contents){};
  void AddMinimumAdResourceExpectation(int num_ad_resources) {
    expected_minimum_num_ad_resources_ = num_ad_resources;
  };

 protected:
  bool ExpectationsSatisfied() const override {
    int num_ad_resources = 0;
    for (auto& kv : page_resources_) {
      if (kv.second->reported_as_ad_resource)
        num_ad_resources++;
    }
    return num_ad_resources >= expected_minimum_num_ad_resources_ &&
           PageLoadMetricsTestWaiter::ExpectationsSatisfied();
  };

 private:
  int expected_minimum_num_ad_resources_ = 0;
};

class AdsPageLoadMetricsObserverResourceBrowserTest
    : public subresource_filter::SubresourceFilterBrowserTest {
 public:
  AdsPageLoadMetricsObserverResourceBrowserTest() {
    scoped_feature_list_.InitAndEnableFeature(features::kAdsFeature);
  }

  ~AdsPageLoadMetricsObserverResourceBrowserTest() override {}
  void SetUpOnMainThread() override {
    g_browser_process->subresource_filter_ruleset_service()
        ->SetIsAfterStartupForTesting();
    host_resolver()->AddRule("*", "127.0.0.1");
    SetRulesetWithRules(
        {subresource_filter::testing::CreateSuffixRule("ad_script.js"),
         subresource_filter::testing::CreateSuffixRule("create_frame.js")});
  }

 protected:
  std::unique_ptr<AdsPageLoadMetricsTestWaiter>
  CreateAdsPageLoadMetricsTestWaiter() {
    content::WebContents* web_contents =
        browser()->tab_strip_model()->GetActiveWebContents();
    return std::make_unique<AdsPageLoadMetricsTestWaiter>(web_contents);
  }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
};

IN_PROC_BROWSER_TEST_F(AdsPageLoadMetricsObserverResourceBrowserTest,
                       ReceivedAdResources) {
  embedded_test_server()->ServeFilesFromSourceDirectory(
      "chrome/test/data/ad_tagging");
  content::SetupCrossSiteRedirector(embedded_test_server());
  ASSERT_TRUE(embedded_test_server()->Start());

  auto waiter = CreateAdsPageLoadMetricsTestWaiter();
  ui_test_utils::NavigateToURL(
      browser(),
      embedded_test_server()->GetURL("foo.com", "/frame_factory.html"));
  // Both subresources should have been reported as ads.
  waiter->AddMinimumAdResourceExpectation(2);
  waiter->Wait();
}

// Main resources for adframes are counted as ad resources.
IN_PROC_BROWSER_TEST_F(AdsPageLoadMetricsObserverResourceBrowserTest,
                       ReceivedMainResourceAds) {
  embedded_test_server()->ServeFilesFromSourceDirectory(
      "chrome/test/data/ad_tagging");
  content::SetupCrossSiteRedirector(embedded_test_server());
  ASSERT_TRUE(embedded_test_server()->Start());

  auto waiter = CreateAdsPageLoadMetricsTestWaiter();

  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ui_test_utils::NavigateToURL(
      browser(),
      embedded_test_server()->GetURL("foo.com", "/frame_factory.html"));
  contents->GetMainFrame()->ExecuteJavaScriptForTests(
      base::ASCIIToUTF16("createFrame('frame_factory.html', '');"));
  // Both pages subresources should have been reported as ad. The iframe
  // resource should also be reported as an ad.
  waiter->AddMinimumAdResourceExpectation(5);
  waiter->Wait();
}

// Subframe navigations report ad resources correctly.
IN_PROC_BROWSER_TEST_F(AdsPageLoadMetricsObserverResourceBrowserTest,
                       ReceivedSubframeNavigationAds) {
  embedded_test_server()->ServeFilesFromSourceDirectory(
      "chrome/test/data/ad_tagging");
  content::SetupCrossSiteRedirector(embedded_test_server());
  ASSERT_TRUE(embedded_test_server()->Start());

  auto waiter = CreateAdsPageLoadMetricsTestWaiter();

  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ui_test_utils::NavigateToURL(
      browser(),
      embedded_test_server()->GetURL("foo.com", "/frame_factory.html"));
  contents->GetMainFrame()->ExecuteJavaScriptForTests(
      base::ASCIIToUTF16("createFrame('frame_factory.html', 'test');"));
  waiter->AddMinimumAdResourceExpectation(5);
  waiter->Wait();
  NavigateIframeToURL(
      web_contents(), "test",
      embedded_test_server()->GetURL("foo.com", "/frame_factory.html"));
  // All resources except the top-level main resource should be reported as an
  // ad.
  waiter->AddMinimumAdResourceExpectation(8);
  waiter->Wait();
}
