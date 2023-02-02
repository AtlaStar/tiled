/*
 * automapper.h
 * Copyright 2010-2016, Stefan Beller <stefanbeller@googlemail.com>
 * Copyright 2016-2022, Thorbjørn Lindijer <bjorn@lindeijer.nl>
 *
 * This file is part of Tiled.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "addremovemapobject.h"
#include "tilededitor_global.h"
#include "tilelayer.h"
#include "tileset.h"

#include <QList>
#include <QMap>
#include <QRegion>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QVector>

#include <memory>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace Tiled {

class Layer;
class Map;
class MapObject;
class ObjectGroup;
class TileLayer;

class MapDocument;

struct InputLayer
{
    const TileLayer *tileLayer;
    bool strictEmpty;
};

struct InputConditions
{
    InputConditions(const QString &layerName) : layerName(layerName) {}

    QString layerName;
    QVector<InputLayer> listYes;        // "input"
    QVector<InputLayer> listNo;         // "inputnot"
};

struct InputSet
{
    InputSet(const QString &name) : name(name) {}

    QString name;
    std::vector<InputConditions> layers;
};

// One set of output layers sharing the same index
struct OutputSet
{
    OutputSet(const QString &name) : name(name) {}

    QString name;
    // Maps output layers in mRulesMap to their names in mTargetMap
    QHash<const Layer*, QString> layers;
};

struct RuleOptions
{
    enum Enum
    {
        SkipChance          = 1 << 0,
        ModX                = 1 << 1,
        ModY                = 1 << 2,
        OffsetX             = 1 << 3,
        OffsetY             = 1 << 4,
        NoOverlappingOutput = 1 << 5,
        Disabled            = 1 << 6
    };

    qreal skipChance = 0.0;
    unsigned modX = 1;
    unsigned modY = 1;
    int offsetX = 0;
    int offsetY = 0;
    bool noOverlappingOutput = false;
    bool disabled = false;
};

struct RuleOptionsArea
{
    QRect area;
    RuleOptions options;
    unsigned setOptions = 0;
};

struct RuleMapSetup
{
    /**
     * The TileLayer that defines the input and output regions ('regions').
     */
    const TileLayer *mLayerRegions = nullptr;

    /**
     * The TileLayer that defines the input regions ('regions_input').
     */
    const TileLayer *mLayerInputRegions = nullptr;

    /**
     * The TileLayer that defines the output regions ('regions_output').
     */
    const TileLayer *mLayerOutputRegions = nullptr;

    /**
     * Holds different input sets. A rule matches when any of its input sets
     * match.
     */
    std::vector<InputSet> mInputSets;

    /**
     * Holds different output sets. One of the sets is chosen by chance, so
     * randomness is available.
     */
    std::vector<OutputSet> mOutputSets;

    std::vector<RuleOptionsArea> mRuleOptionsAreas;

    QSet<QString> mInputLayerNames;
    QSet<QString> mOutputTileLayerNames;
    QSet<QString> mOutputObjectGroupNames;
};

struct RuleInputLayer
{
    const TileLayer *targetLayer = nullptr;   // reference to layer in target map
    int posCount = 0;
};

struct RuleInputLayerPos
{
    int x;                          // position relative to match location
    int y;
    int anyCount;                   // any of these cells
    int noneCount;                  // none of these cells
};

/**
 * An efficient structure for matching purposes. Each data structure has a
 * single container, which keeps things packed together in memory.
 */
struct RuleInputSet
{
    QVector<RuleInputLayer> layers;
    QVector<RuleInputLayerPos> positions;
    QVector<Cell> cells;
};

struct CompileContext;
struct ApplyContext;

/**
 * A single context is used for running all active AutoMapper instances on a
 * specific target map.
 *
 * The AutoMapper does not change the target map directly. Instead, the changes
 * can be found in the AutoMappingContext and still need to be applied
 * manually.
 *
 * AutoMapping is done as follows:
 *
 * - Create a single AutoMappingContext
 * - Call AutoMapper::prepareAutoMap for each active AutoMapper instance
 * - Call AutoMapper::autoMap for each active AutoMapper instance
 * - Apply the changes visible in the AutoMappingContext to the target map
 */
