// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/gtk/tabs/tab_renderer_gtk.h"

#include <algorithm>
#include <utility>

#include "base/debug/trace_event.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/defaults.h"
#include "chrome/browser/extensions/tab_helper.h"
#include "chrome/browser/favicon/favicon_tab_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_properties.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/gtk/bookmarks/bookmark_utils_gtk.h"
#include "chrome/browser/ui/gtk/custom_button.h"
#include "chrome/browser/ui/gtk/gtk_theme_service.h"
#include "chrome/browser/ui/gtk/gtk_util.h"
#include "chrome/browser/ui/tab_contents/core_tab_helper.h"
#include "chrome/browser/ui/tabs/tab_utils.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/web_contents.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "grit/ui_resources.h"
#include "skia/ext/image_operations.h"
#include "ui/base/gtk/gtk_screen_util.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/animation/throb_animation.h"
#include "ui/gfx/canvas_skia_paint.h"
#include "ui/gfx/favicon_size.h"
#include "ui/gfx/gtk_compat.h"
#include "ui/gfx/gtk_util.h"
#include "ui/gfx/image/cairo_cached_surface.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/pango_util.h"
#include "ui/gfx/platform_font_pango.h"
#include "ui/gfx/skbitmap_operations.h"

using content::WebContents;

namespace {

const int kFontPixelSize = 12;
const int kLeftPadding = 16;
const int kTopPadding = 6;
const int kRightPadding = 15;
const int kBottomPadding = 5;
const int kFaviconTitleSpacing = 4;
const int kTitleCloseButtonSpacing = 5;
const int kStandardTitleWidth = 175;
const int kDropShadowOffset = 2;
const int kInactiveTabBackgroundOffsetY = 15;

// When a non-mini-tab becomes a mini-tab the width of the tab animates. If
// the width of a mini-tab is >= kMiniTabRendererAsNormalTabWidth then the tab
// is rendered as a normal tab. This is done to avoid having the title
// immediately disappear when transitioning a tab from normal to mini-tab.
const int kMiniTabRendererAsNormalTabWidth =
    browser_defaults::kMiniTabWidth + 30;

// The tab images are designed to overlap the toolbar by 1 pixel. For now we
// don't actually overlap the toolbar, so this is used to know how many pixels
// at the bottom of the tab images are to be ignored.
const int kToolbarOverlap = 1;

// How long the hover state takes.
const int kHoverDurationMs = 90;

// How opaque to make the hover state (out of 1).
const double kHoverOpacity = 0.33;

// Opacity for non-active selected tabs.
const double kSelectedTabOpacity = 0.45;

// Selected (but not active) tabs have their throb value scaled down by this.
const double kSelectedTabThrobScale = 0.5;

// Max opacity for the mini-tab title change animation.
const double kMiniTitleChangeThrobOpacity = 0.75;

// Duration for when the title of an inactive mini-tab changes.
const int kMiniTitleChangeThrobDuration = 1000;

// The horizontal offset used to position the close button in the tab.
const int kCloseButtonHorzFuzz = 4;

// Gets the bounds of |widget| relative to |parent|.
gfx::Rect GetWidgetBoundsRelativeToParent(GtkWidget* parent,
                                          GtkWidget* widget) {
  gfx::Rect bounds = ui::GetWidgetScreenBounds(widget);
  bounds.Offset(-ui::GetWidgetScreenOffset(parent));
  return bounds;
}

// Returns a GdkPixbuf after resizing the SkBitmap as necessary to the
// specified desired width and height. Caller must g_object_unref the returned
// pixbuf when no longer used.
GdkPixbuf* GetResizedGdkPixbufFromSkBitmap(const SkBitmap& bitmap,
                                           int dest_w,
                                           int dest_h) {
  float float_dest_w = static_cast<float>(dest_w);
  float float_dest_h = static_cast<float>(dest_h);
  int bitmap_w = bitmap.width();
  int bitmap_h = bitmap.height();

  // Scale proportionately.
  float scale = std::min(float_dest_w / bitmap_w,
                         float_dest_h / bitmap_h);
  int final_dest_w = static_cast<int>(bitmap_w * scale);
  int final_dest_h = static_cast<int>(bitmap_h * scale);

  GdkPixbuf* pixbuf;
  if (final_dest_w == bitmap_w && final_dest_h == bitmap_h) {
    pixbuf = gfx::GdkPixbufFromSkBitmap(bitmap);
  } else {
    SkBitmap resized_icon = skia::ImageOperations::Resize(
        bitmap,
        skia::ImageOperations::RESIZE_BETTER,
        final_dest_w, final_dest_h);
    pixbuf = gfx::GdkPixbufFromSkBitmap(resized_icon);
  }
  return pixbuf;
}

}  // namespace

TabRendererGtk::LoadingAnimation::Data::Data(
    GtkThemeService* theme_service) {
  // The loading animation image is a strip of states. Each state must be
  // square, so the height must divide the width evenly.
  SkBitmap loading_animation_frames =
      theme_service->GetImageNamed(IDR_THROBBER).AsBitmap();
  DCHECK(!loading_animation_frames.isNull());
  DCHECK_EQ(loading_animation_frames.width() %
            loading_animation_frames.height(), 0);
  loading_animation_frame_count =
      loading_animation_frames.width() /
      loading_animation_frames.height();

  SkBitmap waiting_animation_frames =
      theme_service->GetImageNamed(IDR_THROBBER_WAITING).AsBitmap();
  DCHECK(!waiting_animation_frames.isNull());
  DCHECK_EQ(waiting_animation_frames.width() %
            waiting_animation_frames.height(), 0);
  waiting_animation_frame_count =
      waiting_animation_frames.width() /
      waiting_animation_frames.height();

  waiting_to_loading_frame_count_ratio =
      waiting_animation_frame_count /
      loading_animation_frame_count;
  // TODO(beng): eventually remove this when we have a proper themeing system.
  //             themes not supporting IDR_THROBBER_WAITING are causing this
  //             value to be 0 which causes DIV0 crashes. The value of 5
  //             matches the current bitmaps in our source.
  if (waiting_to_loading_frame_count_ratio == 0)
    waiting_to_loading_frame_count_ratio = 5;
}

