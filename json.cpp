// Copyright 2025 - Michael Nicolella

#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include <new>
#include <type_traits>
#include <utility>

struct json_default_memory_interface : json_memory_interface
{
    json_default_memory_interface()
    {
        allocate = [](unsigned int size) -> void* { return malloc(size); };
        reallocate = [](const void* ptr, unsigned int new_size) -> void* { return realloc(const_cast<void*>(ptr), new_size); };
        free = [](const void* ptr) { ::free(const_cast<void*>(ptr)); };
    }

} g_json_memory_interface;

void json_set_memory_interface(const json_memory_interface memory_interface)
{
    g_json_memory_interface.allocate = memory_interface.allocate;
    g_json_memory_interface.reallocate = memory_interface.reallocate;
    g_json_memory_interface.free = memory_interface.free;
}

enum json_token_type
{
    k_tok_invalid,
    k_tok_string,
    k_tok_number,
    k_tok_openbrace,    // {
    k_tok_closebrace,   // }
    k_tok_colon,        // :
    k_tok_openbracket,  // [
    k_tok_closebracket, // ]
    k_tok_comma,        // ,
    k_tok_true,         // true
    k_tok_false,        // false
    k_tok_null,         // null
};

template<typename array_t>
void internal_array_copy_ctor(array_t& lhs, const array_t& rhs)
{
    using element_t = std::remove_reference<decltype(array_t::data[0])>::type;

    lhs.data = nullptr;
    lhs.size = 0;
    lhs.capacity = 0;

    if (rhs.size > 0)
    {
        unsigned int new_capacity = rhs.size;
        element_t* new_data = (element_t*)g_json_memory_interface.allocate(new_capacity * sizeof(element_t));

        for (unsigned int x = 0; x < rhs.size; x++)
        {
            new (&new_data[x]) element_t(rhs.data[x]);
        }

        lhs.size = rhs.size;
        lhs.data = new_data;
        lhs.capacity = new_capacity;
    }
}

template<typename array_t>
void internal_array_move_ctor(array_t& lhs, array_t&& rhs)
{
    using element_t = std::remove_reference<decltype(array_t::data[0])>::type;

    lhs.data = rhs.data;
    lhs.size = rhs.size;
    lhs.capacity = rhs.capacity;

    rhs.data = nullptr;
    rhs.size = 0;
    rhs.capacity = 0;
}

template<typename array_t>
void internal_array_dtor(array_t& arr)
{
    using element_t = std::remove_reference<decltype(array_t::data[0])>::type;

    for (unsigned int x = 0; x < arr.size; x++)
    {
        arr.data[x].~element_t();
    }

    if (arr.data)
        g_json_memory_interface.free(arr.data);

    arr.data = nullptr;
    arr.size = 0;
    arr.capacity = 0;
}

template<typename array_t>
decltype(array_t::data) internal_array_add(array_t& arr)
{
    using element_t = std::remove_reference<decltype(array_t::data[0])>::type;

    if (arr.size == arr.capacity)
    {
        unsigned int new_capacity = (arr.capacity + 1) * 2;
        element_t* new_data = (element_t*)g_json_memory_interface.allocate(new_capacity * sizeof(element_t));
        
        if (arr.data)
        {
            for (unsigned int x = 0; x < arr.size; x++)
            {
                new (&new_data[x]) element_t(std::move(arr.data[x]));
                arr.data[x].~element_t();
            }

            g_json_memory_interface.free(arr.data);
        }

        arr.data = new_data;
        arr.capacity = new_capacity;
    }

    element_t* new_elem = &arr.data[arr.size];
    new (new_elem) element_t{};
    arr.size++;
    return new_elem;
}

template<typename array_t>
void internal_array_reserve(array_t& arr, unsigned int new_capacity)
{
    using element_t = std::remove_reference<decltype(array_t::data[0])>::type;

    if (new_capacity <= arr.capacity)
        return;

    element_t* new_data = (element_t*)g_json_memory_interface.allocate(new_capacity * sizeof(element_t));

    if (arr.data)
    {
        for (unsigned int x = 0; x < arr.size; x++)
        {
            new (&new_data[x]) element_t(std::move(arr.data[x]));
            arr.data[x].~element_t();
        }

        g_json_memory_interface.free(arr.data);
    }

    arr.data = new_data;
    arr.capacity = new_capacity;
}

template<typename array_t>
void internal_array_resize(array_t& arr, unsigned int new_size)
{
    using element_t = std::remove_reference<decltype(array_t::data[0])>::type;

    if (new_size < arr.size)
    {
        for (unsigned int x = new_size; x < arr.size; x++)
        {
            arr.data[x].~element_t();
        }

        arr.size = new_size;
        return;
    }

    if (new_size == arr.size)
        return;

    if (new_size > arr.capacity)
    {
        unsigned int new_capacity = new_size;
        element_t* new_data = (element_t*)g_json_memory_interface.allocate(new_capacity * sizeof(element_t));

        if (arr.data)
        {
            for (unsigned int x = 0; x < arr.size; x++)
            {
                new (&new_data[x]) element_t(std::move(arr.data[x]));
                arr.data[x].~element_t();
            }

            g_json_memory_interface.free(arr.data);
        }

        arr.data = new_data;
        arr.capacity = new_capacity;
    }

    for (unsigned int x = arr.size; x < new_size; x++)
    {
        new (&arr.data[x]) element_t{};
    }

    arr.size = new_size;
}

template<typename array_t>
void internal_array_copy_assign(array_t& lhs, const array_t& rhs)
{
    using element_t = std::remove_reference<decltype(array_t::data[0])>::type;

    unsigned int old_size = lhs.size;

    internal_array_resize(lhs, rhs.size);

    for (unsigned int x = old_size; x < rhs.size; x++)
    {
        lhs.data[x] = rhs.data[x];
    }
}

template<typename array_t>
void internal_array_move_assign(array_t& lhs, array_t&& rhs)
{
    using element_t = std::remove_reference<decltype(array_t::data[0])>::type;

    internal_array_dtor(lhs);

    lhs.data = rhs.data;
    lhs.size = rhs.size;
    lhs.capacity = rhs.capacity;

    rhs.data = nullptr;
    rhs.size = 0;
    rhs.capacity = 0;
}

json_string::json_string(const json_string& rhs)
{
    text = nullptr;
    set_string(rhs.text);
}

json_string::json_string(json_string&& rhs) noexcept
{
    text = rhs.text;
    rhs.text = nullptr;
}

json_string::~json_string()
{
    if (text)
        g_json_memory_interface.free(text);
}

void json_string::set_string(const char* src)
{
    if (text)
    {
        g_json_memory_interface.free(text);
        text = nullptr;
    }

    if (src)
    {
        unsigned int length = strlen(src);
        text = (char*)g_json_memory_interface.reallocate(text, length + 1);
        memcpy(text, src, length);
        text[length] = '\0';
    }
}

const char* json_string::get_string() const
{
    return text ? text : "";
}

