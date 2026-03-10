// ==============================================================================
// Metrics HTTP Server - Asio-based Prometheus metrics endpoint
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include "metrics.hpp"
#include "astra/core/async/thread_pool.hpp"
#include <asio.hpp>
#include <sstream>
#include <memory>

namespace astra::metrics {

// ASIO-based HTTP server for Prometheus metrics using server's io_context
class MetricsHTTPServer : public std::enable_shared_from_this<MetricsHTTPServer> {
 public:
  MetricsHTTPServer(asio::io_context& io_context,
                    const std::string& bind_addr, 
                    uint16_t port,
                    std::shared_ptr<prometheus::Registry> registry)
      : io_context_(io_context),
        acceptor_(io_context_),
        registry_(registry),
        bind_addr_(bind_addr),
        port_(port),
        running_(false) {
    asio::ip::tcp::endpoint endpoint(asio::ip::address::from_string(bind_addr), port);
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen();
    
    ASTRADB_LOG_INFO("Metrics HTTP server acceptor listening on {}:{}", bind_addr, port);
  }

  void Start() {
    if (running_.exchange(true)) {
      return;
    }
    
    ASTRADB_LOG_INFO("Starting Metrics HTTP server on {}:{}", bind_addr_, port_);
    AcceptConnection();
  }

  ~MetricsHTTPServer() {
    ASTRADB_LOG_INFO("Shutting down Metrics HTTP server");
    
    if (running_.exchange(false)) {
      std::error_code ec;
      acceptor_.close(ec);
    }
  }

 private:
  void AcceptConnection() {
    ASTRADB_LOG_DEBUG("Metrics HTTP: AcceptConnection called");
    
    if (!acceptor_.is_open()) {
      ASTRADB_LOG_ERROR("Metrics HTTP: Acceptor is not open");
      return;
    }
    
    auto socket = std::make_shared<asio::ip::tcp::socket>(io_context_);
    acceptor_.async_accept(*socket, [this, socket](std::error_code ec) {
      ASTRADB_LOG_DEBUG("Metrics HTTP: async_accept callback, ec={}", ec.message());
      
      if (running_.load()) {
        if (!ec) {
          ASTRADB_LOG_DEBUG("Metrics HTTP: Calling HandleConnection");
          HandleConnection(socket);
        } else {
          ASTRADB_LOG_ERROR("Metrics HTTP: async_accept error: {}", ec.message());
        }
        AcceptConnection();
      }
    });
  }

  void HandleConnection(std::shared_ptr<asio::ip::tcp::socket> socket) {
    auto buffer = std::make_shared<std::array<char, 4096>>();
    socket->async_read_some(asio::buffer(*buffer),
      [this, socket, buffer](std::error_code ec, size_t bytes_read) {
        if (ec) {
          return;
        }
        
        std::string request(buffer->data(), bytes_read);
        std::string response;
        
        if (request.find("GET /metrics") == 0) {
          std::vector<prometheus::MetricFamily> metrics = registry_->Collect();
          std::string body = SerializeMetrics(metrics);
          response = "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/plain; version=0.0.4; charset=utf-8\r\n"
                    "Content-Length: " + std::to_string(body.size()) + "\r\n"
                    "\r\n" + body;
        } else {
          response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        }
        
        auto response_ptr = std::make_shared<std::string>(response);
        asio::async_write(*socket, asio::buffer(*response_ptr),
          [socket](std::error_code, size_t) {
            socket->close();
          });
      });
  }

  std::string SerializeMetrics(const std::vector<prometheus::MetricFamily>& metrics) {
    std::ostringstream oss;
    
    for (const auto& family : metrics) {
      oss << "# HELP " << family.name << " " << family.help << "\n";
      oss << "# TYPE " << family.name << " " << MetricTypeToString(family.type) << "\n";
      
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
            oss << family.name << "_count" << labels << " " << metric.histogram.sample_count << "\n";
            oss << family.name << "_sum" << labels << " " << metric.histogram.sample_sum << "\n";
            for (size_t i = 0; i < metric.histogram.bucket.size(); ++i) {
              oss << family.name << "_bucket" << labels
                  << "{le=\"" << metric.histogram.bucket[i].upper_bound << "\"} "
                  << metric.histogram.bucket[i].cumulative_count << "\n";
            }
            oss << family.name << "_bucket" << labels
                << "{le=\"+Inf\"} " << metric.histogram.sample_count << "\n";
            break;
          case prometheus::MetricType::Summary:
            oss << family.name << "_count" << labels << " " << metric.summary.sample_count << "\n";
            oss << family.name << "_sum" << labels << " " << metric.summary.sample_sum << "\n";
            for (size_t i = 0; i < metric.summary.quantile.size(); ++i) {
              oss << family.name << labels
                  << "{quantile=\"" << metric.summary.quantile[i].quantile << "\"} "
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
      case prometheus::MetricType::Counter: return "counter";
      case prometheus::MetricType::Gauge: return "gauge";
      case prometheus::MetricType::Histogram: return "histogram";
      case prometheus::MetricType::Summary: return "summary";
      case prometheus::MetricType::Untyped: return "untyped";
      case prometheus::MetricType::Info: return "info";
      default: return "unknown";
    }
  }

  std::string SerializeLabels(const std::vector<prometheus::ClientMetric::Label>& labels) {
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
void MetricsRegistry::StartHTTPServer(asio::io_context& io_context, const MetricsConfig& config) {
  if (!config.enabled || !initialized_.load()) {
    return;
  }
  
  g_http_server = std::make_shared<MetricsHTTPServer>(io_context, config.bind_addr, config.port, registry_);
  g_http_server->Start();
}

}  // namespace astra::metrics