TabRendererGtk::LoadingAnimation::Data::Data(
    int loading, int waiting, int waiting_to_loading)
    : loading_animation_frame_count(loading),
      waiting_animation_frame_count(waiting),
      waiting_to_loading_frame_count_ratio(waiting_to_loading) {
}

bool TabRendererGtk::initialized_ = false;
int TabRendererGtk::tab_active_l_width_ = 0;
int TabRendererGtk::tab_active_l_height_ = 0;
int TabRendererGtk::tab_inactive_l_height_ = 0;
gfx::Font* TabRendererGtk::title_font_ = NULL;
int TabRendererGtk::title_font_height_ = 0;
int TabRendererGtk::close_button_width_ = 0;
int TabRendererGtk::close_button_height_ = 0;

////////////////////////////////////////////////////////////////////////////////
// TabRendererGtk::LoadingAnimation, public:
//
TabRendererGtk::LoadingAnimation::LoadingAnimation(
    GtkThemeService* theme_service)
    : data_(new Data(theme_service)),
      theme_service_(theme_service),
      animation_state_(ANIMATION_NONE),
      animation_frame_(0) {
  registrar_.Add(this,
                 chrome::NOTIFICATION_BROWSER_THEME_CHANGED,
                 content::Source<ThemeService>(theme_service_));
}

TabRendererGtk::LoadingAnimation::LoadingAnimation(
    const LoadingAnimation::Data& data)
    : data_(new Data(data)),
      theme_service_(NULL),
      animation_state_(ANIMATION_NONE),
      animation_frame_(0) {
}

TabRendererGtk::LoadingAnimation::~LoadingAnimation() {}

bool TabRendererGtk::LoadingAnimation::ValidateLoadingAnimation(
    AnimationState animation_state) {
  bool has_changed = false;
  if (animation_state_ != animation_state) {
    // The waiting animation is the reverse of the loading animation, but at a
    // different rate - the following reverses and scales the animation_frame_
    // so that the frame is at an equivalent position when going from one
    // animation to the other.
    if (animation_state_ == ANIMATION_WAITING &&
        animation_state == ANIMATION_LOADING) {
      animation_frame_ = data_->loading_animation_frame_count -
          (animation_frame_ / data_->waiting_to_loading_frame_count_ratio);
    }
    animation_state_ = animation_state;
    has_changed = true;
  }

  if (animation_state_ != ANIMATION_NONE) {
    animation_frame_ = (animation_frame_ + 1) %
                       ((animation_state_ == ANIMATION_WAITING) ?
                         data_->waiting_animation_frame_count :
                         data_->loading_animation_frame_count);
    has_changed = true;
  } else {
    animation_frame_ = 0;
  }
  return has_changed;
}

void TabRendererGtk::LoadingAnimation::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  DCHECK(type == chrome::NOTIFICATION_BROWSER_THEME_CHANGED);
  data_.reset(new Data(theme_service_));
}

TabRendererGtk::TabData::TabData()
    : is_default_favicon(false),
      loading(false),
      crashed(false),
      incognito(false),
      show_icon(true),
      mini(false),
      blocked(false),
      animating_mini_change(false),
      app(false),
      media_state(TAB_MEDIA_STATE_NONE),
      previous_media_state(TAB_MEDIA_STATE_NONE) {
}

TabRendererGtk::TabData::~TabData() {}

////////////////////////////////////////////////////////////////////////////////
// FaviconCrashAnimation
//
//  A custom animation subclass to manage the favicon crash animation.
class TabRendererGtk::FaviconCrashAnimation : public gfx::LinearAnimation,
                                              public gfx::AnimationDelegate {
 public:
  explicit FaviconCrashAnimation(TabRendererGtk* target)
      : gfx::LinearAnimation(1000, 25, this),
        target_(target) {
  }
  virtual ~FaviconCrashAnimation() {}

  // gfx::Animation overrides:
  virtual void AnimateToState(double state) OVERRIDE {
    const double kHidingOffset = 27;

    if (state < .5) {
      target_->SetFaviconHidingOffset(
          static_cast<int>(floor(kHidingOffset * 2.0 * state)));
    } else {
      target_->DisplayCrashedFavicon();
      target_->SetFaviconHidingOffset(
          static_cast<int>(
              floor(kHidingOffset - ((state - .5) * 2.0 * kHidingOffset))));
    }
  }

  // gfx::AnimationDelegate overrides:
  virtual void AnimationCanceled(const gfx::Animation* animation) OVERRIDE {
    target_->SetFaviconHidingOffset(0);
  }

 private:
  TabRendererGtk* target_;

  DISALLOW_COPY_AND_ASSIGN(FaviconCrashAnimation);
};

////////////////////////////////////////////////////////////////////////////////
// TabRendererGtk, public:

