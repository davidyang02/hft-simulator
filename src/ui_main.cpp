#include "config.hpp"
#include "latency.hpp"
#include "simulation.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/table.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <mutex>
#include <thread>

using namespace ftxui;
using namespace hft;

// Shared state between simulation thread and UI
struct UIState {
  std::mutex mutex;
  std::atomic<bool> running{true};
  std::atomic<bool> simulationComplete{false};

  // Order book snapshot
  std::vector<PriceLevel> bids;
  std::vector<PriceLevel> asks;
  BBO bbo;

  // Trade tape (most recent first)
  struct Trade {
    double price;
    uint32_t qty;
    std::string time;
    bool isBuy; // Aggressor side
  };
  std::deque<Trade> trades;
  static constexpr size_t MAX_TRADES = 15;

  // Basic Statistics
  uint64_t ordersProcessed = 0;
  uint64_t tradesExecuted = 0;
  uint64_t totalVolume = 0;
  double vwap = 0.0;
  double currentTime = 0.0;
  double durationSeconds = 0.0;

  // === LATENCY METRICS (µs) ===
  double latencyP50 = 0.0;
  double latencyP95 = 0.0;
  double latencyP99 = 0.0;
  double latencyP999 = 0.0;
  double latencyMin = 0.0;
  double latencyMax = 0.0;
  double latencyMean = 0.0;

  // === ORDER FLOW METRICS ===
  double ordersPerSecond = 0.0;
  uint64_t ordersCancelled = 0;
  double cancelRate = 0.0; // %
  double fillRate = 0.0;   // %
  double avgFillSize = 0.0;

  // === MICROSTRUCTURE METRICS ===
  double orderImbalance = 0.0;     // (buyOrders - sellOrders) / total
  double tradeImbalance = 0.0;     // Net aggressor side
  double realizedVolatility = 0.0; // Rolling std dev
  double midpointMovement = 0.0;   // Rate of change in mid-price (bps/sec)
  uint64_t buyOrders = 0;
  uint64_t sellOrders = 0;
  uint64_t buyVolume = 0;
  uint64_t sellVolume = 0;

  // === MARKET MAKER METRICS ===
  int64_t mmInventory = 0;      // Net position (long/short)
  double mmPnL = 0.0;           // Realized P&L
  double mmUnrealizedPnL = 0.0; // Mark-to-market P&L
  double mmFairValue = 0.0;     // Current fair value estimate
  double avgQuoteTimeMs = 0.0;  // Average time quotes rest in market

  // Participant stats
  struct ParticipantStats {
    std::string id;
    uint64_t orders;
    uint64_t fills;
    uint64_t volume;
    int64_t inventory; // For market makers
    double pnl;        // For market makers
  };
  std::vector<ParticipantStats> participants;

  // Price history for volatility calc
  std::deque<double> priceHistory;
  static constexpr size_t MAX_PRICE_HISTORY = 250;
};

// Format price
std::string formatPrice(double price) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%.2f", price);
  return buf;
}

// Format quantity
std::string formatQty(uint32_t qty) { return std::to_string(qty); }

// Create order book display
Element orderBookView(const UIState &state) {
  std::vector<std::vector<std::string>> askRows;
  std::vector<std::vector<std::string>> bidRows;

  // Asks (reversed so lowest is at bottom, near spread)
  for (auto it = state.asks.rbegin(); it != state.asks.rend(); ++it) {
    askRows.push_back({"", "", formatPrice(it->price), formatQty(it->totalQty),
                       std::to_string(it->orderCount)});
  }

  // Bids
  for (const auto &level : state.bids) {
    bidRows.push_back({formatQty(level.totalQty),
                       std::to_string(level.orderCount),
                       formatPrice(level.price), "", ""});
  }

  // Build table
  std::vector<std::vector<std::string>> rows;
  rows.push_back({"Bid Qty", "#", "Price", "Ask Qty", "#"});
  for (const auto &row : askRows)
    rows.push_back(row);

  // Spread row
  std::string spreadStr =
      state.bbo.spread() ? formatPrice(*state.bbo.spread()) : "---";
  rows.push_back({"", "", "═══ " + spreadStr + " ═══", "", ""});

  for (const auto &row : bidRows)
    rows.push_back(row);

  auto table = Table(rows);
  table.SelectAll().Border(LIGHT);
  table.SelectRow(0).Decorate(bold);
  table.SelectRow(0).SeparatorVertical(LIGHT);
  table.SelectRow(0).Border(DOUBLE);

  // Color asks red, bids green
  for (size_t i = 1; i <= askRows.size(); ++i) {
    table.SelectRow(i).Decorate(color(Color::Red));
  }
  for (size_t i = askRows.size() + 2; i < rows.size(); ++i) {
    table.SelectRow(i).Decorate(color(Color::Green));
  }

  return vbox({
      text("Order Book") | bold | center,
      separator(),
      table.Render() | flex,
  });
}

