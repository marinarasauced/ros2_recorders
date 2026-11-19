
#include <exception>
#include <memory>

#include "rclcpp/rclcpp.hpp"

#include "recorders/node.hpp"


int main(
    int argc,
    char** argv
)
{
    rclcpp::init(argc, argv);
    int status = 0;
    try {
        auto node = std::make_shared<recorders::Node>();

        // Event driven and single threaded : callbacks run only when an event arrives.
        rclcpp::experimental::executors::EventsExecutor executor;
        executor.add_node(node);
        executor.spin();    // files are flushed in the node destructor
    } catch (const std::exception& e) {
        RCLCPP_FATAL(rclcpp::get_logger("csv_recorder"), "%s", e.what());
        status = 1;
    }
    if (rclcpp::ok()) { rclcpp::shutdown(); }
    return status;
}
