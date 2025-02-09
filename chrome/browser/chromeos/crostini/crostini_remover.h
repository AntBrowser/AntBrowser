// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_CROSTINI_CROSTINI_REMOVER_H_
#define CHROME_BROWSER_CHROMEOS_CROSTINI_CROSTINI_REMOVER_H_

#include "chrome/browser/chromeos/crostini/crostini_manager.h"

namespace crostini {

class CrostiniRemover : public base::RefCountedThreadSafe<CrostiniRemover> {
 public:
  CrostiniRemover(Profile* profile,
                  std::string vm_name,
                  std::string container_name,
                  CrostiniManager::RemoveCrostiniCallback callback);

  void RemoveCrostini();

 private:
  friend class base::RefCountedThreadSafe<CrostiniRemover>;

  ~CrostiniRemover();

  void OnComponentLoaded(bool is_successful);
  void OnConciergeStarted(bool is_successful);
  void StopVmFinished(crostini::ConciergeClientResult result);
  void DestroyDiskImageFinished(crostini::ConciergeClientResult result);
  void StopConciergeFinished(bool is_successful);

  Profile* profile_;
  std::string vm_name_;
  std::string container_name_;
  CrostiniManager::RemoveCrostiniCallback callback_;

  DISALLOW_COPY_AND_ASSIGN(CrostiniRemover);
};

}  // namespace crostini

#endif  // CHROME_BROWSER_CHROMEOS_CROSTINI_CROSTINI_REMOVER_H_
