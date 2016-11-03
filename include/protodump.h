#pragma once

#include <iostream>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <chrono>
#include <string>
#include <sys/io.h>
#include <sys/fcntl.h>

#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/gzip_stream.h>

#include "tsv_parser.h"

namespace protodump { ;

static const char* dump_file_pattern = "%s.%02d.%s";

struct stopwatch {
    stopwatch()
        : start(std::chrono::high_resolution_clock::now()) {
    }
    std::int64_t elapsed() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start).count();
    }
    std::chrono::high_resolution_clock::time_point start;
};

struct dump_request_t {
    std::string input_file;
    std::string dump_file_suffix;
    std::int64_t each_dump_size;
};

struct dump_result_t {
    std::uint32_t total_count;
    std::uint32_t parsed_count;
    std::uint32_t zero_length_count;
    std::uint32_t invalid_length_count;
    std::size_t total_read;
    std::int64_t elapsed;
    std::vector<std::string> dump_files;

    dump_result_t()
        : total_count(0), parsed_count(0)
        , zero_length_count(0), invalid_length_count(0)
        , total_read(0), elapsed(0) {}
};

struct foreach_result_t {
    std::uint32_t read_count;
    std::int64_t elapsed;
    std::vector<std::string> read_files;

    foreach_result_t()
        : read_count(0), elapsed(0) {}
};

template <typename _LogType, typename Mapper>
bool tsv_to_bgz(const dump_request_t& request, dump_result_t& result, Mapper&& mapper);

template <typename _LogType, typename _Executor>
bool bgz_foreach(const std::vector<std::string>& dump_files, foreach_result_t& result, _Executor&& executor);

template <typename _LogType, typename Mapper>
bool tsv_to_bgz(const dump_request_t& request, dump_result_t& result, Mapper&& mapper) {
    stopwatch watch;
    tsv::parser p(request.input_file.c_str());

    std::string dump_file_prefix(request.input_file);
    if (dump_file_prefix.find_last_of('.') != std::string::npos) {
        dump_file_prefix = dump_file_prefix.substr(0, dump_file_prefix.find_last_of('.'));
    }

    _LogType log;
    std::vector<std::uint8_t> buffer;
    int dump_no = 0;
    while (true) {
        char dump_file[1024] = { 0, };
        snprintf(dump_file, sizeof(dump_file), dump_file_pattern, dump_file_prefix.c_str(), dump_no, request.dump_file_suffix.c_str());
        ++dump_no;

        int out_fd = open(dump_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out_fd < 0) {
            std::perror("cannot open output file");
            return false;
        }
        result.dump_files.push_back(std::string(dump_file));

        bool has_more = false;
        {
            google::protobuf::io::FileOutputStream zero_out(out_fd);
            google::protobuf::io::GzipOutputStream gzip_out(&zero_out);
            google::protobuf::io::CodedOutputStream coded_out(&gzip_out);

            while (p.next_line()) {
                ++result.total_count;
                if (!mapper(p, log)) {
                    continue;
                }

                int len = log.ByteSize();
                if (len == 0) {
                    ++result.zero_length_count;
                    continue;
                }
                else if (len < 0) {
                    ++result.invalid_length_count;
                    continue;
                }

                buffer.resize(len);
                log.SerializeToArray(buffer.data(), len);
                log.Clear();

                coded_out.WriteVarint32(len);
                coded_out.WriteRaw(buffer.data(), len);
                ++result.parsed_count;

                if (zero_out.ByteCount() > request.each_dump_size) {
                    has_more = true;
                    break;
                }
            }
            coded_out.WriteVarint32(0);
        }
        close(out_fd);
        if (!has_more) {
            break;
        }
    }

    result.elapsed = watch.elapsed();
    result.total_read = p.total_read();

    return result.dump_files.empty();
}

std::vector<std::string> find_dump_files(const std::string& dump_file_prefix, const std::string& dump_file_suffix) {
    std::vector<std::string> dump_files;
    for (int dump_no = 0; ; ++dump_no) {
        char dump_file[1024] = { 0, };
        snprintf(dump_file, sizeof(dump_file), dump_file_pattern, dump_file_prefix.c_str(), dump_no, dump_file_suffix.c_str());

        int fd = open(dump_file, O_RDONLY | O_EXCL);
        if (fd < 0) {
            break;
        }
        close(fd);
        dump_files.push_back(std::string(dump_file));
    }
    return dump_files;
}

template <typename _LogType, typename _Executor>
bool bgz_foreach(const std::vector<std::string>& dump_files, foreach_result_t& result, _Executor&& executor) {
    stopwatch watch;
    _LogType log;
    std::vector<std::uint8_t> buffer;
    for (const auto& dump_file : dump_files) {
        int in_fd = open(dump_file.c_str(), O_RDONLY | O_EXCL);
        if (in_fd < 0) {
            return false;
        }
        result.read_files.push_back(dump_file);

        {
            google::protobuf::io::FileInputStream zero_in(in_fd);
            google::protobuf::io::GzipInputStream gzip_in(&zero_in);
            google::protobuf::io::CodedInputStream coded_in(&gzip_in);
            coded_in.SetTotalBytesLimit(std::numeric_limits<int>::max(), -1);

            google::protobuf::uint32_t len;
            while (coded_in.ReadVarint32(&len) && len > 0) {
                buffer.resize(len);
                coded_in.ReadRaw(buffer.data(), static_cast<int>(len));
                log.ParseFromArray(buffer.data(), static_cast<int>(len));

                executor(log);
                ++result.read_count;
            }
        }
        close(in_fd);
    }
    result.elapsed = watch.elapsed();
    return true;
}

#ifndef TSV_DEBUG
#define PROTODUMP_required(p, log, name, type) \
    { if (!p.has_more_field()) return false; else log.set_##name(p.next_field<type>()); }
#else
#define PROTODUMP_required(p, log, name, type) \
    { if (!p.has_more_field()) { std::cerr << p.current_line() << std::endl; return false; } else log.set_##name(p.next_field<type>()); }
#endif
#define PROTODUMP_optional(p, log, name, type) \
    { if (p.has_more_field()) log.set_##name(p.next_field<type>()); }

#define PROTODUMP_begin(type) [](tsv::parser& p, type& log) -> bool {
#define PROTODUMP_req(name, type) PROTODUMP_required(p, log, name, type)
#define PROTODUMP_opt(name, type) PROTODUMP_optional(p, log, name, type)
#define PROTODUMP_end() return true; }

#define PROTODUMP_enum_begin(enumtype, valtype) \
    namespace tsv { ; \
        template <> \
        enumtype val(const char* str, tag<enumtype>) { \
            static std::unordered_map<valtype, enumtype> enums = {
#define PROTODUMP_enum_end(valref) \
            }; \
            return enums[(valref)]; \
        } \
    }

}