json_array::json_array(const json_array& rhs)
{
    internal_array_copy_ctor(*this, rhs);
}

json_array::json_array(json_array&& rhs) noexcept
{
    internal_array_move_ctor(*this, std::move(rhs));
}

json_array::~json_array()
{
    internal_array_dtor(*this);
}

json_value* json_array::add_null()
{
    json_value* new_elem = internal_add(); 
    new_elem->set_null(); 
    return new_elem; 
}

json_value* json_array::add_string(const char* value)
{
    json_value* new_elem = internal_add();
    new_elem->set_string(value);
    return new_elem;
}

json_value* json_array::add_uint(unsigned long long value)
{
    json_value* new_elem = internal_add(); 
    new_elem->set_uint(value);
    return new_elem;
}

json_value* json_array::add_int(long long value) 
{
    json_value* new_elem = internal_add();
    new_elem->set_int(value); 
    return new_elem;
}

json_value* json_array::add_float(double value)
{
    json_value* new_elem = internal_add();
    new_elem->set_float(value);
    return new_elem;
}

json_value* json_array::add_bool(bool value)
{ 
    json_value* new_elem = internal_add();
    new_elem->set_bool(value);
    return new_elem;
}

json_value* json_array::add_array() 
{
    json_value* new_elem = internal_add();
    new_elem->set_array();
    return new_elem;
}

json_value* json_array::add_object()
{ 
    json_value* new_elem = internal_add();
    new_elem->set_object(); 
    return new_elem;
}

json_value* json_array::get_element(unsigned int index)
{
    json_value* result = nullptr;

    if (index < size)
        result = &data[index];

    return result;
}

const json_value* json_array::get_element(unsigned int index) const
{
    const json_value* result = nullptr;

    if (index < size)
        result = &data[index];

    return result;
}

json_value* json_array::internal_add()
{
    json_value* new_elem = internal_array_add(*this);
    return new_elem;
}

struct json_object_member
{
    char* member_name = nullptr;
    json_value member_value;

    void set_name(const char* value);

    json_value* set_null();
    json_value* set_string(const char* value);
    json_value* set_uint(unsigned long long value);
    json_value* set_int(long long value);
    json_value* set_float(double value);
    json_value* set_bool(bool value);
    json_value* set_array();

    json_object_member() = default;
    json_object_member(const json_object_member& rhs);
    json_object_member(json_object_member&& rhs) noexcept;
    ~json_object_member();
};

void json_object_member::set_name(const char* name)
{
    unsigned int name_length = strlen(name);
    member_name = (char*)g_json_memory_interface.reallocate(member_name, name_length + 1);
    memcpy(member_name, name, name_length);
    member_name[name_length] = '\0';
}

json_value* json_object_member::set_null() 
{ 
    member_value.set_null(); 
    return &member_value;
}

json_value* json_object_member::set_string(const char* value) 
{ 
    member_value.set_string(value); 
    return &member_value;
}

json_value* json_object_member::set_uint(unsigned long long value) 
{
    member_value.set_uint(value);
    return &member_value;
}

json_value* json_object_member::set_int(long long value) 
{ 
    member_value.set_int(value); 
    return &member_value; 
}

json_value* json_object_member::set_float(double value)
{
    member_value.set_float(value);
    return &member_value;
}

json_value* json_object_member::set_bool(bool value) 
{
    member_value.set_bool(value);
    return &member_value;
}

json_value* json_object_member::set_array() 
{ 
    member_value.set_array(); 
    return &member_value;
}

json_object_member::json_object_member(const json_object_member& rhs) :
    member_name(nullptr),
    member_value(rhs.member_value)
{
    if (rhs.member_name)
        set_name(rhs.member_name);
}

json_object_member::json_object_member(json_object_member&& rhs) noexcept :
    member_name(rhs.member_name),
    member_value(std::move(rhs.member_value))
{
    rhs.member_name = nullptr;
}

json_object_member::~json_object_member()
{
    if (member_name)
        g_json_memory_interface.free(member_name);
}

json_object::json_object(const json_object& rhs)
{
    internal_array_copy_ctor(*this, rhs);
}

json_object::json_object(json_object&& rhs) noexcept
{
    internal_array_move_ctor(*this, std::move(rhs));
}

json_object::~json_object()
{
    internal_array_dtor(*this);
}

json_value* json_object::set_null(const char* name)
{
    json_value* member_value = get_member_value(name);
    if (!member_value) member_value = internal_add_member(name);
    member_value->set_null();
    return member_value;
}

json_value* json_object::set_string(const char* name, const char* value)
{
    json_value* member_value = get_member_value(name);
    if (!member_value) member_value = internal_add_member(name);
    member_value->set_string(value);
    return member_value;
}

json_value* json_object::set_uint(const char* name, unsigned long long value)
{
    json_value* member_value = get_member_value(name);
    if (!member_value) member_value = internal_add_member(name);
    member_value->set_uint(value);
    return member_value;
}

json_value* json_object::set_int(const char* name, long long value)
{
    json_value* member_value = get_member_value(name);
    if (!member_value) member_value = internal_add_member(name);
    member_value->set_int(value);
    return member_value;
}

json_value* json_object::set_float(const char* name, double value)
{
    json_value* member_value = get_member_value(name);
    if (!member_value) member_value = internal_add_member(name);
    member_value->set_float(value);
    return member_value;
}

json_value* json_object::set_bool(const char* name, bool value)
{
    json_value* member_value = get_member_value(name);
    if (!member_value) member_value = internal_add_member(name);
    member_value->set_bool(value);
    return member_value;
}

json_value* json_object::set_array(const char* name)
{
    json_value* member_value = get_member_value(name);
    if (!member_value) member_value = internal_add_member(name);
    member_value->set_array();
    return member_value;
}

json_value* json_object::set_object(const char* name)
{
    json_value* member_value = get_member_value(name);
    if (!member_value) member_value = internal_add_member(name);
    member_value->set_object();
    return member_value;
}

const char* json_object::get_string(const char* name) const
{
    const char* result = nullptr;

    const json_value* value = get_member_value(name);
    if (value)
        result = value->get_string();

    return result;
}

unsigned long long json_object::get_uint(const char* name) const
{
    unsigned long long result = 0;

    const json_value* value = get_member_value(name);
    if (value)
        result = value->get_uint();

    return result;
}

long long json_object::get_int(const char* name) const
{
    long long result = 0;

    const json_value* value = get_member_value(name);
    if (value)
        result = value->get_int();

    return result;
}

double json_object::get_float(const char* name) const
{
    double result = 0.0;

    const json_value* value = get_member_value(name);
    if (value)
        result = value->get_float();

    return result;
}

bool json_object::get_bool(const char* name) const
{
    bool result = false;

    const json_value* value = get_member_value(name);
    if (value)
        result = value->get_bool();

    return result;
}

unsigned int json_object::get_member_count() const
{
    return size;
}

const char* json_object::get_member_name(unsigned int index) const
{
    return data[index].member_name;
}

