#pragma once

class Point {
public:
    int x;
    int y;

    void operator+=(Point const& other) {
        x += other.x;
        y += other.y;
    }
    void operator+=(int addend) {
        x += addend;
        y += addend;
    }

    void operator-=(Point const& other) {
        x -= other.x;
        y -= other.y;
    }
    void operator-=(int subtrahend) {
        x -= subtrahend;
        y -= subtrahend;
    }

    void operator/=(Point const& other) {
        x /= other.x;
        y /= other.y;
    }

    void operator/=(int divisor) {
        x /= divisor;
        y /= divisor;
    }

    void operator*=(Point const& other) {
        x *= other.x;
        y *= other.y;
    }

    void operator*=(int multiplier) {
        x *= multiplier;
        y *= multiplier;
    }

    friend Point operator+(Point const& a, Point const& b) {
        Point ret(a);

        ret += b;
        return ret;
    }
    friend Point operator+(Point const& a, int b) {
        Point ret(a);

        ret += b;
        return ret;
    }

    friend Point operator-(Point const& a, Point const& b) {
        Point ret(a);

        ret -= b;
        return ret;
    }

    friend Point operator-(Point const& a, int b) {
        Point ret(a);

        ret -= b;
        return ret;
    }

    friend Point operator/(Point const& a, Point const& b) {
        Point ret(a);

        ret /= b;
        return ret;
    }
    friend Point operator/(Point const& a, int divisor) {
        Point ret(a);

        ret /= divisor;
        return ret;
    }

    friend Point operator*(Point const& a, Point const& b) {
        Point ret(a);

        ret *= b;
        return ret;
    }
    friend Point operator*(Point const& a, int multiplier) {
        Point ret(a);

        ret *= multiplier;
        return ret;
    }

    friend bool operator==(Point const& a, Point const& b) { return a.x == b.x && a.y == b.y; }

    Point to_display() const;
    Point from_display() const;

    Point() : x(0), y(0) {}

    Point(int x_, int y_) {
        x = x_;
        y = y_;
    }
};
