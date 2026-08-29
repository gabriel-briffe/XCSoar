// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "Topography/TopographyFileRenderer.hpp"
#include "Topography/TopographyFile.hpp"
#include "Topography/XShape.hpp"
#include "Topography/ShapeRenderer.hpp"
#include "Look/TopographyLook.hpp"
#include "Renderer/LabelBlock.hpp"
#include "Projection/WindowProjection.hpp"
#include "ui/canvas/Canvas.hpp"
#include "ui/canvas/Features.hpp"
#include "Screen/Layout.hpp"
#include "shapelib/mapserver.h"
#include "util/AllocatedArray.hxx"
#include "Geo/GeoClip.hpp"
#include "Geo/FAISphere.hpp"

#ifdef ENABLE_OPENGL
#include "ui/canvas/opengl/VertexPointer.hpp"
#include "ui/canvas/opengl/Buffer.hpp"
#include "ui/canvas/opengl/Dynamic.hpp"
#include "ui/canvas/opengl/Geo.hpp"

#include "ui/canvas/opengl/Program.hpp"
#include "ui/canvas/opengl/Shaders.hpp"

#include <glm/gtc/type_ptr.hpp>
#endif

#include <algorithm>
#include <numeric>
#include <set>
#include <span>
#include <string>

TopographyFileRenderer::TopographyFileRenderer(const TopographyFile &_file,
                                               const TopographyLook &_look) noexcept
  :file(_file), look(_look),
   pen(Layout::ScaleFinePenWidth(file.GetPenWidth()), Color{file.GetColor()})
#ifndef ENABLE_OPENGL
  , brush(Color{file.GetColor()})
#endif
{
  ResourceId icon_ID = file.GetIcon();
  if (icon_ID.IsDefined())
    icon.LoadResource(icon_ID, file.GetBigIcon(), file.GetUltraIcon());
}

TopographyFileRenderer::~TopographyFileRenderer() noexcept = default;

/**
 * True if a feature's geographic box is smaller than
 * #SHAPE_MIN_BBOX_PX on screen.  Cheap (angle spans only).
 */
[[gnu::pure]]
static bool
ShapeTooSmall(const GeoBounds &bounds, Angle min_span) noexcept
{
  return bounds.GetWidth() < min_span && bounds.GetHeight() < min_span;
}

[[gnu::pure]]
static bool
ShapeTooSmallToDraw(const XShape &shape, Angle min_span) noexcept
{
  /* Lines: never skip.  At 120 km, 1 px is ~150 m; OSM road sticks
     shorter than that would leave a gapped network.  Polygons: skip
     sub-pixel fills that would still cost ear-clip. */
  return shape.get_type() == MS_SHAPE_POLYGON &&
    ShapeTooSmall(shape.get_bounds(), min_span);
}

#ifdef ENABLE_OPENGL

namespace {

static constexpr unsigned GLUSHORT_VERTEX_LIMIT = 0x10000;

class FillIndexBatch {
  ScopeVertexPointer &vp;
  const ShapePoint *const buffer;
  std::vector<GLsizei> counts;
  std::vector<GLushort> indices;
  unsigned batch_base = 0;

public:
  FillIndexBatch(ScopeVertexPointer &_vp,
                 const ShapePoint *_buffer) noexcept
    :vp(_vp), buffer(_buffer) {}