json_value* json_object::internal_add_member(const char* name)
{
    json_object_member* new_member = internal_array_add(*this);
    new_member->set_name(name);

    return &new_member->member_value;
}

json_value* json_object::get_member_value(unsigned int index)
{
    return &data[index].member_value;
}

const json_value* json_object::get_member_value(unsigned int index) const
{
    return &data[index].member_value;
}

bool json_object::has_member(const char* name) const
{
    return get_member_value(name) != nullptr;
}

const json_value* json_object::get_member_value(const char* name) const
{
    const json_value* result = nullptr;

    for (unsigned int x = 0; x < size; x++)
    {
        if (strcmp(data[x].member_name, name) == 0)
        {
            result = &data[x].member_value;
            break;
        }
    }

    return result;
}

json_value* json_object::get_member_value(const char* name)
{
    json_value* result = nullptr;

    for (unsigned int x = 0; x < size; x++)
    {
        if (strcmp(data[x].member_name, name) == 0)
        {
            result = &data[x].member_value;
            break;
        }
    }

    return result;
}

bool json_value::is_null()   const { return type == k_json_null; }
bool json_value::is_string() const { return type == k_json_string; }
bool json_value::is_uint()   const { return type == k_json_int && !int_value.get_is_signed(); }
bool json_value::is_int()    const { return type == k_json_int && int_value.get_is_signed(); }
bool json_value::is_float()  const { return type == k_json_float; }
bool json_value::is_bool()   const { return type == k_json_bool; }
bool json_value::is_array()  const { return type == k_json_array; }
bool json_value::is_object() const { return type == k_json_object; }

const char* json_value::get_string() const { return string_value.get_string(); }
unsigned long long json_value::get_uint() const { return int_value.get_uint(); }
long long json_value::get_int() const { return int_value.get_int(); }
double json_value::get_float() const { return float_value; }
bool json_value::get_bool() const { return bool_value; }

json_value* json_value::array_get_element(unsigned int index) { return array_value.get_element(index); }
const json_value* json_value::array_get_element(unsigned int index) const { return array_value.get_element(index); }
unsigned int json_value::array_get_size() const { return array_value.get_size(); }

const char* json_value::array_get_string(unsigned int index) const
{
    const char* result = nullptr;

    const json_value* element = array_get_element(index);
    if (element)
        result = element->get_string();

    return result;
}

unsigned long long json_value::array_get_uint(unsigned int index) const
{
    unsigned long long result = 0;

    const json_value* element = array_get_element(index);
    if (element)
        result = element->get_uint();

    return result;
}

long long json_value::array_get_int(unsigned int index) const
{
    long long result = 0;

    const json_value* element = array_get_element(index);
    if (element)
        result = element->get_int();

    return result;
}

double json_value::array_get_float(unsigned int index) const
{
    double result = 0.0;

    const json_value* element = array_get_element(index);
    if (element)
        result = element->get_float();

    return result;
}

bool json_value::array_get_bool(unsigned int index) const
{
    bool result = false;

    const json_value* element = array_get_element(index);
    if (element)
        result = element->get_bool();

    return result;
}

const json_value* json_value::array_get_array(unsigned int index) const
{
    const json_value* element = array_get_element(index);
    return element;
}

json_value* json_value::array_get_array(unsigned int index)
{
    json_value* element = array_get_element(index);
    return element;
}

const json_value* json_value::array_get_object(unsigned int index) const
{
    const json_value* element = array_get_element(index);
    return element;
}

json_value* json_value::array_get_object(unsigned int index)
{
    json_value* element = array_get_element(index);
    return element;
}

unsigned int json_value::object_get_member_count() const { return object_value.get_member_count(); }
json_value* json_value::object_get_member(unsigned int index) { return object_value.get_member_value(index); }
const json_value* json_value::object_get_member(unsigned int index) const { return object_value.get_member_value(index); }
const char* json_value::object_get_member_name(unsigned int index) const { return object_value.get_member_name(index); }

json_value* json_value::object_get_value(const char* name) { return object_value.get_member_value(name); }
const json_value* json_value::object_get_value(const char* name) const { return object_value.get_member_value(name); }

bool json_value::object_has_value(const char* name) const { return object_value.has_member(name); }

const char* json_value::object_get_string(const char* name) const { return object_value.get_string(name); }
unsigned long long json_value::object_get_uint(const char* name) const { return object_value.get_uint(name); }
long long json_value::object_get_int(const char* name) const { return object_value.get_int(name); }
double json_value::object_get_float(const char* name) const { return object_value.get_float(name); }
bool json_value::object_get_bool(const char* name) const { return object_value.get_bool(name); }
const json_value* json_value::object_get_array(const char* name) const { return object_value.get_member_value(name); }
json_value* json_value::object_get_array(const char* name) { return object_value.get_member_value(name); }
const json_value* json_value::object_get_object(const char* name) const { return object_value.get_member_value(name); }
json_value* json_value::object_get_object(const char* name) { return object_value.get_member_value(name); }

void json_value::set_null() { internal_make(k_json_null); }
void json_value::set_string(const char* value) { internal_make(k_json_string); string_value.set_string(value); }
void json_value::set_uint(unsigned long long value) { internal_make(k_json_int); int_value.set_uint(value); }
void json_value::set_int(long long value) { internal_make(k_json_int); int_value.set_int(value); }
void json_value::set_float(double value) { internal_make(k_json_float); float_value = value; }
void json_value::set_bool(bool value) { internal_make(k_json_bool); bool_value = value; }

void json_value::set_array() { internal_make(k_json_array); }
json_value* json_value::array_add_null() { return array_value.add_null(); }
json_value* json_value::array_add_string(const char* value) { return array_value.add_string(value); }
json_value* json_value::array_add_uint(unsigned long long value) { return array_value.add_uint(value); }
json_value* json_value::array_add_int(long long value) { return array_value.add_int(value); }
json_value* json_value::array_add_float(double value) { return array_value.add_float(value); }
json_value* json_value::array_add_bool(bool value) { return array_value.add_bool(value); }
json_value* json_value::array_add_array() { return array_value.add_array(); }
json_value* json_value::array_add_object() { return array_value.add_object(); }

void json_value::set_object() { internal_make(k_json_object); }
json_value* json_value::object_set_null(const char* name) { return object_value.set_null(name); }
json_value* json_value::object_set_string(const char* name, const char* value) { return object_value.set_string(name, value); }
json_value* json_value::object_set_uint(const char* name, unsigned long long value) { return object_value.set_uint(name, value); }
json_value* json_value::object_set_int(const char* name, long long value) { return object_value.set_int(name, value); }
json_value* json_value::object_set_float(const char* name, double value) { return object_value.set_float(name, value); }
json_value* json_value::object_set_bool(const char* name, bool value) { return object_value.set_bool(name, value); }
json_value* json_value::object_set_array(const char* name) { return object_value.set_array(name); }
json_value* json_value::object_set_object(const char* name) { return object_value.set_object(name); }

