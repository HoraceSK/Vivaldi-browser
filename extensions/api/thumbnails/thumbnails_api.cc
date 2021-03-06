//
// Copyright (c) 2015 Vivaldi Technologies AS. All rights reserved.
//

#include "extensions/api/thumbnails/thumbnails_api.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "app/vivaldi_apptools.h"
#include "base/base64.h"
#include "base/bind.h"
#include "base/files/file_util.h"
#include "base/guid.h"
#include "base/lazy_instance.h"
#include "base/memory/ptr_util.h"
#include "base/path_service.h"
#include "base/strings/stringprintf.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/post_task.h"
#include "base/time/time.h"
#include "base/values.h"
#include "browser/thumbnails/thumbnail_capture_contents.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/common/chrome_paths.h"
#include "content/browser/renderer_host/render_view_host_delegate_view.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/web_contents.h"
#include "extensions/common/api/extension_types.h"
#include "extensions/common/error_utils.h"
#include "extensions/schema/thumbnails.h"
#include "renderer/vivaldi_render_messages.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/display/screen.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/geometry/dip_util.h"
#include "ui/vivaldi_browser_window.h"
#include "ui/vivaldi_skia_utils.h"
#include "ui/vivaldi_ui_utils.h"
#include "vivaldi/prefs/vivaldi_gen_prefs.h"

using content::BrowserThread;
using content::RenderWidgetHost;
using content::RenderWidgetHostView;
using content::WebContents;
using vivaldi::skia_utils::SmartCropAndSize;
using vivaldi::ui_tools::EncodeBitmap;

namespace {

const int kMaximumPageHeight = 30000;
const int kMaximumFilenameLength = 100;

}  // namespace

