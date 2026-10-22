#include "json_writer.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>

JsonWriter::JsonWriter(char* buf, size_t cap) : buf_(buf), cap_(cap) {
    if (cap_ == 0) ok_ = false;
    else buf_[0] = '\0';
}

void JsonWriter::clear_buffer() {
    len_ = 0;
    if (cap_ > 0) buf_[0] = '\0';
}

void JsonWriter::raw(const char* s, size_t n) {
    if (!ok_) return;
    if (len_ + n >= cap_) {  // keep room for the terminator
        ok_ = false;
        return;
    }
    memcpy(buf_ + len_, s, n);
    len_ += n;
    buf_[len_] = '\0';
}

void JsonWriter::before_value() {
    if (after_key_) {
        after_key_ = false;
        return;
    }
    if (depth_ > 0) {
        if (!first_[depth_ - 1]) raw(',');
        first_[depth_ - 1] = false;
    }
}

void JsonWriter::open(char c) {
    before_value();
    if (depth_ >= MAX_DEPTH) {
        ok_ = false;
        return;
    }
    raw(c);
    first_[depth_++] = true;
}

void JsonWriter::close(char c) {
    if (depth_ == 0) {
        ok_ = false;
        return;
    }
    depth_--;
    raw(c);
}

JsonWriter& JsonWriter::begin_object() { open('{');  return *this; }
JsonWriter& JsonWriter::end_object()   { close('}'); return *this; }
JsonWriter& JsonWriter::begin_array()  { open('[');  return *this; }
JsonWriter& JsonWriter::end_array()    { close(']'); return *this; }

JsonWriter& JsonWriter::key(const char* k) {
    before_value();
    raw('"');
    escaped(k);
    raw("\":", 2);
    after_key_ = true;
    return *this;
}

JsonWriter& JsonWriter::str(const char* v) {
    before_value();
    raw('"');
    escaped(v);
    raw('"');
    return *this;
}

JsonWriter& JsonWriter::num(int64_t v) {
    before_value();
    char tmp[24];
    int n = snprintf(tmp, sizeof(tmp), "%" PRId64, v);
    raw(tmp, static_cast<size_t>(n));
    return *this;
}

JsonWriter& JsonWriter::num_tenths(int64_t tenths) {
    if (tenths % 10 == 0) return num(tenths / 10);
    before_value();
    char tmp[28];
    int64_t whole = tenths / 10, frac = tenths % 10;
    int n = snprintf(tmp, sizeof(tmp), "%s%" PRId64 ".%" PRId64, (tenths < 0 && whole == 0) ? "-" : "",
                     whole, frac < 0 ? -frac : frac);
    raw(tmp, static_cast<size_t>(n));
    return *this;
}

JsonWriter& JsonWriter::boolean(bool v) {
    before_value();
    if (v) raw("true", 4);
    else   raw("false", 5);
    return *this;
}

JsonWriter& JsonWriter::null() {
    before_value();
    raw("null", 4);
    return *this;
}

void JsonWriter::escaped(const char* s) {
    for (; *s; s++) {
        unsigned char c = static_cast<unsigned char>(*s);
        switch (c) {
            case '"':  raw("\\\"", 2); break;
            case '\\': raw("\\\\", 2); break;
            case '\n': raw("\\n", 2);  break;
            case '\r': raw("\\r", 2);  break;
            case '\t': raw("\\t", 2);  break;
            default:
                if (c < 0x20) {
                    char tmp[7];
                    snprintf(tmp, sizeof(tmp), "\\u%04x", c);
                    raw(tmp, 6);
                } else {
                    raw(static_cast<char>(c));
                }
        }
    }
}