TabRendererGtk::TabRendererGtk(GtkThemeService* theme_service)
    : showing_icon_(false),
      showing_media_indicator_(false),
      showing_close_button_(false),
      favicon_hiding_offset_(0),
      should_display_crashed_favicon_(false),
      animating_media_state_(TAB_MEDIA_STATE_NONE),
      loading_animation_(theme_service),
      background_offset_x_(0),
      background_offset_y_(kInactiveTabBackgroundOffsetY),
      theme_service_(theme_service),
      close_button_color_(0),
      is_active_(false),
      selected_title_color_(SK_ColorBLACK),
      unselected_title_color_(SkColorSetRGB(64, 64, 64)) {
  InitResources();

  theme_service_->InitThemesFor(this);
  registrar_.Add(this, chrome::NOTIFICATION_BROWSER_THEME_CHANGED,
                 content::Source<ThemeService>(theme_service_));

  tab_.Own(gtk_fixed_new());
  gtk_widget_set_app_paintable(tab_.get(), TRUE);
  g_signal_connect(tab_.get(), "expose-event",
                   G_CALLBACK(OnExposeEventThunk), this);
  g_signal_connect(tab_.get(), "size-allocate",
                   G_CALLBACK(OnSizeAllocateThunk), this);
  close_button_.reset(MakeCloseButton());
  gtk_widget_show(tab_.get());

  hover_animation_.reset(new gfx::SlideAnimation(this));
  hover_animation_->SetSlideDuration(kHoverDurationMs);
}

TabRendererGtk::~TabRendererGtk() {
  tab_.Destroy();
}

void TabRendererGtk::Observe(int type,
                             const content::NotificationSource& source,
                             const content::NotificationDetails& details) {
  DCHECK(chrome::NOTIFICATION_BROWSER_THEME_CHANGED);
  selected_title_color_ =
      theme_service_->GetColor(ThemeProperties::COLOR_TAB_TEXT);
  unselected_title_color_ =
      theme_service_->GetColor(ThemeProperties::COLOR_BACKGROUND_TAB_TEXT);
}

void TabRendererGtk::UpdateData(WebContents* contents,
                                bool app,
                                bool loading_only) {
  DCHECK(contents);
  FaviconTabHelper* favicon_tab_helper =
      FaviconTabHelper::FromWebContents(contents);

  if (!loading_only) {
    data_.title = contents->GetTitle();
    data_.incognito = contents->GetBrowserContext()->IsOffTheRecord();

    TabMediaState next_media_state;
    if (contents->IsCrashed()) {
      data_.crashed = true;
      next_media_state = TAB_MEDIA_STATE_NONE;
    } else {
      data_.crashed = false;
      next_media_state = chrome::GetTabMediaStateForContents(contents);
    }
    if (data_.media_state != next_media_state) {
      data_.previous_media_state = data_.media_state;
      data_.media_state = next_media_state;
    }

    SkBitmap* app_icon =
        extensions::TabHelper::FromWebContents(contents)->GetExtensionAppIcon();
    if (app_icon) {
      data_.favicon = *app_icon;
    } else {
      data_.favicon = favicon_tab_helper->GetFavicon().AsBitmap();
    }

    data_.app = app;

    // Make a cairo cached version of the favicon.
    if (!data_.favicon.isNull()) {
      // Instead of resizing the icon during each frame, create our resized
      // icon resource now, send it to the xserver and use that each frame
      // instead.

      // For source images smaller than the favicon square, scale them as if
      // they were padded to fit the favicon square, so we don't blow up tiny
      // favicons into larger or nonproportional results.
      GdkPixbuf* pixbuf = GetResizedGdkPixbufFromSkBitmap(data_.favicon,
          gfx::kFaviconSize, gfx::kFaviconSize);
      data_.cairo_favicon.UsePixbuf(pixbuf);
      g_object_unref(pixbuf);
    } else {
      data_.cairo_favicon.Reset();
    }

    // This is kind of a hacky way to determine whether our icon is the default
    // favicon. But the plumbing that would be necessary to do it right would
    // be a good bit of work and would sully code for other platforms which
    // don't care to custom-theme the favicon. Hopefully the default favicon
    // will eventually be chromium-themable and this code will go away.
    data_.is_default_favicon =
        (data_.favicon.pixelRef() ==
        ui::ResourceBundle::GetSharedInstance().GetImageNamed(
            IDR_DEFAULT_FAVICON).AsBitmap().pixelRef());
  }

  // Loading state also involves whether we show the favicon, since that's where
  // we display the throbber.
  data_.loading = contents->IsLoading();
  data_.show_icon = favicon_tab_helper->ShouldDisplayFavicon();
}

void TabRendererGtk::UpdateFromModel() {
  // Force a layout, since the tab may have grown a favicon.
  Layout();
  SchedulePaint();

  if (data_.crashed) {
    if (!should_display_crashed_favicon_ && !IsPerformingCrashAnimation())
      StartCrashAnimation();
  } else {
    if (IsPerformingCrashAnimation())
      StopCrashAnimation();
    ResetCrashedFavicon();
  }

  if (data_.media_state != data_.previous_media_state) {
    data_.previous_media_state = data_.media_state;
    if (data_.media_state != TAB_MEDIA_STATE_NONE)
      animating_media_state_ = data_.media_state;
    StartMediaIndicatorAnimation();
  }
}

void TabRendererGtk::SetBlocked(bool blocked) {
  if (data_.blocked == blocked)
    return;
  data_.blocked = blocked;
  // TODO(zelidrag) bug 32399: Make tabs pulse on Linux as well.
}

bool TabRendererGtk::is_blocked() const {
  return data_.blocked;
}

bool TabRendererGtk::IsActive() const {
  return is_active_;
}

bool TabRendererGtk::IsSelected() const {
  return true;
}

bool TabRendererGtk::IsVisible() const {
  return gtk_widget_get_visible(tab_.get());
}

void TabRendererGtk::SetVisible(bool visible) const {
  if (visible) {
    gtk_widget_show(tab_.get());
    if (data_.mini)
      gtk_widget_show(close_button_->widget());
  } else {
    gtk_widget_hide_all(tab_.get());
  }
}

bool TabRendererGtk::ValidateLoadingAnimation(AnimationState animation_state) {
  return loading_animation_.ValidateLoadingAnimation(animation_state);
}