namespace extensions {

/*
Patterns supported:

$timestamp - Long date in YYY-MM-DD HH.MM.SS format
$year - Year in YYYY format
$month - Month in MM format
$day - Day in DD format
$hour - Hour in HH format
$minute - Minute in MM format
$second - Second in SS format
$ms - Millisecond in MMM format
$longid - GUID in standard format
$shortid - Short GUID, only the last 12 characters
$host - Hostname of the active tab, eg. www.vivaldi.com

*/

enum class CaptureFilePatternType {
  TIMESTAMP,
  YEAR,
  MONTH,
  DAY,
  HOUR,
  MINUTE,
  SECOND,
  MILLISECOND,
  LONGID,
  SHORTID,
  HOST,
  TITLE,
};

struct CaptureFilePattern {
  const char* pattern;
  CaptureFilePatternType pattern_id;
};

struct CaptureFilePattern file_patterns[] = {
  {"$timestamp", CaptureFilePatternType::TIMESTAMP},
  {"$year",     CaptureFilePatternType::YEAR},
  {"$month",    CaptureFilePatternType::MONTH},
  {"$day",      CaptureFilePatternType::DAY},
  {"$hour",     CaptureFilePatternType::HOUR},
  {"$minute",   CaptureFilePatternType::MINUTE},
  {"$second",   CaptureFilePatternType::SECOND},
  {"$ms",       CaptureFilePatternType::MILLISECOND},
  {"$longid",   CaptureFilePatternType::LONGID},
  {"$shortid",  CaptureFilePatternType::SHORTID},
  {"$host",     CaptureFilePatternType::HOST},
  {"$title",    CaptureFilePatternType::TITLE},
};

std::string ConstructCaptureArgument(CaptureFilePatternType type,
                                     const GURL& url,
                                     const base::Time::Exploded& now,
                                     const std::string& title) {
  switch (type) {
    case CaptureFilePatternType::TIMESTAMP:
      return base::StringPrintf("%d-%02d-%02d %02d.%02d.%02d", now.year,
                                now.month, now.day_of_month, now.hour,
                                now.minute, now.second);
    case CaptureFilePatternType::YEAR:
      return base::StringPrintf("%d", now.year);
    case CaptureFilePatternType::MONTH:
      return base::StringPrintf("%02d", now.month);
    case CaptureFilePatternType::DAY:
      return base::StringPrintf("%02d", now.day_of_month);
    case CaptureFilePatternType::HOUR:
      return base::StringPrintf("%02d", now.hour);
    case CaptureFilePatternType::MINUTE:
      return base::StringPrintf("%02d", now.minute);
    case CaptureFilePatternType::SECOND:
      return base::StringPrintf("%02d", now.second);
    case CaptureFilePatternType::MILLISECOND:
      return base::StringPrintf("%02d", now.millisecond);
    case CaptureFilePatternType::LONGID:
      return base::GenerateGUID();
    case CaptureFilePatternType::SHORTID: {
      std::string short_id = base::GenerateGUID();
      short_id.erase(0, short_id.length() - 12);
      return short_id;
    }
    case CaptureFilePatternType::HOST: {
      std::string host = url.host();
      // Special case the vivaldi app id.
      if (::vivaldi::IsVivaldiApp(host)) {
        host = "vivaldi";
      }
      return host;
    }
    case CaptureFilePatternType::TITLE: {
      return title;
    }
    default:
      break;
  }
  return std::string();
}

base::FilePath ConstructCaptureFilename(
    base::FilePath base_path,
    const std::string& pattern,
    const GURL& url,
    const std::string& title,
    const base::FilePath::StringPieceType& extension) {
  if (pattern.empty()) {
    base_path = base_path.AppendASCII(base::GenerateGUID());
  } else {
    // Function might replace in-place
    std::string new_string = pattern;
    base::Time::Exploded now;
    base::Time::Now().LocalExplode(&now);

    for (CaptureFilePattern& pattern : file_patterns) {
      base::ReplaceSubstringsAfterOffset(
          &new_string, 0, pattern.pattern,
          ConstructCaptureArgument(pattern.pattern_id, url, now, title));
    }
    // Strip invalid characters from the generated filename.
    // Windows is the strictest OS so we use the list from
    // https://docs.microsoft.com/en-us/windows/desktop/FileIO/naming-a-file
    // This is not performance sensitive code so brute force it.
    std::string special_file_chars = "\\/?%*:|\"<>\n\r";
    for (int i = 0; i < 32; i++) {
      special_file_chars += static_cast<char>(i);
    }

    base::ReplaceChars(new_string, special_file_chars, "_", &new_string);

    // Trim the maximum filename length.
    if (new_string.length() > kMaximumFilenameLength) {
      std::string truncated;
      base::TruncateUTF8ToByteSize(new_string, kMaximumFilenameLength,
                                   &truncated);
      new_string = truncated;
    }

#if defined(OS_POSIX)
    base_path = base_path.Append(new_string);
#elif defined(OS_WIN)
    base_path = base_path.Append(base::UTF8ToUTF16(new_string));
#endif
    base_path = base_path.AddExtension(extension);
  }
  // Ensure unique filename.
  int unique_number = base::GetUniquePathNumber(base_path);
  if (unique_number > 0) {
    base_path = base_path.InsertBeforeExtensionASCII(
      base::StringPrintf(" (%d)", unique_number));
  }
  return base_path;
}

bool SaveBitmapOnWorkerThread(SkBitmap bitmap,
                              const CaptureFormat& format,
                              std::string* image_data,
                              base::FilePath* file_path) {
  DCHECK(image_data->empty());
  DCHECK(file_path->empty());
  std::vector<unsigned char> data;
  std::string mime_type;
  bool encoded = EncodeBitmap(bitmap, &data, &mime_type, format.image_format,
                              format.encode_quality);
  if (!encoded)
    return false;
  if (!format.save_to_disk) {
    // If the base path is not set, we want to encode the image as a data url.
    base::StringPiece base64_input(reinterpret_cast<const char*>(&data[0]),
                                   data.size());
    Base64Encode(base64_input, image_data);
    image_data->insert(
        0, base::StringPrintf("data:%s;base64,", mime_type.c_str()));
    return true;
  }
  base::FilePath path = base::FilePath::FromUTF8Unsafe(format.save_folder);
  if (!base::PathExists(path)) {
    base::CreateDirectory(path);
  }
  base::FilePath::StringPieceType ext = FILE_PATH_LITERAL(".jpg");
  if (format.image_format == ImageFormat::IMAGE_FORMAT_PNG) {
    ext = FILE_PATH_LITERAL(".png");
  }
  path = ConstructCaptureFilename(std::move(path), format.save_file_pattern,
                                  format.url, format.title, ext);
  int nwritten = base::WriteFile(path, reinterpret_cast<const char*>(&data[0]),
                                 static_cast<int>(data.size()));
  if (nwritten < 0 || static_cast<size_t>(nwritten) != data.size()) {
    LOG(ERROR) << "Failed to write to " << path;
    return false;
  }
  *file_path = std::move(path);
  return true;
}

namespace {

void OnCopyFromBackingStoreComplete(float device_scale_factor,
                                    UICaptureCallback callback,
                                    const SkBitmap& bitmap) {
  bool success = !bitmap.drawsNothing();
  if (!success) {
    LOG(ERROR) << "empty bitmap was returned";
  }
  std::move(callback).Run(success, device_scale_factor, bitmap);
}

}  // namespace

void StartUICapture(content::WebContents* web_contents,
                    float x,
                    float y,
                    float width,
                    float height,
                    UICaptureCallback callback) {
  RenderWidgetHostView* view = nullptr;
  if (!web_contents) {
    LOG(ERROR) << "WebContents is null";
  } else {
    view = web_contents->GetRenderWidgetHostView();
    if (!view || !view->GetRenderWidgetHost()) {
      LOG(ERROR) << "View is invisible";
      view = nullptr;
    }
  }
  if (!view) {
    std::move(callback).Run(false, 0.0f, SkBitmap());
    return;
  }

  gfx::RectF rect_f(x, y, width, height);
  ::vivaldi::FromUICoordinates(web_contents, &rect_f);

  // CopyFromSurface takes the area in device-independent pixels. However, we
  // want to capture all physical pixels. So scale the bitmap size accordingly.
  gfx::SizeF size_f = rect_f.size();
  const gfx::NativeView native_view = view->GetNativeView();
  display::Screen* const screen = display::Screen::GetScreen();
  const float device_scale_factor =
      screen->GetDisplayNearestView(native_view).device_scale_factor();
  size_f.Scale(device_scale_factor);

  gfx::Rect capture_area(std::round(rect_f.x()), std::round(rect_f.y()),
                         std::round(rect_f.width()),
                         std::round(rect_f.height()));
  gfx::Size bitmap_size = gfx::ToRoundedSize(size_f);
  view->CopyFromSurface(
      capture_area, bitmap_size,
      base::BindOnce(&OnCopyFromBackingStoreComplete, device_scale_factor,
                     std::move(callback)));
}

CaptureFormat::CaptureFormat() = default;
CaptureFormat::~CaptureFormat() = default;

ThumbnailsCaptureUIFunction::ThumbnailsCaptureUIFunction() = default;
ThumbnailsCaptureUIFunction::~ThumbnailsCaptureUIFunction() = default;

ExtensionFunction::ResponseAction ThumbnailsCaptureUIFunction::Run() {
  using vivaldi::thumbnails::CaptureUI::Params;

  std::unique_ptr<Params> params = Params::Create(*args_);
  EXTENSION_FUNCTION_VALIDATE(params);

  if (params->params.save_file_pattern) {
    format_.save_file_pattern = *params->params.save_file_pattern;
  }
  if (params->params.encode_format.get()) {
    format_.image_format = *params->params.encode_format == "jpg"
      ? ImageFormat::IMAGE_FORMAT_JPEG
      : ImageFormat::IMAGE_FORMAT_PNG;
  }
  if (params->params.encode_quality.get()) {
    format_.encode_quality = *params->params.encode_quality.get();
  }
  if (params->params.save_to_disk.get()) {
    format_.save_to_disk = *params->params.save_to_disk;
  }
  if (format_.save_to_disk) {
    if (params->params.show_file_in_path) {
      format_.show_file_in_path = *params->params.show_file_in_path;
    }
    Profile* profile = Profile::FromBrowserContext(browser_context());
    format_.save_folder =
        profile->GetPrefs()->GetString(vivaldiprefs::kWebpagesCaptureDirectory);
  }
  if (params->params.copy_to_clipboard.get()) {
    format_.copy_to_clipboard = *params->params.copy_to_clipboard;
  }

  content::WebContents* web_contents = GetSenderWebContents();
  Browser* browser = ::vivaldi::FindBrowserForEmbedderWebContents(web_contents);
  if (!browser)
    return RespondNow(Error("Missing Browser"));
  content::WebContents* tab =
      browser->tab_strip_model()->GetActiveWebContents();
  if (tab) {
    format_.url = tab->GetVisibleURL();
    format_.title = base::UTF16ToUTF8(tab->GetTitle());
  }

  StartUICapture(
      web_contents, params->params.pos_x, params->params.pos_y,
      params->params.width, params->params.height,
      base::BindOnce(&ThumbnailsCaptureUIFunction::OnCaptureDone, this));
  return did_respond() ? AlreadyResponded() : RespondLater();
}

void ThumbnailsCaptureUIFunction::OnCaptureDone(bool success,
                                                float device_scale_factor,
                                                const SkBitmap& bitmap) {
  if (!success) {
    SendResult(false);
    return;
  }

  // TODO(igor@vivaldi.com): Consider using device_scale_factor to embed
  // DPI comments into the resulting image.

  if (format_.copy_to_clipboard) {
    // Ignore everything else, we copy it raw to the clipboard.
    ui::ScopedClipboardWriter scw(ui::ClipboardBuffer::kCopyPaste);
    scw.Reset();
    scw.WriteImage(bitmap);
    SendResult(true);
    return;
  }

  base::PostTask(
      FROM_HERE,
      {base::ThreadPool(), base::TaskPriority::USER_VISIBLE, base::MayBlock(),
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&ThumbnailsCaptureUIFunction::ProcessImageOnWorkerThread,
                     this, bitmap));
}

void ThumbnailsCaptureUIFunction::ProcessImageOnWorkerThread(
    SkBitmap bitmap) {
  bool success = SaveBitmapOnWorkerThread(std::move(bitmap), format_,
                                          &image_data_, &file_path_);
  base::PostTask(
      FROM_HERE, {BrowserThread::UI},
      base::BindOnce(&ThumbnailsCaptureUIFunction::SendResult, this, success));
}

void ThumbnailsCaptureUIFunction::SendResult(bool success) {
  namespace Results = vivaldi::thumbnails::CaptureUI::Results;

  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!success) {
    LOG(ERROR) << "Failed to capture UI "
               << format_.url.possibly_invalid_spec();
  }
  Respond(ArgumentList(Results::Create(success, image_data_)));

