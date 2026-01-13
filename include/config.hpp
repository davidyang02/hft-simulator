#pragma once

#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace hft {

/// Configuration for a Market Maker participant
struct MarketMakerConfig {
  std::string id = "MM1";
  double fairValue = 150.0;
  double halfSpread = 0.03;
  uint32_t quoteSize = 100;
  int levels = 5;
  double levelSpacing = 0.02;
  double requoteIntervalSeconds = 0.2;
  double inventoryLimit = 1000;
  double skewFactor = 0.01;
};

/// Configuration for a Noise Trader participant
struct NoiseTraderConfig {
  std::string id = "NT1";
  double orderRatePerSecond = 3.0;
  double marketOrderProbability = 0.4;
  double priceRange = 0.10;
  uint32_t minQuantity = 10;
  uint32_t maxQuantity = 200;
};

/// Participant configuration (variant)
struct ParticipantConfig {
  std::string type; // "MarketMaker" or "NoiseTrader"
  MarketMakerConfig mm;
  NoiseTraderConfig nt;
};

/// Full simulation configuration
struct SimulationConfig {
  double duration = 30.0;
  std::string symbol = "AAPL";
  std::vector<ParticipantConfig> participants;

  /// Load config from JSON file
  static SimulationConfig load(const std::string &path) {
    std::ifstream file(path);
    if (!file.is_open()) {
      throw std::runtime_error("Cannot open config file: " + path);
    }

    nlohmann::json j;
    file >> j;

    SimulationConfig config;
    config.duration = j.value("duration", 30.0);
    config.symbol = j.value("symbol", "AAPL");

    if (j.contains("participants")) {
      for (const auto &p : j["participants"]) {
        ParticipantConfig pc;
        pc.type = p.value("type", "");

        if (pc.type == "MarketMaker") {
          pc.mm.id = p.value("id", "MM");
          pc.mm.fairValue = p.value("fairValue", 150.0);
          pc.mm.halfSpread = p.value("halfSpread", 0.03);
          pc.mm.quoteSize = p.value("quoteSize", 100);
          pc.mm.levels = p.value("levels", 5);
          pc.mm.levelSpacing = p.value("levelSpacing", 0.02);
          pc.mm.requoteIntervalSeconds = p.value("requoteInterval", 0.2);
          pc.mm.inventoryLimit = p.value("inventoryLimit", 1000.0);
          pc.mm.skewFactor = p.value("skewFactor", 0.01);
        } else if (pc.type == "NoiseTrader") {
          pc.nt.id = p.value("id", "NT");
          pc.nt.orderRatePerSecond = p.value("orderRate", 3.0);
          pc.nt.marketOrderProbability = p.value("marketOrderPct", 0.4);
          pc.nt.priceRange = p.value("priceRange", 0.10);
          pc.nt.minQuantity = p.value("minQty", 10);
          pc.nt.maxQuantity = p.value("maxQty", 200);
        }

        config.participants.push_back(pc);
      }
    }

    return config;
  }

  /// Create default config (same as hardcoded values)
  static SimulationConfig defaultConfig() {
    SimulationConfig config;
    config.duration = 30.0;
    config.symbol = "AAPL";

    // Market Maker 1
    ParticipantConfig mm1;
    mm1.type = "MarketMaker";
    mm1.mm.id = "MM1";
    mm1.mm.fairValue = 150.0;
    mm1.mm.halfSpread = 0.03;
    mm1.mm.quoteSize = 100;
    mm1.mm.levels = 5;
    mm1.mm.requoteIntervalSeconds = 0.2;
    config.participants.push_back(mm1);

    // Market Maker 2
    ParticipantConfig mm2;
    mm2.type = "MarketMaker";
    mm2.mm.id = "MM2";
    mm2.mm.fairValue = 150.0;
    mm2.mm.halfSpread = 0.04;
    mm2.mm.quoteSize = 100;
    mm2.mm.levels = 5;
    mm2.mm.requoteIntervalSeconds = 0.2;
    config.participants.push_back(mm2);

    // Noise Traders
    for (int i = 1; i <= 3; ++i) {
      ParticipantConfig nt;
      nt.type = "NoiseTrader";
      nt.nt.id = "NT" + std::to_string(i);
      nt.nt.orderRatePerSecond = (i == 3) ? 1.5 : 3.0;
      nt.nt.marketOrderProbability = 0.4;
      config.participants.push_back(nt);
    }

    return config;
  }
};

} // namespace hft
