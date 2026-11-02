// std::sort as the game's MSVC6 STL does it (the order of equal elements and the comparisons made
// depend on the algorithm, so the port keeps it): a quicksort with a median of three down to runs
// of 16 (the smaller part recursed into), then an insertion sort of the first 16 and unguarded
// insertions of the rest. Seen inlined in the server list's sort (0x006677c0; 0x00667b30,
// 0x00667e20, 0x00667f40).
#pragma once

#include <cstddef>

namespace msvc6 {

constexpr std::ptrdiff_t SORT_MAX = 16;

template <class T, class Pr> T Median(T x, T y, T z, Pr pred)
{
    if (pred(x, y))
        return pred(y, z) ? y : pred(x, z) ? z : x;
    return pred(x, z) ? x : pred(y, z) ? z : y;
}

template <class T, class Pr> T *UnguardedPartition(T *first, T *last, T pivot, Pr pred)
{
    for (;; ++first) {
        for (; pred(*first, pivot); ++first) {
        }
        for (; pred(pivot, *--last);) {
        }
        if (last <= first)
            return first;
        T swapped = *first;
        *first = *last;
        *last = swapped;
    }
}

template <class T, class Pr> void UnguardedInsert(T *last, T value, Pr pred)
{
    for (T *m = last; pred(value, *--m); last = m)
        *last = *m;
    *last = value;
}

template <class T, class Pr> void InsertionSort(T *first, T *last, Pr pred)
{
    if (first == last)
        return;
    for (T *m = first; ++m != last;) {
        T value = *m;
        if (!pred(value, *first)) {
            UnguardedInsert(m, value, pred);
        } else {
            for (T *p = m; p != first; --p)
                *p = p[-1];
            *first = value;
        }
    }
}

template <class T, class Pr> void SortRuns(T *first, T *last, Pr pred)
{
    while (SORT_MAX < last - first) {
        T *m = UnguardedPartition(first, last, Median(*first, first[(last - first) / 2], last[-1], pred), pred);
        if (last - m <= m - first) {
            SortRuns(m, last, pred);
            last = m;
        } else {
            SortRuns(first, m, pred);
            first = m;
        }
    }
}

template <class T, class Pr> void Sort(T *first, T *last, Pr pred)
{
    if (last - first <= SORT_MAX) {
        InsertionSort(first, last, pred);
        return;
    }
    SortRuns(first, last, pred);
    InsertionSort(first, first + SORT_MAX, pred);
    for (first += SORT_MAX; first != last; ++first)
        UnguardedInsert(first, *first, pred);
}

} // namespace msvc6