json_value::json_value()
{ 
    internal_make(k_json_null);
    formatting_option = k_json_format_default;
}

json_value::json_value(json_value_type ty)
{
    internal_make(ty);
    formatting_option = k_json_format_default; 
}

json_value::json_value(const json_value& rhs) :
    type(k_json_null),
    formatting_option(rhs.formatting_option)
{
    internal_make(rhs.type);
    switch (type)
    {
    case k_json_null: break;
    case k_json_int:    int_value = rhs.int_value; break;
    case k_json_float:  float_value = rhs.float_value; break;
    case k_json_bool:   bool_value = rhs.bool_value; break;
    case k_json_string: new (&string_value) json_string(rhs.string_value); break;
    case k_json_array:  new (&array_value) json_array(rhs.array_value); break;
    case k_json_object: new (&object_value) json_object(rhs.object_value); break;
    }
}

json_value::json_value(json_value&& rhs) noexcept :
    type(rhs.type),
    formatting_option(rhs.formatting_option)
{
    switch (type)
    {
    case k_json_null: break;
    case k_json_int:    int_value = rhs.int_value; break;
    case k_json_float:  float_value = rhs.float_value; break;
    case k_json_bool:   bool_value = rhs.bool_value; break;
    case k_json_string: new (&string_value) json_string(std::move(rhs.string_value)); break;
    case k_json_array:  new (&array_value) json_array(std::move(rhs.array_value)); break;
    case k_json_object: new (&object_value) json_object(std::move(rhs.object_value)); break;
    }
    rhs.type = k_json_null;
}

json_value::~json_value() { internal_destroy(); }


void json_value::set_formatting_option(json_formatting_option new_formatting_option)
{
    formatting_option = new_formatting_option;
}

json_formatting_option json_value::get_formatting_option() const
{
    return formatting_option;
}


void json_value::internal_destroy()
{
    switch (type)
    {
    case k_json_null: break;
    case k_json_int:    int_value.set_uint(0); break;
    case k_json_float:  float_value = 0.0; break;
    case k_json_bool:   bool_value = false; break;
    case k_json_string: string_value.~json_string(); break;
    case k_json_array:  array_value.~json_array(); break;
    case k_json_object: object_value.~json_object(); break;
    }

    type = k_json_null;
}

void json_value::internal_make(json_value_type new_type)
{
    internal_destroy();

    type = new_type;

    // init new
    switch (type)
    {
    case k_json_null: break;
    case k_json_int:    int_value.set_uint(0); break;
    case k_json_float:  float_value = 0.0; break;
    case k_json_bool:   bool_value = false; break;
    case k_json_string: new (&string_value) json_string; break;
    case k_json_array:  new (&array_value) json_array; break;
    case k_json_object: new (&object_value) json_object; break;
    }
}

static int sprintf_indent(char* output, int indent)
{
    int ret = indent * 2;

    while (indent--)
    {
        *output++ = ' ';
        *output++ = ' ';
    }

    *output = 0;
    return ret;
}

static char* json_pretty_print_internal(int indent, char* output, const json_extensions* extensions, const json_value* root, const json_formatting_option formatting_option)
{
    auto output_escaped_string = [&output, extensions](const char* str, char quote_char)
        {
            for (;;)
            {
                char ch = *str++;
                if (ch == 0) break;

                switch (ch)
                {
                case '"':  if (quote_char == '\"') { *output++ = '\\'; *output++ = '"'; } else { *output++ = '"'; } break;
                case '\\': *output++ = '\\'; *output++ = '\\'; break;
                case '/': *output++ = ch; break; // escaping forward slash is not required
                case '\'': if (quote_char == '\'') { *output++ = '\\'; *output++ = '\''; } else { *output++ = '\''; } break;
                case '\b': *output++ = '\\'; *output++ = 'b'; break;
                case '\f': *output++ = '\\'; *output++ = 'f'; break;
                case '\n': *output++ = '\\'; *output++ = 'n'; break;
                case '\r': *output++ = '\\'; *output++ = 'r'; break;
                case '\t': *output++ = '\\'; *output++ = 't'; break;
                default: *output++ = ch; break;
                }
            }
        };

    *output = '\0';

    if (root->is_null())
        output += sprintf(output, "null");
    else if (root->is_string())
    {
        char quote_char = extensions->use_single_quotes_for_strings ? '\'' : '"';

        *output++ = quote_char;
        output_escaped_string(root->get_string(), quote_char);
        *output++ = quote_char;
        *output = '\0';
    }
    else if (root->is_uint())
        output += sprintf(output, "%llu", root->get_uint());
    else if (root->is_int())
        output += sprintf(output, "%lld", root->get_int());
    else if (root->is_float())
        output += sprintf(output, "%f", root->get_float());
    else if (root->is_bool())
        output += sprintf(output, "%s", root->get_bool() ? "true" : "false");
    else if (root->is_array())
    {
        if (root->array_get_size() > 0)
        {
            const json_value* first_value = root->array_get_element(0);

            if ((formatting_option == k_json_format_multi_line) || (first_value->is_object() || first_value->is_array()) && (formatting_option == k_json_format_default))
            {
                *output++ = '\n';
                output += sprintf_indent(output, indent);
                output += sprintf(output, "[\n");

                for (unsigned int x = 0; x < root->array_get_size(); x++)
                {
                    output += sprintf_indent(output, indent + 1);

                    const json_value* value = root->array_get_element(x);
                    output = json_pretty_print_internal(indent + 1, output, extensions, value, (formatting_option == k_json_format_default) ? value->get_formatting_option() : formatting_option);

                    if (x < root->array_get_size() - 1)
                    {
                        *output++ = ',';
                    }

                    *output++ = '\n';
                }

                output += sprintf_indent(output, indent);
                *output++ = ']';
            }
            else
            {
                *output++ = '[';

                for (unsigned int x = 0; x < root->array_get_size(); x++)
                {
                    const json_value* value = root->array_get_element(x);
                    output = json_pretty_print_internal(indent + 1, output, extensions, value, (formatting_option == k_json_format_default) ? value->get_formatting_option() : formatting_option);

                    if (x < root->array_get_size() - 1)
                    {
                        *output++ = ',';
                        *output++ = ' ';
                    }
                }

                *output++ = ']';
            }
        }
        else
        {
            *output++ = '[';
            *output++ = ']';
        }
    }
    else if (root->is_object())
    {
        if (formatting_option == k_json_format_single_line)
        {
            output += sprintf(output, "{ ");

            for (unsigned int x = 0; x < root->object_get_member_count(); x++)
            {
                const char* member_name = root->object_get_member_name(x);
                const json_value* member_value = root->object_get_member(x);

                char quote_char = extensions->use_single_quotes_for_strings ? '\'' : '"';
                *output++ = quote_char;
                output_escaped_string(member_name, quote_char);
                *output++ = quote_char;

                *output++ = ' ';
                *output++ = ':';
                *output++ = ' ';
                output = json_pretty_print_internal(indent + 1, output, extensions, member_value, k_json_format_single_line);

                if (x < root->object_get_member_count() - 1)
                {
                    *output++ = ',';
                    *output++ = ' ';
                }
            }

            output += sprintf(output, "}");
        }
        else
        {
            output += sprintf(output, "{\n");

            for (unsigned int x = 0; x < root->object_get_member_count(); x++)
            {
                const char* member_name = root->object_get_member_name(x);
                const json_value* member_value = root->object_get_member(x);

                output += sprintf_indent(output, indent + 1);

                char quote_char = extensions->use_single_quotes_for_strings ? '\'' : '"';
                *output++ = quote_char;
                output_escaped_string(member_name, quote_char);
                *output++ = quote_char;

                *output++ = ' ';
                *output++ = ':';
                *output++ = ' ';

                if (member_value->is_object())
                {
                    *output++ = '\n';
                    output += sprintf_indent(output, indent + 1);
                }

                output = json_pretty_print_internal(indent + 1, output, extensions, member_value, member_value->get_formatting_option());

                if (x < root->object_get_member_count() - 1)
                {
                    *output++ = ',';
                }

                *output++ = '\n';
            }

            output += sprintf_indent(output, indent);
            output += sprintf(output, "}");
        }
    }

    *output = 0;
    return output;
}

