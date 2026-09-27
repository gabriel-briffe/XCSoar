// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "OverlayBitmap.hpp"
#include "ui/canvas/Canvas.hpp"
#include "ui/canvas/opengl/Texture.hpp"
#include "ui/canvas/opengl/Scope.hpp"
#include "ui/canvas/opengl/ConstantAlpha.hpp"
#include "ui/canvas/opengl/VertexPointer.hpp"
#include "Projection/WindowProjection.hpp"
#include "Math/Point2D.hpp"
#include "Math/Boost/Point.hpp"
#include "system/Path.hpp"
#include "util/StaticArray.hxx"

#include <algorithm>
#include <cmath>
#include <boost/geometry/geometries/register/ring.hpp>
#include <boost/geometry/algorithms/covered_by.hpp>
#include <boost/geometry/strategies/strategies.hpp>

using ArrayQuadrilateral = StaticArray<DoublePoint2D, 5>;
BOOST_GEOMETRY_REGISTER_RING(ArrayQuadrilateral);

MapOverlayBitmap::MapOverlayBitmap(Path path)
  :label((path.GetBase() != nullptr ? path.GetBase() : path).c_str())
{
  bounds = bitmap.LoadGeoFile(path);
  simple_bounds = bounds.GetBounds();

  /* Slippy PNG/JPEG tiles (SkySight sat/rain, etc.) are Web Mercator;
     GeoTIFF products stay geographic with linear lat. */
  if (path.EndsWithIgnoreCase(".jpg") ||
      path.EndsWithIgnoreCase(".jpeg") ||
      path.EndsWithIgnoreCase(".jfif") ||
      path.EndsWithIgnoreCase(".png"))
    web_mercator = true;
}

/**
 * Convert a GeoPoint to a "fake" flat DoublePoint2D.  This conversion
 * is flawed in many ways, but good enough for hit-testing.
 */
static constexpr DoublePoint2D
GeoTo2D(GeoPoint p) noexcept
{
  return {p.longitude.Native(), p.latitude.Native()};
}

/**
 * Convert a #GeoQuadrilateral instance to a boost::geometry ring.
 */
[[gnu::const]]
static ArrayQuadrilateral
ToArrayQuadrilateral(const GeoQuadrilateral q) noexcept
{
  return {GeoTo2D(q.top_left), GeoTo2D(q.top_right),
      GeoTo2D(q.bottom_right), GeoTo2D(q.bottom_left),
      /* close the ring: */
      GeoTo2D(q.top_left) };
}

[[gnu::pure]]
static double
LatitudeToMercatorY(double lat_deg) noexcept
{
  return std::asinh(std::tan(lat_deg * M_PI / 180.0));
}

[[gnu::pure]]
static double
MercatorYToLatitude(double merc_y) noexcept
{
  return 180.0 / M_PI * std::atan(std::sinh(merc_y));
}

/**
 * Geographic UV: lon and lat both linear in texture space (GeoTIFF).
 */
[[gnu::pure]]
static GeoPoint
InterpolateGeographic(const GeoQuadrilateral &q,
                      double u, double v) noexcept
{
  const auto top = q.top_left.Interpolate(q.top_right, u);
  const auto bottom = q.bottom_left.Interpolate(q.bottom_right, u);
  return top.Interpolate(bottom, v);
}

/**
 * Web Mercator UV: lon linear, lat via mercator Y (slippy tiles).
 * Assumes an axis-aligned tile quad (constant N/S lat, E/W lon).
 */
[[gnu::pure]]
static GeoPoint
InterpolateWebMercator(const GeoQuadrilateral &q,
                       double u, double v) noexcept
{
  const double west = q.top_left.longitude.Degrees();
  const double east = q.top_right.longitude.Degrees();
  const double north = q.top_left.latitude.Degrees();
  const double south = q.bottom_left.latitude.Degrees();

  const double lon = west + (east - west) * u;
  const double merc0 = LatitudeToMercatorY(north);
  const double merc1 = LatitudeToMercatorY(south);
  const double lat = MercatorYToLatitude(merc0 + (merc1 - merc0) * v);
  return {Angle::Degrees(lon), Angle::Degrees(lat)};
}

