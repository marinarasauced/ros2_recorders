
#include "recorders/node.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <set>
#include <sstream>


namespace recorders {

    namespace impl {

        // "~/logs" -> "/home/user/logs"
        std::string expand_home(
            const std::string& path
        )
        {
            if (!path.empty() && path[0] == '~' && (path.size() == 1 || path[1] == '/')) {
                if (const char* home = std::getenv("HOME")) {
                    return std::string(home) + path.substr(1);
                }
            }
            return path;
        }

        // "<base>/NNNN_YYYY-MM-DD_HH-MM-SS", NNNN is one more than the highest number in base.
        std::filesystem::path next_trial_dir(
            const std::filesystem::path& base
        )
        {
            int index_max = 0;
            std::error_code error_code;
            for (std::filesystem::directory_iterator it(base, error_code), end; !error_code && it != end; it.increment(error_code)) {
                const std::string name = it->path().filename().string();
                if (name.size() > 5 && name[4] == '_' &&
                    std::all_of(name.begin(), name.begin() + 4, [](unsigned char c) { return std::isdigit(c); }))
                {
                    index_max = std::max(index_max, std::stoi(name.substr(0, 4)));
                }
            }
            const std::time_t now = std::time(nullptr);
            std::ostringstream timestamp;
            timestamp << std::setw(4) << std::setfill('0') << index_max + 1 << '_'
                  << std::put_time(std::localtime(&now), "%Y-%m-%d_%H-%M-%S");
            return base / timestamp.str();
        }

        // Accepts ["a", "b"] and ["a, b"]: split on commas, trim, drop empty entries.
        std::vector<std::string> split_topics(
            const std::vector<std::string>& entries
        )
        {
            std::vector<std::string> out;
            for (const auto& entry : entries) {
                std::istringstream stream(entry);
                std::string item;
                while (std::getline(stream, item, ',')) {
                    const auto begin = item.find_first_not_of(" \t");
                    if (begin == std::string::npos) { continue; }
                    out.push_back(item.substr(begin, item.find_last_not_of(" \t") - begin + 1));
                }
            }
            return out;
        }

        // "/a/b" -> "a__b.csv"
        std::string topic_to_filename(
            const std::string& name
        )
        {
            std::string out;
            for (char c : name.substr(name.find_first_not_of('/'))) {
                if (c == '/') { out += "__"; } else { out += c; }
            }
            return out + ".csv";
        }

        // Parameters are read once at start, so they are read only.
        rcl_interfaces::msg::ParameterDescriptor descriptor(
            const std::string& description
        )
        {
            rcl_interfaces::msg::ParameterDescriptor d;
            d.description = description;
            d.read_only = true;
            return d;
        }

        rcl_interfaces::msg::ParameterDescriptor descriptor(
            const std::string& description,
            double from,
            double to
        )
        {
            auto d = descriptor(description);
            d.floating_point_range.resize(1);
            d.floating_point_range[0].from_value = from;
            d.floating_point_range[0].to_value = to;
            return d;
        }

        rcl_interfaces::msg::ParameterDescriptor descriptor(
            const std::string& description,
            std::int64_t from,
            std::int64_t to
        )
        {
            auto d = descriptor(description);
            d.integer_range.resize(1);
            d.integer_range[0].from_value = from;
            d.integer_range[0].to_value = to;
            return d;
        }

    } // namespace impl