char* json_pretty_print(char* output, const json_extensions* extensions, const json_value* root)
{
    json_extensions extensions_to_use{};
    if (extensions)
    {
        extensions_to_use = *extensions;
    }

    return json_pretty_print_internal(0, output, &extensions_to_use, root, root->get_formatting_option());
}

struct json_token
{
    json_token_type type;
    char const* symbol_begin;
    char const* symbol_end;

    unsigned int row;
    unsigned int col;

    json_token()
    {
        reset();
    }

    void reset()
    {
        row = 0;
        col = 0;
        type = k_tok_invalid;
        symbol_begin = nullptr;
        symbol_end = nullptr;
    }

    bool text_equals(const char* str) const
    {
        if (!symbol_begin || !symbol_end)
            return false;
        unsigned int symbol_length = (unsigned int)(symbol_end - symbol_begin);
        unsigned int str_length = (unsigned int)strlen(str);
        if (symbol_length != str_length)
            return false;
        return (strncmp(symbol_begin, str, symbol_length) == 0);
    }

    char* allocate_unescaped_string()
    {
        unsigned int estimated_size = (unsigned int)(symbol_end - symbol_begin);
        char* unescaped_string = (char*)g_json_memory_interface.allocate(estimated_size + 1);

        const char* src_cursor = symbol_begin;
        char* dst_cursor = unescaped_string;

        unsigned int high_surrogate = 0;
        bool expect_surrogate_low = false;

        auto consume_hex_codepoint = [](const char*& cursor) -> unsigned int
            {
                char hex0 = *cursor++;
                char hex1 = *cursor++;
                char hex2 = *cursor++;
                char hex3 = *cursor++;

                auto hex_to_val = [](char hex) -> unsigned char
                    {
                        if ('0' <= hex && hex <= '9') return hex - '0';
                        if ('a' <= hex && hex <= 'f') return hex - 'a' + 0xa;
                        if ('A' <= hex && hex <= 'F') return hex - 'A' + 0xA;

                        return 0;
                    };

                unsigned char val0 = hex_to_val(hex0);
                unsigned char val1 = hex_to_val(hex1);
                unsigned char val2 = hex_to_val(hex2);
                unsigned char val3 = hex_to_val(hex3);

                unsigned int codepoint = (val0 << 12) | (val1 << 8) | (val2 << 4) | val3;
                return codepoint;
            };

        while (src_cursor != symbol_end)
        {
            char ch = *src_cursor++;

            if (expect_surrogate_low)
            {
                if (ch == '\\' && src_cursor != symbol_end)
                {
                    char u = *src_cursor++;

                    if (u == 'u')
                    {
                        unsigned int low_surrogate = consume_hex_codepoint(src_cursor);

                        if (0xDC00 <= low_surrogate && low_surrogate <= 0xDFFF)
                        {
                            // low surrogate
                            unsigned int codepoint = high_surrogate + (low_surrogate - 0xDC00u) + 0x10000u;
                            // 4 bytes
                            *dst_cursor++ = (char)(0xF0 | ((codepoint >> 18) & 0x07));
                            *dst_cursor++ = (char)(0x80 | ((codepoint >> 12) & 0x3F));
                            *dst_cursor++ = (char)(0x80 | ((codepoint >> 6) & 0x3F));
                            *dst_cursor++ = (char)(0x80 | (codepoint & 0x3F));

                            expect_surrogate_low = false;
                        }
                        else
                        {
                            // should have been low surrogate
                            g_json_memory_interface.free(unescaped_string);
                            return nullptr;
                        }
                    }
                }
            }
            else if (ch == '\\')
            {
                switch (*src_cursor++)
                {
                case '"':  *dst_cursor++ = '"'; break;
                case '\\': *dst_cursor++ = '\\'; break;
                case '/':  *dst_cursor++ = '/'; break;
                case '\'':  *dst_cursor++ = '\''; break;
                case 'b':  *dst_cursor++ = '\b'; break;
                case 'f':  *dst_cursor++ = '\f'; break;
                case 'n':  *dst_cursor++ = '\n'; break;
                case 'r':  *dst_cursor++ = '\r'; break;
                case 't':  *dst_cursor++ = '\t'; break;

                case 'u':
                    // four hex digits
                    unsigned int codepoint = consume_hex_codepoint(src_cursor);

                    if (codepoint <= 0x7F)
                    {
                        *dst_cursor++ = (char)codepoint;
                    }
                    else if (codepoint <= 0x7FF)
                    {
                        // 2 bytes
                        *dst_cursor++ = (char)(0xC0 | ((codepoint >> 6) & 0x1F));
                        *dst_cursor++ = (char)(0x80 | (codepoint & 0x3F));
                    }
                    else if (0xD800 <= codepoint && codepoint <= 0xDBFF)
                    {
                        // high surrogate
                        expect_surrogate_low = true;
                        high_surrogate = (codepoint - 0xD800u) * 0x400u;
                    }
                    else if (0xDC00 <= codepoint && codepoint <= 0xDFFF)
                    {
                        // unexpected low surrogate
                        g_json_memory_interface.free(unescaped_string);
                        return nullptr;
                    }
                    else
                    {
                        // 3 bytes
                        *dst_cursor++ = (char)(0xE0 | ((codepoint >> 12) & 0x0F));
                        *dst_cursor++ = (char)(0x80 | ((codepoint >> 6) & 0x3F));
                        *dst_cursor++ = (char)(0x80 | (codepoint & 0x3F));
                    }
                }
            }
            else
            {
                *dst_cursor++ = ch;
            }
        }

        *dst_cursor = '\0';

        if (expect_surrogate_low)
        {
            // expected low surrogate but didn't get one
            g_json_memory_interface.free(unescaped_string);
            return nullptr;
        }

        return unescaped_string;
    }
};