void TabRendererGtk::PaintFaviconArea(GtkWidget* widget, cairo_t* cr) {
  DCHECK(ShouldShowIcon());

  cairo_rectangle(cr,
                  x() + favicon_bounds_.x(),
                  y() + favicon_bounds_.y(),
                  favicon_bounds_.width(),
                  favicon_bounds_.height());
  cairo_clip(cr);

  // The tab is rendered into a windowless widget whose offset is at the
  // coordinate event->area.  Translate by these offsets so we can render at
  // (0,0) to match Windows' rendering metrics.
  cairo_matrix_t cairo_matrix;
  cairo_matrix_init_translate(&cairo_matrix, x(), y());
  cairo_set_matrix(cr, &cairo_matrix);

  // Which background should we be painting?
  int theme_id;
  int offset_y = 0;
  if (IsActive()) {
    theme_id = IDR_THEME_TOOLBAR;
  } else {
    theme_id = data_.incognito ? IDR_THEME_TAB_BACKGROUND_INCOGNITO :
               IDR_THEME_TAB_BACKGROUND;

    if (!theme_service_->HasCustomImage(theme_id))
      offset_y = background_offset_y_;
  }

  // Paint the background behind the favicon.
  const gfx::Image tab_bg = theme_service_->GetImageNamed(theme_id);
  tab_bg.ToCairo()->SetSource(cr, widget, -x(), -offset_y);
  cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_REPEAT);
  cairo_rectangle(cr,
                  favicon_bounds_.x(), favicon_bounds_.y(),
                  favicon_bounds_.width(), favicon_bounds_.height());
  cairo_fill(cr);

  if (!IsActive()) {
    double throb_value = GetThrobValue();
    if (throb_value > 0) {
      cairo_push_group(cr);
      gfx::Image active_bg =
          theme_service_->GetImageNamed(IDR_THEME_TOOLBAR);
      active_bg.ToCairo()->SetSource(cr, widget, -x(), 0);
      cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_REPEAT);

      cairo_rectangle(cr,
                      favicon_bounds_.x(), favicon_bounds_.y(),
                      favicon_bounds_.width(), favicon_bounds_.height());
      cairo_fill(cr);

      cairo_pop_group_to_source(cr);
      cairo_paint_with_alpha(cr, throb_value);
    }
  }

  PaintIcon(widget, cr);
}

void TabRendererGtk::MaybeAdjustLeftForMiniTab(gfx::Rect* icon_bounds) const {
  if (!(mini() || data_.animating_mini_change) ||
      bounds_.width() >= kMiniTabRendererAsNormalTabWidth)
    return;
  const int mini_delta = kMiniTabRendererAsNormalTabWidth - GetMiniWidth();
  const int ideal_delta = bounds_.width() - GetMiniWidth();
  const int ideal_x = (GetMiniWidth() - icon_bounds->width()) / 2;
  icon_bounds->set_x(icon_bounds->x() + static_cast<int>(
      (1 - static_cast<float>(ideal_delta) / static_cast<float>(mini_delta)) *
      (ideal_x - icon_bounds->x())));
}

// static
gfx::Size TabRendererGtk::GetMinimumUnselectedSize() {
  InitResources();

  gfx::Size minimum_size;
  minimum_size.set_width(kLeftPadding + kRightPadding);
  // Since we use bitmap images, the real minimum height of the image is
  // defined most accurately by the height of the end cap images.
  minimum_size.set_height(tab_active_l_height_ - kToolbarOverlap);
  return minimum_size;
}

// static
gfx::Size TabRendererGtk::GetMinimumSelectedSize() {
  gfx::Size minimum_size = GetMinimumUnselectedSize();
  minimum_size.set_width(kLeftPadding + gfx::kFaviconSize + kRightPadding);
  return minimum_size;
}

// static
gfx::Size TabRendererGtk::GetStandardSize() {
  gfx::Size standard_size = GetMinimumUnselectedSize();
  standard_size.Enlarge(kFaviconTitleSpacing + kStandardTitleWidth, 0);
  return standard_size;
}

// static
int TabRendererGtk::GetMiniWidth() {
  return browser_defaults::kMiniTabWidth;
}

// static
int TabRendererGtk::GetContentHeight() {
  // The height of the content of the Tab is the largest of the favicon,
  // the title text and the close button graphic.
  int content_height = std::max(gfx::kFaviconSize, title_font_height_);
  return std::max(content_height, close_button_height_);
}

gfx::Rect TabRendererGtk::GetNonMirroredBounds(GtkWidget* parent) const {
  // The tabstrip widget is a windowless widget so the tab widget's allocation
  // is relative to the browser titlebar.  We need the bounds relative to the
  // tabstrip.
  gfx::Rect bounds = GetWidgetBoundsRelativeToParent(parent, widget());
  bounds.set_x(gtk_util::MirroredLeftPointForRect(parent, bounds));
  return bounds;
}

gfx::Rect TabRendererGtk::GetRequisition() const {
  return gfx::Rect(requisition_.x(), requisition_.y(),
                   requisition_.width(), requisition_.height());
}

void TabRendererGtk::StartMiniTabTitleAnimation() {
  if (!mini_title_animation_.get()) {
    mini_title_animation_.reset(new gfx::ThrobAnimation(this));
    mini_title_animation_->SetThrobDuration(kMiniTitleChangeThrobDuration);
  }

  if (!mini_title_animation_->is_animating())
    mini_title_animation_->StartThrobbing(-1);
}

void TabRendererGtk::StopMiniTabTitleAnimation() {
  if (mini_title_animation_.get())
    mini_title_animation_->Stop();
}

void TabRendererGtk::SetBounds(const gfx::Rect& bounds) {
  requisition_ = bounds;
  gtk_widget_set_size_request(tab_.get(), bounds.width(), bounds.height());
}

////////////////////////////////////////////////////////////////////////////////
// TabRendererGtk, protected:

void TabRendererGtk::Raise() const {
  if (gtk_button_get_event_window(GTK_BUTTON(close_button_->widget())))
    gdk_window_raise(gtk_button_get_event_window(
        GTK_BUTTON(close_button_->widget())));
}

