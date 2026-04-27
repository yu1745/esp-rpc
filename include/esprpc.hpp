#ifndef ESPRPC_HPP
#define ESPRPC_HPP

#include "esprpc.h"
#include "esprpc_binary.h"
#include "esprpc_service.h"
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <type_traits>
#include <utility>
#include <tuple>

template<typename T>
struct rpc_stream {
    void *ctx;
};

namespace esprpc {

class Buffer {
    uint8_t *data_;
    size_t size_;
    size_t capacity_;

    void reserve(size_t n) {
        if (n <= capacity_) return;
        uint8_t *d = (uint8_t *)realloc(data_, n);
        if (d) {
            data_ = d;
            capacity_ = n;
        }
    }

public:
    Buffer(size_t initial = 256) : data_(nullptr), size_(0), capacity_(0) {
        reserve(initial);
    }
    ~Buffer() { free(data_); }
    Buffer(const Buffer &) = delete;
    Buffer &operator=(const Buffer &) = delete;

    void write(const void *src, size_t len) {
        if (size_ + len > capacity_)
            reserve((capacity_ * 3) / 2 + len);
        memcpy(data_ + size_, src, len);
        size_ += len;
    }
    void write_u8(uint8_t v) { write(&v, 1); }
    void write_u16(uint16_t v) {
        uint8_t b[2] = {(uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF)};
        write(b, 2);
    }
    void write_u32(uint32_t v) {
        uint8_t b[4] = {(uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF),
                        (uint8_t)((v >> 16) & 0xFF), (uint8_t)((v >> 24) & 0xFF)};
        write(b, 4);
    }
    void write_i32(int32_t v) { write_u32((uint32_t)v); }

    size_t size() const { return size_; }
    const uint8_t *data() const { return data_; }
};

class Reader {
    const uint8_t *pos_;
    const uint8_t *end_;
    bool error_ = false;

public:
    Reader(const uint8_t *data, size_t len) : pos_(data), end_(data + len) {}

    bool error() const { return error_; }
    void reset() { error_ = false; }