static char* json_allocate_tokenizer_fault_msg(unsigned int row, unsigned int col, char const* message)
{
    char* output = (char*)g_json_memory_interface.allocate(1024);
    sprintf(output, "[row %u][col %u] : %s", row, col, message);
    return output;
}

struct json_tokenizer
{
    const char* buffer = nullptr;
    unsigned int buffer_offset = 0;
    unsigned int buffer_size = 0;

    const json_extensions* extensions = nullptr;

    json_token token;

    unsigned int row = 0;
    unsigned int col = 0;

    bool fault = false;
    const char* fault_msg = nullptr;
    bool eof = false;

    explicit json_tokenizer(const char* buffer, unsigned int buffer_size, const json_extensions* extensions) :
        buffer(buffer),
        buffer_offset(0),
        buffer_size(buffer_size),
        extensions(extensions)
    {
        row = 1;
        col = 1;

        fault = false;
        eof = false;
    }

    ~json_tokenizer()
    {
        g_json_memory_interface.free(fault_msg);
    }

    void set_fault(const char* message)
    {
        fault = true;
        fault_msg = json_allocate_tokenizer_fault_msg(row, col - 1, message);
    }

    void skip_ws()
    {
        while (buffer_offset < buffer_size)
        {
            const char* ch = &buffer[buffer_offset];

            if (*ch == '\n')
            {
                row++;
                col = 1;

                buffer_offset++;
            }
            else if ((*ch == '\r') || (*ch == '\t') || (*ch == ' '))
            {
                col++;

                buffer_offset++;
            }
            else
            {
                break;
            }
        }
    }

    const char* next_char_skip_ws()
    {
        skip_ws();

        if (buffer_offset < buffer_size)
        {
            const char* ch = &buffer[buffer_offset++];
            col++;

            return ch;
        }

        return nullptr;
    }

