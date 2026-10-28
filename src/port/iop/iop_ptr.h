// Typed access to IOP RAM for the readable port of the IOP modules (plan P5).
//
// Native replacements of module functions run on the host but keep all state where the original keeps it: in the IOP's 2 MB of RAM. The
// structures of config/iop/<module>_types.h are laid over that memory; their pointer fields are 32 bit IOP addresses, IopPtr<T>.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace iop
{
    // The RAM as host memory; set at every native entry (the emulator hands it to the function's context).
    inline uint8_t *g_ram = nullptr;
    inline constexpr uint32_t kRamMask = 0x1FFFFFu; // 2 MB; the KSEG0 and KSEG1 mirrors of RAM fall on the same bytes

    inline uint8_t *host(uint32_t address)
    {
        return g_ram + (address & kRamMask);
    }

    template <typename T>
    T load(uint32_t address)
    {
        T value;
        std::memcpy(&value, host(address), sizeof(T));
        return value;
    }

    template <typename T>
    void store(uint32_t address, T value)
    {
        std::memcpy(host(address), &value, sizeof(T));
    }
}

template <typename T>
struct IopPtr;
template <>
struct IopPtr<void>;

// A 32 bit pointer as the module stores it. Same size and layout as the IOP's pointer, so structures that contain it keep the module's layout.
// Test for null with `if (p)`.
template <typename T>
struct IopPtr
{
    uint32_t address;

    T *get() const { return reinterpret_cast<T *>(iop::host(address)); }
    T *operator->() const { return get(); }
    T &operator*() const { return *get(); }
    T &operator[](int32_t i) const { return *(*this + i).get(); }
    IopPtr operator+(int32_t i) const { return IopPtr{address + static_cast<uint32_t>(i) * static_cast<uint32_t>(sizeof(T))}; }
    IopPtr operator-(int32_t i) const { return *this + -i; }
    explicit operator bool() const { return address != 0; }
    bool operator==(const IopPtr &other) const { return address == other.address; }
    bool operator!=(const IopPtr &other) const { return address != other.address; }
    operator IopPtr<void>() const;
};

template <>
struct IopPtr<void>
{
    uint32_t address;

    void *get() const { return iop::host(address); }
    explicit operator bool() const { return address != 0; }
    bool operator==(const IopPtr &other) const { return address == other.address; }
    bool operator!=(const IopPtr &other) const { return address != other.address; }
};

template <typename T>
IopPtr<T>::operator IopPtr<void>() const
{
    return IopPtr<void>{address};
}

static_assert(sizeof(IopPtr<int>) == 4 && std::is_standard_layout_v<IopPtr<int>> && std::is_trivially_copyable_v<IopPtr<int>>);

namespace iop
{
    // A variable of the module at a fixed IOP address (the generated <module>_globals.h).
    template <typename T>
    struct Var
    {
        uint32_t address;

        T *get() const { return reinterpret_cast<T *>(host(address)); }
        T *operator->() const { return get(); }
        operator T &() const { return *get(); }
        Var &operator=(const T &value)
        {
            *get() = value;
            return *this;
        }
        IopPtr<T> ptr() const { return IopPtr<T>{address}; }
    };

    // An array of the module at a fixed IOP address.
    template <typename T, size_t N>
    struct ArrayVar
    {
        uint32_t address;

        T &operator[](size_t i) const { return reinterpret_cast<T *>(host(address))[i]; }
        IopPtr<T> ptr() const { return IopPtr<T>{address}; }
        IopPtr<T> at(size_t i) const { return IopPtr<T>{address + static_cast<uint32_t>(i * sizeof(T))}; }
        static constexpr size_t size() { return N; }
    };
}