base::string16 TabRendererGtk::GetTitle() const {
  return data_.title;
}

///////////////////////////////////////////////////////////////////////////////
// TabRendererGtk, gfx::AnimationDelegate implementation:

void TabRendererGtk::AnimationProgressed(const gfx::Animation* animation) {
  gtk_widget_queue_draw(tab_.get());
}

void TabRendererGtk::AnimationCanceled(const gfx::Animation* animation) {
  if (media_indicator_animation_ == animation)
    animating_media_state_ = data_.media_state;
  AnimationEnded(animation);
}

void TabRendererGtk::AnimationEnded(const gfx::Animation* animation) {
  if (media_indicator_animation_ == animation)
    animating_media_state_ = data_.media_state;
  gtk_widget_queue_draw(tab_.get());
}

////////////////////////////////////////////////////////////////////////////////
// TabRendererGtk, private:

void TabRendererGtk::StartCrashAnimation() {
  if (!crash_animation_.get())
    crash_animation_.reset(new FaviconCrashAnimation(this));
  crash_animation_->Stop();
  crash_animation_->Start();
}

void TabRendererGtk::StopCrashAnimation() {
  if (!crash_animation_.get())
    return;
  crash_animation_->Stop();
}

bool TabRendererGtk::IsPerformingCrashAnimation() const {
  return crash_animation_.get() && crash_animation_->is_animating();
}

void TabRendererGtk::StartMediaIndicatorAnimation() {
  media_indicator_animation_ =
      chrome::CreateTabMediaIndicatorFadeAnimation(data_.media_state);
  media_indicator_animation_->set_delegate(this);
  media_indicator_animation_->Start();
}

void TabRendererGtk::SetFaviconHidingOffset(int offset) {
  favicon_hiding_offset_ = offset;
  SchedulePaint();
}

void TabRendererGtk::DisplayCrashedFavicon() {
  should_display_crashed_favicon_ = true;
}

void TabRendererGtk::ResetCrashedFavicon() {
  should_display_crashed_favicon_ = false;
}

void TabRendererGtk::Paint(GtkWidget* widget, cairo_t* cr) {
  // Don't paint if we're narrower than we can render correctly. (This should
  // only happen during animations).
  if (width() < GetMinimumUnselectedSize().width() && !mini())
    return;

  // See if the model changes whether the icons should be painted.
  const bool show_icon = ShouldShowIcon();
  const bool show_media_indicator = ShouldShowMediaIndicator();
  const bool show_close_button = ShouldShowCloseBox();
  if (show_icon != showing_icon_ ||
      show_media_indicator != showing_media_indicator_ ||
      show_close_button != showing_close_button_)
    Layout();

  PaintTabBackground(widget, cr);

  if (!mini() || width() > kMiniTabRendererAsNormalTabWidth)
    PaintTitle(widget, cr);

  if (show_icon)
    PaintIcon(widget, cr);

  if (show_media_indicator)
    PaintMediaIndicator(widget, cr);
}

cairo_surface_t* TabRendererGtk::PaintToSurface(GtkWidget* widget,
                                                cairo_t* cr) {
  cairo_surface_t* target = cairo_get_target(cr);
  cairo_surface_t* out_surface = cairo_surface_create_similar(
      target,
      CAIRO_CONTENT_COLOR_ALPHA,
      width(), height());

  cairo_t* out_cr = cairo_create(out_surface);
  Paint(widget, out_cr);
  cairo_destroy(out_cr);

  return out_surface;
}

void TabRendererGtk::SchedulePaint() {
  gtk_widget_queue_draw(tab_.get());
}

gfx::Rect TabRendererGtk::GetLocalBounds() {
  return gfx::Rect(0, 0, bounds_.width(), bounds_.height());
}