  void Flush() noexcept;
  bool Add(unsigned offset, unsigned n_verts,
           const GLushort *idx, unsigned n) noexcept;
};

void
FillIndexBatch::Flush() noexcept
{
#ifdef GL_EXT_multi_draw_arrays
  if (indices.empty())
    return;

  std::vector<const GLushort *> pointers;
  pointers.reserve(counts.size());
  unsigned i = 0;
  for (auto count : counts) {
    pointers.push_back(indices.data() + i);
    i += count;
  }

  vp.Update(GL_FLOAT, buffer + batch_base);
  GLExt::MultiDrawElements(GL_TRIANGLES, counts.data(),
                           GL_UNSIGNED_SHORT,
                           (const GLvoid **)pointers.data(),
                           counts.size());
  counts.clear();
  indices.clear();
#endif
}

bool
FillIndexBatch::Add(unsigned offset, unsigned n_verts,
                    const GLushort *idx, unsigned n) noexcept
{
#ifdef GL_EXT_multi_draw_arrays
  if (!GLExt::HaveMultiDrawElements() ||
      n_verts == 0 || n_verts > GLUSHORT_VERTEX_LIMIT)
    return false;

  if (indices.empty())
    batch_base = offset;
  else if (offset < batch_base ||
           n_verts > GLUSHORT_VERTEX_LIMIT - (offset - batch_base)) {
    Flush();
    batch_base = offset;
  }

  const unsigned base = offset - batch_base;
  counts.push_back(GLsizei(n));
  const std::size_t size = indices.size();
  indices.resize(size + n, GLushort(base));
  for (unsigned i = 0; i < n; ++i)
    indices[size + i] += idx[i];
  return true;
#else
  (void)offset;
  (void)n_verts;
  (void)idx;
  (void)n;
  return false;
#endif
}

[[gnu::pure]]
static ShapeScalar
FillMinDistance(const WindowProjection &projection) noexcept
{
  return ShapeScalar(projection.PixelsToAngle(1).Native());
}

static void
PaintOpenGLLine(ScopeVertexPointer &vp, const ShapePoint *points,
                std::span<const uint16_t> lines, const XShape &shape,
                unsigned level, ShapeScalar min_distance) noexcept
{
  vp.Update(GL_FLOAT, points);

  XShape::Indices indices;
  if (level == 0 ||
      (indices = shape.GetIndices(level, min_distance)).indices == nullptr) {
    unsigned offset = 0;
    for (unsigned n : lines) {
      glDrawArrays(GL_LINE_STRIP, offset, n);
      offset += n;
    }
    return;
  }

  for (unsigned n : std::span<const GLushort>{indices.count, lines.size()}) {
    glDrawElements(GL_LINE_STRIP, n, GL_UNSIGNED_SHORT, indices.indices);
    indices.indices += n;
  }
}

static void
PaintOpenGLPolygon(ScopeVertexPointer &vp, const ShapePoint *buffer,
                   const XShape &shape, ShapeScalar fill_min_distance,
                   FillIndexBatch &batch) noexcept
{
  const auto triangles = shape.GetIndices(0, fill_min_distance);
  if (triangles.indices == nullptr || triangles.count == nullptr)
    return;

  const GLushort *idx = triangles.indices;
  const GLushort *counts = triangles.count;
  unsigned vbase = 0;
  for (const unsigned nv : shape.GetLines()) {
    const unsigned n = *counts++;
    if (n >= 3) {
      const unsigned offset = shape.GetOffset() + vbase;
      if (!batch.Add(offset, nv, idx, n)) {
        vp.Update(GL_FLOAT, buffer + offset);
        glDrawElements(GL_TRIANGLES, n, GL_UNSIGNED_SHORT, idx);
      }
    }
    idx += n;
    vbase += nv;
  }
}

} // namespace

#endif

void
TopographyFileRenderer::UpdateVisibleShapes(const WindowProjection &projection) noexcept
{
  const double scale = projection.GetScale();
  const GeoBounds screen = projection.GetScreenBounds();
  if (file.GetSerial() == visible_serial &&
      scale <= visible_scale &&
      visible_bounds.IsValid() &&
      visible_bounds.IsInside(screen))
    /* still inside the last 2× viewport; pan only reprojects */
    return;

  visible_serial = file.GetSerial();
  visible_scale = scale;
  visible_bounds = screen.Scale(TopographyFile::CACHE_BOUNDS_SCALE);
  visible_shapes.clear();
  visible_points.clear();
  visible_labels.clear();

  const Angle min_span =
    projection.PixelsToAngle(SHAPE_MIN_BBOX_PX);

  for (const XShape &shape : file) {
    if (!visible_bounds.Overlaps(shape.get_bounds()))
      continue;

    const bool too_small = ShapeTooSmallToDraw(shape, min_span);

    if (shape.get_type() != MS_SHAPE_NULL && !too_small) {
      if (shape.get_type() == MS_SHAPE_POINT) {
        if (icon.IsDefined()) {
          const auto *points = shape.GetPoints();
          for (const unsigned line_size : shape.GetLines()) {
            const auto *end = points + line_size;
            for (; points < end; ++points) {
#ifdef ENABLE_OPENGL
              visible_points.push_back(file.ToGeoPoint(*points));
#else
              visible_points.push_back(*points);
#endif
            }
          }
        }
      } else
        visible_shapes.push_back(&shape);
    }

    if (shape.GetLabel() != nullptr && !too_small)
      visible_labels.push_back(&shape);
  }
}

#ifdef ENABLE_OPENGL

