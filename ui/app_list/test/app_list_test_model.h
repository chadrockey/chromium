// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_APP_LIST_TEST_APP_LIST_TEST_MODEL_H_
#define UI_APP_LIST_TEST_APP_LIST_TEST_MODEL_H_

#include <string>

#include "ui/app_list/app_list_item.h"
#include "ui/app_list/app_list_model.h"

namespace app_list {

namespace test {

// Extends AppListModel with helper functions for use in tests.
class AppListTestModel : public AppListModel {
 public:
  class AppListTestItemModel : public AppListItem {
   public:
    AppListTestItemModel(const std::string& id, AppListTestModel* model);
    virtual ~AppListTestItemModel();
    virtual void Activate(int event_flags) OVERRIDE;
    virtual const char* GetItemType() const OVERRIDE;

    void SetPosition(const syncer::StringOrdinal& new_position);

   private:
    AppListTestModel* model_;

    DISALLOW_COPY_AND_ASSIGN(AppListTestItemModel);
  };

  AppListTestModel();

  // Generates a name based on |id|.
  std::string GetItemName(int id);

  // Populate the model with |n| items titled "Item #".
  void PopulateApps(int n);

  // Populate the model with an item titled "Item |id|".
  void PopulateAppWithId(int id);

  // Get a string of all apps in |model| joined with ','.
  std::string GetModelContent();

  // Creates an item with |title| and |full_name|. Caller owns the result.
  AppListTestItemModel* CreateItem(const std::string& title,
                                   const std::string& full_name);

  // Creates and adds an item with |title| and |full_name| to the model.
  void CreateAndAddItem(const std::string& title, const std::string& full_name);

  // Convenience version of CreateAndAddItem(title, title).
  void CreateAndAddItem(const std::string& title);

  // Call SetHighlighted on the specified item.
  void HighlightItemAt(int index);

  int activate_count() { return activate_count_; }
  AppListItem* last_activated() { return last_activated_; }

  static const char kItemType[];

 private:
  void ItemActivated(AppListTestItemModel* item);

  int activate_count_;
  AppListItem* last_activated_;

  DISALLOW_COPY_AND_ASSIGN(AppListTestModel);
};

}  // namespace test
}  // namespace app_list

#endif  // UI_APP_LIST_TEST_APP_LIST_TEST_MODEL_H_
