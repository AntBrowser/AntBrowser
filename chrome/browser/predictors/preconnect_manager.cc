// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/predictors/preconnect_manager.h"

#include <utility>

#include "base/trace_event/trace_event.h"
#include "chrome/browser/predictors/resource_prefetch_predictor.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/storage_partition.h"
#include "net/base/load_flags.h"
#include "services/network/public/mojom/network_context.mojom.h"

namespace predictors {

const bool kAllowCredentialsOnPreconnectByDefault = true;

PreconnectedRequestStats::PreconnectedRequestStats(const GURL& origin,
                                                   bool was_preresolve_cached,
                                                   bool was_preconnected)
    : origin(origin),
      was_preresolve_cached(was_preresolve_cached),
      was_preconnected(was_preconnected) {}

PreconnectedRequestStats::PreconnectedRequestStats(
    const PreconnectedRequestStats& other) = default;
PreconnectedRequestStats::~PreconnectedRequestStats() = default;

PreconnectStats::PreconnectStats(const GURL& url)
    : url(url), start_time(base::TimeTicks::Now()) {}
PreconnectStats::~PreconnectStats() = default;

PreresolveInfo::PreresolveInfo(const GURL& url, size_t count)
    : url(url),
      queued_count(count),
      inflight_count(0),
      was_canceled(false),
      stats(std::make_unique<PreconnectStats>(url)) {}

PreresolveInfo::~PreresolveInfo() = default;

PreresolveJob::PreresolveJob(const GURL& url,
                             int num_sockets,
                             bool allow_credentials,
                             PreresolveInfo* info)
    : url(url),
      num_sockets(num_sockets),
      allow_credentials(allow_credentials),
      info(info) {
  DCHECK_GE(num_sockets, 0);
}

PreresolveJob::PreresolveJob(PreresolveJob&& other) = default;
PreresolveJob::~PreresolveJob() = default;

PreconnectManager::PreconnectManager(base::WeakPtr<Delegate> delegate,
                                     Profile* profile)
    : delegate_(std::move(delegate)),
      profile_(profile),
      inflight_preresolves_count_(0),
      weak_factory_(this) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DCHECK(profile_);
}

PreconnectManager::~PreconnectManager() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
}

void PreconnectManager::Start(const GURL& url,
                              std::vector<PreconnectRequest> requests) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  const std::string host = url.host();
  if (preresolve_info_.find(host) != preresolve_info_.end())
    return;

  auto iterator_and_whether_inserted = preresolve_info_.emplace(
      host, std::make_unique<PreresolveInfo>(url, requests.size()));
  PreresolveInfo* info = iterator_and_whether_inserted.first->second.get();

  for (const auto& request : requests) {
    DCHECK(request.origin.GetOrigin() == request.origin);
    PreresolveJobId job_id = preresolve_jobs_.Add(
        std::make_unique<PreresolveJob>(request.origin, request.num_sockets,
                                        request.allow_credentials, info));
    queued_jobs_.push_back(job_id);
  }

  TryToLaunchPreresolveJobs();
}

void PreconnectManager::StartPreresolveHost(const GURL& url) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!url.SchemeIsHTTPOrHTTPS())
    return;
  PreresolveJobId job_id = preresolve_jobs_.Add(std::make_unique<PreresolveJob>(
      url.GetOrigin(), 0, kAllowCredentialsOnPreconnectByDefault, nullptr));
  queued_jobs_.push_front(job_id);

  TryToLaunchPreresolveJobs();
}

void PreconnectManager::StartPreresolveHosts(
    const std::vector<std::string>& hostnames) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  // Push jobs in front of the queue due to higher priority.
  for (auto it = hostnames.rbegin(); it != hostnames.rend(); ++it) {
    PreresolveJobId job_id =
        preresolve_jobs_.Add(std::make_unique<PreresolveJob>(
            GURL("http://" + *it), 0, kAllowCredentialsOnPreconnectByDefault,
            nullptr));
    queued_jobs_.push_front(job_id);
  }

  TryToLaunchPreresolveJobs();
}

void PreconnectManager::StartPreconnectUrl(const GURL& url,
                                           bool allow_credentials) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!url.SchemeIsHTTPOrHTTPS())
    return;
  PreresolveJobId job_id = preresolve_jobs_.Add(std::make_unique<PreresolveJob>(
      url.GetOrigin(), 1, allow_credentials, nullptr));
  queued_jobs_.push_front(job_id);

  TryToLaunchPreresolveJobs();
}

void PreconnectManager::Stop(const GURL& url) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto it = preresolve_info_.find(url.host());
  if (it == preresolve_info_.end()) {
    return;
  }

  it->second->was_canceled = true;
}

