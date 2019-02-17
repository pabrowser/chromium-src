// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_APP_LIST_APP_LIST_METRICS_H_
#define ASH_APP_LIST_APP_LIST_METRICS_H_

#include "ash/app_list/app_list_export.h"

namespace app_list {

class AppListModel;
class SearchModel;
class SearchResult;

// The UMA histogram that logs the input latency from input event to the
// representation time of the shown launcher UI.
constexpr char kAppListShowInputLatencyHistogram[] =
    "Apps.AppListShow.InputLatency";

// The UMA histogram that logs the input latency from input event to the
// representation time of the dismissed launcher UI.
constexpr char kAppListHideInputLatencyHistogram[] =
    "Apps.AppListHide.InputLatency";

// The UMA histogram that logs usage of suggested and regular apps.
constexpr char kAppListAppLaunched[] = "Apps.AppListAppLaunched";

// The UMA histogram that logs usage of suggested and regular apps while the
// fullscreen launcher is enabled.
constexpr char kAppListAppLaunchedFullscreen[] =
    "Apps.AppListAppLaunchedFullscreen";

// The UMA histogram that logs different ways to move an app in app list's apps
// grid.
constexpr char kAppListAppMovingType[] = "Apps.AppListAppMovingType";

// The UMA histogram that logs the creation time of the AppListView.
constexpr char kAppListCreationTimeHistogram[] = "Apps.AppListCreationTime";

// The UMA histogram that logs usage of state transitions in the new
// app list UI.
constexpr char kAppListStateTransitionSourceHistogram[] =
    "Apps.AppListStateTransitionSource";

// The UMA histogram that logs the source of vertical page switcher usage in the
// app list.
constexpr char kAppListPageSwitcherSourceHistogram[] =
    "Apps.AppListPageSwitcherSource";

// The UMA histogram that logs usage of the original and redesigned folders.
constexpr char kAppListFolderOpenedHistogram[] = "Apps.AppListFolderOpened";

// The UMA histogram that logs how the app list transitions from peeking to
// fullscreen.
constexpr char kAppListPeekingToFullscreenHistogram[] =
    "Apps.AppListPeekingToFullscreenSource";

// The UMA histogram that logs how the app list is shown.
constexpr char kAppListToggleMethodHistogram[] = "Apps.AppListShowSource";

// The UMA histogram that logs which page gets opened by the user.
constexpr char kPageOpenedHistogram[] = "Apps.AppListPageOpened";

// The UMA histogram that logs how many apps users have in folders.
constexpr char kNumberOfAppsInFoldersHistogram[] =
    "Apps.AppsInFolders.FullscreenAppListEnabled";

// The UMA histogram that logs how many folders users have.
constexpr char kNumberOfFoldersHistogram[] = "Apps.NumberOfFolders";

// The UMA histogram that logs how many pages users have in top level apps grid.
constexpr char kNumberOfPagesHistogram[] = "Apps.NumberOfPages";

// The UMA histogram that logs how many pages with empty slots users have in top
// level apps grid.
constexpr char kNumberOfPagesNotFullHistogram[] = "Apps.NumberOfPagesNotFull";

// The UMA histogram that logs the type of search result opened.
constexpr char kSearchResultOpenDisplayTypeHistogram[] =
    "Apps.AppListSearchResultOpenDisplayType";

// The UMA histogram that logs how long the search query was when a result was
// opened.
constexpr char kSearchQueryLength[] = "Apps.AppListSearchQueryLength";

// The UMA histogram that logs the Manhattan distance from the origin of the
// search results to the selected result.
constexpr char kSearchResultDistanceFromOrigin[] =
    "Apps.AppListSearchResultDistanceFromOrigin";

// These are used in histograms, do not remove/renumber entries. If you're
// adding to this enum with the intention that it will be logged, update the
// AppListZeroStateSearchResultUserActionType enum listing in
// tools/metrics/histograms/enums.xml.
enum class ZeroStateSearchResultUserActionType {
  kRemoveResult = 0,
  kAppendResult = 1,
  kMaxValue = kAppendResult,
};

// These are used in histograms, do not remove/renumber entries. If you're
// adding to this enum with the intention that it will be logged, update the
// AppListZeroStateResultRemovalConfirmation enum listing in
// tools/metrics/histograms/enums.xml.
enum class ZeroStateSearchResutRemovalConfirmation {
  kRemovalConfirmed = 0,
  kRemovalCanceled = 1,
  kMaxValue = kRemovalCanceled,
};

// The different ways that the app list can transition from PEEKING to
// FULLSCREEN_ALL_APPS. These values are written to logs.  New enum
// values can be added, but existing enums must never be renumbered or deleted
// and reused.
enum AppListPeekingToFullscreenSource {
  kSwipe = 0,
  kExpandArrow = 1,
  kMousepadScroll = 2,
  kMousewheelScroll = 3,
  kMaxPeekingToFullscreen = 4,
};

// The different ways the app list can be shown. These values are written to
// logs.  New enum values can be added, but existing enums must never be
// renumbered or deleted and reused.
enum AppListShowSource {
  kSearchKey = 0,
  kShelfButton = 1,
  kSwipeFromShelf = 2,
  kTabletMode = 3,
  kSearchKeyFullscreen = 4,
  kShelfButtonFullscreen = 5,
  kMaxValue = kShelfButtonFullscreen,
};

// The two versions of folders. These values are written to logs.  New enum
// values can be added, but existing enums must never be renumbered or deleted
// and reused.
enum AppListFolderOpened {
  kOldFolders = 0,
  kFullscreenAppListFolders = 1,
  kMaxFolderOpened = 2,
};

// The valid AppListState transitions. These values are written to logs.  New
// enum values can be added, but existing enums must never be renumbered or
// deleted and reused. If adding a state transition, add it to the switch
// statement in AppListView::GetAppListStateTransitionSource.
enum AppListStateTransitionSource {
  kFullscreenAllAppsToClosed = 0,
  kFullscreenAllAppsToFullscreenSearch = 1,
  kFullscreenAllAppsToPeeking = 2,
  kFullscreenSearchToClosed = 3,
  kFullscreenSearchToFullscreenAllApps = 4,
  kHalfToClosed = 5,
  KHalfToFullscreenSearch = 6,
  kHalfToPeeking = 7,
  kPeekingToClosed = 8,
  kPeekingToFullscreenAllApps = 9,
  kPeekingToHalf = 10,
  kMaxAppListStateTransition = 11,
};

// The different ways to change pages in the app list's app grid. These values
// are written to logs.  New enum values can be added, but existing enums must
// never be renumbered or deleted and reused.
enum AppListPageSwitcherSource {
  kTouchPageIndicator = 0,
  kClickPageIndicator = 1,
  kSwipeAppGrid = 2,
  kFlingAppGrid = 3,
  kMouseWheelScroll = 4,
  kMousePadScroll = 5,
  kDragAppToBorder = 6,
  kMaxAppListPageSwitcherSource = 7,
};

// The different ways to move an app in app list's apps grid. These values are
// written to logs. New enum values can be added, but existing enums must never
// be renumbered or deleted and reused.
enum AppListAppMovingType {
  kMoveIntoFolder = 0,
  kMoveOutOfFolder = 1,
  kMoveIntoAnotherFolder = 2,
  kReorderInFolder = 3,
  kReorderInTopLevel = 4,
  kMaxAppListAppMovingType = 5,
};

void RecordFolderShowHideAnimationSmoothness(int actual_frames,
                                             int ideal_duration_ms,
                                             float refresh_rate);

void RecordPaginationAnimationSmoothness(int actual_frames,
                                         int ideal_duration_ms,
                                         float refresh_rate);

void RecordZeroStateSearchResultUserActionHistogram(
    ZeroStateSearchResultUserActionType action);

void RecordZeroStateSearchResultRemovalHistogram(
    ZeroStateSearchResutRemovalConfirmation removal_decision);

APP_LIST_EXPORT void RecordSearchResultOpenSource(
    const SearchResult* result,
    const AppListModel* model,
    const SearchModel* search_model);

}  // namespace app_list

#endif  // ASH_APP_LIST_APP_LIST_METRICS_H_