    void get_next_token(json_token* out_token)
    {
        // find the start of the next token
        if (const char* token_start = next_char_skip_ws())
        {
            const char ch = *token_start;

            token.row = row;
            token.col = col - 1;

            if (ch == '{') { token.type = k_tok_openbrace; }
            else if (ch == '}') { token.type = k_tok_closebrace; }

            else if (ch == '[') { token.type = k_tok_openbracket; }
            else if (ch == ']') { token.type = k_tok_closebracket; }

            else if (ch == ':') { token.type = k_tok_colon; }
            else if (ch == ',') { token.type = k_tok_comma; }

            else if ((ch == '"') || (extensions->use_single_quotes_for_strings && (ch == '\'')))
            {
                const char quote_char = ch;
                bool escaping = false;

                // skip starting quote
                token_start++;

                unsigned int string_state = 0;

                auto is_control_character = [](char ch) -> bool
                    {
                        return (0 <= ch && ch <= 0x1F);
                    };

                unsigned char codepoint_length_remaining = 0;

                while ((string_state != 99) && (buffer_offset < buffer_size))
                {
                    char string_ch = buffer[buffer_offset];
                    unsigned char string_uch = (unsigned char)string_ch;

                    switch (string_state)
                    {
                    case 0:
                        // '"' or '\' or codepoint
                        if (string_ch == quote_char)
                        {
                            string_state = 99;
                        }
                        else if (string_ch == '\\')
                        {
                            buffer_offset++;
                            col++;

                            string_state = 5;
                        }
                        else if (is_control_character(string_ch))
                        {
                            set_fault("unescaped control character in string");
                            return;
                        }
                        else
                        {
                            string_state = 1;
                        }
                        break;

                    case 1:
                        // first byte of codepoint
                        buffer_offset++;
                        col++;

                        if ((string_uch & 0b10000000) == 0b00000000)
                        {
                            // single ascii character
                            string_state = 0;
                        }
                        else if ((string_uch & 0b111'00000) == 0b110'00000)
                        {
                            // two byte codepoint
                            codepoint_length_remaining = 1;

                            string_state = 2;
                        }
                        else if ((string_uch & 0b1111'0000) == 0b1110'0000)
                        {
                            // three byte codepoint
                            codepoint_length_remaining = 2;

                            string_state = 2;
                        }
                        else if ((string_uch & 0b11111'000) == 0b11110'000)
                        {
                            // four byte codepoint
                            codepoint_length_remaining = 3;

                            string_state = 2;
                        }
                        else
                        {
                            set_fault("invalid utf8 leading byte in string");
                            return;
                        }
                        break;

                    case 2:
                        if ((string_uch & 0b11'000000) == 0b10'000000)
                        {
                            // valid continuation byte
                            codepoint_length_remaining--;

                            buffer_offset++;
                            col++;

                            if (codepoint_length_remaining > 0)
                            {
                                string_state = 2;
                            }
                            else
                            {
                                string_state = 0;
                            }
                        }
                        else
                        {
                            set_fault("invalid utf8 continuation byte in string");
                            return;
                        }
                        break;

                    case 5:
                        // escape character
                        if (string_ch == '\"' ||
                            string_ch == '\\' ||
                            string_ch == '/' ||
                            string_ch == '\'' ||
                            string_ch == 'b' ||
                            string_ch == 'f' || 
                            string_ch == 'n' || 
                            string_ch == 'r' || 
                            string_ch == 't')
                        {
                            // valid escape character
                            buffer_offset++;
                            col++;

                            string_state = 0;
                        }
                        else if (string_ch == 'u')
                        {
                            // goto hex1
                            buffer_offset++;
                            col++;

                            string_state = 10;
                        }
                        else
                        {
                            char fault_msg_buffer[64];
                            sprintf(fault_msg_buffer, "unrecognized escape character '%c'", ch);
                            set_fault(fault_msg_buffer);
                            return;
                        }
                        break;

                    case 10: // hex1
                        if (('0' <= string_ch && string_ch <= '9') ||
                            ('A' <= string_ch && string_ch <= 'F') ||
                            ('a' <= string_ch && string_ch <= 'f'))
                        {
                            // valid hex digit
                            buffer_offset++;
                            col++;

                            string_state = 11; // goto hex2
                        }
                        else
                        {
                            set_fault("expected hex digit in escaped unicode codepoint");
                            return;
                        }
                        break;

                    case 11: // hex2
                        if (('0' <= string_ch && string_ch <= '9') ||
                            ('A' <= string_ch && string_ch <= 'F') ||
                            ('a' <= string_ch && string_ch <= 'f'))
                        {
                            // valid hex digit
                            buffer_offset++;
                            col++;

                            string_state = 12; // goto hex3
                        }
                        else
                        {
                            set_fault("expected hex digit in escaped unicode codepoint");
                            return;
                        }
                        break;

                    case 12: // hex3
                        if (('0' <= string_ch && string_ch <= '9') ||
                            ('A' <= string_ch && string_ch <= 'F') ||
                            ('a' <= string_ch && string_ch <= 'f'))
                        {
                            // valid hex digit
                            buffer_offset++;
                            col++;

                            string_state = 13; // goto hex4
                        }
                        else
                        {
                            set_fault("expected hex digit in escaped unicode codepoint");
                            return;
                        }
                        break;

                    case 13: // hex4
                        if (('0' <= string_ch && string_ch <= '9') ||
                            ('A' <= string_ch && string_ch <= 'F') ||
                            ('a' <= string_ch && string_ch <= 'f'))
                        {
                            // valid hex digit
                            buffer_offset++;
                            col++;

                            string_state = 0; // goto normal
                        }
                        else
                        {
                            set_fault("expected hex digit in escaped unicode codepoint");
                            return;
                        }
                        break;

                    case 99:
                        // done
                        if (string_ch == quote_char)
                        {
                            //end of string
                            break;
                        }
                        else
                        {
                            set_fault("unterminated string");
                            return;
                        }
                        break;
                    }
                }

                token.type = k_tok_string;
                token.symbol_begin = token_start;
                token.symbol_end = &buffer[buffer_offset];

                // consume ending quote
                buffer_offset++;
                col++;
            }

            else if ((ch == '-') || ('0' <= ch && ch <= '9'))
            {
                // number
                buffer_offset--; // to re-parse the first character
                col--;

                unsigned int number_state = 0;
                
                while ((number_state != 4) && (buffer_offset < buffer_size))
                {
                    char number_ch = buffer[buffer_offset];

                    switch (number_state)
                    {
                    case 0:
                        // '-' or empty
                        if (number_ch == '-')
                        {
                            buffer_offset++;
                            col++;
                        }

                        number_state = 1;
                        break;

                    case 1:
                    {
                        // '0' or 1-9
                        if (number_ch == '0')
                        {
                            buffer_offset++;
                            col++;

                            number_state = 2;
                        }
                        else if ('1' <= number_ch && number_ch <= '9')
                        {
                            buffer_offset++;
                            col++;

                            number_state = 5;
                        }
                        else
                        {
                            set_fault("invalid number format");
                            return;
                        }
                    }
                    break;

                    case 2:
                        // '.' or empty
                        if (number_ch == '.')
                        {
                            buffer_offset++;
                            col++;

                            number_state = 7;
                        }
                        else
                        {
                            number_state = 3;
                        }
                        break;

                    case 3:
                        // 'e'/'E' or empty
                        if (number_ch == 'e' || number_ch == 'E')
                        {
                            buffer_offset++;
                            col++;

                            number_state = 9;
                        }
                        else
                        {
                            number_state = 4;
                        }
                        break;

                    case 5:
                        // '0'-'9' or empty
                        if ('0' <= number_ch && number_ch <= '9')
                        {
                            buffer_offset++;
                            col++;

                            number_state = 5;
                        }
                        else
                        {
                            number_state = 2;
                        }
                        break;

                    case 7:
                        // '0'-'9'
                        if ('0' <= number_ch && number_ch <= '9')
                        {
                            buffer_offset++;
                            col++;

                            number_state = 8;
                        }
                        else
                        {
                            set_fault("invalid number format");
                            return;
                        }
                        break;

                    case 8:
                        // '0'-'9' or empty
                        if ('0' <= number_ch && number_ch <= '9')
                        {
                            buffer_offset++;
                            col++;

                            number_state = 8;
                        }
                        else
                        {
                            number_state = 3;
                        }
                        break;

                    case 9:
                        // '+'/'-' or empty
                        if (number_ch == '+' || number_ch == '-')
                        {
                            buffer_offset++;
                            col++;
                        }

                        number_state = 10;
                        break;

                    case 10:
                        // '0'-'9'
                        if ('0' <= number_ch && number_ch <= '9')
                        {
                            buffer_offset++;
                            col++;

                            number_state = 11;
                        }
                        else
                        {
                            set_fault("invalid number format");
                            return;
                        }
                        break;

                    case 11:
                        // '0'-'9' or empty
                        if ('0' <= number_ch && number_ch <= '9')
                        {
                            buffer_offset++;
                            col++;

                            number_state = 11;
                        }
                        else
                        {
                            number_state = 4;
                        }
                        break;
                    }
                }

                token.type = k_tok_number;
                token.symbol_begin = token_start;
                token.symbol_end = &buffer[buffer_offset];
            }
            
            else if ('a' <= ch && ch <= 'z')
            {
                // true | false | null

                while (buffer_offset < buffer_size)
                {
                    char next_ch = buffer[buffer_offset];
                    
                    if ('a' <= next_ch && next_ch <= 'z')
                    {
                        buffer_offset++;
                        col++;
                    }
                    else
                    {
                        break;
                    }
                }

                token.symbol_begin = token_start;
                token.symbol_end = &buffer[buffer_offset];

                if (token.text_equals("true"))
                {
                    token.type = k_tok_true;
                }
                else if (token.text_equals("false"))
                {
                    token.type = k_tok_false;
                }
                else if (token.text_equals("null"))
                {
                    token.type = k_tok_null;
                }
            }

            if (fault) return;

            if (token.type == k_tok_invalid)
            {
                if (ch > 0 && isprint(ch))
                {
                    char fault_msg_buffer[64];
                    sprintf(fault_msg_buffer, "Unrecognized character '%c'", ch);
                    set_fault(fault_msg_buffer);
                    return;
                }
                else
                {
                    set_fault("Non-printable character. Binary data?");
                    return;
                }
            }

            *out_token = token;
            token.reset();
            return;
        }

        eof = true;
    }
};

static const char* json_allocate_parser_fault_msg(const json_token* token, const char* message)
{
    char* output = (char*)g_json_memory_interface.allocate(1024);
    output[0] = '\0';
    
    if (token)
    {
        sprintf(output, "[row %u][col %u] : %s", token->row, token->col, message);
    }
    else
    {
        sprintf(output, "%s", message);
    }

    return output;
}

struct json_parser
{
    json_tokenizer* tokenizer = nullptr;
    const json_extensions* extensions = nullptr;

    json_token token;

    unsigned int pos = 0;
    bool cur_token = false;

    bool fault = false;
    const char* fault_msg = nullptr;

    json_parser(json_tokenizer* tokenizer, json_extensions* extensions)
        :tokenizer(tokenizer),
        extensions(extensions)
    {
    }

    ~json_parser()
    {
        g_json_memory_interface.free(fault_msg);
    }

    void set_fault(char const* message)
    {
        fault = true;
        fault_msg = json_allocate_parser_fault_msg(&token, message);
    }

    void set_fault(const json_token* fault_token, char const* message)
    {
        fault = true;
        fault_msg = json_allocate_parser_fault_msg(fault_token, message);
    }

    void parse(json_value* root)
    {
        cur_token = false;
        pos = 0;

        if (extensions->allow_nonobject_root)
        {
            parse_value(root);
        }
        else
        {
            parse_object(root);
        }

        if (fault) return;

        // skip trailing whitespace
        tokenizer->skip_ws();

        if (tokenizer->buffer_offset != tokenizer->buffer_size)
        {
            set_fault("parsing complete but entire input was not consumed");
        }
    }

    bool next_token()
    {
        if (cur_token)
        {
            cur_token = false;
            return true;
        }

        tokenizer->get_next_token(&token);

        if (tokenizer->fault)
        {
            set_fault(tokenizer->fault_msg);
        }

        if (tokenizer->eof)
        {
            return false;
        }

        pos++;
        return true;
    }

    bool expect_token_type(json_token_type type)
    {
        if (next_token() && token.type == type) return true;

        return false;
    }

    void parse_object(json_value* obj)
    {
        if (!expect_token_type(k_tok_openbrace)) { set_fault("Expected start-of-object"); return; }

        obj->set_object();

        parse_object_members(obj);
        if (fault) return;

        if (!expect_token_type(k_tok_closebrace)) { set_fault("Expected end-of-object"); return; }
    }

    int prefetch_tokens(int num, json_token* out_tokens)
    {
        int x = 0;
        while (x < num)
        {
            if (next_token())
            {
                out_tokens[x] = token;
            }
            else
            {
                break;
            }

            x++;
        }

        return x;
    }

    void parse_value(json_value* val)
    {
        //figure out what this is
        if (next_token())
        {
            switch (token.type)
            {
            case k_tok_string:
            {
                char* unescaped_str = token.allocate_unescaped_string();
                if (unescaped_str == nullptr) { set_fault(&token, "failed to parse string"); return; }
                val->set_string(unescaped_str);
                g_json_memory_interface.free(unescaped_str);
            }
            break;

            case k_tok_number:
            {
                cur_token = true;
                parse_number(val);
            }
            break;

            case k_tok_openbrace:
            {
                cur_token = true;
                parse_object(val);
            }
            break;

            case k_tok_openbracket:
            {
                cur_token = true;
                parse_array(val);
            }
            break;

            case k_tok_true:
            {
                val->set_bool(true);
            }
            break;

            case k_tok_false:
            {
                val->set_bool(false);
            }
            break;

            case k_tok_null:
            {
                val->set_null();
            }
            break;

            case k_tok_closebrace:
            case k_tok_colon:
            case k_tok_closebracket:
            case k_tok_comma:
                //bad!
                set_fault("Expected value");
                break;

            case k_tok_invalid:
            default:
                //bad!
                set_fault("unexpected token");
                break;
            }
        }
    }

    void parse_array(json_value* arr)
    {
        if (!expect_token_type(k_tok_openbracket)) { set_fault("Expected start-of-array"); return; }

        arr->set_array();

        parse_array_elements(arr);
        if (fault) return;

        if (!expect_token_type(k_tok_closebracket)) { set_fault("Expected end-of-array"); return; }
    }

    void parse_array_elements(json_value* arr)
    {
        for (;;)
        {
            if (next_token())
            {
                cur_token = true;

                if (token.type == k_tok_closebracket) //empty array?
                {
                    return;
                }
            }
            else
            {
                set_fault("Expected array elements, got EOF ");
                return;
            }

            json_value* element = arr->array_add_null();
            parse_value(element);
            if (fault) return;

            if (next_token())
            {
                if (token.type == k_tok_comma)
                {
                    continue;
                }
                else if (token.type == k_tok_closebracket)
                {
                    cur_token = true;
                    return;
                }
                else
                {
                    set_fault("Parsing array elements, expected comma or end-of-array");
                    return;
                }
            }
            else
            {
                set_fault("Parsing array elements, unexpected EOF ");
                return;
            }
        }
    }

    void parse_number(json_value* num)
    {
        //figure out if it's an int or float
        if (next_token() && token.type == k_tok_number)
        {
            char* token_text = token.allocate_unescaped_string();
            if (token_text == nullptr) { set_fault(&token, "failed to parse string"); return; }

            unsigned int token_length = strlen(token_text);

            bool is_float = false;

            for (unsigned int i = 0; i < token_length; i++)
            {
                char tch = token_text[i];
                if ((tch == '.') || (tch == 'e') || (tch == 'E'))
                {
                    is_float = true;
                    break;
                }
            }

            if (is_float)
            {
                //float
                double f = strtod(token_text, nullptr);
                num->set_float(f);
            }
            else
            {
                //int or uint
                if (token_text[0] == '-')
                {
                    long long i = strtoll(token_text, nullptr, 10);
                    num->set_int(i);
                }
                else
                {
                    unsigned long long i = strtoull(token_text, nullptr, 10);
                    num->set_uint(i);
                }
            }


            g_json_memory_interface.free(token_text);
        }
        else
        {
            set_fault("Expected number, got EOF");
        }
    }

    void parse_object_members(json_value* obj)
    {
        for (;;)
        {
            //check for empty object
            if (next_token())
            {
                cur_token = true;

                if (token.type == k_tok_closebrace) //empty object
                {
                    return;
                }
            }
            else
            {
                set_fault("Expected object member or end of object, got EOF");
                return;
            }

            json_token prefetch[3];
            int prefetch_size = prefetch_tokens(3, &prefetch[0]);

            if (prefetch_size == 1)
            {
                //this is OK, if we hit a close brace (end of object)
                if (prefetch[0].type == k_tok_closebrace)
                {
                    cur_token = true;
                }
                else
                {
                    set_fault("Expected object member, got EOF");
                    return;
                }

                return;
            }

            if (prefetch_size != 3)
            {
                set_fault("Expected object member, got EOF");
                return;
            }

            if (prefetch[0].type != k_tok_string)
            {
                set_fault(&prefetch[0], "Parsing object member, expected member name string");
                return;
            }

            if (prefetch[1].type != k_tok_colon)
            {
                set_fault(&prefetch[1], "Parsing object member, expected colon separator");
                return;
            }

            cur_token = true;

            char* member_name = prefetch[0].allocate_unescaped_string();
            if (member_name == nullptr) { set_fault(&prefetch[0], "failed to parse string"); return; }
            json_value* member_value = obj->object_set_null(member_name);
            g_json_memory_interface.free(member_name);

            parse_value(member_value);
            if (fault) return;

            if (next_token() && token.type == k_tok_comma)
            {
                continue;
            }
            else
            {
                //end of the line
                cur_token = true;
                return;
            }
        }
    }
};

bool json_parse(const char* buffer, unsigned int buffer_size, const json_extensions* extensions, json_value* out_obj)
{
    json_extensions extensions_to_use{};
    if (extensions)
    {
        extensions_to_use = *extensions;
    }

    json_tokenizer tokenizer(buffer, buffer_size, &extensions_to_use);
    json_parser parser(&tokenizer, &extensions_to_use);
    parser.parse(out_obj);

    if (parser.fault)
    {
        fputs(parser.fault_msg, stderr);
    }

    return !parser.fault;
}