void PreconnectManager::PreconnectUrl(const GURL& url,
                                      const GURL& site_for_cookies,
                                      int num_sockets,
                                      bool allow_credentials) const {
  DCHECK(url.GetOrigin() == url);
  DCHECK(url.SchemeIsHTTPOrHTTPS());
  if (observer_)
    observer_->OnPreconnectUrl(url, num_sockets, allow_credentials);

  auto* network_context = GetNetworkContext();
  if (!network_context)
    return;

  bool privacy_mode = false;
  int load_flags = net::LOAD_NORMAL;

  if (!allow_credentials) {
    privacy_mode = true;
    load_flags = net::LOAD_DO_NOT_SEND_COOKIES | net::LOAD_DO_NOT_SAVE_COOKIES |
                 net::LOAD_DO_NOT_SEND_AUTH_DATA;
  }

  network_context->PreconnectSockets(num_sockets, url, load_flags,
                                     privacy_mode);
}

std::unique_ptr<ResolveHostClientImpl> PreconnectManager::PreresolveUrl(
    const GURL& url,
    ResolveHostCallback callback) const {
  DCHECK(url.GetOrigin() == url);
  DCHECK(url.SchemeIsHTTPOrHTTPS());
  if (observer_)
    observer_->OnPreresolveUrl(url);

  auto* network_context = GetNetworkContext();
  if (!network_context) {
    // Cannot invoke the callback right away because it would cause the
    // use-after-free after returning from this function.
    content::BrowserThread::PostTask(
        content::BrowserThread::UI, FROM_HERE,
        base::BindOnce(std::move(callback), false));
    return nullptr;
  }

  return std::make_unique<ResolveHostClientImpl>(url, std::move(callback),
                                                 GetNetworkContext());
}

void PreconnectManager::TryToLaunchPreresolveJobs() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  while (!queued_jobs_.empty() &&
         inflight_preresolves_count_ < kMaxInflightPreresolves) {
    auto job_id = queued_jobs_.front();
    queued_jobs_.pop_front();
    PreresolveJob* job = preresolve_jobs_.Lookup(job_id);
    DCHECK(job);
    PreresolveInfo* info = job->info;

    if (!(info && info->was_canceled)) {
      // TODO(crbug.com/838763): Preconnect to url if proxy is enabled.
      job->resolve_host_client = PreresolveUrl(
          job->url, base::BindOnce(&PreconnectManager::OnPreresolveFinished,
                                   weak_factory_.GetWeakPtr(), job_id));
      if (info)
        ++info->inflight_count;
      ++inflight_preresolves_count_;
    } else {
      preresolve_jobs_.Remove(job_id);
    }

    if (info)
      --info->queued_count;
  }
}

void PreconnectManager::OnPreresolveFinished(PreresolveJobId job_id,
                                             bool success) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  PreresolveJob* job = preresolve_jobs_.Lookup(job_id);
  DCHECK(job);
  PreresolveInfo* info = job->info;

  if (observer_)
    observer_->OnPreresolveFinished(job->url, success);

  FinishPreresolve(job_id, success, false);
  --inflight_preresolves_count_;
  if (info)
    --info->inflight_count;
  if (info && info->is_done())
    AllPreresolvesForUrlFinished(info);
  TryToLaunchPreresolveJobs();
}

void PreconnectManager::FinishPreresolve(PreresolveJobId job_id,
                                         bool found,
                                         bool cached) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  PreresolveJob* job = preresolve_jobs_.Lookup(job_id);
  DCHECK(job);
  PreresolveInfo* info = job->info;

  bool need_preconnect =
      found && job->need_preconnect() && (!info || !info->was_canceled);
  if (need_preconnect) {
    PreconnectUrl(job->url, info ? info->url : GURL(), job->num_sockets,
                  job->allow_credentials);
  }
  if (info && found)
    info->stats->requests_stats.emplace_back(job->url, cached, need_preconnect);
  preresolve_jobs_.Remove(job_id);
}

void PreconnectManager::AllPreresolvesForUrlFinished(PreresolveInfo* info) {
  DCHECK(info);
  DCHECK(info->is_done());
  auto it = preresolve_info_.find(info->url.host());
  DCHECK(it != preresolve_info_.end());
  DCHECK(info == it->second.get());
  if (delegate_)
    delegate_->PreconnectFinished(std::move(info->stats));
  preresolve_info_.erase(it);
}

network::mojom::NetworkContext* PreconnectManager::GetNetworkContext() const {
  if (network_context_)
    return network_context_;

  if (profile_->AsTestingProfile()) {
    // We're testing and |network_context_| wasn't set. Return nullptr to avoid
    // hitting the network.
    return nullptr;
  }

  return content::BrowserContext::GetDefaultStoragePartition(profile_)
      ->GetNetworkContext();
}

}  // namespace predictors
