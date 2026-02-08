// Copyright 2025 - Michael Nicolella

#pragma once

enum json_value_type : unsigned char
{
    k_json_null,
    k_json_string,
    k_json_int,
    k_json_float,
    k_json_bool,
    k_json_array,
    k_json_object
};

enum json_formatting_option : unsigned char
{
    k_json_format_default,
    k_json_format_single_line,
    k_json_format_multi_line,
};

struct json_value;

struct json_string
{
    json_string() = default;
    json_string(const json_string& rhs);
    json_string(json_string&& rhs) noexcept;
    ~json_string();

    void set_string(const char* src);
    const char* get_string() const;

private:
    char* text = nullptr;
};

struct json_array
{
    json_array() = default;
    json_array(const json_array& rhs);
    json_array(json_array&& rhs) noexcept;
    ~json_array();

    json_value* add_null();
    json_value* add_string(const char* value);
    json_value* add_uint(unsigned long long value);
    json_value* add_int(long long value);
    json_value* add_float(double value);
    json_value* add_bool(bool value);
    json_value* add_array();
    json_value* add_object();

    json_value* get_element(unsigned int index);
    const json_value* get_element(unsigned int index) const;
    unsigned int get_size() const { return size; }

    // ---- internals ----
    json_value* internal_add();
    json_value* data = nullptr;
    unsigned int size = 0;
    unsigned int capacity = 0;
};

struct json_object_member;

struct json_object
{
    json_object() = default;
    json_object(const json_object& rhs);
    json_object(json_object&& rhs) noexcept;
    ~json_object();

    json_value* set_null(const char* name);
    json_value* set_string(const char* name, const char* value);
    json_value* set_uint(const char* name, unsigned long long value);
    json_value* set_int(const char* name, long long value);
    json_value* set_float(const char* name, double value);
    json_value* set_bool(const char* name, bool value);
    json_value* set_array(const char* name);
    json_value* set_object(const char* name);

    const char* get_string(const char* name) const;
    unsigned long long get_uint(const char* name) const;
    long long get_int(const char* name) const;
    double get_float(const char* name) const;
    bool get_bool(const char* name) const;

    unsigned int get_member_count() const;
    const char* get_member_name(unsigned int index) const;

    json_value* get_member_value(unsigned int index);
    const json_value* get_member_value(unsigned int index) const;

    bool has_member(const char* name) const;
    json_value* get_member_value(const char* name);
    const json_value* get_member_value(const char* name) const;

    // ---- internals ----
    json_value* internal_add_member(const char* name);
    json_object_member* data = nullptr;
    unsigned int size = 0;
    unsigned int capacity = 0;
};

struct json_int
{
    bool get_is_signed() const { return is_signed; }

    unsigned long long get_uint() const { return uint_value; }
    long long get_int() const { return int_value; }

    void set_uint(unsigned long long value) { is_signed = false; uint_value = value; }
    void set_int(long long value) { is_signed = true; int_value = value; }

private:
    union
    {
        long long int_value;
        unsigned long long uint_value = 0;
    };

    bool is_signed = false;
};

struct json_value
{
    bool is_null() const;
    bool is_string() const;
    bool is_uint() const;
    bool is_int() const;
    bool is_float() const;
    bool is_bool() const;
    bool is_array() const;
    bool is_object() const;

    const char* get_string() const;
    unsigned long long get_uint() const;
    long long get_int() const;
    double get_float() const;
    bool get_bool() const;

    json_value* array_get_element(unsigned int index);
    const json_value* array_get_element(unsigned int index) const;
    unsigned int array_get_size() const;

    const char* array_get_string(unsigned int index) const;
    unsigned long long array_get_uint(unsigned int index) const;
    long long array_get_int(unsigned int index) const;
    double array_get_float(unsigned int index) const;
    bool array_get_bool(unsigned int index) const;
    const json_value* array_get_array(unsigned int index) const;
    json_value* array_get_array(unsigned int index);
    const json_value* array_get_object(unsigned int index) const;
    json_value* array_get_object(unsigned int index);

    unsigned int object_get_member_count() const;
    json_value* object_get_member(unsigned int index);
    const json_value* object_get_member(unsigned int index) const;
    const char* object_get_member_name(unsigned int index) const;

    json_value* object_get_value(const char* name);
    const json_value* object_get_value(const char* name) const;

    bool object_has_value(const char* name) const;
    const char* object_get_string(const char* name) const;
    unsigned long long object_get_uint(const char* name) const;
    long long object_get_int(const char* name) const;
    double object_get_float(const char* name) const;
    bool object_get_bool(const char* name) const;
    const json_value* object_get_array(const char* name) const;
    json_value* object_get_array(const char* name);
    const json_value* object_get_object(const char* name) const;
    json_value* object_get_object(const char* name);

    void set_null();
    void set_string(const char* value);
    void set_uint(unsigned long long value);
    void set_int(long long value);
    void set_float(double value);
    void set_bool(bool value);

    void set_array();
    json_value* array_add_null();
    json_value* array_add_string(const char* value);
    json_value* array_add_uint(unsigned long long value);
    json_value* array_add_int(long long value);
    json_value* array_add_float(double value);
    json_value* array_add_bool(bool value);
    json_value* array_add_array();
    json_value* array_add_object();

    void set_object();
    json_value* object_set_null(const char* name);
    json_value* object_set_string(const char* name, const char* value);
    json_value* object_set_uint(const char* name, unsigned long long value);
    json_value* object_set_int(const char* name, long long value);
    json_value* object_set_float(const char* name, double value);
    json_value* object_set_bool(const char* name, bool value);
    json_value* object_set_array(const char* name);
    json_value* object_set_object(const char* name);

    json_value();
    json_value(json_value_type ty);
    json_value(const json_value& rhs);
    json_value(json_value&& rhs) noexcept;
    ~json_value();

    void set_formatting_option(json_formatting_option formatting_option);
    json_formatting_option get_formatting_option() const;

private:
    void internal_destroy();
    void internal_make(json_value_type new_type);

    union
    {
        json_int int_value;
        double float_value;
        bool bool_value;
        json_string string_value;
        json_array array_value;
        json_object object_value;
    };

    json_value_type type;
    json_formatting_option formatting_option;
};

struct json_memory_interface
{
    void* (*allocate)(unsigned int size) = nullptr;
    void* (*reallocate)(const void* ptr, unsigned int new_size) = nullptr;
    void (*free)(const void* ptr) = nullptr;
};

struct json_extensions
{
    bool allow_nonobject_root = false;
    bool use_single_quotes_for_strings = false;
    bool allow_unquoted_object_keys = false;
};

void json_set_memory_interface(const json_memory_interface memory_interface);

typedef void (*json_output_callback)(void* user_data, const char* data, unsigned int size);
void json_pretty_print(json_output_callback callback, void* user_data, const json_extensions* extensions, const json_value* root);

bool json_parse(const char* buffer, unsigned int buffer_size, const json_extensions* extensions, json_value* out_obj);
