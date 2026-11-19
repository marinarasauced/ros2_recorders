#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <Vector>

#include "rcpputils/shared_library.hpp"
#include "rclcpp/serialized_message.hpp"
#include "rosidl_typesupport_introspection_cpp/message_introspection.hpp"


namespace recorders::csv {

    class Formatter
    {
    public:

        explicit Formatter(const std::string& type);
        ~Formatter();
        Formatter(const Formatter&) = delete;
        Formatter& operator=(const Formatter&) = delete;

        const std::string& header() const { return header_; }
        void format(const rclcpp::SerializedMessage& msg, std::string& out);

    private:

        std::shared_ptr<rcpputils::SharedLibrary> introspection_lib_;
        std::shared_ptr<rcpputils::SharedLibrary> serialization_lib_;
        const rosidl_message_type_support_t* serialization_type_support_{nullptr};
        const rosidl_typesupport_introspection_cpp::MessageMembers* members_{nullptr};

        std::vector<std::max_align_t> storage_;
        std::string header_;
        std::string scratch_;

        void flatten(const rosidl_typesupport_introspection_cpp::MessageMembers* members, std::uint32_t base, const std::string& prefix);

        struct Message {
            std::uint32_t offset;
            const rosidl_typesupport_introspection_cpp::MessageMember* member;
        };

        std::vector<Message> msgs_;
    };

} // namespace recorders::csv
