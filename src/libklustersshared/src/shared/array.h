/***************************************************************************
                          array.h  -  description
                             -------------------
    begin                : Mond Dec 29 2003
    copyright            : (C) 2003 by Lynn Hazan
    email                : lynn.hazan@myrealbox.com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef ARRAY_H
#define ARRAY_H

#include <cstring>
#include <memory>
#include <stdexcept>

/**
 * Template array class for simple (POD / trivially-copyable) types.
 * Uses RAII via std::unique_ptr for memory management.
 *
 * Row and column indices are 1-based (matching original API).
 *
 * @author Lynn Hazan (original), modernized for C++17.
 */
template <class T>
class Array
{
public:
    Array(long nbOfRows, long nbOfColumns)
        : nbColumns(nbOfColumns)
        , nbRows(nbOfRows)
        , array(std::make_unique<T[]>(static_cast<std::size_t>(nbRows * nbColumns)))
    {}

    Array() = default;
    ~Array() = default;

    // Deep copy
    Array(const Array &other)
        : nbColumns(other.nbColumns)
        , nbRows(other.nbRows)
        , array(other.nbRows * other.nbColumns > 0
                ? std::make_unique<T[]>(static_cast<std::size_t>(other.nbRows * other.nbColumns))
                : nullptr)
    {
        if (array)
            std::memcpy(array.get(), other.array.get(),
                        static_cast<std::size_t>(nbRows * nbColumns) * sizeof(T));
    }

    Array &operator=(const Array &other)
    {
        if (this != &other) {
            nbColumns = other.nbColumns;
            nbRows    = other.nbRows;
            auto n    = static_cast<std::size_t>(nbRows * nbColumns);
            array     = n > 0 ? std::make_unique<T[]>(n) : nullptr;
            if (array)
                std::memcpy(array.get(), other.array.get(), n * sizeof(T));
        }
        return *this;
    }

    Array(Array &&) noexcept = default;
    Array &operator=(Array &&) noexcept = default;

    /** Resize the array, discarding previous contents. */
    void setSize(long nbOfRows, long nbOfColumns)
    {
        nbColumns = nbOfColumns;
        nbRows    = nbOfRows;
        array     = std::make_unique<T[]>(static_cast<std::size_t>(nbRows * nbColumns));
    }

    /**
     * 1-based element access (i = row, j = column).
     * Returns a reference so the element can be assigned.
     */
    [[nodiscard]] T &operator()(long i, long j)
    {
        return array[static_cast<std::size_t>((i - 1) * nbColumns + (j - 1))];
    }

    [[nodiscard]] const T &operator()(long i, long j) const
    {
        return array[static_cast<std::size_t>((i - 1) * nbColumns + (j - 1))];
    }

    /**
     * 0-based flat index access (matches original operator[] API).
     */
    [[nodiscard]] T &operator[](long position) noexcept
    {
        return array[static_cast<std::size_t>(position)];
    }
    [[nodiscard]] const T &operator[](long position) const noexcept
    {
        return array[static_cast<std::size_t>(position)];
    }

    /**
     * Copy columns 1..lastColumnToCopy from @p source into this array.
     * Assumes this array has the correct size already.
     */
    void copySubset(Array &source, long lastColumnToCopy)
    {
        for (long i = 0; i < nbRows; ++i)
            std::memcpy(&array[static_cast<std::size_t>(i * lastColumnToCopy)],
                        &source.array[static_cast<std::size_t>(i * source.nbColumns)],
                        static_cast<std::size_t>(lastColumnToCopy) * sizeof(T));
    }

    /**
     * Copy columns firstColumnToCopy..lastColumnToCopy from @p source into this array
     * starting at @p startingColumn. Assumes this array has the correct size.
     */
    void copySubset(Array &source, long firstColumnToCopy, long lastColumnToCopy, long startingColumn)
    {
        long nbColumnsToCopy = lastColumnToCopy - firstColumnToCopy + 1;
        for (long i = 0; i < nbRows; ++i)
            std::memcpy(&array[static_cast<std::size_t>(i * nbColumns + startingColumn - 1)],
                        &source.array[static_cast<std::size_t>(i * source.nbColumns + (firstColumnToCopy - 1))],
                        static_cast<std::size_t>(nbColumnsToCopy) * sizeof(T));
    }

