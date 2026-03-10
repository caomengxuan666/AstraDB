// ==============================================================================
// Metrics HTTP Server - Asio-based Prometheus metrics endpoint
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include "metrics.hpp"
#include <asio.hpp>
#include <sstream>
#include <thread>
#include <memory>

namespace astra::metrics {

// ASIO-based HTTP server for Prometheus metrics
class MetricsHTTPServer : public std::enable_shared_from_this<MetricsHTTPServer> {
 public:
  static std::shared_ptr<MetricsHTTPServer> Create(asio::io_context& io_context, 
                                                     const std::string& bind_addr, 
                                                     uint16_t port,
                                                     std::shared_ptr<prometheus::Registry> registry) {
    auto server = std::shared_ptr<MetricsHTTPServer>(
      new MetricsHTTPServer(io_context, bind_addr, port, registry));
    server->Start();
    return server;
  }

 private:
  MetricsHTTPServer(asio::io_context& io_context, 
                    const std::string& bind_addr, 
                    uint16_t port,
                    std::shared_ptr<prometheus::Registry> registry)
      : acceptor_(io_context),
        socket_(io_context),
        registry_(registry) {
    asio::ip::tcp::endpoint endpoint(asio::ip::address::from_string(bind_addr), port);
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen();
    
    ASTRADB_LOG_INFO("Metrics HTTP server listening on {}:{}", bind_addr, port);
  }

  void Start() {
    AcceptConnection();
  }

  void AcceptConnection() {
    acceptor_.async_accept(socket_, [self = shared_from_this()](std::error_code ec) {
      if (!ec) {
        self->HandleConnection();
      }
      self->AcceptConnection();
    });
  }

  void HandleConnection() {
    std::shared_ptr<asio::ip::tcp::socket> socket = 
      std::make_shared<asio::ip::tcp::socket>(std::move(socket_));
    
    // Read request
    std::shared_ptr<std::array<char, 4096>> buffer = 
      std::make_shared<std::array<char, 4096>>();
    
    socket->async_read_some(asio::buffer(*buffer),
      [this, socket, buffer](std::error_code ec, size_t bytes_read) {
        if (ec) {
          return;
        }
        
        // Parse request
        std::string request(buffer->data(), bytes_read);
        
        // Check if it's a GET /metrics request
        std::string response;
        if (request.find("GET /metrics") == 0) {
          // Collect metrics
          std::vector<prometheus::MetricFamily> metrics = registry_->Collect();
          
          // Format as Prometheus text format
          std::string body = SerializeMetrics(metrics);
          
          // Send HTTP response
          response = 
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain; version=0.0.4; charset=utf-8\r\n"
            "Content-Length: " + std::to_string(body.size()) + "\r\n"
            "\r\n" + body;
        } else {
          // Send 404
          response = 
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Length: 0\r\n"
            "\r\n";
        }
        
        // Send response
        std::shared_ptr<std::string> response_ptr = 
          std::make_shared<std::string>(response);
        
        asio::async_write(*socket, asio::buffer(*response_ptr),
          [socket](std::error_code ec, size_t) {
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

  asio::ip::tcp::acceptor acceptor_;
  asio::ip::tcp::socket socket_;
  std::shared_ptr<prometheus::Registry> registry_;
};

// Global pointer to keep the HTTP server alive
static std::shared_ptr<MetricsHTTPServer> g_http_server;

// Start HTTP server implementation
void MetricsRegistry::StartHTTPServer(asio::io_context& io_context, const MetricsConfig& config) {
  if (!config.enabled || !initialized_.load()) {
    return;
  }
  
  g_http_server = MetricsHTTPServer::Create(io_context, config.bind_addr, config.port, registry_);
}

}  // namespace astra::metrics