// Create trade tape display
Element tradeTapeView(const UIState &state) {
  Elements trades;
  trades.push_back(hbox({
                       text("Time") | size(WIDTH, EQUAL, 10),
                       text("Price") | size(WIDTH, EQUAL, 10),
                       text("Qty") | size(WIDTH, EQUAL, 8),
                   }) |
                   bold);
  trades.push_back(separator());

  for (const auto &trade : state.trades) {
    trades.push_back(hbox({
        text(trade.time) | size(WIDTH, EQUAL, 10),
        text(formatPrice(trade.price)) | size(WIDTH, EQUAL, 10) |
            color(Color::Yellow),
        text(formatQty(trade.qty)) | size(WIDTH, EQUAL, 8),
    }));
  }

  return vbox({
      text("Trade Tape") | bold | center,
      separator(),
      vbox(trades) | flex,
  });
}

// Create statistics panel
Element statsView(const UIState &state) {
  double progress = state.durationSeconds > 0
                        ? state.currentTime / state.durationSeconds
                        : 0.0;

  return vbox({
      text("Statistics") | bold | center,
      separator(),
      hbox({text("Time: "), text(formatPrice(state.currentTime) + "s") | bold}),
      hbox({text("Progress: "), gauge(progress) | flex}),
      separator(),
      hbox({text("Orders: "),
            text(std::to_string(state.ordersProcessed)) | bold}),
      hbox({text("Trades: "),
            text(std::to_string(state.tradesExecuted)) | bold}),
      hbox({text("Volume: "), text(std::to_string(state.totalVolume)) | bold}),
      hbox({text("VWAP: "),
            text(formatPrice(state.vwap)) | bold | color(Color::Cyan)}),
      separator(),
      hbox({
          text("Bid: "),
          text(state.bbo.bidPrice ? formatPrice(*state.bbo.bidPrice) : "---") |
              color(Color::Green),
          text(" x "),
          text(std::to_string(state.bbo.bidQty)),
      }),
      hbox({
          text("Ask: "),
          text(state.bbo.askPrice ? formatPrice(*state.bbo.askPrice) : "---") |
              color(Color::Red),
          text(" x "),
          text(std::to_string(state.bbo.askQty)),
      }),
  });
}

// Create participant summary
Element participantView(const UIState &state) {
  std::vector<std::vector<std::string>> rows;
  rows.push_back({"ID", "Orders", "Fills", "Volume"});

  for (const auto &p : state.participants) {
    rows.push_back({p.id, std::to_string(p.orders), std::to_string(p.fills),
                    std::to_string(p.volume)});
  }

  auto table = Table(rows);
  table.SelectAll().Border(LIGHT);
  table.SelectRow(0).Decorate(bold);
  table.SelectRow(0).Border(DOUBLE);

  return vbox({
      text("Participants") | bold | center,
      separator(),
      table.Render(),
  });
}