inline void
TopographyFileRenderer::UpdateArrayBuffer() noexcept
{
  if (array_buffer == nullptr)
    array_buffer = std::make_unique<GLArrayBuffer>();
  else if (file.GetSerial() == array_buffer_serial)
    return;

  array_buffer_serial = file.GetSerial();

  unsigned n = 0;
  for (auto &shape : file) {
    shape.SetOffset(n);

    const auto lines = shape.GetLines();
    n = std::accumulate(lines.begin(), lines.end(), n);
  }

  ShapePoint *p = (ShapePoint *)
    array_buffer->BeginWrite(n * sizeof(*p));
  assert (p != nullptr);

  for (const auto &shape : file) {
    const auto lines = shape.GetLines();
    const ShapePoint *src = shape.GetPoints();
    for (const auto n_points : lines) {
      p = std::copy_n(src, n_points, p);
      src += n_points;
    }
  }

  array_buffer->CommitWrite(n * sizeof(*p), p - n);
}

#endif

inline void
TopographyFileRenderer::PaintPoints(Canvas &canvas,
                                    const WindowProjection &projection) noexcept
{
  for (const auto &point : visible_points) {
    if (auto p = projection.GeoToScreenIfVisible(point))
      icon.Draw(canvas, *p);
  }
}

void
TopographyFileRenderer::Paint(Canvas &canvas,
                              const WindowProjection &projection) noexcept
{
  const std::lock_guard lock{file.mutex};

  const auto map_scale = projection.GetMapScale();
  if (!file.ShowsShapes() || !file.IsVisible(map_scale))
    return;

  UpdateVisibleShapes(projection);
  PaintPoints(canvas, projection);

  if (visible_shapes.empty())
    return;

#ifdef ENABLE_OPENGL
  PaintOpenGL(projection);
#else
  PaintSoftware(canvas, projection);
#endif
}

#ifdef ENABLE_OPENGL

void
TopographyFileRenderer::PaintOpenGL(const WindowProjection &projection) noexcept
{
  OpenGL::solid_shader->Use();

  UpdateArrayBuffer();
  array_buffer->Bind();
  const ShapePoint *const buffer = nullptr;

  pen.Bind();

  if (!pen.GetColor().IsOpaque()) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  }

  const auto map_scale = projection.GetMapScale();
  const unsigned level = file.GetThinningLevel(map_scale);
  const ShapeScalar min_distance =
    ShapeScalar(file.GetMinimumPointDistance(level))
    / (Layout::Scale(1) * FAISphere::REARTH);
  const ShapeScalar fill_min_distance = FillMinDistance(projection);

  glUniformMatrix4fv(OpenGL::solid_modelview, 1, GL_FALSE,
                     glm::value_ptr(ToGLM(projection, file.GetCenter())));

  ScopeVertexPointer vp;
  FillIndexBatch batch(vp, buffer);

  const Angle min_span =
    projection.PixelsToAngle(SHAPE_MIN_BBOX_PX);

  for (const XShape *shape_p : visible_shapes) {
    const XShape &shape = *shape_p;
    if (ShapeTooSmallToDraw(shape, min_span))
      continue;

    switch (shape.get_type()) {
    case MS_SHAPE_NULL:
    case MS_SHAPE_POINT:
      break;

    case MS_SHAPE_LINE:
      PaintOpenGLLine(vp, buffer + shape.GetOffset(),
                      shape.GetLines(), shape, level, min_distance);
      break;

    case MS_SHAPE_POLYGON:
      PaintOpenGLPolygon(vp, buffer, shape, fill_min_distance, batch);
      break;
    }
  }

  batch.Flush();

  glUniformMatrix4fv(OpenGL::solid_modelview, 1, GL_FALSE,
                     glm::value_ptr(glm::mat4(1)));
  if (!pen.GetColor().IsOpaque())
    glDisable(GL_BLEND);

  pen.Unbind();
  array_buffer->Unbind();
}

#else // !ENABLE_OPENGL

