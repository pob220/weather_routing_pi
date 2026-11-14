// Private benchmark adapter. Uses the unchanged shipping hazard evaluator.
// Missing chart evidence always rejects a segment; GSHHG supplies additional
// coast screening and distance guidance, never substitute chart depth.
#pragma once
#include "ChartSafetyCache.h"
#include "ChartHazardEvaluator.h"
#include "ChartLongitude.h"
#include "original_routing/GshhgProvider.h"
#include <json/json.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <thread>
#include <unistd.h>

class FrozenChartProvider final : public supercpn::weather_routing::LandAndBoundaryProvider {
  using Point=supercpn::weather_routing::GeoPoint;
  std::filesystem::path directory_;
  std::string identity_;
  double minimumDepth_;
  bool frozen_;
  std::shared_ptr<const supercpn::weather_routing::LandAndBoundaryProvider> coast_;
  mutable weather_routing::ChartSafetyCache cache_;
  mutable weather_routing::ChartHazardEvaluator evaluator_{cache_};
  mutable std::set<std::pair<long,long>> loaded_;
  mutable std::set<std::pair<long,long>> missing_;
  std::filesystem::path tilePath(long lat,long lon) const {
    return directory_/"tiles"/(std::to_string(lat)+"_"+std::to_string(lon)+".json");
  }
  void load(const std::filesystem::path& path) const {
    Json::Value raw;{std::ifstream file(path);file>>raw;}
    if(raw["identity"].asString()!=identity_)throw std::runtime_error("Chart tile identity mismatch");
    PlugInSegmentSafetyTile tile{};tile.struct_size=sizeof(tile);
    tile.group_index=raw["group"].asInt();tile.lat_tile=raw["lat_tile"].asInt64();tile.lon_tile=raw["lon_tile"].asInt64();
    tile.resolution=raw["resolution"].asDouble();tile.rows=raw["rows"].asInt();tile.cols=raw["cols"].asInt();
    const int count=tile.rows*tile.cols;
    if(count<=0 || count>10000 || raw["hazards"].size()!=unsigned(count) || raw["has_depth"].size()!=unsigned(count) || raw["depth"].size()!=unsigned(count))throw std::runtime_error("Invalid chart tile arrays");
    tile.chart_db_index=raw["chart_db_index"].asInt();tile.chart_scale=raw["chart_scale"].asInt();
    tile.source=raw["source"].asInt();tile.hazard_summary_flags=raw["summary"].asUInt();
    tile.depth_complete=raw["depth_complete"].asBool();tile.cell_capacity=count;
    std::snprintf(tile.chart_path,sizeof(tile.chart_path),"%s",raw["chart_path"].asCString());
    std::snprintf(tile.dependency_identity,sizeof(tile.dependency_identity),"%s",raw["dependency"].asCString());
    std::vector<unsigned short> hazards;std::vector<unsigned char> hasDepth;std::vector<float> depths;
    for(int i=0;i<count;++i){hazards.push_back(raw["hazards"][i].asUInt());hasDepth.push_back(raw["has_depth"][i].asUInt());depths.push_back(raw["depth"][i].asFloat());}
    tile.hazard_flags=hazards.data();tile.has_depth=hasDepth.data();tile.min_depth_m=depths.data();
    cache_.Store(&tile);loaded_.insert({tile.lat_tile,tile.lon_tile});
  }
  bool ensure(Point a,Point b,double margin) const {
    using supercpn::weather_routing::normalizeLongitude;
    std::set<std::pair<long,long>> wanted;
    const double delta=normalizeLongitude(b.longitude-a.longitude);
    const int count=std::max(1,int(std::ceil(std::max(std::abs(delta),std::abs(b.latitude-a.latitude))/.0125)));
    const int halo=std::max(1,int(std::ceil(margin/2.5)));
    for(int i=0;i<=count;++i){
      const long lat=long(std::floor((a.latitude+(b.latitude-a.latitude)*i/count)/.05));
      const long lon=long(std::floor((a.longitude+delta*i/count)/.05));
      for(int y=-halo;y<=halo;++y)for(int x=-halo;x<=halo;++x){
        const auto key=std::pair{lat+y,weather_routing::CanonicalChartLongitudeTile(lon+x)};
        std::shared_ptr<const weather_routing::ChartHazardTile> resident;
        if (!cache_.LookupSnapshot(key.first, key.second, true, &resident) || !resident)
          wanted.insert(key);
      }
    }
    if(wanted.empty())return true;
    bool available = true;
    for (auto [lat, lon] : wanted) {
      const auto path = tilePath(lat, lon);
      if (!std::filesystem::is_regular_file(path)) {
        missing_.insert({lat, lon}); available = false; continue;
      }
      load(path); ++newTiles;
    }
    evaluator_.ClearDerivedMasks();
    return available;
  }
  bool blocked(Point a,Point b,double margin) const {
    if(coast_->segmentForbidden(a,b,margin))return true;
    if(!ensure(a,b,margin))return true;
    PlugInSegmentSafetyOptions options{};options.struct_size=sizeof(options);
    options.check_land=1;options.check_depth=minimumDepth_>0.;options.minimum_depth_m=minimumDepth_;options.safety_margin_nm=margin;
    PlugInSegmentSafetyResult result{};result.struct_size=sizeof(result);
    if(!evaluator_.CheckSegment(a.latitude,a.longitude,b.latitude,b.longitude,options,&result)){++unanswered;return true;}
    ++statuses[result.status];return result.status!=PI_SEGMENT_SAFETY_SAFE;
  }
public:
  mutable unsigned requests{},newTiles{},unanswered{};
  mutable double extractionSeconds{};
  mutable std::map<int,unsigned> statuses;
  FrozenChartProvider(std::filesystem::path directory,double depth,bool frozen,
      std::shared_ptr<const supercpn::weather_routing::LandAndBoundaryProvider> coast)
      :directory_(std::move(directory)),minimumDepth_(depth),frozen_(frozen),coast_(std::move(coast)) {
    Json::Value receipt;{std::ifstream file(directory_/"identity.json");file>>receipt;}identity_=receipt["identity"].asString();
    if(identity_.empty())throw std::runtime_error("Missing chart database identity");
    if (!frozen_) throw std::runtime_error("Only frozen chart evidence is allowed");
    // RAM only. No request files, persistent cache writes, or running host.
    cache_.Configure("", 1024, false);
    cache_.SetIdentity(identity_);
  }
  bool pointForbidden(Point p) const override{return blocked(p,p,0);}
  bool segmentForbidden(Point a,Point b,double m) const override{return blocked(a,b,m);}
  double distanceToForbiddenNm(Point p) const override{return coast_->distanceToForbiddenNm(p);}
  std::optional<double> depthMetres(Point p) const override {
    if(!ensure(p,p,0))return {};
    const long lat=long(std::floor(p.latitude/.05)),lon=weather_routing::CanonicalChartLongitudeTile(long(std::floor(p.longitude/.05)));
    std::shared_ptr<const weather_routing::ChartHazardTile> tile;
    if(!cache_.LookupSnapshot(lat,lon,true,&tile) || !tile)return {};
    const auto row=std::clamp(long(std::floor((p.latitude-lat*.05)/tile->resolution)),0L,long(tile->rows-1));
    const auto col=std::clamp(long(std::floor((weather_routing::CanonicalChartLongitude(p.longitude)-lon*.05)/tile->resolution)),0L,long(tile->cols-1));
    const auto index=std::size_t(row*tile->cols+col);
    if(index>=tile->has_depth.size() || !tile->has_depth[index])return {};
    return tile->min_depth_m[index];
  }
  const auto& missingTileKeys() const{return missing_;}
  std::size_t missingTiles() const{return missing_.size();}
  std::size_t loadedTiles() const{return loaded_.size();}
  std::string identity() const override{return identity_;}
};
