// ==============================================================================
// Metrics HTTP Server - Coroutine-based Prometheus metrics endpoint
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include <asio.hpp>
#include <memory>
#include <sstream>

#include "astra/core/async/awaitable_ops.hpp"
#include "metrics.hpp"

namespace astra::metrics {

// ASIO-based HTTP server for Prometheus metrics using coroutines
class MetricsHTTPServer
    : public std::enable_shared_from_this<MetricsHTTPServer> {
 public:
  MetricsHTTPServer(asio::io_context& io_context, const std::string& bind_addr,
                    uint16_t port,
                    std::shared_ptr<prometheus::Registry> registry)
      : io_context_(io_context),
        acceptor_(io_context_),
        registry_(registry),
        bind_addr_(bind_addr),
        port_(port),
        running_(false) {
    asio::ip::tcp::endpoint endpoint(asio::ip::address::from_string(bind_addr),
                                     port);
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen();

    ASTRADB_LOG_INFO("Metrics HTTP server acceptor listening on {}:{}",
                     bind_addr, port);
  }

  void Start() {
    if (running_.exchange(true)) {
      return;
    }

    ASTRADB_LOG_INFO("Starting Metrics HTTP server on {}:{}", bind_addr_,
                     port_);

    // Spawn coroutine for accepting connections
    asio::co_spawn(
        acceptor_.get_executor(),
        [self = shared_from_this()]() -> asio::awaitable<void> {
          co_await self->DoAccept();
        },
        asio::detached);
  }

  void Stop() {
    if (running_.exchange(false)) {
      std::error_code ec;
      acceptor_.close(ec);
    }
  }

  ~MetricsHTTPServer() {
    ASTRADB_LOG_INFO("Shutting down Metrics HTTP server");
    Stop();
  }

 private:
  asio::awaitable<void> DoAccept() {
    while (running_.load()) {
      auto socket = std::make_shared<asio::ip::tcp::socket>(
          co_await asio::this_coro::executor);

      asio::error_code ec;
      co_await acceptor_.async_accept(
          *socket, asio::redirect_error(asio::use_awaitable, ec));

      if (ec) {
        if (running_.load()) {
          ASTRADB_LOG_ERROR("Metrics HTTP: async_accept error: {}",
                            ec.message());
        }
        continue;
      }

      ASTRADB_LOG_DEBUG("Metrics HTTP: Connection accepted from {}",
                        socket->remote_endpoint().address().to_string());

      // Spawn coroutine for handling this connection
      asio::co_spawn(
          socket->get_executor(),
          [self = shared_from_this(), socket]() -> asio::awaitable<void> {
            co_await self->HandleConnection(socket);
          },
          asio::detached);
    }
  }

  asio::awaitable<void> HandleConnection(
      std::shared_ptr<asio::ip::tcp::socket> socket) {
    auto executor = co_await asio::this_coro::executor;
    std::array<char, 4096> buffer;

    asio::error_code ec;
    size_t bytes_read = co_await socket->async_read_some(
        asio::buffer(buffer), asio::redirect_error(asio::use_awaitable, ec));

    if (ec) {
      ASTRADB_LOG_DEBUG("Metrics HTTP: Read error: {}", ec.message());
      co_return;
    }

    std::string request(buffer.data(), bytes_read);
    std::string response;

    if (request.find("GET /metrics") == 0) {
      std::vector<prometheus::MetricFamily> metrics = registry_->Collect();
      std::string body = SerializeMetrics(metrics);
      response =
          "HTTP/1.1 200 OK\r\n"
          "Content-Type: text/plain; version=0.0.4; charset=utf-8\r\n"
          "Content-Length: " +
          std::to_string(body.size()) + "\r\n\r\n" + body;

      ASTRADB_LOG_DEBUG("Metrics HTTP: Sending metrics response (size={})",
                        body.size());
    } else {
      response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
      ASTRADB_LOG_DEBUG("Metrics HTTP: 404 Not Found");
    }

    co_await asio::async_write(*socket, asio::buffer(response),
                               asio::redirect_error(asio::use_awaitable, ec));

    if (!ec) {
      ASTRADB_LOG_DEBUG("Metrics HTTP: Response sent successfully");
    } else {
      ASTRADB_LOG_ERROR("Metrics HTTP: Write error: {}", ec.message());
    }

    socket->close(ec);
  }

  std::string SerializeMetrics(
      const std::vector<prometheus::MetricFamily>& metrics) {
    std::ostringstream oss;

    for (const auto& family : metrics) {
      oss << "# HELP " << family.name << " " << family.help << "\n";
      oss << "# TYPE " << family.name << " " << MetricTypeToString(family.type)
          << "\n";

      for (const auto& metric : family.metric) {
        std::string labels = SerializeLabels(metric.label);

        switch (family.type) {
          case prometheus::MetricType::Counter:
            oss << family.name << labels << " " << metric.counter.value << "\n";
            break;
          case prometheus::MetricType::Gauge:
            oss << family.name << labels << " " << metric.gauge.value << "\n";
            break;
          case prometheus::MetricType::Histogram:
            oss << family.name << "_count" << labels << " "
                << metric.histogram.sample_count << "\n";
            oss << family.name << "_sum" << labels << " "
                << metric.histogram.sample_sum << "\n";
            for (size_t i = 0; i < metric.histogram.bucket.size(); ++i) {
              oss << family.name << "_bucket" << labels << "{le=\""
                  << metric.histogram.bucket[i].upper_bound << "\"} "
                  << metric.histogram.bucket[i].cumulative_count << "\n";
            }
            oss << family.name << "_bucket" << labels << "{le=\"+Inf\"} "
                << metric.histogram.sample_count << "\n";
            break;
          case prometheus::MetricType::Summary:
            oss << family.name << "_count" << labels << " "
                << metric.summary.sample_count << "\n";
            oss << family.name << "_sum" << labels << " "
                << metric.summary.sample_sum << "\n";
            for (size_t i = 0; i < metric.summary.quantile.size(); ++i) {
              oss << family.name << labels << "{quantile=\""
                  << metric.summary.quantile[i].quantile << "\"} "
                  << metric.summary.quantile[i].value << "\n";
            }
            break;
          case prometheus::MetricType::Untyped:
            oss << family.name << labels << " " << metric.untyped.value << "\n";
            break;
          case prometheus::MetricType::Info:
            oss << family.name << labels << " 1\n";
            break;
        }
      }
    }

    return oss.str();
  }

  std::string MetricTypeToString(prometheus::MetricType type) {
    switch (type) {
      case prometheus::MetricType::Counter:
        return "counter";
      case prometheus::MetricType::Gauge:
        return "gauge";
      case prometheus::MetricType::Histogram:
        return "histogram";
      case prometheus::MetricType::Summary:
        return "summary";
      case prometheus::MetricType::Untyped:
        return "untyped";
      case prometheus::MetricType::Info:
        return "info";
      default:
        return "unknown";
    }
  }

  std::string SerializeLabels(
      const std::vector<prometheus::ClientMetric::Label>& labels) {
    if (labels.empty()) return "";

    std::ostringstream oss;
    oss << "{";
    for (size_t i = 0; i < labels.size(); ++i) {
      oss << labels[i].name << "=\"" << labels[i].value << "\"";
      if (i < labels.size() - 1) oss << ",";
    }
    oss << "}";
    return oss.str();
  }

  asio::io_context& io_context_;
  asio::ip::tcp::acceptor acceptor_;
  std::shared_ptr<prometheus::Registry> registry_;
  std::string bind_addr_;
  uint16_t port_;
  std::atomic<bool> running_;
};

// Global pointer to keep the HTTP server alive
static std::shared_ptr<MetricsHTTPServer> g_http_server;

// Start HTTP server implementation
void MetricsRegistry::StartHTTPServer(asio::io_context& io_context,
                                      const MetricsConfig& config) {
  if (!config.enabled || !initialized_.load()) {
    return;
  }

  g_http_server = std::make_shared<MetricsHTTPServer>(
      io_context, config.bind_addr, config.port, registry_);
  g_http_server->Start();
}

// Stop HTTP server implementation
void MetricsRegistry::StopHTTPServer() {
  if (g_http_server) {
    g_http_server->Stop();
    g_http_server.reset();
  }
}

}  // namespace astra::metrics