    uint8_t read_u8() {
        if (pos_ >= end_) { error_ = true; return 0; }
        return *pos_++;
    }
    uint16_t read_u16() {
        if (pos_ + 2 > end_) { error_ = true; return 0; }
        uint16_t v = (uint16_t)pos_[0] | ((uint16_t)pos_[1] << 8);
        pos_ += 2;
        return v;
    }
    uint32_t read_u32() {
        if (pos_ + 4 > end_) { error_ = true; return 0; }
        uint32_t v = (uint32_t)pos_[0] | ((uint32_t)pos_[1] << 8) |
                     ((uint32_t)pos_[2] << 16) | ((uint32_t)pos_[3] << 24);
        pos_ += 4;
        return v;
    }
    int32_t read_i32() { return (int32_t)read_u32(); }
    bool read_bool() { return read_u8() != 0; }
    void read_bytes(void *dst, size_t len) {
        if (pos_ + len > end_) { error_ = true; len = end_ - pos_; }
        memcpy(dst, pos_, len);
        pos_ += len;
    }
    const uint8_t *pos() const { return pos_; }
    const uint8_t *end() const { return end_; }
};

template <typename T>
struct Serializer;

#define ESPRPC_PRIMITIVE(T, WRITE_OP, READ_OP)                  \
    template <>                                                 \
    struct Serializer<T> {                                      \
        static void write(Buffer &buf, const T &v) {            \
            buf.WRITE_OP(v);                                    \
        }                                                       \
        static void read(Reader &r, T &v) { v = r.READ_OP(); } \
    };

ESPRPC_PRIMITIVE(int32_t, write_i32, read_i32)
ESPRPC_PRIMITIVE(uint32_t, write_u32, read_u32)

template <>
struct Serializer<int16_t> {
    static void write(Buffer &buf, int16_t v) { buf.write_u16((uint16_t)v); }
    static void read(Reader &r, int16_t &v) { v = (int16_t)r.read_u16(); }
};

ESPRPC_PRIMITIVE(uint16_t, write_u16, read_u16)
ESPRPC_PRIMITIVE(uint8_t, write_u8, read_u8)

template <>
struct Serializer<int8_t> {
    static void write(Buffer &buf, int8_t v) { buf.write_u8((uint8_t)v); }
    static void read(Reader &r, int8_t &v) { v = (int8_t)r.read_u8(); }
};
ESPRPC_PRIMITIVE(bool, write_u8, read_bool)

#undef ESPRPC_PRIMITIVE

template <typename T>
void write(Buffer &buf, const T &v) {
    Serializer<T>::write(buf, v);
}

template <typename T>
void read(Reader &r, T &v) {
    Serializer<T>::read(r, v);
}

struct StringBuf {
    static constexpr size_t MAX = 128;
    char data[MAX];
    size_t len = 0;
    const char *c_str() const { return data; }
    bool operator==(const char *s) const {
        return strncmp(data, s, len) == 0 && s[len] == '\0';
    }
    StringBuf &operator=(const char *s) {
        len = strnlen(s, MAX - 1);
        memcpy(data, s, len);
        data[len] = '\0';
        return *this;
    }
    void setf(const char *fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        int n = vsnprintf(data, MAX, fmt, ap);
        va_end(ap);
        len = n < 0 ? 0 : (size_t)n < MAX ? (size_t)n : MAX - 1;
        data[len] = '\0';
    }
};

template <>
struct Serializer<StringBuf> {
    static void write(Buffer &buf, const StringBuf &s) {
        buf.write_u16((uint16_t)s.len);
        buf.write(s.data, s.len);
    }
    static void read(Reader &r, StringBuf &v) {
        v.len = r.read_u16();
        if (v.len >= StringBuf::MAX) v.len = StringBuf::MAX - 1;
        r.read_bytes(v.data, v.len);
        v.data[v.len] = '\0';
    }
};

template <typename T>
struct Optional {
    bool present = false;
    T value{};
    Optional &operator=(const T &v) {
        present = true;
        value = v;
        return *this;
    }
};

template <typename T>
struct Serializer<Optional<T>> {
    static void write(Buffer &buf, const Optional<T> &v) {
        buf.write_u8(v.present ? 1 : 0);
        if (v.present) Serializer<T>::write(buf, v.value);
    }
    static void read(Reader &r, Optional<T> &v) {
        v.present = r.read_u8() != 0;
        if (v.present) Serializer<T>::read(r, v.value);
    }
};

template <typename T>
struct List {
    T *items = nullptr;
    size_t len = 0;
};

template <typename T>
struct Serializer<List<T>> {
    static void write(Buffer &buf, const List<T> &v) {
        buf.write_u32((uint32_t)v.len);
        for (size_t i = 0; i < v.len; i++)
            Serializer<T>::write(buf, v.items[i]);
    }
    static void read(Reader &r, List<T> &v) {
        uint32_t n = r.read_u32();
        if (n == 0 || !v.items) {
            v.len = 0;
            T tmp;
            for (uint32_t i = 0; i < n; i++)
                Serializer<T>::read(r, tmp);
            return;
        }
        size_t cap = v.len;
        size_t actual = n < cap ? n : cap;
        for (size_t i = 0; i < actual; i++)
            Serializer<T>::read(r, v.items[i]);
        v.len = actual;
        T tmp;
        for (size_t i = actual; i < n; i++)
            Serializer<T>::read(r, tmp);
    }
};

enum MethodFlag : uint32_t {
    MF_NONE = 0,
    MF_VOID = 1 << 0,
    MF_STREAM = 1 << 1,
};

struct MethodInfo {
    uint16_t id;
    uint32_t flags;
    int (*dispatch)(uint16_t method_id, const uint8_t *req, size_t req_len,
                    uint8_t **resp, size_t *resp_len, void *impl);
};

template <typename T>
struct is_rpc_stream : std::false_type {};
template <typename T>
struct is_rpc_stream<rpc_stream<T>> : std::true_type {};

template <typename T>
struct is_void_return : std::false_type {};
template <>
struct is_void_return<void> : std::true_type {};


template <typename Tuple, size_t... Is>
void read_tuple_impl(Reader &r, Tuple &t, std::index_sequence<Is...>) {
    (Serializer<std::remove_const_t<std::remove_reference_t<
         typename std::tuple_element<Is, Tuple>::type>>>::read(r, std::get<Is>(t)), ...);
}

template <typename Tuple>
void read_tuple(Reader &r, Tuple &t) {
    read_tuple_impl(r, t,
                    std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}

template <typename C, auto Method>
struct MethodDispatch;

template <typename C, typename R, typename... Args,
          R (C::*Method)(Args...)>
struct MethodDispatch<C, Method> {
    static int dispatch(uint16_t method_id, const uint8_t *req, size_t req_len,
                        uint8_t **resp, size_t *resp_len, void *impl) {
        auto *self = static_cast<C *>(impl);
        Reader reader(req, req_len);

        using ArgStorage =
            std::tuple<std::remove_const_t<std::remove_reference_t<Args>>...>;
        ArgStorage args{};
        read_tuple(reader, args);
        if (reader.error()) return -1;

        constexpr bool is_void = is_void_return<R>::value;
        constexpr bool is_stream = is_rpc_stream<R>::value;

        if constexpr (is_stream) {
            esprpc_set_stream_method_id(method_id);
            auto result = (self->*Method)(std::get<Args>(args)...);
            esprpc_set_stream_method_id(ESPRPC_STREAM_METHOD_ID_NONE);
            (void)result;
            *resp = nullptr;
            *resp_len = 0;
        } else if constexpr (is_void) {
            (self->*Method)(std::get<Args>(args)...);
            *resp = nullptr;
            *resp_len = 0;
        } else {
            auto result = (self->*Method)(std::get<Args>(args)...);
            Buffer buf(256);
            Serializer<R>::write(buf, result);
            *resp_len = buf.size();
            if (*resp_len > 0) {
                *resp = (uint8_t *)malloc(*resp_len);
                if (*resp) memcpy(*resp, buf.data(), *resp_len);
            } else {
                *resp = nullptr;
            }
        }
        return 0;
    }
};

template <typename C, auto Method>
MethodInfo method(uint16_t id, uint32_t flags = 0) {
    return MethodInfo{id, flags, &MethodDispatch<C, Method>::dispatch};
}

template <typename T>
void stream_emit(uint16_t method_id, const T &v) {
    Buffer buf(128);
    write(buf, v);
    esprpc_stream_emit(method_id, buf.data(), buf.size());
}

} // namespace esprpc

/* ---------- 自动序列化宏 ---------- */

#define ESPRPC_CONCAT(a, b) ESPRPC_CONCAT_I(a, b)
#define ESPRPC_CONCAT_I(a, b) a##b

#define ESPRPC_NARG(...) ESPRPC_NARG_I(__VA_ARGS__, 16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0)
#define ESPRPC_NARG_I(a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15,a16,n,...) n

/* ---------- ESPRPC_STRUCT：定义结构体 + 自动序列化 ---------- */

#define ESPRPC_PAIR_FIRST(p) ESPRPC_PAIR_FIRST_I p
#define ESPRPC_PAIR_FIRST_I(a, b) a
#define ESPRPC_PAIR_SECOND(p) ESPRPC_PAIR_SECOND_I p
#define ESPRPC_PAIR_SECOND_I(a, b) b

#define ESPRPC_STRUCT_FIELD(p) ESPRPC_PAIR_SECOND(p) ESPRPC_PAIR_FIRST(p);
#define ESPRPC_STRUCT_WRITE(p) ::esprpc::write(buf, v.ESPRPC_PAIR_FIRST(p));
#define ESPRPC_STRUCT_READ(p) ::esprpc::read(r, v.ESPRPC_PAIR_FIRST(p));

#define ESPRPC_FOR_EACH_1(m, p) m(p)
#define ESPRPC_FOR_EACH_2(m, p, ...) m(p) ESPRPC_FOR_EACH_1(m, __VA_ARGS__)
#define ESPRPC_FOR_EACH_3(m, p, ...) m(p) ESPRPC_FOR_EACH_2(m, __VA_ARGS__)
#define ESPRPC_FOR_EACH_4(m, p, ...) m(p) ESPRPC_FOR_EACH_3(m, __VA_ARGS__)
#define ESPRPC_FOR_EACH_5(m, p, ...) m(p) ESPRPC_FOR_EACH_4(m, __VA_ARGS__)
#define ESPRPC_FOR_EACH_6(m, p, ...) m(p) ESPRPC_FOR_EACH_5(m, __VA_ARGS__)
#define ESPRPC_FOR_EACH_7(m, p, ...) m(p) ESPRPC_FOR_EACH_6(m, __VA_ARGS__)
#define ESPRPC_FOR_EACH_8(m, p, ...) m(p) ESPRPC_FOR_EACH_7(m, __VA_ARGS__)
#define ESPRPC_FOR_EACH_9(m, p, ...) m(p) ESPRPC_FOR_EACH_8(m, __VA_ARGS__)
#define ESPRPC_FOR_EACH_10(m, p, ...) m(p) ESPRPC_FOR_EACH_9(m, __VA_ARGS__)
#define ESPRPC_FOR_EACH_11(m, p, ...) m(p) ESPRPC_FOR_EACH_10(m, __VA_ARGS__)
#define ESPRPC_FOR_EACH_12(m, p, ...) m(p) ESPRPC_FOR_EACH_11(m, __VA_ARGS__)
#define ESPRPC_FOR_EACH_13(m, p, ...) m(p) ESPRPC_FOR_EACH_12(m, __VA_ARGS__)
#define ESPRPC_FOR_EACH_14(m, p, ...) m(p) ESPRPC_FOR_EACH_13(m, __VA_ARGS__)
#define ESPRPC_FOR_EACH_15(m, p, ...) m(p) ESPRPC_FOR_EACH_14(m, __VA_ARGS__)
#define ESPRPC_FOR_EACH_16(m, p, ...) m(p) ESPRPC_FOR_EACH_15(m, __VA_ARGS__)

#define ESPRPC_FOR_EACH(m, ...) ESPRPC_CONCAT(ESPRPC_FOR_EACH_, ESPRPC_NARG(__VA_ARGS__))(m, __VA_ARGS__)

#define ESPRPC_STRUCT(T, ...)                                                                  \
    struct T {                                                                                 \
        ESPRPC_FOR_EACH(ESPRPC_STRUCT_FIELD, __VA_ARGS__)                                      \
    };                                                                                         \
    namespace esprpc {                                                                         \
    template <>                                                                                \
    struct Serializer<T> {                                                                     \
        static void write(Buffer &buf, const T &v) {                                          \
            ESPRPC_FOR_EACH(ESPRPC_STRUCT_WRITE, __VA_ARGS__)                                  \
        }                                                                                      \
        static void read(Reader &r, T &v) {                                                    \
            ESPRPC_FOR_EACH(ESPRPC_STRUCT_READ, __VA_ARGS__)                                   \
        }                                                                                      \
    };                                                                                         \
    }

/* ---------- 向后兼容：仅为已有 struct 添加序列化 ---------- */

#define ESPRPC_WRITE_0(v)
#define ESPRPC_WRITE_1(v, f) ::esprpc::write(buf, v.f);
#define ESPRPC_WRITE_2(v, f, ...) ::esprpc::write(buf, v.f); ESPRPC_WRITE_1(v, __VA_ARGS__)
#define ESPRPC_WRITE_3(v, f, ...) ::esprpc::write(buf, v.f); ESPRPC_WRITE_2(v, __VA_ARGS__)
#define ESPRPC_WRITE_4(v, f, ...) ::esprpc::write(buf, v.f); ESPRPC_WRITE_3(v, __VA_ARGS__)
#define ESPRPC_WRITE_5(v, f, ...) ::esprpc::write(buf, v.f); ESPRPC_WRITE_4(v, __VA_ARGS__)
#define ESPRPC_WRITE_6(v, f, ...) ::esprpc::write(buf, v.f); ESPRPC_WRITE_5(v, __VA_ARGS__)
#define ESPRPC_WRITE_7(v, f, ...) ::esprpc::write(buf, v.f); ESPRPC_WRITE_6(v, __VA_ARGS__)
#define ESPRPC_WRITE_8(v, f, ...) ::esprpc::write(buf, v.f); ESPRPC_WRITE_7(v, __VA_ARGS__)
#define ESPRPC_WRITE_9(v, f, ...) ::esprpc::write(buf, v.f); ESPRPC_WRITE_8(v, __VA_ARGS__)
#define ESPRPC_WRITE_10(v, f, ...) ::esprpc::write(buf, v.f); ESPRPC_WRITE_9(v, __VA_ARGS__)
#define ESPRPC_WRITE_11(v, f, ...) ::esprpc::write(buf, v.f); ESPRPC_WRITE_10(v, __VA_ARGS__)
#define ESPRPC_WRITE_12(v, f, ...) ::esprpc::write(buf, v.f); ESPRPC_WRITE_11(v, __VA_ARGS__)
#define ESPRPC_WRITE_13(v, f, ...) ::esprpc::write(buf, v.f); ESPRPC_WRITE_12(v, __VA_ARGS__)
#define ESPRPC_WRITE_14(v, f, ...) ::esprpc::write(buf, v.f); ESPRPC_WRITE_13(v, __VA_ARGS__)
#define ESPRPC_WRITE_15(v, f, ...) ::esprpc::write(buf, v.f); ESPRPC_WRITE_14(v, __VA_ARGS__)
#define ESPRPC_WRITE_16(v, f, ...) ::esprpc::write(buf, v.f); ESPRPC_WRITE_15(v, __VA_ARGS__)

#define ESPRPC_READ_0(v, r)
#define ESPRPC_READ_1(v, r, f) ::esprpc::read(r, v.f);
#define ESPRPC_READ_2(v, r, f, ...) ::esprpc::read(r, v.f); ESPRPC_READ_1(v, r, __VA_ARGS__)
#define ESPRPC_READ_3(v, r, f, ...) ::esprpc::read(r, v.f); ESPRPC_READ_2(v, r, __VA_ARGS__)
#define ESPRPC_READ_4(v, r, f, ...) ::esprpc::read(r, v.f); ESPRPC_READ_3(v, r, __VA_ARGS__)
#define ESPRPC_READ_5(v, r, f, ...) ::esprpc::read(r, v.f); ESPRPC_READ_4(v, r, __VA_ARGS__)
#define ESPRPC_READ_6(v, r, f, ...) ::esprpc::read(r, v.f); ESPRPC_READ_5(v, r, __VA_ARGS__)
#define ESPRPC_READ_7(v, r, f, ...) ::esprpc::read(r, v.f); ESPRPC_READ_6(v, r, __VA_ARGS__)
#define ESPRPC_READ_8(v, r, f, ...) ::esprpc::read(r, v.f); ESPRPC_READ_7(v, r, __VA_ARGS__)
#define ESPRPC_READ_9(v, r, f, ...) ::esprpc::read(r, v.f); ESPRPC_READ_8(v, r, __VA_ARGS__)
#define ESPRPC_READ_10(v, r, f, ...) ::esprpc::read(r, v.f); ESPRPC_READ_9(v, r, __VA_ARGS__)
#define ESPRPC_READ_11(v, r, f, ...) ::esprpc::read(r, v.f); ESPRPC_READ_10(v, r, __VA_ARGS__)
#define ESPRPC_READ_12(v, r, f, ...) ::esprpc::read(r, v.f); ESPRPC_READ_11(v, r, __VA_ARGS__)
#define ESPRPC_READ_13(v, r, f, ...) ::esprpc::read(r, v.f); ESPRPC_READ_12(v, r, __VA_ARGS__)
#define ESPRPC_READ_14(v, r, f, ...) ::esprpc::read(r, v.f); ESPRPC_READ_13(v, r, __VA_ARGS__)
#define ESPRPC_READ_15(v, r, f, ...) ::esprpc::read(r, v.f); ESPRPC_READ_14(v, r, __VA_ARGS__)
#define ESPRPC_READ_16(v, r, f, ...) ::esprpc::read(r, v.f); ESPRPC_READ_15(v, r, __VA_ARGS__)

#define ESPRPC_SERIALIZE(T, ...)                                                               \
    namespace esprpc {                                                                         \
    template <>                                                                                \
    struct Serializer<T> {                                                                     \
        static void write(Buffer &buf, const T &v) {                                          \
            ESPRPC_CONCAT(ESPRPC_WRITE_, ESPRPC_NARG(__VA_ARGS__))(v, __VA_ARGS__)             \
        }                                                                                      \
        static void read(Reader &r, T &v) {                                                    \
            ESPRPC_CONCAT(ESPRPC_READ_, ESPRPC_NARG(__VA_ARGS__))(v, r, __VA_ARGS__)           \
        }                                                                                      \
    };                                                                                         \
    }

#define ESPRPC_SERVICE(NAME, IMPL, ...)                                         \
    static esprpc::MethodInfo _esprpc_##NAME##_methods[] = {__VA_ARGS__};      \
    static int _esprpc_##NAME##_dispatch(uint16_t method_id,                    \
                                         const uint8_t *req, size_t req_len,    \
                                         uint8_t **resp, size_t *resp_len,      \
                                         void *impl) {                          \
        uint8_t mth = method_id & 0x1F;                                         \
        for (size_t i = 0; i < sizeof(_esprpc_##NAME##_methods) /               \
                                 sizeof(_esprpc_##NAME##_methods[0]);            \
             i++) {                                                             \
            if (_esprpc_##NAME##_methods[i].id == mth) {                        \
                return _esprpc_##NAME##_methods[i].dispatch(                    \
                    method_id, req, req_len, resp, resp_len, impl);             \
            }                                                                   \
        }                                                                       \
        return -1;                                                              \
    }                                                                           \
    static inline void NAME##_register_rpc() {                                  \
        esprpc_register_service_ex(                                             \
            #NAME, &IMPL,                                                       \
            (esprpc_dispatch_fn)_esprpc_##NAME##_dispatch);                     \
    }

#endif
