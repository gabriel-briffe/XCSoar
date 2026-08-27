// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "ShapeFile.hpp"
#include "Geo/GeoBounds.hpp"
#include "util/AllocatedArray.hxx"
#include "util/IntrusiveForwardList.hxx"
#include "util/Serial.hpp"
#include "util/StaticString.hxx"
#include "ui/canvas/PortableColor.hpp"
#include "ResourceId.hpp"
#include "thread/Mutex.hxx"

#ifdef ENABLE_OPENGL
#include "XShapePoint.hpp"
#endif

#include <cassert>
#include <cstdint>
#include <memory>

class WindowProjection;
class XShape;
struct zzip_dir;

class TopographyFile {
public:
  /**
   * Per-layer map display filter (Map Display → Topology → Filter).
   * Thresholds still apply when shapes or labels are enabled.
   */
  enum class LayerDisplayMode : uint8_t {
    BOTH,
    GRAPHIC,
    LABEL,
    NONE,
  };

private:
  struct ShapeEnvelope final : IntrusiveForwardListHook {
    std::unique_ptr<const XShape> shape;
  };

  /**
   * This gets incremented by Update().
   */
  Serial serial;

  zzip_dir *const dir;

  ShapeFile file;

  /**
   * The center of shapefileObj::bounds.
   */
  GeoPoint center;

  AllocatedArray<ShapeEnvelope> shapes;

  using ShapeList = IntrusiveForwardList<ShapeEnvelope>;
  ShapeList list;

  const int label_field;

  const ResourceId icon, mdpi_icon, xhdpi_icon, xxhdpi_icon;

  const unsigned pen_width;

  const BGRA8Color color;

  StaticString<32> layer_name;

  /**
   * Default thresholds from topology.tpl (GetMapScale() units, nm×1000).
   */
  const double default_scale_threshold;
  const double default_label_threshold;
  const double default_important_label_threshold;

  /**
   * Effective thresholds (may be overridden in Map Display → Topology).
   */
  double scale_threshold;
  double label_threshold;
  double important_label_threshold;

  LayerDisplayMode display_mode = LayerDisplayMode::BOTH;

  /**
   * The current scope of the shape cache.  If the screen exceeds this
   * rectangle, then we need to update the cache.
   */
  GeoBounds cache_bounds = GeoBounds::Invalid();

public:
  /**
   * Protects #serial, #shapes, #first.
   * The caller is responsible for locking it.
   */
  mutable Mutex mutex;

  class const_iterator {
    friend class TopographyFile;

    ShapeList::const_iterator i;

    constexpr const_iterator(ShapeList::const_iterator _i) noexcept:i(_i) {}

  public:
    const_iterator &operator++() {
      ++i;
      return *this;
    }

    const XShape &operator*() const {
      return *i->shape;
    }

    const XShape *operator->() const {
      return i->shape.operator->();
    }

    bool operator==(const const_iterator &other) const {
      return i == other.i;
    }

    bool operator!=(const const_iterator &other) const {
      return !(*this == other);
    }
  };

public:
  /**
   * The constructor opens the given shapefile and clears the cache
   *
   * Throws on error.
   *
   * @param shpname The shapefile to open (*.shp)
   * @param layer_name Layer name from topology.tpl (without .shp)
   * @param threshold the zoom threshold for displaying this object
   * @param color The color to use for drawing, including alpha for OpenGL
   * @param label_field The field in which the labels should be searched
   * @param icon the resource id of the icon, 0 for no icon
   * @param mdpi_icon the resource id of the mdpi icon, 0 for none
   * @param pen_width The pen width used for line drawing
   * @param label_threshold the zoom threshold for label rendering
   * @param important_label_threshold labels below this zoom threshold will
   * be rendered in default style
   */
  TopographyFile(zzip_dir *dir, const char *shpname,
                 const char *layer_name,
                 double threshold, double label_threshold,
                 double important_label_threshold,
                 const BGRA8Color color,
                 int label_field=-1,
                 ResourceId icon=ResourceId::Null(),
                 ResourceId mdpi_icon=ResourceId::Null(),
                 ResourceId xhdpi_icon=ResourceId::Null(),
                 ResourceId xxhdpi_icon=ResourceId::Null(),
                 unsigned pen_width=1);

  TopographyFile(const TopographyFile &) = delete;

  /**
   * The destructor clears the cache and closes the shapefile
   */
  ~TopographyFile() noexcept;

  const Serial &GetSerial() const noexcept {
    return serial;
  }

  const GeoPoint &GetCenter() const noexcept {
    return center;
  }

  const char *GetLayerName() const noexcept {
    return layer_name.c_str();
  }

  [[gnu::pure]]
  double GetDefaultScaleThreshold() const noexcept {
    return default_scale_threshold;
  }