void TabRendererGtk::Layout() {
  gfx::Rect local_bounds = GetLocalBounds();
  if (local_bounds.IsEmpty())
    return;
  local_bounds.Inset(kLeftPadding, kTopPadding, kRightPadding, kBottomPadding);

  // Figure out who is tallest.
  int content_height = GetContentHeight();

  // Size the Favicon.
  showing_icon_ = ShouldShowIcon();
  if (showing_icon_) {
    int favicon_top = kTopPadding + (content_height - gfx::kFaviconSize) / 2;
    favicon_bounds_.SetRect(local_bounds.x(), favicon_top,
                            gfx::kFaviconSize, gfx::kFaviconSize);
    MaybeAdjustLeftForMiniTab(&favicon_bounds_);
  } else {
    favicon_bounds_.SetRect(local_bounds.x(), local_bounds.y(), 0, 0);
  }

  // Size the Close button.
  showing_close_button_ = ShouldShowCloseBox();
  if (showing_close_button_) {
    int close_button_top = kTopPadding +
        (content_height - close_button_height_) / 2;
    int close_button_left =
        local_bounds.right() - close_button_width_ + kCloseButtonHorzFuzz;
    close_button_bounds_.SetRect(close_button_left,
                                 close_button_top,
                                 close_button_width_,
                                 close_button_height_);

    // If the close button color has changed, generate a new one.
    if (theme_service_) {
      SkColor tab_text_color =
          theme_service_->GetColor(ThemeProperties::COLOR_TAB_TEXT);
      if (!close_button_color_ || tab_text_color != close_button_color_) {
        close_button_color_ = tab_text_color;
        ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
        close_button_->SetBackground(close_button_color_,
            rb.GetImageNamed(IDR_CLOSE_1).AsBitmap(),
            rb.GetImageNamed(IDR_CLOSE_1_MASK).AsBitmap());
      }
    }
  } else {
    close_button_bounds_.SetRect(0, 0, 0, 0);
  }

  showing_media_indicator_ = ShouldShowMediaIndicator();
  if (showing_media_indicator_) {
    const gfx::Image& media_indicator_image =
        chrome::GetTabMediaIndicatorImage(animating_media_state_);
    media_indicator_bounds_.set_width(media_indicator_image.Width());
    media_indicator_bounds_.set_height(media_indicator_image.Height());
    media_indicator_bounds_.set_y(
        kTopPadding + (content_height - media_indicator_bounds_.height()) / 2);
    const int right = showing_close_button_ ?
        close_button_bounds_.x() : local_bounds.right();
    media_indicator_bounds_.set_x(std::max(
        local_bounds.x(), right - media_indicator_bounds_.width()));
    MaybeAdjustLeftForMiniTab(&media_indicator_bounds_);
  } else {
    media_indicator_bounds_.SetRect(local_bounds.x(), local_bounds.y(), 0, 0);
  }

  if (!mini() || width() >= kMiniTabRendererAsNormalTabWidth) {
    // Size the Title text to fill the remaining space.
    int title_left = favicon_bounds_.right() + kFaviconTitleSpacing;
    int title_top = kTopPadding;

    // If the user has big fonts, the title will appear rendered too far down
    // on the y-axis if we use the regular top padding, so we need to adjust it
    // so that the text appears centered.
    gfx::Size minimum_size = GetMinimumUnselectedSize();
    int text_height = title_top + title_font_height_ + kBottomPadding;
    if (text_height > minimum_size.height())
      title_top -= (text_height - minimum_size.height()) / 2;

    int title_width;
    if (showing_media_indicator_) {
      title_width = media_indicator_bounds_.x() - kTitleCloseButtonSpacing -
          title_left;
    } else if (close_button_bounds_.width() && close_button_bounds_.height()) {
      title_width = close_button_bounds_.x() - kTitleCloseButtonSpacing -
          title_left;
    } else {
      title_width = local_bounds.width() - title_left;
    }
    title_width = std::max(title_width, 0);
    title_bounds_.SetRect(title_left, title_top, title_width, content_height);
  }

  favicon_bounds_.set_x(
      gtk_util::MirroredLeftPointForRect(tab_.get(), favicon_bounds_));
  media_indicator_bounds_.set_x(
      gtk_util::MirroredLeftPointForRect(tab_.get(), media_indicator_bounds_));
  close_button_bounds_.set_x(
      gtk_util::MirroredLeftPointForRect(tab_.get(), close_button_bounds_));
  title_bounds_.set_x(
      gtk_util::MirroredLeftPointForRect(tab_.get(), title_bounds_));

  MoveCloseButtonWidget();
}

void TabRendererGtk::MoveCloseButtonWidget() {
  if (!close_button_bounds_.IsEmpty()) {
    gtk_fixed_move(GTK_FIXED(tab_.get()), close_button_->widget(),
                   close_button_bounds_.x(), close_button_bounds_.y());
    gtk_widget_show(close_button_->widget());
  } else {
    gtk_widget_hide(close_button_->widget());
  }
}

void TabRendererGtk::PaintTab(GtkWidget* widget, GdkEventExpose* event) {
  cairo_t* cr = gdk_cairo_create(gtk_widget_get_window(widget));
  gdk_cairo_rectangle(cr, &event->area);
  cairo_clip(cr);

  // The tab is rendered into a windowless widget whose offset is at the
  // coordinate event->area.  Translate by these offsets so we can render at
  // (0,0) to match Windows' rendering metrics.
  cairo_matrix_t cairo_matrix;
  cairo_matrix_init_translate(&cairo_matrix, event->area.x, event->area.y);
  cairo_set_matrix(cr, &cairo_matrix);

  // Save the original x offset so we can position background images properly.
  background_offset_x_ = event->area.x;

  Paint(widget, cr);
  cairo_destroy(cr);
}

void TabRendererGtk::PaintTitle(GtkWidget* widget, cairo_t* cr) {
  if (title_bounds_.IsEmpty())
    return;

  // Paint the Title.
  base::string16 title = data_.title;
  if (title.empty()) {
    title = data_.loading ?
        l10n_util::GetStringUTF16(IDS_TAB_LOADING_TITLE) :
        CoreTabHelper::GetDefaultTitle();
  } else {
    Browser::FormatTitleForDisplay(&title);
  }

  GtkAllocation allocation;
  gtk_widget_get_allocation(widget, &allocation);
  gfx::Rect bounds(allocation);

  // Draw the text directly onto the Cairo context. This is necessary for
  // getting the draw order correct, and automatically applying transformations
  // such as scaling when a tab is detached.
  gfx::CanvasSkiaPaintCairo canvas(cr, bounds.size(), true);

  SkColor title_color = IsSelected() ? selected_title_color_
                                     : unselected_title_color_;

  // Disable subpixel rendering. This does not work because the canvas has a
  // transparent background.
  int flags = gfx::Canvas::NO_ELLIPSIS | gfx::Canvas::NO_SUBPIXEL_RENDERING;
  canvas.DrawFadeTruncatingStringRectWithFlags(
      title, gfx::Canvas::TruncateFadeTail, gfx::FontList(*title_font_),
      title_color, title_bounds_, flags);
}

void TabRendererGtk::PaintIcon(GtkWidget* widget, cairo_t* cr) {
  if (loading_animation_.animation_state() != ANIMATION_NONE) {
    PaintLoadingAnimation(widget, cr);
    return;
  }

  gfx::CairoCachedSurface* to_display = NULL;
  if (should_display_crashed_favicon_) {
    to_display = theme_service_->GetImageNamed(IDR_SAD_FAVICON).ToCairo();
  } else if (!data_.favicon.isNull()) {
    if (data_.is_default_favicon && theme_service_->UsingNativeTheme()) {
      to_display = GtkThemeService::GetDefaultFavicon(true).ToCairo();
    } else if (data_.cairo_favicon.valid()) {
      to_display = &data_.cairo_favicon;
    }
  }

  if (to_display) {
    to_display->SetSource(cr, widget, favicon_bounds_.x(),
                          favicon_bounds_.y() + favicon_hiding_offset_);
    cairo_paint(cr);
  }
}