// Create market metrics view (algo-trading stats)
Element marketMetricsView(const UIState &state) {
  char buf[64];
  Elements metrics;

  // === LATENCY SECTION (in nanoseconds for HFT precision) ===
  metrics.push_back(text("Latency (ns)") | bold | color(Color::Cyan));

  // Convert µs back to ns for display (more precision for HFT)
  snprintf(buf, sizeof(buf), "p50: %.0f  p95: %.0f", state.latencyP50 * 1000,
           state.latencyP95 * 1000);
  metrics.push_back(text(buf));

  snprintf(buf, sizeof(buf), "p99: %.0f  p99.9: %.0f", state.latencyP99 * 1000,
           state.latencyP999 * 1000);
  metrics.push_back(text(buf));

  snprintf(buf, sizeof(buf), "min: %.0f  max: %.0f", state.latencyMin * 1000,
           state.latencyMax * 1000);
  metrics.push_back(text(buf) | dim);

  metrics.push_back(separator());

  // === ORDER FLOW SECTION ===
  metrics.push_back(text("Order Flow") | bold | color(Color::Yellow));

  snprintf(buf, sizeof(buf), "%.0f orders/sec", state.ordersPerSecond);
  metrics.push_back(text(buf));

  snprintf(buf, sizeof(buf), "Fill Rate: %.1f%%", state.fillRate);
  metrics.push_back(hbox(
      {text(buf) | color(state.fillRate > 50 ? Color::Green : Color::Red)}));

  snprintf(buf, sizeof(buf), "Cancel Rate: %.1f%%", state.cancelRate);
  metrics.push_back(text(buf));

  snprintf(buf, sizeof(buf), "Avg Fill Size: %.0f", state.avgFillSize);
  metrics.push_back(text(buf) | dim);

  metrics.push_back(separator());

  // === MICROSTRUCTURE SECTION ===
  metrics.push_back(text("Microstructure") | bold | color(Color::Magenta));

  // Order imbalance bar
  snprintf(buf, sizeof(buf), "Order Imbalance: %+.1f%%",
           state.orderImbalance * 100);
  auto imbalanceColor = state.orderImbalance >= 0 ? Color::Green : Color::Red;
  metrics.push_back(text(buf) | color(imbalanceColor));

  // Trade imbalance
  snprintf(buf, sizeof(buf), "Trade Imbalance: %+.1f%%",
           state.tradeImbalance * 100);
  auto tradeColor = state.tradeImbalance >= 0 ? Color::Green : Color::Red;
  metrics.push_back(text(buf) | color(tradeColor));

  // Realized volatility (as bps)
  snprintf(buf, sizeof(buf), "Volatility: %.2f bps",
           state.realizedVolatility * 10000);
  metrics.push_back(text(buf));

  // Midpoint movement (bps/sec)
  snprintf(buf, sizeof(buf), "Mid Move: %+.2f bps/s", state.midpointMovement);
  auto moveColor = state.midpointMovement >= 0 ? Color::Green : Color::Red;
  metrics.push_back(text(buf) | color(moveColor));

  // VWAP
  snprintf(buf, sizeof(buf), "VWAP: %.2f", state.vwap);
  metrics.push_back(text(buf) | color(Color::Cyan));

  metrics.push_back(separator());

  // === MARKET MAKER SECTION ===
  metrics.push_back(text("Market Maker") | bold | color(Color::Blue));

  snprintf(buf, sizeof(buf), "Inventory: %+lld",
           static_cast<long long>(state.mmInventory));
  auto invColor = state.mmInventory == 0
                      ? Color::White
                      : (state.mmInventory > 0 ? Color::Green : Color::Red);
  metrics.push_back(text(buf) | color(invColor));

  snprintf(buf, sizeof(buf), "P&L: $%+.2f", state.mmPnL);
  auto pnlColor = state.mmPnL >= 0 ? Color::Green : Color::Red;
  metrics.push_back(text(buf) | color(pnlColor));

  snprintf(buf, sizeof(buf), "Unreal P&L: $%+.2f", state.mmUnrealizedPnL);
  auto upnlColor = state.mmUnrealizedPnL >= 0 ? Color::Green : Color::Red;
  metrics.push_back(text(buf) | color(upnlColor) | dim);

  snprintf(buf, sizeof(buf), "Fair Value: %.2f", state.mmFairValue);
  metrics.push_back(text(buf));

  metrics.push_back(separator());

  // === VOLUME SECTION ===
  metrics.push_back(text("Volume") | bold);

  snprintf(buf, sizeof(buf), "Buy:  %llu",
           static_cast<unsigned long long>(state.buyVolume));
  metrics.push_back(text(buf) | color(Color::Green));

  snprintf(buf, sizeof(buf), "Sell: %llu",
           static_cast<unsigned long long>(state.sellVolume));
  metrics.push_back(text(buf) | color(Color::Red));

  // Calculate spread
  double spread = 0.0;
  if (state.bbo.bidPrice && state.bbo.askPrice) {
    spread = *state.bbo.askPrice - *state.bbo.bidPrice;
  }
  snprintf(buf, sizeof(buf), "Spread: %.2f", spread);
  metrics.push_back(text(buf) | color(Color::Yellow));

  return vbox(std::move(metrics));
}