    Node::Node(
    ) : rclcpp::Node("csv_recorder")
    {
        declare_parameter("directory", rclcpp::PARAMETER_STRING, impl::descriptor("Directory for the CSV files. A leading ~ is expanded."));
        declare_parameter("topics", rclcpp::PARAMETER_STRING_ARRAY, impl::descriptor("Topics to record. Relative names use the node namespace."));
        declare_parameter("trial", "", impl::descriptor("Sub-directory of directory. Empty: none. 'auto': next NNNN_<time>. Other: used as the name."));
        declare_parameter("discovery_period", 5.0, impl::descriptor("Seconds between checks for topics that are not advertised yet.", 0.01, 3600.0));
        declare_parameter("discovery_timeout", 60.0, impl::descriptor("Seconds to wait for topics before giving up. 0 waits forever.", 0.0, 86400.0));
        declare_parameter("flush_interval", 1.0, impl::descriptor("Seconds between file flushes.", 0.01, 3600.0));
        declare_parameter("queue_depth", 10, impl::descriptor("Subscription queue depth.", std::int64_t{1}, std::int64_t{100000}));
        declare_parameter("overwrite", false, impl::descriptor("Replace existing CSV files. If false, an existing file is an error."));

        std::vector<std::string> names;
        std::filesystem::path directory;
        try {
            directory = impl::expand_home(get_parameter("directory").as_string());
            names = get_parameter("topics").as_string_array();
        } catch (const rclcpp::exceptions::ParameterUninitializedException& e) {
            throw std::runtime_error(std::string("Required parameter not set : ") + e.what());
        }
        names = impl::split_topics(names);
        if (names.empty()) { throw std::runtime_error("Parameter topics is empty"); }

        const auto trial = get_parameter("trial").as_string();
        if (trial == "auto") {
            directory = impl::next_trial_dir(directory);
        } else if (!trial.empty()) {
            directory /= trial;
        }
        std::filesystem::create_directories(directory);
        directory_ = std::filesystem::absolute(directory).string();

        discovery_timeout_ = get_parameter("discovery_timeout").as_double();
        queue_depth_ = static_cast<std::size_t>(get_parameter("queue_depth").as_int());
        overwrite_ = get_parameter("overwrite").as_bool();
        started_ = std::chrono::steady_clock::now();

        // Relative names use the node namespace: --ros-args -r __ns:=/robot1
        std::set<std::string> files;
        for (const auto& name : names) {
            const std::string full = get_node_topics_interface()->resolve_topic_name(name);
            if (!files.insert(impl::topic_to_filename(full)).second) {
                throw std::runtime_error("Topics map to the same file name : " + full);
            }
            if (std::find(pending_.begin(), pending_.end(), full) == pending_.end()) {
                pending_.push_back(full);
            }
        }

        discovery_timer_ = create_wall_timer(
            std::chrono::duration<double>(get_parameter("discovery_period").as_double()),
            [this] { discover(); }
        );
        flush_timer_ = create_wall_timer(
            std::chrono::duration<double>(get_parameter("flush_interval").as_double()),
            [this] { flush(); resubscribe(); }
        );
        discover();
    }


    Node::~Node(
    )
    {
        flush();
    }


    void Node::discover(
    )
    {
        const auto known = get_topic_names_and_types();
        for (auto it = pending_.begin(); it != pending_.end();) {
            const auto found = known.find(*it);
            if (found != known.end() && !found->second.empty()) {
                try {
                    start(*it, found->second.front());
                } catch (const std::exception& e) {     // one bad topic must not stop the others
                    RCLCPP_ERROR(get_logger(), "Cannot record %s : %s", it->c_str(), e.what());
                }
                it = pending_.erase(it);
            } else {
                ++it;
            }
        }

        if (pending_.empty()) {
            discovery_timer_->cancel();
            return;
        }
        const double waited = std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count();
        if (discovery_timeout_ > 0.0 && waited >= discovery_timeout_) {
            RCLCPP_WARN(get_logger(), "Gave up on %zu topic(s) not advertised after %.0f s", pending_.size(), waited);
            pending_.clear();
            discovery_timer_->cancel();
            if (topics_.empty()) {
                RCLCPP_ERROR(get_logger(), "No topic to record, shutting down");
                rclcpp::shutdown();
            }
        } else {
            RCLCPP_INFO(get_logger(), "Waiting for %zu topic(s)", pending_.size());
        }
    }


