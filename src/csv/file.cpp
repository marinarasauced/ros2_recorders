
#include "recorders/csv/file.hpp"

#include <stdexcept>


namespace recorders::csv {

    File::File(
        const std::string& path,
        bool overwrite
    )
    {
        file_ = std::fopen(path.c_str(), overwrite ? "w" : "wx");
        if (!file_) {
            throw std::runtime_error("Unable to create log file (does it exist?) : " + path);
        }
        std::setvbuf(file_, nullptr, _IOFBF, 1 << 16);
    }

    File::~File(
    )
    {
        if (file_) { std::fclose(file_); }
    }

    bool File::write(
        const std::string& data
    )
    {
        return std::fwrite(data.data(), 1, data.size(), file_) == data.size();
    }

    bool File::flush(
    )
    {
        return std::fflush(file_) == 0;
    }

} // namespace recorders::csv