// Create ASCII depth histogram
Element depthHistogramView(const UIState &state) {
  const int maxBarWidth = 25; // Max width for bars on each side

  // Find max quantity for scaling
  uint32_t maxQty = 1;
  for (const auto &level : state.bids)
    maxQty = std::max(maxQty, level.totalQty);
  for (const auto &level : state.asks)
    maxQty = std::max(maxQty, level.totalQty);

  Elements rows;
  rows.push_back(text("Order Book Depth") | bold | center);
  rows.push_back(separator());

  // Get depth levels (up to 10)
  const size_t maxLevels = 10;

  // Collect all price levels in order (asks high to low, then bids high to low)
  std::vector<std::tuple<double, uint32_t, bool>> levels; // price, qty, isBid

  // Add asks (in reverse so highest is at top)
  for (size_t i = std::min(state.asks.size(), maxLevels); i > 0; --i) {
    levels.push_back(
        {state.asks[i - 1].price, state.asks[i - 1].totalQty, false});
  }

  // Add spread marker if we have both sides
  bool hasSpread = state.bbo.bidPrice && state.bbo.askPrice;

  // Add bids
  for (size_t i = 0; i < std::min(state.bids.size(), maxLevels); ++i) {
    levels.push_back({state.bids[i].price, state.bids[i].totalQty, true});
  }

  // Render each level
  for (const auto &[price, qty, isBid] : levels) {
    int barLen =
        static_cast<int>((static_cast<double>(qty) / maxQty) * maxBarWidth);
    barLen = std::max(1, barLen);

    char priceBuf[16];
    snprintf(priceBuf, sizeof(priceBuf), "%7.2f", price);

    if (isBid) {
      // Bids: bar extends LEFT from price, right-aligned
      std::string leftPad(maxBarWidth - barLen, ' ');
      std::string bar(barLen, '#');
      rows.push_back(hbox({
          text(leftPad),
          text(bar) | color(Color::Green),
          text(" "),
          text(priceBuf) | bold,
          text(std::string(maxBarWidth + 1, ' ')),
      }));
    } else {
      // Asks: bar extends RIGHT from price
      std::string bar(barLen, '#');
      std::string rightPad(maxBarWidth - barLen, ' ');
      rows.push_back(hbox({
          text(std::string(maxBarWidth + 1, ' ')),
          text(priceBuf) | bold,
          text(" "),
          text(bar) | color(Color::Red),
          text(rightPad),
      }));
    }

    // Insert spread line after last ask before first bid
    if (!isBid && hasSpread) {
      // Check if next is a bid
      hasSpread = false; // Only show once
      double spread = *state.bbo.askPrice - *state.bbo.bidPrice;
      char spreadBuf[48];
      snprintf(spreadBuf, sizeof(spreadBuf),
               "------------ Spread: %.2f ------------", spread);
      rows.push_back(text(spreadBuf) | center | color(Color::Yellow) | dim);
    }
  }

  return vbox(std::move(rows)) | center;
}

