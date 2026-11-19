#pragma once

#include <cstdio>
#include <string>


namespace recorders::csv {

    class File
    {
    public:

        explicit File(const std::string& path, bool overwrite = false);
        ~File();
        File(const File&) = delete;
        File& operator=(const File&) = delete;

        bool write(const std::string& data);
        bool flush();

    private:

        std::FILE* file_{nullptr};
    };

} // namespace recorders::csv