struct TILED_EDITOR_EXPORT AutoMappingContext
{
    AutoMappingContext(MapDocument *mapDocument);
    AutoMappingContext(const AutoMappingContext &) = delete;

    const MapDocument * const targetDocument;
    const Map * const targetMap;

    QVector<SharedTileset> newTilesets;             // New tilesets that might get used
    std::vector<std::unique_ptr<Layer>> newLayers;  // Layers created in AutoMapper::prepareAutoMap
    QVector<QVector<AddMapObjects::Entry>> newMapObjects;   // Objects placed by AutoMapper
    QSet<MapObject*> mapObjectsToRemove;
    QHash<Layer*, Properties> changedProperties;

    // Clones of existing tile layers that might have been changed in AutoMapper::autoMap
    std::unordered_map<TileLayer*, std::unique_ptr<TileLayer>> originalToOutputLayerMapping;

    // Used to keep track of touched tile layers (only when initially non-empty)
    QVector<const TileLayer*> touchedTileLayers;

private:
    friend class AutoMapper;

    QHash<QString, const TileLayer*> inputLayers;
    QHash<QString, TileLayer*> outputTileLayers;
    QHash<QString, ObjectGroup*> outputObjectGroups;
};

/**
 * This class does all the work for the automapping feature.
 * basically it can do the following:
 * - check the rules map for rules and store them
 * - compare TileLayers (i. e. check if/where a certain rule must be applied)
 * - copy regions of Maps (multiple Layers, the layerlist is a
 *                         lookup-table for matching the Layers)
 */
class TILED_EDITOR_EXPORT AutoMapper : public QObject
{
    Q_OBJECT

public:
    struct Options
    {
        /**
         * Determines whether all tiles in all touched layers should be deleted
         * first.
         */
        bool deleteTiles = false;

        /**
         * Whether rules can match when their input region is partially outside
         * of the map.
         */
        bool matchOutsideMap = false;

        /**
         * If "matchOutsideMap" is true, treat the out-of-bounds tiles as if they
         * were the nearest inbound tile possible
         */
        bool overflowBorder = false;

        /**
         * If "matchOutsideMap" is true, wrap the map in the edges to apply the
         * automapping rules
         */
        bool wrapBorder = false;

        /**
         * Determines whether the rules on the map need to be matched in order.
         */
        std::optional<bool> matchInOrder;

        /**
         * This variable determines, how many overlapping tiles should be used.
         * The bigger the more area is remapped at an automapping operation.
         * This can lead to higher latency, but provides a better behavior on
         * interactive automapping.
         */
        int autoMappingRadius = 0;
    };

    using GetCell = std::add_pointer_t<const Cell &(int x, int y, const TileLayer &tileLayer)>;

    /**
     * Constructs an AutoMapper.
     *
     * All data structures, which only rely on the rules map are setup
     * here.
     *
     * @param rulesMap The map containing the AutoMapping rules. The
     *                 AutoMapper takes ownership of this map.
     */
    AutoMapper(std::unique_ptr<Map> rulesMap, const QRegularExpression &mapNameFilter = {});
    ~AutoMapper() override;

    QString rulesMapFileName() const;
    const QRegularExpression &mapNameFilter() const;

    /**
     * Checks if the passed \a ruleLayerName is used as input layer in this
     * instance of AutoMapper.
     */
    bool ruleLayerNameUsed(const QString &ruleLayerName) const;

    /**
     * This needs to be called directly before the autoMap call.
     * It sets up some data structures which change rapidly, so it is quite
     * painful to keep these data structures up to date all time. (indices of
     * layers of the working map)
     */
    void prepareAutoMap(AutoMappingContext &context);

    /**
     * Here is done all the AutoMapping.
     *
     * When an \a appliedRegion is provided, it is set to the region where
     * rule outputs have been applied.
     */
    void autoMap(const QRegion &where,
                 QRegion *appliedRegion,
                 AutoMappingContext &context) const;