  [[gnu::pure]]
  double GetDefaultLabelThreshold() const noexcept {
    return default_label_threshold;
  }

  [[gnu::pure]]
  double GetDefaultImportantLabelThreshold() const noexcept {
    return default_important_label_threshold;
  }

  [[gnu::pure]]
  double GetScaleThreshold() const noexcept {
    return scale_threshold;
  }

  [[gnu::pure]]
  double GetLabelThreshold() const noexcept {
    return label_threshold;
  }

  [[gnu::pure]]
  double GetImportantLabelThreshold() const noexcept {
    return important_label_threshold;
  }

  void SetThresholds(double shape_threshold, double label_thr,
                     double important_label_thr) noexcept;

  void ResetThresholds() noexcept;

  [[gnu::pure]]
  LayerDisplayMode GetDisplayMode() const noexcept {
    return display_mode;
  }

  void SetDisplayMode(LayerDisplayMode mode) noexcept;

  void CycleDisplayMode() noexcept;

  [[gnu::pure]]
  bool HasLabels() const noexcept {
    return label_field >= 0;
  }

  [[gnu::pure]]
  bool ShowsShapes() const noexcept {
    return display_mode == LayerDisplayMode::BOTH ||
           display_mode == LayerDisplayMode::GRAPHIC;
  }

  [[gnu::pure]]
  bool ShowsLabels() const noexcept {
    return HasLabels() &&
           (display_mode == LayerDisplayMode::BOTH ||
            display_mode == LayerDisplayMode::LABEL);
  }

  /**
   * True when shapes should be painted at this map scale.
   */
  bool IsVisible(double map_scale) const noexcept {
    return map_scale <= scale_threshold;
  }

  bool IsLabelVisible(double map_scale) const noexcept {
    return map_scale <= label_threshold;
  }

  /**
   * True when this layer needs shapes in the cache (for painting
   * shapes and/or labels).
   */
  [[gnu::pure]]
  bool IsNeeded(double map_scale) const noexcept {
    return (ShowsShapes() && IsVisible(map_scale)) ||
           (ShowsLabels() && IsLabelVisible(map_scale));
  }

  /**
   * Returns the map scale threshold that will be reached next by
   * zooming in.  This is used to decide when to rescan shapes that
   * must be loaded.  A negative value is returned when all thresholds
   * have been reached already.
   */
  [[gnu::pure]]
  double GetNextScaleThreshold(double map_scale) const noexcept {
    return map_scale <= scale_threshold
      ? (map_scale <= label_threshold
         /* both thresholds reached: not relevant */
         ? -1.
         /* only label_threshold not yet reached */
         : label_threshold)
      /* scale_threshold not yet reached */
      : (map_scale <= label_threshold
         /* only scale_threshold not yet reached */
         ? scale_threshold
         /* choose the bigger threshold, that will trigger next */
         : std::max(scale_threshold, label_threshold));
  }

  bool IsLabelImportant(double map_scale) const noexcept {
    return map_scale <= important_label_threshold;
  }

  ResourceId GetIcon() const noexcept {
    return icon;
  }

  ResourceId GetMdpiIcon() const noexcept {
    return mdpi_icon;
  }

  ResourceId GetXhdpiIcon() const noexcept {
    return xhdpi_icon;
  }

  ResourceId GetXxhdpiIcon() const noexcept {
    return xxhdpi_icon;
  }

  const auto &GetColor() const noexcept {
    return color;
  }

  unsigned GetPenWidth() const noexcept {
    return pen_width;
  }

  const_iterator begin() const noexcept {
    return const_iterator{list.begin()};
  }

  const_iterator end() const noexcept {
    return const_iterator{list.end()};
  }

  [[gnu::pure]]
  unsigned GetSkipSteps(double map_scale) const noexcept;

#ifdef ENABLE_OPENGL
  [[gnu::pure]]
  GeoPoint ToGeoPoint(const ShapePoint &p) const noexcept {
    return GeoPoint(center.longitude + Angle::Native(p.x),
                    center.latitude + Angle::Native(p.y));
  }

  /**
   * @return thinning level, range: 0 .. XShape::THINNING_LEVELS-1
   */
  [[gnu::pure]]
  unsigned GetThinningLevel(double map_scale) const noexcept;

  /**
   * @return minimum distance between points in ShapePoint coordinates
   */
  [[gnu::pure]]
  unsigned GetMinimumPointDistance(unsigned level) const noexcept;
#endif

  /**
   * Throws on error.
   *
   * @return true if new data from the topography file has been loaded
   */
  bool Update(const WindowProjection &map_projection);

  /**
   * Throws on error.
   *
   * Load all shapes into memory.  For debugging purposes.
   */
  void LoadAll();

protected:
  void ClearCache() noexcept;
};