  if (success && format_.show_file_in_path && !format_.copy_to_clipboard) {
    Profile* profile = Profile::FromBrowserContext(browser_context());
    platform_util::ShowItemInFolder(profile, file_path_);
  }
}

ThumbnailsCaptureTabFunction::ThumbnailsCaptureTabFunction() = default;

ThumbnailsCaptureTabFunction::~ThumbnailsCaptureTabFunction() = default;

ExtensionFunction::ResponseAction ThumbnailsCaptureTabFunction::Run() {
  using vivaldi::thumbnails::CaptureTab::Params;

  std::unique_ptr<Params> params = Params::Create(*args_);
  EXTENSION_FUNCTION_VALIDATE(params);

  if (params->params.save_file_pattern) {
    format_.save_file_pattern = *params->params.save_file_pattern;
  }
  if (params->params.encode_format) {
    format_.image_format = *params->params.encode_format == "jpg"
                               ? ImageFormat::IMAGE_FORMAT_JPEG
                               : ImageFormat::IMAGE_FORMAT_PNG;
  }
  if (params->params.encode_quality) {
    format_.encode_quality = *params->params.encode_quality;
  }
  bool capture_full_page = false;
  if (params->params.full_page) {
    capture_full_page = *params->params.full_page;
  }
  gfx::Rect rect;
  if (params->params.rect) {
    rect.set_x(params->params.rect->left);
    rect.set_y(params->params.rect->top);
    rect.set_width(params->params.rect->width);
    rect.set_height(params->params.rect->height);
  }
  // This is scale out size.
  gfx::Size out_dimension;
  if (params->params.height) {
    out_dimension.set_height(*params->params.height);
  }
  if (params->params.width) {
    out_dimension.set_width(*params->params.width);
  }
  if (params->params.save_to_disk) {
    format_.save_to_disk = *params->params.save_to_disk;
  }
  // If full-page capture and no crop-rect set a default height.
  if (capture_full_page && rect.height() == 0) {
    rect.set_height(kMaximumPageHeight);
  }
  // Sanitize the user input.
  rect.set_height(std::min(kMaximumPageHeight, rect.height()));
  if (format_.save_to_disk) {
    if (params->params.show_file_in_path) {
      format_.show_file_in_path = *params->params.show_file_in_path;
    }
    Profile* profile = Profile::FromBrowserContext(browser_context());
    format_.save_folder =
        profile->GetPrefs()->GetString(vivaldiprefs::kWebpagesCaptureDirectory);
  }
  if (params->params.copy_to_clipboard) {
    format_.copy_to_clipboard = *params->params.copy_to_clipboard;
  }
  if (capture_full_page && !out_dimension.IsEmpty()) {
    return RespondNow(
        Error("width or height must not be given with full_page"));
  }

  int tab_id = params->tab_id;
  content::WebContents* tabstrip_contents =
      ::vivaldi::ui_tools::GetWebContentsFromTabStrip(tab_id,
                                                      browser_context());
  if (!tabstrip_contents)
    return RespondNow(Error("No such tab - " + std::to_string(tab_id)));

  format_.url = tabstrip_contents->GetVisibleURL();
  format_.title = base::UTF16ToUTF8(tabstrip_contents->GetTitle());

  ::vivaldi::CapturePage::CaptureParams capture_params;
  capture_params.full_page = capture_full_page;

// Need to transform the rect to pixelsize from device-size.
  double scale = 1.0f;
  display::Screen* screen = display::Screen::GetScreen();
  if (screen) {
    scale = screen->GetDisplayNearestPoint(gfx::Point(rect.x(), rect.y()))
                .device_scale_factor();
  }
  capture_params.rect = gfx::ConvertRectToPixel(scale, rect);
  capture_params.target_size = out_dimension;

  ::vivaldi::CapturePage::Capture(
      tabstrip_contents, capture_params,
      base::BindOnce(
          &ThumbnailsCaptureTabFunction::OnThumbnailsCaptureCompleted,
          this));
  return RespondLater();
}

