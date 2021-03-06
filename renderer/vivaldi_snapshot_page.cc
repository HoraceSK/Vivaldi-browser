// Copyright (c) 2016 Vivaldi Technologies AS. All rights reserved

#include "renderer/vivaldi_snapshot_page.h"

#include <memory>
#include <string>

#include "cc/paint/skia_paint_canvas.h"
#include "skia/ext/image_operations.h"
#include "third_party/blink/public/platform/web_scroll_types.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_element.h"
#include "third_party/blink/public/web/web_element_collection.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_view.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/frame/web_local_frame_impl.h"
#include "third_party/blink/renderer/core/input/event_handler.h"
#include "third_party/blink/renderer/core/layout/layout_view.h"
#include "third_party/blink/renderer/core/paint/paint_layer_scrollable_area.h"
#include "third_party/blink/renderer/platform/graphics/compositing/paint_chunks_to_cc_layer.h"
#include "third_party/blink/renderer/platform/graphics/graphics_context.h"
#include "third_party/blink/renderer/platform/graphics/paint/cull_rect.h"
#include "third_party/blink/renderer/platform/graphics/paint/paint_record_builder.h"
#include "third_party/blink/renderer/platform/graphics/static_bitmap_image.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkSurface.h"

namespace {

bool ToSkBitmap(
    const scoped_refptr<blink::StaticBitmapImage>& static_bitmap_image,
    SkBitmap* dest) {
  const sk_sp<SkImage> image =
      static_bitmap_image->PaintImageForCurrentFrame().GetSkImage();
  return image && image->asLegacyBitmap(
                      dest, SkImage::LegacyBitmapMode::kRO_LegacyBitmapMode);
}

}  // namespace

bool VivaldiSnapshotPage(blink::LocalFrame* local_frame,
                         bool full_page,
                         const blink::IntRect& rect,
                         SkBitmap* bitmap) {
  // Disabled because third_party/blink/renderer/core/layout/layout_view.h
  // requires access to blink-module-only function WebString(const
  // WTF::String&), activated by the blink-only define INSIDE_BLINK This include
  // file is only referenced inside blink. This code will have to be rewritten
  // to not depend on internal blink functions or those parts will have to be
  // migrated to a vivaldi code file that can be inserted into the blink public
  // API.
  blink::Document* document = local_frame->GetDocument();
  if (!document || !document->GetLayoutView())
    return false;

  /*
  We follow DragController::DragImageForSelection here while making sure that
  we paint the whole document including the parts outside the scroll view.
  TODO: See ChromePrintRenderFrameHelperDelegate::GetPdfElement for
  capture of PDF.

  TODO(igor@vivaldi.com): Find out why when full_page is true and we paint the
  whole page including the invisible parts outside the scroll area and when
  document->Lifecycle() is DocumentLifecycle::kVisualUpdatePending or perhaps is
  anything but DocumentLifecycle::kPaintClean or kPrePaintClean painting here
  may affect painting of the page later when the user scrolls the previously
  invisible parts. In such case the scrolled in areas may contains unpainted
  rectangles. For this reason we can only paint the visible part of the page
  when !full_page and we are drawing thumbnails to avoid rendering regressions
  later on each and every page.
  */
#if !defined(OS_ANDROID)
  bool has_accelerated_compositing =
    document->GetSettings()->GetAcceleratedCompositingEnabled();

  // Disable accelerated compositing temporary to make canvas and other
  // normally HWA element show up, restrict to full page rendering for now.
  if (full_page) {
    document->GetSettings()->SetAcceleratedCompositingEnabled(false);
  }
#endif

  // Force an update of the lifecycle since we changed the painting method
  // of accelerated elements.
  local_frame->View()->UpdateAllLifecyclePhasesExceptPaint();

  blink::LayoutView* view = document->GetLayoutView();
  blink::PhysicalRect document_rect = view->DocumentRect();
  blink::IntRect visible_content_rect =
    local_frame->View()->LayoutViewport()->VisibleContentRect();

  blink::IntSize page_size;
  if (full_page) {
    blink::FloatSize float_page_size = local_frame->ResizePageRectsKeepingRatio(
      blink::FloatSize(document_rect.Width(), document_rect.Height()),
      blink::FloatSize(document_rect.Width(), document_rect.Height()));
    float_page_size.SetHeight(
        std::min(float_page_size.Height(), static_cast<float>(rect.Height())));
    page_size = ExpandedIntSize(float_page_size);
  } else {
    page_size.SetWidth(visible_content_rect.Width());
    page_size.SetHeight(visible_content_rect.Height());
  }

  blink::IntRect page_rect(0, 0, page_size.Width(), page_size.Height());
  if (full_page) {
    // page_rect is relative to the visible scroll area. To include the
    // document top we must use negative offsets for the upper left
    // corner.
    page_rect.SetX(-visible_content_rect.X());
    page_rect.SetY(-visible_content_rect.Y());
  }

  blink::PaintRecordBuilder picture_builder;
  {
    blink::GraphicsContext& context = picture_builder.Context();
    context.SetShouldAntialias(false);

    blink::GlobalPaintFlags global_paint_flags =
      blink::kGlobalPaintFlattenCompositingLayers;
    if (full_page) {
      global_paint_flags |= blink::kGlobalPaintWholePage;
    }

    local_frame->View()->PaintContentsOutsideOfLifecycle(
        context, global_paint_flags, blink::CullRect(page_rect));
  }

#if !defined(OS_ANDROID)
  if (full_page) {
    document->GetSettings()->
      SetAcceleratedCompositingEnabled(has_accelerated_compositing);
  }
#endif

  SkSurfaceProps surface_props(0, kUnknown_SkPixelGeometry);
  sk_sp<SkSurface> surface =
    SkSurface::MakeRasterN32Premul(
      page_rect.Width(), page_rect.Height(), &surface_props);
  if (!surface)
    return false;

  cc::SkiaPaintCanvas canvas(surface->getCanvas());

  if (full_page) {
    // Translate scroll view coordinates into page-relative ones.
    blink::AffineTransform transform;
    transform.Translate(visible_content_rect.X(), visible_content_rect.Y());
    canvas.concat(blink::AffineTransformToSkMatrix(transform));

    // Prepare PaintChunksToCcLayer called deep under EndRecording
    // to ignore clipping to the visible area.
    DCHECK(!blink::PaintChunksToCcLayer::TopClipToIgnore());
    const blink::ObjectPaintProperties *root_properties =
      view->FirstFragment().PaintProperties();
    if (root_properties) {
      blink::PaintChunksToCcLayer::TopClipToIgnore() =
        root_properties->OverflowClip();
    }
  }

  blink::PropertyTreeState root_tree_state = blink::PropertyTreeState::Root();
  picture_builder.EndRecording(canvas, root_tree_state);

  if (full_page) {
    blink::PaintChunksToCcLayer::TopClipToIgnore() = nullptr;
  } else {
    DCHECK(!blink::PaintChunksToCcLayer::TopClipToIgnore());
  }

  // Crop to rect if required.
  scoped_refptr<blink::StaticBitmapImage> image;
  if (rect.IsEmpty() || full_page) {
    image = blink::StaticBitmapImage::Create(surface->makeImageSnapshot());
  } else {
    image = blink::StaticBitmapImage::Create(
        surface->makeImageSnapshot(SkIRect(rect)));
  }
  return image ? ToSkBitmap(image, bitmap) : false;
}
