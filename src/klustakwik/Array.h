// Array.h — C++17 modernisation of KlustaKwik array helpers
// Changes from original:
//   - Added required standard headers (was using cerr without them)
//   - Added move constructor + move assignment (eliminates copies on return)
//   - operator[] now has a proper const overload returning const T&
//   - SetSize initialises to zero (avoids valgrind noise)
//   - Array2 uses a single flat allocation instead of pointer-to-pointer
//     (better cache behaviour; layout is row-major, same as before)
//   - Removed raw abort() in favour of std::runtime_error where practical
//   - Annotated GPU layout notes for CUDA port
#pragma once

#include <iostream>
#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <utility>
#include <string>

// ---------------------------------------------------------------------------
// Array<T> — 1-D heap array with bounds checking
// Memory layout: Data[i],  i in [0, size)
// GPU note: m_Data is the raw pointer passed to cudaMemcpy / device kernels.
// ---------------------------------------------------------------------------
template <class T>
class Array {
public:
    // --- constructors / destructor -----------------------------------------

    Array() noexcept : m_Data(nullptr), m_Size(0) {}

    explicit Array(int n) : m_Data(nullptr), m_Size(0) { SetSize(n); }

    Array(const T a[], int n) : m_Data(nullptr), m_Size(0) {
        if (n < 1) throw std::runtime_error("Array: size < 1");
        SetSize(n);
        std::copy(a, a + n, m_Data);
    }

    // Construct from a sub-range of another Array
    Array(const Array<T>& a, int start, int n) : m_Data(nullptr), m_Size(0) {
        if (n < 1) throw std::runtime_error("Array: sub-range size < 1");
        SetSize(n);
        for (int i = 0; i < n; i++) m_Data[i] = a[i + start];
    }

    // Deep copy constructor
    Array(const Array<T>& rhs) : m_Data(nullptr), m_Size(0) {
        if (rhs.m_Size > 0) {
            SetSize(rhs.m_Size);
            std::copy(rhs.m_Data, rhs.m_Data + m_Size, m_Data);
        }
    }

    // Move constructor — avoids copies when returning from functions
    Array(Array<T>&& rhs) noexcept : m_Data(rhs.m_Data), m_Size(rhs.m_Size) {
        rhs.m_Data = nullptr;
        rhs.m_Size = 0;
    }

    ~Array() { delete[] m_Data; }

    // --- assignment -------------------------------------------------------

    Array<T>& operator=(const Array<T>& rhs) {
        if (this != &rhs) {
            delete[] m_Data; m_Data = nullptr; m_Size = 0;
            if (rhs.m_Size > 0) {
                SetSize(rhs.m_Size);
                std::copy(rhs.m_Data, rhs.m_Data + m_Size, m_Data);
            }
        }
        return *this;
    }

    Array<T>& operator=(Array<T>&& rhs) noexcept {
        if (this != &rhs) {
            delete[] m_Data;
            m_Data = rhs.m_Data; m_Size = rhs.m_Size;
            rhs.m_Data = nullptr; rhs.m_Size = 0;
        }
        return *this;
    }

    // --- sizing -----------------------------------------------------------

    void SetSize(int n) {
        if (n < 1) throw std::runtime_error("Array::SetSize: n < 1");
        delete[] m_Data;
        m_Size = n;
        m_Data = new T[n]();   // value-initialise (zero for arithmetic types)
    }

    int size() const noexcept { return m_Size; }

    // --- element access ---------------------------------------------------

    T& operator[](int i) {
#ifndef NDEBUG
        if (i < 0 || i >= m_Size) {
            throw std::out_of_range("Array index " + std::to_string(i) +
                                    " out of bounds (size=" + std::to_string(m_Size) + ")");
        }
#endif
        return m_Data[i];
    }

    const T& operator[](int i) const {
#ifndef NDEBUG
        if (i < 0 || i >= m_Size) {
            throw std::out_of_range("Array index " + std::to_string(i) +
                                    " out of bounds (size=" + std::to_string(m_Size) + ")");
        }
#endif
        return m_Data[i];
    }

    // Public raw pointer — used by CUDA kernels and legacy MatPrint
    // GPU note: copy to device with cudaMemcpy(d_ptr, m_Data, size()*sizeof(T), H2D)
    T* m_Data;

private:
    int m_Size;
};


// ---------------------------------------------------------------------------
// Array2<T> — 2-D row-major array, single flat allocation
// Layout: element [r][c] is at m_Flat[r*ncol + c]
// GPU note: m_Flat is directly copyable to device memory.
// ---------------------------------------------------------------------------
template <class T>
class Array2 {
public:
    Array2(int n1, int n2) : nrow(n1), ncol(n2), m_Flat(nullptr) {
        if (n1 < 1 || n2 < 1)
            throw std::runtime_error("Array2: dimensions must be >= 1");
        m_Flat = new T[static_cast<size_t>(n1) * n2]();
    }

    ~Array2() { delete[] m_Flat; }

    // Non-copyable for now (not needed in KlustaKwik)
    Array2(const Array2&) = delete;
    Array2& operator=(const Array2&) = delete;

    int nRows() const noexcept { return nrow; }
    int nCols() const noexcept { return ncol; }

    // Row accessor — returns a pointer, not an Array (avoids extra allocation)
    T* operator[](int r) {
#ifndef NDEBUG
        if (r < 0 || r >= nrow)
            throw std::out_of_range("Array2 row index out of bounds");
#endif
        return m_Flat + r * ncol;
    }
    const T* operator[](int r) const {
#ifndef NDEBUG
        if (r < 0 || r >= nrow)
            throw std::out_of_range("Array2 row index out of bounds");
#endif
        return m_Flat + r * ncol;
    }

    T* data() noexcept { return m_Flat; }
    const T* data() const noexcept { return m_Flat; }

private:
    int nrow, ncol;
    T* m_Flat;
};
