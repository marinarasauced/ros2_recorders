#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp/generic_subscription.hpp"

#include "recorders/csv/file.hpp"
#include "recorders/csv/formatter.hpp"


namespace recorders {

    // One node records all topics. Single thread, so no locks are needed.
    class Node : public rclcpp::Node
    {
    public:

        Node();
        ~Node() override;
        Node(const Node&) = delete;
        Node& operator=(const Node&) = delete;

    private:

        struct Topic {
            std::string name;
            std::string type;
            std::unique_ptr<csv::Formatter> formatter;
            std::unique_ptr<csv::File> file;
            rclcpp::GenericSubscription::SharedPtr subscription;
            std::string line;   // reused for each message
            bool write_error_reported{false};

            // QoS of the current subscription, and a flag set by the incompatible QoS event.
            rmw_qos_reliability_policy_t reliability{RMW_QOS_POLICY_RELIABILITY_RELIABLE};
            rmw_qos_durability_policy_t durability{RMW_QOS_POLICY_DURABILITY_VOLATILE};
            bool qos_mismatch{false};
        };

        std::string directory_;
        std::vector<std::string> pending_;
        std::vector<std::unique_ptr<Topic>> topics_;
        std::chrono::steady_clock::time_point started_;
        double discovery_timeout_{0.0};
        std::size_t queue_depth_{10};
        bool overwrite_{false};
        rclcpp::TimerBase::SharedPtr discovery_timer_;
        rclcpp::TimerBase::SharedPtr flush_timer_;

        void discover();
        void start(const std::string& name, const std::string& type);
        void subscribe(Topic& topic, const rclcpp::QoS& qos);
        void on_message(Topic& topic, const rclcpp::SerializedMessage& msg, const rclcpp::MessageInfo& message_info);
        void flush();
        void resubscribe();

        // Always matches the publishers: best effort if any publisher is best effort,
        // transient local if all publishers are transient local.
        rclcpp::QoS select_qos(const std::string& name) const;
    };

} // namespace recorders