[[gnu::pure]]
static GeoPoint
InterpolateUV(const GeoQuadrilateral &q, double u, double v,
              bool web_mercator) noexcept
{
  return web_mercator
    ? InterpolateWebMercator(q, u, v)
    : InterpolateGeographic(q, u, v);
}

[[gnu::pure]]
static GeoQuadrilateral
SliceUV(const GeoQuadrilateral &q,
        double u0, double v0, double u1, double v1,
        bool web_mercator) noexcept
{
  return {
    InterpolateUV(q, u0, v0, web_mercator),
    InterpolateUV(q, u1, v0, web_mercator),
    InterpolateUV(q, u0, v1, web_mercator),
    InterpolateUV(q, u1, v1, web_mercator),
  };
}

bool
MapOverlayBitmap::IsInside(GeoPoint p) const noexcept
{
  return simple_bounds.IsInside(p) &&
    boost::geometry::covered_by(GeoTo2D(p), ToArrayQuadrilateral(bounds));
}

void
MapOverlayBitmap::Draw([[maybe_unused]] Canvas &canvas,
                       [[maybe_unused]] const WindowProjection &projection) noexcept
{
  const auto screen_bounds = projection.GetScreenBounds();
  if (!simple_bounds.Overlaps(screen_bounds))
    /* not visible, outside of screen area */
    return;

  GLTexture &texture = *bitmap.GetNative();
  const PixelSize allocated = texture.GetAllocatedSize();
  const double x_factor = double(texture.GetWidth()) / allocated.width;
  const double y_factor = double(texture.GetHeight()) / allocated.height;

  Point2D<GLfloat> coord[16];
  BulkPixelPoint vertices[16];

  const ScopeVertexPointer vp(vertices);

  texture.Bind();

  const ScopeTextureConstantAlpha blend(use_bitmap_alpha, alpha);

  glEnableVertexAttribArray(OpenGL::Attribute::TEXCOORD);
  glVertexAttribPointer(OpenGL::Attribute::TEXCOORD, 2, GL_FLOAT, GL_FALSE,
                        0, coord);

  /* Always subdivide: one screen triangle-fan per cell.  A single
     quad warps badly under map rotation/scale; ≥8×8 keeps edges
     closer to the true projection.  Larger textures keep ~128 px
     cells (capped at 32).  Antimeridian cull uses
     GeoQuadrilateral::GetBounds() (Normalize + Extend).  Slippy
     tiles also use mercator V when web_mercator is set. */
  const unsigned x_steps = std::clamp((texture.GetWidth() + 127u) / 128u,
                                      8u, 32u);
  const unsigned y_steps = std::clamp((texture.GetHeight() + 127u) / 128u,
                                      8u, 32u);

  for (unsigned y = 0; y < y_steps; ++y) {
    const double v0 = double(y) / y_steps;
    const double v1 = double(y + 1) / y_steps;

    for (unsigned x = 0; x < x_steps; ++x) {
      const double u0 = double(x) / x_steps;
      const double u1 = double(x + 1) / x_steps;

      const auto cell = SliceUV(bounds, u0, v0, u1, v1, web_mercator);
      if (!cell.GetBounds().Overlaps(screen_bounds))
        continue;

      const GeoPoint geo[4] = {
        cell.top_left,
        cell.top_right,
        cell.bottom_right,
        cell.bottom_left,
      };
      const double uv[4][2] = {
        {u0, v0},
        {u1, v0},
        {u1, v1},
        {u0, v1},
      };

      for (unsigned i = 0; i < 4; ++i) {
        coord[i].x = uv[i][0] * x_factor;
        coord[i].y = (bitmap.IsFlipped() ? 1 - uv[i][1] : uv[i][1]) * y_factor;

        vertices[i] = projection.GeoToScreen(geo[i]);
      }

      glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }
  }

  glDisableVertexAttribArray(OpenGL::Attribute::TEXCOORD);
}