void TabRendererGtk::PaintMediaIndicator(GtkWidget* widget, cairo_t* cr) {
  if (media_indicator_bounds_.IsEmpty() || !media_indicator_animation_)
    return;

  double opaqueness = media_indicator_animation_->GetCurrentValue();
  if (data_.media_state == TAB_MEDIA_STATE_NONE)
    opaqueness = 1.0 - opaqueness;  // Fading out, not in.

  const gfx::Image& media_indicator_image =
      chrome::GetTabMediaIndicatorImage(animating_media_state_);
  media_indicator_image.ToCairo()->SetSource(
      cr, widget, media_indicator_bounds_.x(), media_indicator_bounds_.y());
  cairo_paint_with_alpha(cr, opaqueness);
}

void TabRendererGtk::PaintTabBackground(GtkWidget* widget, cairo_t* cr) {
  if (IsActive()) {
    PaintActiveTabBackground(widget, cr);
  } else {
    PaintInactiveTabBackground(widget, cr);

    double throb_value = GetThrobValue();
    if (throb_value > 0) {
      cairo_push_group(cr);
      PaintActiveTabBackground(widget, cr);
      cairo_pop_group_to_source(cr);
      cairo_paint_with_alpha(cr, throb_value);
    }
  }
}

void TabRendererGtk::DrawTabBackground(
    cairo_t* cr,
    GtkWidget* widget,
    const gfx::Image& tab_bg,
    int offset_x,
    int offset_y) {
  tab_bg.ToCairo()->SetSource(cr, widget, -offset_x, -offset_y);
  cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_REPEAT);

  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();

  // Draw left edge
  gfx::Image& tab_l_mask = rb.GetNativeImageNamed(IDR_TAB_ALPHA_LEFT);
  tab_l_mask.ToCairo()->MaskSource(cr, widget, 0, 0);

  // Draw center
  cairo_rectangle(cr,
                  tab_active_l_width_, kDropShadowOffset,
                  width() - (2 * tab_active_l_width_),
                  tab_inactive_l_height_);
  cairo_fill(cr);

  // Draw right edge
  gfx::Image& tab_r_mask = rb.GetNativeImageNamed(IDR_TAB_ALPHA_RIGHT);
  tab_r_mask.ToCairo()->MaskSource(cr, widget,
                                    width() - tab_active_l_width_, 0);
}

void TabRendererGtk::DrawTabShadow(cairo_t* cr,
                                   GtkWidget* widget,
                                   int left_idr,
                                   int center_idr,
                                   int right_idr) {
  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  gfx::Image& active_image_l = rb.GetNativeImageNamed(left_idr);
  gfx::Image& active_image_c = rb.GetNativeImageNamed(center_idr);
  gfx::Image& active_image_r = rb.GetNativeImageNamed(right_idr);

  // Draw left drop shadow
  active_image_l.ToCairo()->SetSource(cr, widget, 0, 0);
  cairo_paint(cr);

  // Draw the center shadow
  active_image_c.ToCairo()->SetSource(cr, widget, 0, 0);
  cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_REPEAT);
  cairo_rectangle(cr, tab_active_l_width_, 0,
                  width() - (2 * tab_active_l_width_),
                  height());
  cairo_fill(cr);

  // Draw right drop shadow
  active_image_r.ToCairo()->SetSource(
      cr, widget, width() - active_image_r.ToCairo()->Width(), 0);
  cairo_paint(cr);
}

void TabRendererGtk::PaintInactiveTabBackground(GtkWidget* widget,
                                                cairo_t* cr) {
  int theme_id = data_.incognito ?
      IDR_THEME_TAB_BACKGROUND_INCOGNITO : IDR_THEME_TAB_BACKGROUND;

  gfx::Image tab_bg = theme_service_->GetImageNamed(theme_id);

  // If the theme is providing a custom background image, then its top edge
  // should be at the top of the tab. Otherwise, we assume that the background
  // image is a composited foreground + frame image.
  int offset_y = theme_service_->HasCustomImage(theme_id) ?
      0 : background_offset_y_;

  DrawTabBackground(cr, widget, tab_bg, background_offset_x_, offset_y);

  DrawTabShadow(cr, widget, IDR_TAB_INACTIVE_LEFT, IDR_TAB_INACTIVE_CENTER,
                IDR_TAB_INACTIVE_RIGHT);
}

void TabRendererGtk::PaintActiveTabBackground(GtkWidget* widget,
                                              cairo_t* cr) {
  gfx::Image tab_bg = theme_service_->GetImageNamed(IDR_THEME_TOOLBAR);

  DrawTabBackground(cr, widget, tab_bg, background_offset_x_, 0);
  DrawTabShadow(cr, widget, IDR_TAB_ACTIVE_LEFT, IDR_TAB_ACTIVE_CENTER,
                IDR_TAB_ACTIVE_RIGHT);
}

void TabRendererGtk::PaintLoadingAnimation(GtkWidget* widget,
                                           cairo_t* cr) {
  int id = loading_animation_.animation_state() == ANIMATION_WAITING ?
           IDR_THROBBER_WAITING : IDR_THROBBER;
  gfx::Image throbber = theme_service_->GetImageNamed(id);

  const int image_size = throbber.ToCairo()->Height();
  const int image_offset = loading_animation_.animation_frame() * image_size;
  DCHECK(image_size == favicon_bounds_.height());
  DCHECK(image_size == favicon_bounds_.width());

  throbber.ToCairo()->SetSource(cr, widget, favicon_bounds_.x() - image_offset,
                                 favicon_bounds_.y());
  cairo_rectangle(cr, favicon_bounds_.x(), favicon_bounds_.y(),
                  image_size, image_size);
  cairo_fill(cr);
}