    rclcpp::QoS Node::select_qos(
        const std::string& name
    ) const
    {
        // A subscriber connects only if its QoS is not stricter than every publisher.
        const auto infos = get_publishers_info_by_topic(name);
        bool any_best_effort = false;
        bool all_transient_local = !infos.empty();
        for (const auto& publisher_info : infos) {
            if (publisher_info.qos_profile().reliability() == rclcpp::ReliabilityPolicy::BestEffort) { any_best_effort = true; }
            if (publisher_info.qos_profile().durability() != rclcpp::DurabilityPolicy::TransientLocal) { all_transient_local = false; }
        }

        rclcpp::QoS qos(queue_depth_);
        if (any_best_effort) { qos.best_effort(); }
        if (all_transient_local) { qos.transient_local(); }
        return qos;
    }


    void Node::subscribe(
        Topic& topic,
        const rclcpp::QoS& qos
    )
    {
        const auto profile = qos.get_rmw_qos_profile();
        topic.reliability = profile.reliability;
        topic.durability = profile.durability;
        topic.qos_mismatch = false;

        // The event only sets a flag. The subscription is replaced later, not inside its own callback.
        rclcpp::SubscriptionOptions options;
        options.event_callbacks.incompatible_qos_callback =
            [&topic](rclcpp::QOSRequestedIncompatibleQoSInfo&) { topic.qos_mismatch = true; };

        topic.subscription = create_generic_subscription(
            topic.name, topic.type, qos,
            [this, &topic](std::shared_ptr<rclcpp::SerializedMessage> msg, const rclcpp::MessageInfo& message_info) { on_message(topic, *msg, message_info); },
            options
        );
    }


    void Node::start(
        const std::string& name,
        const std::string& type
    )
    {
        auto topic = std::make_unique<Topic>();
        topic->name = name;
        topic->type = type;
        topic->formatter = std::make_unique<csv::Formatter>(type);
        topic->file = std::make_unique<csv::File>(
            (std::filesystem::path(directory_) / impl::topic_to_filename(name)).string(),
            overwrite_
        );
        topic->file->write("received_time_ns," + topic->formatter->header());

        subscribe(*topic, select_qos(name));
        RCLCPP_INFO(get_logger(), "Recording %s (%s) to %s", name.c_str(), type.c_str(), directory_.c_str());
        topics_.push_back(std::move(topic));
    }


    void Node::resubscribe(
    )
    {
        // A publisher with a stricter QoS can appear after the subscription was made.
        for (auto& topic : topics_) {
            if (!topic->qos_mismatch) { continue; }
            const rclcpp::QoS qos = select_qos(topic->name);
            const auto profile = qos.get_rmw_qos_profile();
            if (profile.reliability == topic->reliability && profile.durability == topic->durability) {
                topic->qos_mismatch = false;    // nothing better to try
                RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 10000, "QoS mismatch on %s cannot be fixed", topic->name.c_str());
                continue;
            }
            RCLCPP_WARN(get_logger(), "QoS mismatch on %s, subscribing again with the publisher QoS", topic->name.c_str());
            topic->subscription.reset();
            subscribe(*topic, qos);
        }
    }


    void Node::on_message(
        Topic& topic,
        const rclcpp::SerializedMessage& msg,
        const rclcpp::MessageInfo& message_info
    )
    {
        try {
            // Receive time in ns since epoch. The RMW value is the most exact, some RMWs leave it at 0.
            std::int64_t received = message_info.get_rmw_message_info().received_timestamp;
            if (received <= 0) { received = get_clock()->now().nanoseconds(); }

            topic.line.clear();
            topic.line += std::to_string(received);
            topic.line += ',';
            topic.formatter->format(msg, topic.line);
            if (!topic.file->write(topic.line) && !topic.write_error_reported) {
                topic.write_error_reported = true;
                RCLCPP_ERROR(get_logger(), "Write failed for %s (disk full?)", topic.name.c_str());
            }
        } catch (const std::exception& e) {
            RCLCPP_ERROR_THROTTLE(get_logger(), *get_clock(), 5000, "Dropped message on %s : %s", topic.name.c_str(), e.what());
        }
    }


    void Node::flush(
    )
    {
        for (auto& topic : topics_) {
            if (!topic->file->flush() && !topic->write_error_reported) {
                topic->write_error_reported = true;
                RCLCPP_ERROR(get_logger(), "Flush failed for %s", topic->name.c_str());
            }
        }
    }

} // namespace recorders
