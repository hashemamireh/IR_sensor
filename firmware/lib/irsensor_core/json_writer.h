#pragma once

// Minimal streaming JSON writer into a caller-owned buffer. Inserts commas,
// escapes strings and always NUL-terminates; ok() turns false once anything
// fails to fit (the buffer then holds a truncated prefix).
//
// For long arrays, write one element, send length() bytes from c_str(), then
// clear_buffer() and continue — nesting and comma state are kept.

#include <cstddef>
#include <cstdint>

class JsonWriter {
public:
    JsonWriter(char* buf, size_t cap);

    JsonWriter& begin_object();
    JsonWriter& end_object();
    JsonWriter& begin_array();
    JsonWriter& end_array();

    JsonWriter& key(const char* k);
    JsonWriter& str(const char* v);
    JsonWriter& num(int64_t v);
    JsonWriter& boolean(bool v);
    JsonWriter& null();

    bool        ok() const     { return ok_; }
    size_t      length() const { return len_; }
    const char* c_str() const  { return buf_; }

    void clear_buffer();

private:
    static constexpr int MAX_DEPTH = 8;

    void before_value();
    void open(char c);
    void close(char c);
    void raw(const char* s, size_t n);
    void raw(char c) { raw(&c, 1); }
    void escaped(const char* s);

    char*  buf_;
    size_t cap_;
    size_t len_ = 0;
    bool   ok_  = true;
    int    depth_ = 0;
    bool   first_[MAX_DEPTH] = {};  // no element written yet at this level
    bool   after_key_ = false;
};