void
TopographyFileRenderer::PaintSoftware(Canvas &canvas,
                                      const WindowProjection &projection) noexcept
{
  shape_renderer.Configure(&pen, &brush);

  const GeoClip clip(projection.GetScreenBounds().Scale(1.1));
  AllocatedArray<GeoPoint> geo_points;

  const unsigned iskip = file.GetSkipSteps(projection.GetMapScale());
  const Angle min_span =
    projection.PixelsToAngle(SHAPE_MIN_BBOX_PX);

  for (const XShape *shape_p : visible_shapes) {
    const XShape &shape = *shape_p;

    if (ShapeTooSmallToDraw(shape, min_span))
      continue;

    const auto lines = shape.GetLines();
    const GeoPoint *points = shape.GetPoints();

    switch (shape.get_type()) {
    case MS_SHAPE_NULL:
    case MS_SHAPE_POINT:
      break;

    case MS_SHAPE_LINE:
      for (unsigned msize : lines) {
        shape_renderer.Begin(msize);

        const GeoPoint *end = points + msize - 1;
        for (; points < end; ++points)
          shape_renderer.AddPointIfDistant(projection.GeoToScreen(*points));

        shape_renderer.AddPoint(projection.GeoToScreen(*points));
        shape_renderer.FinishPolyline(canvas);
      }
      break;

    case MS_SHAPE_POLYGON:
      {
        const GeoPoint *src = points;
        for (const unsigned n : lines) {
          unsigned msize = n / iskip;

          geo_points.GrowDiscard(msize * 4);
          for (unsigned i = 0; i < msize; ++i)
            geo_points[i] = src[i * iskip];

          msize = clip.ClipPolygon(geo_points.data(),
                                   geo_points.data(), msize);
          if (msize < 3) {
            src += n;
            continue;
          }

          shape_renderer.Begin(msize);

          for (unsigned i = 0; i < msize; ++i)
            shape_renderer.AddPointIfDistant(
              projection.GeoToScreen(geo_points[i]));

          shape_renderer.FinishPolygon(canvas);
          src += n;
        }
      }
      break;
    }
  }

  shape_renderer.Commit();
}

#endif

/**
 * Map scale (metres) at which labels use the largest font (circuit).
 */
static constexpr double LABEL_LARGE_SCALE = 2000;

/**
 * Fraction of the layer's label range at which labels step up to the
 * medium font (well inside the range, not just as they appear).
 */
static constexpr double LABEL_MEDIUM_RANGE_FRACTION = 0.25;

[[gnu::pure]]
static TopographyLook::LabelSize
LabelSizeForScale(double map_scale, double label_threshold,
                  MS_SHAPE_TYPE type) noexcept
{
  /* Lines (roads, rivers) stay SMALL.  Each font size is a separate
     TextCache key; 256 GPU textures.  Upsizing every street name at
     circuit scale misses the cache and uploads glyphs on Mali-400. */
  if (type != MS_SHAPE_POINT)
    return TopographyLook::LabelSize::SMALL;

  if (map_scale <= LABEL_LARGE_SCALE)
    return TopographyLook::LabelSize::LARGE;

  if (label_threshold > 0 &&
      map_scale <= label_threshold * LABEL_MEDIUM_RANGE_FRACTION)
    return TopographyLook::LabelSize::MEDIUM;

  return TopographyLook::LabelSize::SMALL;
}

void
TopographyFileRenderer::PaintLabels(Canvas &canvas,
                                    const WindowProjection &projection,
                                    LabelBlock &label_block) noexcept
{
  const std::lock_guard lock{file.mutex};

  const auto map_scale = projection.GetMapScale();
  if (!file.ShowsLabels() || !file.IsLabelVisible(map_scale))
    return;

  UpdateVisibleShapes(projection);

  if (visible_labels.empty())
    return;

  const bool important = file.IsLabelImportant(map_scale);
  const auto size = LabelSizeForScale(map_scale,
                                      file.GetLabelThreshold(),
                                      visible_labels.front()->get_type());
  canvas.Select(look.GetLabelFont(important, size));
  canvas.SetTextColor(important ? COLOR_BLACK : COLOR_VERY_DARK_GRAY);
  canvas.SetBackgroundTransparent();

  std::set<std::string> drawn_labels;

  const Angle min_span =
    projection.PixelsToAngle(SHAPE_MIN_BBOX_PX);

  for (const XShape *shape_p : visible_labels) {
    const XShape &shape = *shape_p;

    if (ShapeTooSmallToDraw(shape, min_span))
      continue;

    const char *label = shape.GetLabel();
    assert(label != nullptr);
    if (label[0] == '\0')
      continue;

    /* Geographic centre, not the leftmost vertex: on compact shapes
       that vertex flips while panning/rotating, so the text jumps
       and loses the LabelBlock contest. */
    const GeoPoint center = shape.get_bounds().GetCenter();
    if (!center.IsValid())
      continue;

    const auto pt = projection.GeoToScreenIfVisible(center);
    if (!pt)
      continue;

    if (drawn_labels.contains(label))
      continue;

    const PixelRect brect = PixelRect::Centered(*pt,
                                                canvas.CalcTextSize(label));
    if (!label_block.check(brect))
      continue;

    drawn_labels.emplace(label);
    canvas.DrawText(brect.GetTopLeft(), label);
  }
}