int TabRendererGtk::IconCapacity() const {
  if (height() < GetMinimumUnselectedSize().height())
    return 0;
  const int available_width =
      std::max(0, width() - kLeftPadding - kRightPadding);
  const int kPaddingBetweenIcons = 2;
  if (available_width >= gfx::kFaviconSize &&
      available_width < (gfx::kFaviconSize + kPaddingBetweenIcons)) {
    return 1;
  }
  return available_width / (gfx::kFaviconSize + kPaddingBetweenIcons);
}

bool TabRendererGtk::ShouldShowIcon() const {
  return chrome::ShouldTabShowFavicon(
      IconCapacity(), mini(), IsActive(), data_.show_icon,
      animating_media_state_);
}

bool TabRendererGtk::ShouldShowMediaIndicator() const {
  return chrome::ShouldTabShowMediaIndicator(
      IconCapacity(), mini(), IsActive(), data_.show_icon,
      animating_media_state_);
}

bool TabRendererGtk::ShouldShowCloseBox() const {
  return chrome::ShouldTabShowCloseButton(IconCapacity(), mini(), IsActive());
}

CustomDrawButton* TabRendererGtk::MakeCloseButton() {
  CustomDrawButton* button = CustomDrawButton::CloseButtonBar(theme_service_);
  button->ForceChromeTheme();

  g_signal_connect(button->widget(), "clicked",
                   G_CALLBACK(OnCloseButtonClickedThunk), this);
  g_signal_connect(button->widget(), "button-release-event",
                   G_CALLBACK(OnCloseButtonMouseReleaseThunk), this);
  g_signal_connect(button->widget(), "enter-notify-event",
                   G_CALLBACK(OnEnterNotifyEventThunk), this);
  g_signal_connect(button->widget(), "leave-notify-event",
                   G_CALLBACK(OnLeaveNotifyEventThunk), this);
  gtk_widget_set_can_focus(button->widget(), FALSE);
  gtk_fixed_put(GTK_FIXED(tab_.get()), button->widget(), 0, 0);

  return button;
}

double TabRendererGtk::GetThrobValue() {
  bool is_selected = IsSelected();
  double min = is_selected ? kSelectedTabOpacity : 0;
  double scale = is_selected ? kSelectedTabThrobScale : 1;

  if (mini_title_animation_.get() && mini_title_animation_->is_animating()) {
    return mini_title_animation_->GetCurrentValue() *
        kMiniTitleChangeThrobOpacity * scale + min;
  }

  if (hover_animation_.get())
    return kHoverOpacity * hover_animation_->GetCurrentValue() * scale + min;

  return is_selected ? kSelectedTabOpacity : 0;
}

void TabRendererGtk::CloseButtonClicked() {
  // Nothing to do.
}

void TabRendererGtk::OnCloseButtonClicked(GtkWidget* widget) {
  CloseButtonClicked();
}

gboolean TabRendererGtk::OnCloseButtonMouseRelease(GtkWidget* widget,
                                                   GdkEventButton* event) {
  if (event->button == 2) {
    CloseButtonClicked();
    return TRUE;
  }

  return FALSE;
}

gboolean TabRendererGtk::OnExposeEvent(GtkWidget* widget,
                                       GdkEventExpose* event) {
  TRACE_EVENT0("ui::gtk", "TabRendererGtk::OnExposeEvent");

  PaintTab(widget, event);
  gtk_container_propagate_expose(GTK_CONTAINER(tab_.get()),
                                 close_button_->widget(), event);
  return TRUE;
}

void TabRendererGtk::OnSizeAllocate(GtkWidget* widget,
                                    GtkAllocation* allocation) {
  gfx::Rect bounds = gfx::Rect(allocation->x, allocation->y,
                               allocation->width, allocation->height);

  // Nothing to do if the bounds are the same.  If we don't catch this, we'll
  // get an infinite loop of size-allocate signals.
  if (bounds_ == bounds)
    return;

  bounds_ = bounds;
  Layout();
}

gboolean TabRendererGtk::OnEnterNotifyEvent(GtkWidget* widget,
                                            GdkEventCrossing* event) {
  hover_animation_->SetTweenType(gfx::Tween::EASE_OUT);
  hover_animation_->Show();
  return FALSE;
}

gboolean TabRendererGtk::OnLeaveNotifyEvent(GtkWidget* widget,
                                            GdkEventCrossing* event) {
  hover_animation_->SetTweenType(gfx::Tween::EASE_IN);
  hover_animation_->Hide();
  return FALSE;
}

// static
void TabRendererGtk::InitResources() {
  if (initialized_)
    return;

  // Grab the pixel sizes of our masking images.
  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  GdkPixbuf* tab_active_l = rb.GetNativeImageNamed(
      IDR_TAB_ACTIVE_LEFT).ToGdkPixbuf();
  tab_active_l_width_ = gdk_pixbuf_get_width(tab_active_l);
  tab_active_l_height_ = gdk_pixbuf_get_height(tab_active_l);

  GdkPixbuf* tab_inactive_l = rb.GetNativeImageNamed(
      IDR_TAB_INACTIVE_LEFT).ToGdkPixbuf();
  tab_inactive_l_height_ = gdk_pixbuf_get_height(tab_inactive_l);

  GdkPixbuf* tab_close = rb.GetNativeImageNamed(IDR_CLOSE_1).ToGdkPixbuf();
  close_button_width_ = gdk_pixbuf_get_width(tab_close);
  close_button_height_ = gdk_pixbuf_get_height(tab_close);

  const gfx::Font& base_font = rb.GetFont(ui::ResourceBundle::BaseFont);
  title_font_ = new gfx::Font(base_font.GetFontName(), kFontPixelSize);
  title_font_height_ = title_font_->GetHeight();

  initialized_ = true;
}
