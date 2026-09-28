// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "GeoBitmapRenderer.hpp"
#include "ui/canvas/RawBitmap.hpp"
#include "Geo/GeoBounds.hpp"
#include "Geo/GeoPoint.hpp"
#include "Projection/Projection.hpp"

#ifdef ENABLE_OPENGL
#include "ui/canvas/opengl/Texture.hpp"
#include "ui/canvas/opengl/Scope.hpp"
#include "ui/canvas/opengl/VertexPointer.hpp"
#include "ui/canvas/opengl/Attribute.hpp"
#include "ui/dim/BulkPoint.hpp"

#include <algorithm>

/**
 * Lat/lon UV on an axis-aligned geo bitmap.  Texture space is linear
 * in geographic coordinates; screen placement must use #GeoToScreen.
 */
[[gnu::pure]]
static GeoPoint
InterpolateGeoBounds(const GeoBounds &bounds,
                     double u, double v) noexcept
{
  const auto north = bounds.GetNorthWest().Interpolate(bounds.GetNorthEast(),
                                                       u);
  const auto south = bounds.GetSouthWest().Interpolate(bounds.GetSouthEast(),
                                                       u);
  return north.Interpolate(south, v);
}

/**
 * Draw a geo-referenced bitmap texture.  The caller is responsible
 * for setting up the OpenGL shader (e.g. via
 * ScopeTextureConstantAlpha) before calling this function.
 */
void
DrawGeoBitmap(const RawBitmap &bitmap, PixelSize bitmap_size,
              const GeoBounds &bounds,
              const Projection &projection)
{
  assert(bounds.IsValid());

  const GLTexture &texture = bitmap.BindAndGetTexture();
  const PixelSize allocated = texture.GetAllocatedSize();

  const GLfloat src_x = 0, src_y = 0, src_width = bitmap_size.width,
    src_height = bitmap_size.height;

  const GLfloat x0 = src_x / allocated.width;
  const GLfloat y0 = src_y / allocated.height;
  const GLfloat x1 = (src_x + src_width) / allocated.width;
  const GLfloat y1 = (src_y + src_height) / allocated.height;

  /* One stretched quad warps under map rotation/scale.  Subdivide so
     each cell corner is projected with GeoToScreen (same idea as
     MapOverlayBitmap). */
  static constexpr unsigned MAX_STEPS = 16;
  const unsigned steps = std::clamp((bitmap_size.width + 127u) / 128u,
                                    8u, MAX_STEPS);

  BulkPixelPoint vertices[(MAX_STEPS + 1) * 2];
  GLfloat coord[(MAX_STEPS + 1) * 2 * 2];

  const ScopeVertexPointer vp(vertices);
  glEnableVertexAttribArray(OpenGL::Attribute::TEXCOORD);
  glVertexAttribPointer(OpenGL::Attribute::TEXCOORD, 2, GL_FLOAT, GL_FALSE,
                        0, coord);

  for (unsigned y = 0; y < steps; ++y) {
    const double v0 = double(y) / steps;
    const double v1 = double(y + 1) / steps;

    unsigned i = 0;
    for (unsigned x = 0; x <= steps; ++x) {
      const double u = double(x) / steps;

      vertices[i] = projection.GeoToScreen(InterpolateGeoBounds(bounds, u, v0));
      coord[i * 2] = x0 + GLfloat(u) * (x1 - x0);
      coord[i * 2 + 1] = y0 + GLfloat(v0) * (y1 - y0);
      ++i;

      vertices[i] = projection.GeoToScreen(InterpolateGeoBounds(bounds, u, v1));
      coord[i * 2] = x0 + GLfloat(u) * (x1 - x0);
      coord[i * 2 + 1] = y0 + GLfloat(v1) * (y1 - y0);
      ++i;
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, (steps + 1) * 2);
  }

  glDisableVertexAttribArray(OpenGL::Attribute::TEXCOORD);
}

#endif