    /**
     * Copy data prepending an empty first column (column 0 left uninitialised).
     */
    void copyAndPrependColumn(Array &source)
    {
        for (long i = 0; i < nbRows; ++i)
            std::memcpy(&array[static_cast<std::size_t>(i * nbColumns + 1)],
                        &source.array[static_cast<std::size_t>(i * source.nbColumns)],
                        static_cast<std::size_t>(source.nbColumns) * sizeof(T));
    }


    /** Fill every element with zero. */
    void fillWithZeros()
    {
        std::memset(array.get(), 0,
                    static_cast<std::size_t>(nbRows * nbColumns) * sizeof(T));
    }

    /** Copy @p nbElements elements from @p source into the array starting at offset 0. */
    void copyData(const T *source, long nbElements)
    {
        std::memcpy(array.get(), source,
                    static_cast<std::size_t>(nbElements) * sizeof(T));
    }

    /** Raw pointer for legacy C API interop. Prefer indexed access when possible. */
    [[nodiscard]] T *data() noexcept { return array.get(); }
    [[nodiscard]] const T *data() const noexcept { return array.get(); }

    [[nodiscard]] long nbOfColumns() const noexcept { return nbColumns; }
    [[nodiscard]] long nbOfRows()    const noexcept { return nbRows; }

protected:
    long nbColumns{0};
    long nbRows{0};
    std::unique_ptr<T[]> array;
};

/**
 * pArray – 2-D array of value-type objects (same storage as Array<T>).
 *
 * Inherits Array<T> for its row×column storage and all its operators.
 * Adds element-wise copy assignment and copySubset overloads that copy
 * element-by-element (required when T is not trivially copyable).
 *
 * Row/column indices are 1-based, matching the original API.
 */
template <class T>
class pArray : public Array<T>
{
    using Base = Array<T>;

protected:
    using Base::nbColumns;
    using Base::nbRows;
    using Base::array;

public:
    pArray() = default;
    ~pArray() = default;

    // Deep copy
    pArray(const pArray &other) : Base(other) {}

    pArray &operator=(pArray &source)
    {
        if (this != &source) {
            nbColumns = source.nbColumns;
            nbRows    = source.nbRows;
            auto n    = static_cast<std::size_t>(nbRows * nbColumns);
            array     = n > 0 ? std::make_unique<T[]>(n) : nullptr;
            for (long i = 0; i < nbRows; ++i)
                for (long j = 0; j < nbColumns; ++j)
                    array[static_cast<std::size_t>(i * nbColumns + j)] =
                        source.array[static_cast<std::size_t>(i * nbColumns + j)];
        }
        return *this;
    }

    // Allow assignment from const (undo/redo pattern)
    pArray &operator=(const pArray &source)
    {
        return operator=(const_cast<pArray &>(source));
    }

    pArray(pArray &&) noexcept = default;
    pArray &operator=(pArray &&) noexcept = default;

    /**
     * Copy data prepending an empty first column.
     */
    void copyAndPrependColumn(pArray &source)
    {
        for (long i = 0; i < nbRows; ++i)
            for (long j = 0; j < nbColumns - 1; ++j)
                array[static_cast<std::size_t>(i * nbColumns + j + 1)] =
                    source.array[static_cast<std::size_t>(i * source.nbColumns + j)];
    }

    /**
     * Copy columns 1..lastColumnToCopy from source into this array.
     */
    void copySubset(pArray &source, long lastColumnToCopy)
    {
        for (long i = 0; i < nbRows; ++i)
            for (long j = 0; j < lastColumnToCopy; ++j)
                array[static_cast<std::size_t>(i * lastColumnToCopy + j)] =
                    source.array[static_cast<std::size_t>(i * source.nbColumns + j)];
    }

    /**
     * Copy columns firstColumnToCopy..lastColumnToCopy from source starting
     * at startingColumn in this array.
     */
    void copySubset(pArray &source, long firstColumnToCopy, long lastColumnToCopy, long startingColumn)
    {
        long n = lastColumnToCopy - firstColumnToCopy + 1;
        for (long i = 0; i < nbRows; ++i)
            for (long j = 0; j < n; ++j)
                array[static_cast<std::size_t>(i * nbColumns + (startingColumn - 1) + j)] =
                    source.array[static_cast<std::size_t>(i * source.nbColumns + (firstColumnToCopy - 1) + j)];
    }
};

#endif // ARRAY_H