void ThumbnailsCaptureTabFunction::OnThumbnailsCaptureCompleted(
    ::vivaldi::CapturePage::Result captured) {
  base::PostTask(
      FROM_HERE,
      {base::ThreadPool(), base::TaskPriority::USER_VISIBLE, base::MayBlock(),
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&ThumbnailsCaptureTabFunction::ConvertImageOnWorkerThread,
                     this, std::move(captured)));
}

void ThumbnailsCaptureTabFunction::ConvertImageOnWorkerThread(
    ::vivaldi::CapturePage::Result captured) {

  bool success = false;
  do {
    if (!captured.MovePixelsToBitmap(&bitmap_))
      break;
    if (format_.copy_to_clipboard) {
      success = true;
      break;
    }
    success = SaveBitmapOnWorkerThread(std::move(bitmap_), format_,
                                       &image_data_, &file_path_);
  } while (false);

  base::PostTask(
      FROM_HERE, {BrowserThread::UI},
      base::BindOnce(&ThumbnailsCaptureTabFunction::OnImageConverted, this,
                     success));
}

void ThumbnailsCaptureTabFunction::OnImageConverted(bool success) {
  namespace Results = vivaldi::thumbnails::CaptureTab::Results;
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  if (!success) {
    LOG(ERROR) << "Failed to capture tab "
               << format_.url.possibly_invalid_spec();
    Respond(ArgumentList(Results::Create(false, std::string())));
    return;
  }

  if (format_.copy_to_clipboard) {
    // Ignore everything else, we copy it raw to the clipboard.
    ui::ScopedClipboardWriter scw(ui::ClipboardBuffer::kCopyPaste);
    scw.Reset();
    scw.WriteImage(bitmap_);
  }
  Respond(ArgumentList(Results::Create(true, image_data_)));

  if (format_.show_file_in_path && !format_.copy_to_clipboard) {
    Profile* profile = Profile::FromBrowserContext(browser_context());
    platform_util::ShowItemInFolder(profile, file_path_);
  }
}

