
#include "recorders/csv/formatter.hpp"

#include <charconv>
#include <cstdio>
#include <stdexcept>

#include "rclcpp/typesupport_helpers.hpp"
#include "rmw/rmw.h"
#include "rosidl_typesupport_introspection_cpp/field_types.hpp"
#include "rosidl_typesupport_introspection_cpp/identifier.hpp"


namespace recorders {

    namespace impl {

        template<typename T>
        void append_number(
            std::string& out,
            T value
        )
        {
            char buf[40];
            const auto res = std::to_chars(buf, buf + sizeof(buf), value);
            out.append(buf, res.ptr);
        }

        void append_long_double(
            std::string& out,
            long double value
        )
        {
            char buf[64];
            const int n = std::snprintf(buf, sizeof(buf), "%.18Lg", value);
            if (n > 0) { out.append(buf, static_cast<std::size_t>(n)); }
        }

        void append_utf8(
            std::string& out,
            std::char32_t c
        )
        {
            if (c < 0x80) {
                out += static_cast<char>(c);
            } else if (c < 0x800) {
                out += static_cast<char>(0xC0 | (c >> 6));
                out += static_cast<char>(0x80 | (c & 0x3F));
            } else if (c < 0x10000) {
                out += static_cast<char>(0xE0 | (c >> 12));
                out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (c & 0x3F));
            } else {
                out += static_cast<char>(0xF0 | (c >> 18));
                out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (c & 0x3F));
            }
        }

        void append_wstring(
            std::string& out,
            const std::u16string& s
        )
        {
            for (std::size_t i = 0; i < s.size(); ++i) {
                std::char32_t c = s[i];
                if (c >= 0xD800 && c < 0xDC00 && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000) {
                    c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00);
                    ++i;
                }
                append_utf8(out, c);
            }
        }

        void append_scalar(
            std::string& out,
            std::uint8_t type_id,
            const void* p
        )
        {
            switch (type_id) {
                case rosidl_typesupport_introspection_cpp::ROS_TYPE_FLOAT: { append_number(out, *static_cast<const float *>(p)); break; }
                case rosidl_typesupport_introspection_cpp::ROS_TYPE_DOUBLE: { append_number(out, *static_cast<const double *>(p)); break; }
                case rosidl_typesupport_introspection_cpp::ROS_TYPE_LONG_DOUBLE:
                { append_long_double(out, *static_cast<const long double *>(p)); break; }
                case rosidl_typesupport_introspection_cpp::ROS_TYPE_CHAR:
                case rosidl_typesupport_introspection_cpp::ROS_TYPE_OCTET:
                case rosidl_typesupport_introspection_cpp::ROS_TYPE_UINT8: { append_number<int>(out, *static_cast<const std::uint8_t *>(p)); break; }
                case rosidl_typesupport_introspection_cpp::ROS_TYPE_INT8: { append_number<int>(out, *static_cast<const std::int8_t *>(p)); break; }
                case rosidl_typesupport_introspection_cpp::ROS_TYPE_WCHAR: { append_number<int>(out, *static_cast<const std::char16_t *>(p)); break; }
                case rosidl_typesupport_introspection_cpp::ROS_TYPE_BOOLEAN: { out += *static_cast<const bool *>(p) ? "true" : "false"; break; }
                case rosidl_typesupport_introspection_cpp::ROS_TYPE_UINT16: { append_number(out, *static_cast<const std::uint16_t *>(p)); break; }
                case rosidl_typesupport_introspection_cpp::ROS_TYPE_INT16: { append_number(out, *static_cast<const std::int16_t *>(p)); break; }
                case rosidl_typesupport_introspection_cpp::ROS_TYPE_UINT32: { append_number(out, *static_cast<const std::uint32_t *>(p)); break; }
                case rosidl_typesupport_introspection_cpp::ROS_TYPE_INT32: { append_number(out, *static_cast<const std::int32_t *>(p)); break; }
                case rosidl_typesupport_introspection_cpp::ROS_TYPE_UINT64: { append_number(out, *static_cast<const std::uint64_t *>(p)); break; }
                case rosidl_typesupport_introspection_cpp::ROS_TYPE_INT64: { append_number(out, *static_cast<const std::int64_t *>(p)); break; }
                case rosidl_typesupport_introspection_cpp::ROS_TYPE_STRING: { out += *static_cast<const std::string *>(p); break; }
                case rosidl_typesupport_introspection_cpp::ROS_TYPE_WSTRING: { append_wstring(out, *static_cast<const std::u16string *>(p)); break; }
                default: break;
            }
        }

        void append_value(
            std::string& out,
            const rosidl_typesupport_introspection_cpp::MessageMember& m,
            const void* field
        );

        void append_message(
            std::string& out,
            const rosidl_typesupport_introspection_cpp::MessageMember* mm,
            const void* msg
        )
        {
            out += '{';
            bool first = true;
            for (std::uint32_t i = 0; i < mm->member_count_; ++i) {
                const auto & m = mm->members_[i];
                if (!first) { out += ", "; }
                first = false;
                append_value(out, m, static_cast<const std::uint8_t *>(msg) + m.offset_);
            }
            out += '}';
        }

        void append_value(
            std::string& out,
            const rosidl_typesupport_introspection_cpp::MessageMember& m,
            const void* field
        )
        {
            const auto* sub = m.type_id_ == rosidl_typesupport_introspection_cpp::ROS_TYPE_MESSAGE > static_cast<const rosidl_typesupport_introspection_cpp::MessageMembers *>(m.members_->data) : nullptr;

            if (!m.is_array_) {
                if (sub) {
                    append_message(out, sub, field);
                } else {
                    append_scalar(out, m.type_id_, field);
                }
                return;
            }

            const std::size_t n = m.size_function(field);
            out += '[';
            for (std::size_t i = 0; i < n; ++i) {
                if (i) { out += ", "; }
                if (m.type_id_ == rosidl_typesupport_introspection_cpp::ROS_TYPE_BOOLEAN) {
                    bool b = false;
                    m.fetch_function(field, i, &b);
                    append_scalar(out, m.type_id_, &b);
                } else {
                    const void* element = m.get_const_function(field, i);
                    if (sub) {
                        append_message(out, sub, element);
                    } else {
                        append_scalar(out, m.type_id_, element);
                    }
                }
            }
            out += ']';
        }

        void append_cell(
            std::string& out,
            const std::string& text
        )
        {
            if (text.find_first_of(",\"\r\n") == std::string::npos) {
                out += text;
                return;
            }
            out += '"';
            for (char c : text) {
                if (c == '"') {
                    out += '"';
                }
                out += c;
            }
            out += '"';
        }

    } // namespace impl


    csv::Formatter::Formatter(
        const std::string& type
    )
    {
        serialization_lib_ = rclcpp::get_typesupport_library(type, "rosidl_typesupport_cpp");
        serialization_type_support_ = rclcpp::get_message_typesupport_handle(type, "rosidl_typesupport_cpp", *serialization_lib_);

        introspection_lib_ = rclcpp::get_typesupport_library(type, intro::typesupport_identifier);
        const auto* rosidl_typesupport_introspection_cpp_type_support = rclcpp::get_message_typesupport_handle(type, rosidl_typesupport_introspection_cpp::typesupport_identifier, *introspection_lib_);
        members_ = static_cast<const rosidl_typesupport_introspection_cpp::MessageMembers *>(rosidl_typesupport_introspection_cpp_type_support->data);

        const std::size_t units = (members_->size_of_ + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t);
        storage_.resize(units ? units : 1);
        members_->init_function(storage_.data(), rosidl_runtime_cpp::MessageInitialization::ALL);

        flatten(members_, 0, "msg.");
        if (!header_.empty()) { header_.pop_back(); }
        header_ += '\n';
    }

    
    csv::Formatter::~Formatter(
    )
    {
        if (members_) { members_->fini_function(stoarge_.data()); }
    }


    void csv::Formatter::flatten(
        const rosidl_typesupport_introspection_cpp::MessageMembers* members,
        std::uint32_t base,
        const std::string& prefix
    )
    {
        for (std::uint32_t i = 0; i < members->member_count_; ++i) {
            const auto & m = members->members_[i];
            const std::string name = prefix + m.name_;
            if (name.ends_with("structure_needs_at_least_one_member")) { continue; }
            if (m.type_id_ == intro::ROS_TYPE_MESSAGE && !m.is_array_) {
                flatten(static_cast<const intro::MessageMembers *>(m.members_->data), base + m.offset_, name + ".");
            } else {
                msgs_.push_back({base + m.offset_, &m});
                append_cell(header_, name);
                header_ += ',';
            }
        }
    }


    void csv::Formatter::format(
        const rclcpp::SerializedMessage& msg,
        std::string& out
    )
    {
        const rmw_ret_t ret = rmw_deserialize(
        &msg.get_rcl_serialized_message(), serialization_ts_, storage_.data());
        if (ret != RMW_RET_OK) { throw std::runtime_error("rmw_deserialize failed"); }

        const auto* base = reinterpret_cast<const std::uint8_t *>(storage_.data());
        bool first = true;
        for (const auto & msg_ : msgs_) {
            if (!first) { out += ','; }
            first = false;
            const auto & m = *msg_.member;
            const void * field = base + msg_.offset;
            const bool plain_number = !m.is_array_ && m.type_id_ != rosidl_typesupport_introspection_cpp::ROS_TYPE_STRING &&
            m.type_id_ != rosidl_typesupport_introspection_cpp::ROS_TYPE_WSTRING;
            if (plain_number) {
                append_scalar(out, m.type_id_, field);
            } else {
                scratch_.clear();
                append_value(scratch_, m, field);
                append_cell(out, scratch_);
            }
        }
        out += '\n';
    }

} // namespace recorders