int main(int argc, char *argv[]) {
  // Parse CLI arguments
  std::string configPath;
  double durationOverride = 0.0;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if ((arg == "-d" || arg == "--duration") && i + 1 < argc) {
      durationOverride = std::stod(argv[++i]);
    } else if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
      configPath = argv[++i];
    }
  }

  // Load config (from file or default)
  SimulationConfig simCfg;
  if (!configPath.empty()) {
    try {
      simCfg = SimulationConfig::load(configPath);
    } catch (const std::exception &e) {
      std::cerr << "Error loading config: " << e.what() << std::endl;
      return 1;
    }
  } else {
    simCfg = SimulationConfig::defaultConfig();
  }

  // Override duration if specified on CLI
  double duration = (durationOverride > 0) ? durationOverride : simCfg.duration;

  UIState state;
  state.durationSeconds = duration;

  // Create screen
  auto screen = ScreenInteractive::Fullscreen();

  // Simulation thread
  std::thread simThread([&state, duration, simCfg]() {
    // Set up simulation
    Simulation::Config internalConfig;
    internalConfig.durationSeconds = duration;
    internalConfig.snapshotIntervalSeconds = 0.1;
    internalConfig.verbose = false;

    // We need access to internals, so we'll create components manually
    SimulationClock clock;
    OrderBook book(simCfg.symbol);
    MatchingEngine engine(book);
    EventScheduler scheduler(clock);

    // Enable latency tracking for real measurements
    engine.setLatencyTracking(true);

    // Create participants from config
    std::vector<std::unique_ptr<Participant>> participants;

    for (const auto &pc : simCfg.participants) {
      if (pc.type == "MarketMaker") {
        MarketMaker::Config mmConfig;
        mmConfig.fairValue = pc.mm.fairValue;
        mmConfig.halfSpread = pc.mm.halfSpread;
        mmConfig.quoteSize = pc.mm.quoteSize;
        mmConfig.levels = pc.mm.levels;
        mmConfig.levelSpacing = pc.mm.levelSpacing;
        mmConfig.requoteIntervalSeconds = pc.mm.requoteIntervalSeconds;
        mmConfig.inventoryLimit = pc.mm.inventoryLimit;
        mmConfig.skewFactor = pc.mm.skewFactor;

        participants.push_back(std::make_unique<MarketMaker>(
            pc.mm.id, engine, scheduler, book, mmConfig));
      } else if (pc.type == "NoiseTrader") {
        NoiseTrader::Config ntConfig;
        ntConfig.orderRatePerSecond = pc.nt.orderRatePerSecond;
        ntConfig.marketOrderProbability = pc.nt.marketOrderProbability;
        ntConfig.priceRange = pc.nt.priceRange;
        ntConfig.minQuantity = pc.nt.minQuantity;
        ntConfig.maxQuantity = pc.nt.maxQuantity;

        participants.push_back(std::make_unique<NoiseTrader>(
            pc.nt.id, engine, scheduler, book, ntConfig));
      }
    }

    // Keep raw pointer to first MM for metrics display
    MarketMaker *mm1 = dynamic_cast<MarketMaker *>(participants[0].get());

    // Tracking variables for metrics
    LatencyHistogram orderLatency("Order Processing");
    uint64_t totalBuyOrders = 0, totalSellOrders = 0;
    uint64_t totalBuyVolume = 0, totalSellVolume = 0;
    uint64_t totalCancels = 0;

    // P&L tracking
    double mmRealizedPnL = 0.0;
    double mmTotalCost = 0.0;  // Cost basis for inventory
    double lastMidPrice = 0.0; // For midpoint movement calc

    // Now set up callback that captures participants by reference
    engine.setExecutionCallback(
        [&state, &clock, &participants, &totalBuyOrders, &totalSellOrders,
         &totalBuyVolume, &totalSellVolume](const ExecutionReport &report) {
          if (report.type == ExecType::FILLED ||
              report.type == ExecType::PARTIAL_FILL) {
            {
              std::lock_guard<std::mutex> lock(state.mutex);

              // Determine if this was a buy or sell (infer from execution
              // price) If exec price >= mid, likely a buy (aggressor hitting
              // ask)
              bool isBuy = true; // Default to buy
              if (state.bbo.midPrice()) {
                isBuy = report.execPrice >= *state.bbo.midPrice();
              }

              // Track buy/sell order counts and volume
              if (isBuy) {
                totalBuyOrders++;
                totalBuyVolume += report.execQty;
              } else {
                totalSellOrders++;
                totalSellVolume += report.execQty;
              }

              // Add to trade tape
              char timeBuf[32];
              snprintf(timeBuf, sizeof(timeBuf), "%.3fs",
                       SimulationClock::toSeconds(clock.now()));
              state.trades.push_front(
                  {report.execPrice, report.execQty, timeBuf, isBuy});
              if (state.trades.size() > UIState::MAX_TRADES) {
                state.trades.pop_back();
              }

              state.tradesExecuted++;
              state.totalVolume += report.execQty;
            }

            // Notify the participant whose order was filled
            for (auto &p : participants) {
              if (p->id() == report.traderId) {
                p->onExecutionReport(report);
                break;
              }
            }
          }
        });

    // Initialize participants (schedule their first events)
    for (auto &p : participants) {
      p->initialize();
    }

    // Schedule UI updates
    auto updateInterval = SimulationClock::fromSeconds(0.1); // 10 FPS
    auto endTime = SimulationClock::fromSeconds(duration);

    for (auto t = updateInterval; t <= endTime; t += updateInterval) {
      scheduler.scheduleAt(
          t,
          [&, t]() {
            std::lock_guard<std::mutex> lock(state.mutex);

            state.bids = book.getBidDepth(10);
            state.asks = book.getAskDepth(10);
            state.bbo = book.getTopOfBook();
            state.ordersProcessed = engine.ordersProcessed();
            state.currentTime = SimulationClock::toSeconds(clock.now());

            // === LATENCY METRICS ===
            auto &latHist = engine.matchingLatency();
            if (latHist.count() > 0) {
              state.latencyP50 = LatencyHistogram::toUs(latHist.p50Ns());
              state.latencyP95 = LatencyHistogram::toUs(latHist.p95Ns());
              state.latencyP99 = LatencyHistogram::toUs(latHist.p99Ns());
              state.latencyP999 = LatencyHistogram::toUs(latHist.p999Ns());
              state.latencyMin = LatencyHistogram::toUs(latHist.minNs());
              state.latencyMax = LatencyHistogram::toUs(latHist.maxNs());
              state.latencyMean = LatencyHistogram::toUs(
                  static_cast<int64_t>(latHist.meanNs()));
            }

            // === ORDER FLOW METRICS ===
            double elapsedTime = state.currentTime;
            if (elapsedTime > 0) {
              state.ordersPerSecond = state.ordersProcessed / elapsedTime;
            }

            // Calculate fill and cancel rates from participant data
            uint64_t totalOrders = 0, totalFills = 0;
            for (const auto &p : participants) {
              totalOrders += p->ordersSubmitted();
              totalFills += p->fillsReceived();
            }

            if (totalOrders > 0) {
              state.fillRate = 100.0 * totalFills / totalOrders;
              state.cancelRate = 100.0 * totalCancels / totalOrders;
            }

            if (totalFills > 0) {
              state.avgFillSize =
                  static_cast<double>(state.totalVolume) / totalFills;
            }

            // === MICROSTRUCTURE METRICS ===
            state.buyOrders = totalBuyOrders;
            state.sellOrders = totalSellOrders;
            state.buyVolume = totalBuyVolume;
            state.sellVolume = totalSellVolume;

            uint64_t totalOrderCount = totalBuyOrders + totalSellOrders;
            if (totalOrderCount > 0) {
              state.orderImbalance =
                  static_cast<double>(static_cast<int64_t>(totalBuyOrders) -
                                      static_cast<int64_t>(totalSellOrders)) /
                  totalOrderCount;
            }

            uint64_t totalVol = totalBuyVolume + totalSellVolume;
            if (totalVol > 0) {
              state.tradeImbalance =
                  static_cast<double>(static_cast<int64_t>(totalBuyVolume) -
                                      static_cast<int64_t>(totalSellVolume)) /
                  totalVol;
            }

            // Realized volatility (std dev of returns)
            if (state.priceHistory.size() >= 10) {
              double sumReturns = 0, sumSq = 0;
              size_t n = 0;
              for (size_t i = 1; i < state.priceHistory.size(); ++i) {
                double ret =
                    (state.priceHistory[i] - state.priceHistory[i - 1]) /
                    state.priceHistory[i - 1];
                sumReturns += ret;
                sumSq += ret * ret;
                n++;
              }
              if (n > 1) {
                double mean = sumReturns / n;
                state.realizedVolatility =
                    std::sqrt((sumSq / n) - (mean * mean));
              }
            }

            // Calculate VWAP
            if (state.totalVolume > 0 && !state.trades.empty()) {
              double sumPV = 0;
              uint64_t sumV = 0;
              for (const auto &t : state.trades) {
                sumPV += t.price * t.qty;
                sumV += t.qty;
              }
              state.vwap = sumV > 0 ? sumPV / sumV : 0;
            }

            // === MIDPOINT MOVEMENT ===
            if (state.bbo.midPrice()) {
              double currentMid = *state.bbo.midPrice();
              if (lastMidPrice > 0 && elapsedTime > 0) {
                // Return in bps per second
                double returnBps =
                    ((currentMid - lastMidPrice) / lastMidPrice) * 10000;
                state.midpointMovement =
                    returnBps / 0.1; // Per second (update is 0.1s)
              }
              lastMidPrice = currentMid;
            }

            // === MARKET MAKER METRICS ===
            if (mm1) {
              state.mmInventory = mm1->inventory();
              state.mmFairValue = mm1->fairValue();

              // Simple unrealized P&L: inventory * (mid - fair value)
              if (state.bbo.midPrice()) {
                double mid = *state.bbo.midPrice();
                state.mmUnrealizedPnL =
                    state.mmInventory * (mid - state.mmFairValue);
              }
            }

            // Update participant stats (with inventory for MMs)
            state.participants.clear();
            for (const auto &p : participants) {
              int64_t inv = 0;
              double pnl = 0.0;

              // Try to cast to MarketMaker to get inventory
              if (auto *mmPtr = dynamic_cast<MarketMaker *>(p.get())) {
                inv = mmPtr->inventory();
                // Simple P&L estimate
                if (state.bbo.midPrice()) {
                  pnl = inv * (*state.bbo.midPrice() - mmPtr->fairValue());
                }
              }

              state.participants.push_back({p->id(), p->ordersSubmitted(),
                                            p->fillsReceived(),
                                            p->volumeTraded(), inv, pnl});
            }

            // Record price history for volatility calc
            if (state.bbo.midPrice()) {
              state.priceHistory.push_back(*state.bbo.midPrice());
              if (state.priceHistory.size() > UIState::MAX_PRICE_HISTORY) {
                state.priceHistory.pop_front();
              }
            }
          },
          1000); // Lower priority
    }

    // Run simulation in REAL TIME (process events with wall-clock delays)
    auto realStartTime = std::chrono::steady_clock::now();
    while (scheduler.hasPendingEvents() && state.running) {
      // Process next batch of events
      scheduler.processNext();

      // Calculate how much sim time has passed
      double simTimeNow = SimulationClock::toSeconds(clock.now());

      // Calculate how much real time should have passed
      auto expectedRealDuration =
          std::chrono::milliseconds(static_cast<int>(simTimeNow * 1000));
      auto targetRealTime = realStartTime + expectedRealDuration;

      // Sleep until we catch up to real time
      auto now = std::chrono::steady_clock::now();
      if (now < targetRealTime) {
        std::this_thread::sleep_until(targetRealTime);
      }
    }

    // Final update
    {
      std::lock_guard<std::mutex> lock(state.mutex);
      state.bids = book.getBidDepth(5);
      state.asks = book.getAskDepth(5);
      state.bbo = book.getTopOfBook();
      state.ordersProcessed = engine.ordersProcessed();
      state.currentTime = duration;
      state.simulationComplete = true;
    }
  });

  // UI renderer
  auto renderer = Renderer([&state] {
    std::lock_guard<std::mutex> lock(state.mutex);

    std::string title = state.simulationComplete
                            ? " HFT Simulator - COMPLETE (Press Q to exit) "
                            : " HFT Simulator - Running... ";

    return vbox({
               text(title) | bold | center | color(Color::Cyan),
               separator(),
               hbox({
                   orderBookView(state) | size(WIDTH, EQUAL, 30),
                   separator(),
                   vbox({
                       marketMetricsView(state),
                       separator(),
                       depthHistogramView(state) | flex,
                   }) | flex,
                   separator(),
                   vbox({
                       tradeTapeView(state) | flex,
                       separator(),
                       statsView(state),
                   }) | size(WIDTH, EQUAL, 32),
                   separator(),
                   participantView(state) | size(WIDTH, EQUAL, 32),
               }) | flex,
           }) |
           border;
  });

  // Handle input
  auto component = CatchEvent(renderer, [&](Event event) {
    if (event == Event::Character('q') || event == Event::Character('Q') ||
        event == Event::Escape) {
      state.running = false;
      screen.Exit();
      return true;
    }
    return false;
  });

  // Refresh loop
  std::thread refreshThread([&screen, &state]() {
    while (state.running) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      screen.PostEvent(Event::Custom);
    }
  });

  screen.Loop(component);

  state.running = false;
  refreshThread.join();
  simThread.join();

  return 0;
}