    /**
     * Contains any errors which occurred while interpreting the rules map.
     */
    QString errorString() const { return mError; }

    /**
     * Contains any warnings which occurred while interpreting the rules map.
     */
    QString warningString() const { return mWarning; }

private:
    struct Rule
    {
        QRegion inputRegion;
        QRegion outputRegion;
        RuleOptions options;
    };

    void setupRuleMapProperties();
    void setupInputLayerProperties(InputLayer &inputLayer);
    void setupRuleOptionsArea(RuleOptionsArea &optionsArea, const MapObject *mapObject);

    /**
     * Sets up the layers in the rules map, which are used for automapping.
     * The layers are detected and put in the internal data structures.
     * @return true when everything is ok, false when errors occurred.
     */
    bool setupRuleMapLayers();
    void setupRules();

    void setupWorkMapLayers(AutoMappingContext &context) const;
    void compileRule(QVector<RuleInputSet> &inputSets,
                     const Rule &rule,
                     const AutoMappingContext &context) const;
    bool compileInputSet(RuleInputSet &index,
                         const InputSet &inputSet,
                         const QRegion &inputRegion,
                         CompileContext &compileContext,
                         const AutoMappingContext &context) const;

    /**
     * This copies all tiles from TileLayer \a srcLayer to TileLayer
     * \a dstLayer.
     *
     * In src the tiles are taken from the \a rect.
     * In dst they get copied to a rectangle given by
     * \a dstX, \a dstY and the size of \a rect.
     * if there is no tile in src TileLayer, there will nothing be copied,
     * so the maybe existing tile in dst will not be overwritten.
     */
    void copyTileRegion(const TileLayer *srcLayer, QRect rect, TileLayer *dstLayer,
                        int dstX, int dstY, const AutoMappingContext &context) const;

    /**
     * This copies all objects from the \a src_lr ObjectGroup to the \a dst_lr
     * in the given \a rect.
     *
     * The parameter \a dstX and \a dstY offset the copied objects in the
     * destination object group.
     */
    void copyObjectRegion(const ObjectGroup *srcLayer, const QRectF &rect,
                          ObjectGroup *dstLayer, int dstX, int dstY,
                          AutoMappingContext &context) const;


    /**
     * This copies multiple TileLayers from one map to another.
     * Only the region \a region is considered for copying.
     * In the destination it will come to the region translated by Offset.
     * The parameter \a ruleOutput contains a map of which layers of the rules
     * map should get copied into which layers of the working map.
     */
    void copyMapRegion(const QRegion &region, QPoint Offset,
                       const OutputSet &ruleOutput,
                       AutoMappingContext &context) const;

    /**
     * This goes through all the positions in \a matchRegion and checks if the
     * \a rule matches there.
     *
     * Calls \a matched for each matching location.
     */
    void matchRule(const Rule &rule,
                   const QRegion &matchRegion,
                   GetCell getCell,
                   const std::function<void (QPoint)> &matched,
                   const AutoMappingContext &context) const;

    /**
     * Applies the given \a rule at each of the given \a positions.
     *
     * Might skip some of the positions to satisfy the NoOverlappingRules
     * option.
     */
    void applyRule(const Rule &rule, QPoint pos, ApplyContext &applyContext,
                   AutoMappingContext &context) const;

    void addWarning(const QString &text,
                    std::function<void()> callback = std::function<void()>());

    /**
     * Map containing the rules.
     */
    const std::unique_ptr<Map> mRulesMap;
    const QRegularExpression mMapNameFilter;

    RuleMapSetup mRuleMapSetup;

    /**
     * Stores the input and output region for each rule in mRulesMap.
     */
    std::vector<Rule> mRules;

    Options mOptions;

    /**
     * Rule options set on the map, which become the default for all rules
     * on this map.
     */
    RuleOptions mRuleOptions;

    QString mError;
    QString mWarning;

    const TileLayer dummy;  // used in case input layers are missing
};

inline const QRegularExpression &AutoMapper::mapNameFilter() const
{
    return mMapNameFilter;
}

} // namespace Tiled
