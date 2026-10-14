// Checks the interning guarantees XyInterned and XyInternedByKey give the
// subtitle renderer: equal values share one handle, and entries go away with
// their last handle.

#include <afx.h>
#include <gtest/gtest.h>
#include "flyweight_base_types.h"

namespace
{
    struct Point
    {
        int x, y;
        Point(int x = 0, int y = 0) : x(x), y(y) {}
        bool operator==(const Point& o) const { return x == o.x && y == o.y; }
    };

    std::size_t hash_value(const Point& p)
    {
        // Deliberately weak, so different values collide.
        return (std::size_t)(p.x + p.y);
    }

    struct Built
    {
        static int constructed;
        int key;
        explicit Built(int key) : key(key) { constructed++; }
    };
    int Built::constructed = 0;
}

TEST(Interned, EqualValuesShareOneCopy)
{
    ASSERT_EQ(0u, XyInterned<Point>::Count());
    {
        XyInterned<Point> a(Point(1, 2)), b(Point(1, 2)), c(Point(2, 1));
        EXPECT_TRUE(a == b);
        EXPECT_EQ(&a.get(), &b.get());
        EXPECT_TRUE(a != c);
        EXPECT_EQ(2, c.get().x);
        EXPECT_EQ(2u, XyInterned<Point>::Count());

        XyInterned<Point> d;
        EXPECT_EQ(0, d.get().x);
        d = Point(2, 1);
        EXPECT_TRUE(d == c);
        EXPECT_EQ(2u, XyInterned<Point>::Count());

        const Point& ref = a;
        EXPECT_EQ(1, ref.x);
    }
    EXPECT_EQ(0u, XyInterned<Point>::Count());

    // A value interned again after being freed works as new.
    XyInterned<Point> e(Point(1, 2)), f(e);
    EXPECT_TRUE(e == f);
    EXPECT_EQ(1u, XyInterned<Point>::Count());
}

TEST(Interned, ByKeyBuildsOncePerKey)
{
    Built::constructed = 0;
    {
        XyInternedByKey<int, Built> a(7), b(7), c(8);
        EXPECT_EQ(&a.get(), &b.get());
        EXPECT_NE(&a.get(), &c.get());
        EXPECT_EQ(7, a.get().key);
        EXPECT_EQ(8, c.get().key);
        EXPECT_EQ(2, Built::constructed);
        EXPECT_EQ(2u, (XyInternedByKey<int, Built>::Count()));
    }
    EXPECT_EQ(0u, (XyInternedByKey<int, Built>::Count()));

    // Freed with the last handle, so the next use builds it again.
    XyInternedByKey<int, Built> d(7);
    EXPECT_EQ(3, Built::constructed);
}
