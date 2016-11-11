#pragma once

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cassert>
#include <string>
#include <cstring>
#include <unordered_map>

namespace tsv { ;

template <typename T>
struct tag {
    typedef T type;
};

template <typename T>
T val(const char*, tag<T>);

class parser {
public:
    parser(const char* file, char delim = '\t');
    parser(std::FILE* file, char delim = '\t');
    ~parser();

    bool next_line();

    template <typename T>
    T next_field();
    const char* next_all();
    bool skip_field() { return move_next_field() != nullptr; }
    bool has_more_field() const { return *field_pos_ != 0; }

    std::size_t total_read() const { return total_read_; }

private:
    bool fill_file_buffer();
    const char* move_next_field();

private:
    static const int file_buffer_size = ((1 << 14) - 1);

    std::FILE* file_;
    std::size_t total_read_;
    char* file_buffer_;
    char delim_;

    char* line_pos_;
    char* field_pos_;
    bool autoclose_;

#ifdef TSV_DEBUG
public:
    std::string current_line() const { return std::string(current_line_copy_); }

private:
    static const int current_line_copy_max_size = 1024;
    char current_line_copy_[current_line_copy_max_size];
#endif
};

inline parser::parser(const char* file, char delim)
    : file_(nullptr), delim_(delim), line_pos_(nullptr), total_read_(0), autoclose_(true) {
    file_ = std::fopen(file, "rb");

    file_buffer_ = new char[file_buffer_size + 1];
    file_buffer_[file_buffer_size] = 0;
}
inline parser::parser(std::FILE* file, char delim)
    : file_(file), delim_(delim), line_pos_(nullptr), total_read_(0), autoclose_(false) {
    file_buffer_ = new char[file_buffer_size + 1];
    file_buffer_[file_buffer_size] = 0;
}

inline parser::~parser() {
    if (autoclose_ && file_ != nullptr)
        std::fclose(file_);
    delete[] file_buffer_;
}

inline bool parser::next_line() {
    if (line_pos_ == nullptr) {
        return fill_file_buffer() ? next_line() : false;
    }
    char* pos = line_pos_;
    while (*pos != '\n') {
        if (*pos == 0) {
            return fill_file_buffer() ? next_line() : false;
        }
        ++pos;
    }
    *pos = 0;

    field_pos_ = line_pos_;
#ifdef TSV_DEBUG
    std::strcpy(current_line_copy_, field_pos_);
#endif
    line_pos_ = pos + 1;
    return true;
}

inline bool parser::fill_file_buffer() {
    if (file_ == nullptr || std::feof(file_))
        return false;

    if (line_pos_ != nullptr && line_pos_ != file_buffer_) {
        std::size_t size_to_fill = line_pos_ - file_buffer_;
        std::size_t new_start = file_buffer_size - size_to_fill;
        std::memmove(file_buffer_, line_pos_, new_start);

        std::size_t read = std::fread(&file_buffer_[new_start], sizeof(char), size_to_fill, file_);
        if (read == 0) {
            return false;
        }
        total_read_ += read;
    }
    else {
        std::size_t read = std::fread(file_buffer_, sizeof(char), file_buffer_size, file_);
        if (read == 0) {
            return false;
        }
        total_read_ += read;
    }
    line_pos_ = file_buffer_;
    return true;
}

template <typename T>
inline T parser::next_field() {
    const char* current = move_next_field();
    assert(current != nullptr);

    return tsv::val<T>(current, tag<T>());
}

inline const char* parser::next_all() {
    return field_pos_;
}

inline const char* parser::move_next_field() {
    if (*field_pos_ == 0) return nullptr;
    const char* current = field_pos_;
    while (*field_pos_ != '\t' && *field_pos_ != 0)
        ++field_pos_;
    if (*field_pos_ == '\t') {
        *field_pos_ = 0;
        ++field_pos_;
    }
    return current;
}

template <>
bool val(const char* str, tag<bool>) {
    return (std::strcmp("true", str) == 0);
}
template <>
const char* val(const char* str, tag<const char*>) {
    return str;
}
template <>
int val(const char* str, tag<int>) {
    return std::atoi(str);
}
template <>
long val(const char* str, tag<long>) {
    return std::atol(str);
}
template <>
long long val(const char* str, tag<long long>) {
    return std::atoll(str);
}
template <>
float val(const char* str, tag<float>) {
    return static_cast<float>(std::atof(str));
}
template <>
double val(const char* str, tag<double>) {
    return std::atof(str);
}
template <>
std::string val(const char* str, tag<std::string>) {
    return std::string(str);
}

}