ThumbnailsCaptureUrlFunction::ThumbnailsCaptureUrlFunction() = default;

ThumbnailsCaptureUrlFunction::~ThumbnailsCaptureUrlFunction() = default;

ExtensionFunction::ResponseAction ThumbnailsCaptureUrlFunction::Run() {
  using vivaldi::thumbnails::CaptureUrl::Params;

  std::unique_ptr<Params> params(Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  if (!base::StringToInt64(params->params.bookmark_id, &bookmark_id_) ||
      bookmark_id_ <= 0) {
    return RespondNow(Error("bookmarkId is not a valid positive integer - " +
                            params->params.bookmark_id));
  }
  gfx::Size initial_size(params->params.width, params->params.height);
  if (initial_size.IsEmpty())
    return RespondNow(Error("initial width or height are not positive"));
  gfx::Size scaled_size(params->params.scaled_width,
                        params->params.scaled_height);
  if (scaled_size.IsEmpty())
    return RespondNow(Error("scaled width or height are not positive"));
  url_ = GURL(params->params.url);

  ::vivaldi::ThumbnailCaptureContents::Start(
      browser_context(), url_, initial_size, scaled_size,
      base::BindOnce(&ThumbnailsCaptureUrlFunction::OnCaptured, this));

  return RespondLater();
}

void ThumbnailsCaptureUrlFunction::OnCaptured(
    ::vivaldi::CapturePage::Result captured) {
  scoped_refptr<VivaldiDataSourcesAPI> api =
      VivaldiDataSourcesAPI::FromBrowserContext(browser_context());
  DCHECK(api);
  if (!api) {
    SendResult(false);
    return;
  }
  base::PostTask(
      FROM_HERE,
      {base::ThreadPool(), base::TaskPriority::USER_VISIBLE, base::MayBlock(),
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&ThumbnailsCaptureUrlFunction::ConvertImageOnWorkerThread,
                     this, std::move(api), std::move(captured)));
}

void ThumbnailsCaptureUrlFunction::ConvertImageOnWorkerThread(
    scoped_refptr<VivaldiDataSourcesAPI> api,
    ::vivaldi::CapturePage::Result captured) {
  do {
    SkBitmap bitmap;
    if (!captured.MovePixelsToBitmap(&bitmap))
      break;
    std::vector<unsigned char> data;
    std::string mime_type;
    bool encoded = EncodeBitmap(bitmap, &data, &mime_type,
                                ImageFormat::IMAGE_FORMAT_PNG, 100);
    if (!encoded)
      break;
    api->AddImageDataForBookmark(
        bookmark_id_, base::RefCountedBytes::TakeVector(&data),
        base::BindOnce(&ThumbnailsCaptureUrlFunction::SendResult, this));
    return;
  } while (false);

  base::PostTask(
      FROM_HERE, {BrowserThread::UI},
      base::BindOnce(&ThumbnailsCaptureUrlFunction::SendResult, this,
                     false));
}

void ThumbnailsCaptureUrlFunction::SendResult(bool success) {
  namespace Results = vivaldi::thumbnails::CaptureUrl::Results;

  if (!success) {
    LOG(ERROR) << "Failed to capture url " << url_.possibly_invalid_spec();
  }
  Respond(ArgumentList(Results::Create(success)));
}

}  // namespace extensions
